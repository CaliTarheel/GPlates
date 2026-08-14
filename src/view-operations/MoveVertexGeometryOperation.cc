/* $Id$ */

/**
 * \file 
 * Moves points/vertices in a geometry as the user selects a vertex and drags it.
 * $Revision$
 * $Date$
 * 
 * Copyright (C) 2008 The University of Sydney, Australia
 * Copyright (C) 2009, 2010 Geological Survey of Norway
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <memory>
#include <utility> // std::move
#include <QDebug>
#include <QUndoCommand>

#include "MoveVertexGeometryOperation.h"

#include "GeometryBuilderUndoCommands.h"
#include "GeometryOperationUndo.h"
#include "QueryProximityThreshold.h"
#include "RenderedGeometryProximity.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayerVisitor.h"
#include "RenderedGeometryParameters.h"
#include "RenderedGeometryUtils.h"
#include "UndoRedo.h"

#include "app-logic/ReconstructionGeometryUtils.h"

#include "canvas-tools/GeometryOperationState.h"
#include "canvas-tools/ModifyGeometryState.h"

#include "gui/CanvasToolWorkflows.h"
#include "gui/FeatureFocus.h"

#include "maths/MathsUtils.h"
#include "maths/FiniteRotation.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/ProximityCriteria.h"
#include "maths/ProximityHitDetail.h"
#include "maths/PolylineOnSphere.h"
#include "maths/Vector3D.h"

#include "presentation/ViewState.h"



GPlatesViewOperations::MoveVertexGeometryOperation::MoveVertexGeometryOperation(
		GeometryBuilder &geometry_builder,
		GPlatesCanvasTools::GeometryOperationState &geometry_operation_state,
		GPlatesCanvasTools::ModifyGeometryState &modify_geometry_state,
		RenderedGeometryCollection &rendered_geometry_collection,
		RenderedGeometryCollection::MainLayerType main_rendered_layer_type,
		GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
		const QueryProximityThreshold &query_proximity_threshold,
		GPlatesGui::FeatureFocus &feature_focus) :
	d_geometry_builder(geometry_builder),
	d_modify_geometry_state(modify_geometry_state),
	d_geometry_operation_state(geometry_operation_state),
	d_rendered_geometry_collection(rendered_geometry_collection),
	d_main_rendered_layer_type(main_rendered_layer_type),
	d_canvas_tool_workflows(canvas_tool_workflows),
	d_query_proximity_threshold(query_proximity_threshold),
	d_selected_vertex_index(0),
	d_is_vertex_selected(false),
	d_is_vertex_highlighted(false),
	d_is_active(false),
	d_is_lassoing(false),
	d_should_check_nearby_vertices(false),
	d_nearby_vertex_threshold(0.),
	d_feature_focus(feature_focus) 
{
	// For updating move-nearby-vertex parameters from the task panel widget. 
	QObject::connect(
			&modify_geometry_state,
			SIGNAL(snap_vertices_setup_changed(
					bool,double,bool,GPlatesModel::integer_plate_id_type)),
			this,
			SLOT(handle_snap_vertices_setup_changed(
					bool,double,bool,GPlatesModel::integer_plate_id_type)));

	QObject::connect(
			&modify_geometry_state,
			SIGNAL(delete_selected_vertices_requested()),
			this,
			SLOT(handle_delete_selected_vertices_requested()));
	QObject::connect(
			&modify_geometry_state,
			SIGNAL(average_selected_vertex_positions_requested()),
			this,
			SLOT(handle_average_selected_vertex_positions_requested()));
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::activate()
{
	d_is_active = true;

	// Let others know we're the currently activated GeometryOperation.
	d_geometry_operation_state.set_active_geometry_operation(this);

	connect_to_geometry_builder_signals();

	// Create the rendered geometry layers required by the GeometryBuilder state
	// and activate/deactivate appropriate layers.
	create_rendered_geometry_layers();

	// Activate our render layers so they become visible.
	d_lines_layer_ptr->set_active(true);
	d_points_layer_ptr->set_active(true);
	d_highlight_point_layer_ptr->set_active(true);
	d_selected_points_layer_ptr->set_active(true);
	d_lasso_layer_ptr->set_active(true);

	// Fill the rendered layers with RenderedGeometry objects by querying
	// the GeometryBuilder state.
	update_rendered_geometries();
	publish_vertex_selection_state();
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::deactivate()
{
	d_is_lassoing = false;
	d_lasso_points.clear();
	clear_vertex_selection();
	d_is_active = false;

	emit_unhighlight_signal(&d_geometry_builder);

	// Let others know there's no currently activated GeometryOperation.
	d_geometry_operation_state.set_no_active_geometry_operation();

	disconnect_from_geometry_builder_signals();

	// Get rid of all render layers, not just the highlighting, even if switching to drag or zoom tool
	// (which normally previously would display the most recent tool's layers).
	// This is because once we are deactivated we won't be able to update the render layers when/if
	// the reconstruction time changes.
	// This means the user won't see this tool's render layers while in the drag or zoom tool.
	d_lines_layer_ptr->set_active(false);
	d_points_layer_ptr->set_active(false);
	d_highlight_point_layer_ptr->set_active(false);
	d_selected_points_layer_ptr->set_active(false);
	d_lasso_layer_ptr->set_active(false);
	d_lines_layer_ptr->clear_rendered_geometries();
	d_points_layer_ptr->clear_rendered_geometries();
	d_highlight_point_layer_ptr->clear_rendered_geometries();
	d_selected_points_layer_ptr->clear_rendered_geometries();
	d_lasso_layer_ptr->clear_rendered_geometries();

	// User will have to click another vertex when this operation activates again.
	d_is_vertex_selected = false;
	d_is_vertex_highlighted = false;
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::start_drag(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		const double &closeness_inclusion_threshold)
{
	//
	// See if the user selected a vertex with their mouse click.
	//


	boost::optional<RenderedGeometryProximityHit> closest_hit = test_proximity_to_points(
		oriented_pos_on_sphere, closeness_inclusion_threshold);

	if (closest_hit)
	{
		// The index of the vertex selected corresponds to index of vertex in
		// the geometry.
		// NOTE: this will have to be changed when multiple internal geometries are
		// possible in the GeometryBuilder.
		d_selected_vertex_index = closest_hit->d_rendered_geom_index;

		// Dragging an already-selected vertex moves the entire selection. Dragging
		// any other vertex begins a new, single-vertex selection.
		if (d_selected_vertex_indices.find(d_selected_vertex_index) ==
				d_selected_vertex_indices.end())
		{
			d_selected_vertex_indices.clear();
			d_selected_vertex_indices.insert(d_selected_vertex_index);
			publish_vertex_selection_state();
		}

		// Get a unique command id so that all move vertex commands in the
		// current mouse drag will be merged together.
		// This id will be released for reuse when the last copy of it is destroyed.
		d_move_vertex_command_id = UndoRedo::instance().get_unique_command_id();

		d_is_vertex_selected = true;
		d_drag_anchor_point = d_geometry_builder.get_geometry_point(0, d_selected_vertex_index);
		d_drag_original_points.clear();
		for (std::set<GeometryBuilder::PointIndex>::const_iterator selected =
				d_selected_vertex_indices.begin();
			selected != d_selected_vertex_indices.end();
			++selected)
		{
			d_drag_original_points.push_back(
					d_geometry_builder.get_geometry_point(0, *selected));
		}

		// Highlight the vertex the mouse is currently hovering over.
		update_highlight_rendered_point(d_selected_vertex_index);
	
	}
	
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_drag(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere)
{
	// If a vertex was selected when user first clicked mouse then move the vertex.
	if (d_is_vertex_selected)
	{
		move_selected_vertices(oriented_pos_on_sphere, true/*is_intermediate_move*/);

		// Highlight the vertex the mouse is currently hovering over.
		update_highlight_rendered_point(d_selected_vertex_index);
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::end_drag(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere)
{
	// If a vertex was selected when user first clicked mouse then move the vertex.
	if (d_is_vertex_selected)
	{
		// Do the final move vertex command to signal that this is the final
		// move of this drag.
		move_selected_vertices(oriented_pos_on_sphere, false/*is_intermediate_move*/);

		// Highlight the vertex the mouse is currently hovering over.
		update_highlight_rendered_point(d_selected_vertex_index);
	}

	// Release our handle on the command id.
	d_move_vertex_command_id = UndoRedo::CommandId();

	d_is_vertex_selected = false;
	d_drag_anchor_point = boost::none;
	d_drag_original_points.clear();
	d_drag_secondary_geometries.clear();

	d_geometry_builder.clear_secondary_geometries();
	// This will clear any secondary geometry highlighting and re-draw the "normal" move vertex geometries.
	update_rendered_geometries();
	
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::mouse_move(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		const double &closeness_inclusion_threshold)
{
	//
	// See if the mouse cursor is near a vertex and highlight it if it is.
	//

	// Clear any currently highlighted point first.
	d_highlight_point_layer_ptr->clear_rendered_geometries();

	boost::optional<RenderedGeometryProximityHit> closest_hit = test_proximity_to_points(
			oriented_pos_on_sphere, closeness_inclusion_threshold);
	if (closest_hit)
	{
		const GeometryBuilder::PointIndex highlight_vertex_index = closest_hit->d_rendered_geom_index;
		
		update_highlight_rendered_point(highlight_vertex_index);

		// Currently only one internal geometry is supported so set geometry index to zero.
		const GeometryBuilder::GeometryIndex geometry_index = 0;

		emit_highlight_point_signal(&d_geometry_builder,
				geometry_index,
				highlight_vertex_index,
				GeometryOperationParameters::HIGHLIGHT_COLOUR);
				
		d_is_vertex_highlighted = true;
		d_selected_vertex_index = highlight_vertex_index;
	}
	else
	{
		emit_unhighlight_signal(&d_geometry_builder);
		d_is_vertex_highlighted = false;
	}
	

}


void
GPlatesViewOperations::MoveVertexGeometryOperation::toggle_vertex_selection(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		const double &closeness_inclusion_threshold)
{
	boost::optional<RenderedGeometryProximityHit> closest_hit = test_proximity_to_points(
			oriented_pos_on_sphere,
			closeness_inclusion_threshold);
	if (!closest_hit)
	{
		return;
	}

	const GeometryBuilder::PointIndex vertex_index = closest_hit->d_rendered_geom_index;
	std::set<GeometryBuilder::PointIndex>::iterator selected =
			d_selected_vertex_indices.find(vertex_index);
	if (selected == d_selected_vertex_indices.end())
	{
		d_selected_vertex_indices.insert(vertex_index);
	}
	else
	{
		d_selected_vertex_indices.erase(selected);
	}

	publish_vertex_selection_state();
	update_rendered_geometries();
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::begin_lasso(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere)
{
	d_is_lassoing = true;
	d_lasso_points.clear();
	d_lasso_points.push_back(oriented_pos_on_sphere);
	update_lasso_rendered_geometry();
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_lasso(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere)
{
	if (!d_is_lassoing)
	{
		return;
	}

	if (d_lasso_points.empty() || !(d_lasso_points.back() == oriented_pos_on_sphere))
	{
		d_lasso_points.push_back(oriented_pos_on_sphere);
		update_lasso_rendered_geometry();
	}
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::end_lasso()
{
	if (!d_is_lassoing)
	{
		return;
	}

	d_selected_vertex_indices.clear();
	if (d_lasso_points.size() >= 3 && d_geometry_builder.get_num_geometries() > 0)
	{
		try
		{
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type lasso_polygon =
					GPlatesMaths::PolygonOnSphere::create(d_lasso_points);
			const unsigned int num_points = d_geometry_builder.get_num_points_in_geometry(0);
			for (GeometryBuilder::PointIndex point_index = 0;
					point_index < num_points;
					++point_index)
			{
				if (lasso_polygon->is_point_in_polygon(
							d_geometry_builder.get_geometry_point(0, point_index)))
				{
					d_selected_vertex_indices.insert(point_index);
				}
			}
		}
		catch (...)
		{
			// An invalid/self-intersecting lasso simply produces an empty selection.
		}
	}

	d_is_lassoing = false;
	d_lasso_points.clear();
	d_lasso_layer_ptr->clear_rendered_geometries();
	publish_vertex_selection_state();
	update_rendered_geometries();
}

boost::optional<GPlatesViewOperations::RenderedGeometryProximityHit>
GPlatesViewOperations::MoveVertexGeometryOperation::test_proximity_to_points(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		const double &closeness_inclusion_threshold)
{

	GPlatesMaths::ProximityCriteria proximity_criteria(
			oriented_pos_on_sphere,
			closeness_inclusion_threshold);

	sorted_rendered_geometry_proximity_hits_type sorted_hits;
	if (!test_proximity(sorted_hits, proximity_criteria, *d_points_layer_ptr))
	{
		return boost::none;
	}

	// Only interested in the closest vertex in the layer.
	const RenderedGeometryProximityHit &closest_hit = sorted_hits.front();

	return closest_hit;
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::create_rendered_geometry_layers()
{
	// Create a rendered layer to draw the line segments of polylines and polygons.
	d_lines_layer_ptr =
		d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				d_main_rendered_layer_type);

	// Create a rendered layer to draw the points in the geometry on top of the lines.
	// NOTE: this must be created second to get drawn on top.
	d_points_layer_ptr =
		d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				d_main_rendered_layer_type);

	// Selected points are drawn above regular geometry points.
	d_selected_points_layer_ptr =
		d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				d_main_rendered_layer_type);

	// Create a rendered layer to draw a single point in the geometry on top of the usual points
	// when the mouse cursor hovers over one of them.
	// NOTE: this must be created third to get drawn on top of the points.
	d_highlight_point_layer_ptr =
		d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				d_main_rendered_layer_type);

	// The in-progress lasso is always the top-most part of this operation.
	d_lasso_layer_ptr =
		d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				d_main_rendered_layer_type);

	// In both cases above we store the returned object as a data member and it
	// automatically destroys the created layer for us when 'this' object is destroyed.
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::connect_to_geometry_builder_signals()
{
	// Connect to the current geometry builder's signals.

	// GeometryBuilder has just finished updating geometry.
	QObject::connect(
			&d_geometry_builder,
			SIGNAL(stopped_updating_geometry()),
			this,
			SLOT(geometry_builder_stopped_updating_geometry()));
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::disconnect_from_geometry_builder_signals()
{
	// Disconnect all signals from the current geometry builder.
	QObject::disconnect(&d_geometry_builder, 0, this, 0);
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::geometry_builder_stopped_updating_geometry()
{
	// The geometry builder has just potentially done a group of
	// geometry modifications and is now notifying us that it's finished.

	// Just clear and add all RenderedGeometry objects.
	// This could be optimised, if profiling says so, by listening to the other signals
	// generated by GeometryBuilder instead and only making the minimum changes needed.
	if (d_geometry_builder.get_num_geometries() == 0)
	{
		d_selected_vertex_indices.clear();
	}
	else
	{
		const unsigned int num_points = d_geometry_builder.get_num_points_in_geometry(0);
		for (std::set<GeometryBuilder::PointIndex>::iterator selected =
					d_selected_vertex_indices.begin();
			selected != d_selected_vertex_indices.end();)
		{
			if (*selected >= num_points)
			{
				d_selected_vertex_indices.erase(selected++);
			}
			else
			{
				++selected;
			}
		}
	}

	update_rendered_geometries();
	update_rendered_secondary_geometries();
	publish_vertex_selection_state();
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::move_vertex(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		bool is_intermediate_move)
{
	// The command that does the actual moving of vertex.
	std::unique_ptr<QUndoCommand> move_vertex_command(
			new GeometryBuilderMovePointUndoCommand(
					d_geometry_builder,
					d_selected_vertex_index,
					oriented_pos_on_sphere,
					is_intermediate_move));
					
	// Command wraps move vertex command with handing canvas tool choice and
	// move vertex tool activation.
	std::unique_ptr<QUndoCommand> undo_command(
			new GeometryOperationUndoCommand(
					QObject::tr("move vertex"),
					std::move(move_vertex_command),
					this,
					d_canvas_tool_workflows,
					d_move_vertex_command_id));

	// Push command onto undo list.
	// Note: the command's redo() gets executed inside the push() call and this is where
	// the vertex is initially moved.
	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::move_selected_vertices(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere,
		bool is_intermediate_move)
{
	if (d_selected_vertex_indices.size() <= 1)
	{
		move_vertex(oriented_pos_on_sphere, is_intermediate_move);
		return;
	}

	if (!d_drag_anchor_point ||
			d_drag_original_points.size() != d_selected_vertex_indices.size())
	{
		return;
	}

	const GPlatesMaths::FiniteRotation drag_rotation =
			GPlatesMaths::FiniteRotation::create_great_circle_point_rotation(
					*d_drag_anchor_point,
					oriented_pos_on_sphere);

	std::vector<GPlatesMaths::PointOnSphere> positions;
	positions.reserve(d_drag_original_points.size());
	for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point =
				d_drag_original_points.begin();
			point != d_drag_original_points.end();
			++point)
	{
		positions.push_back(drag_rotation * *point);
	}

	move_selected_vertices_to(
			positions,
			is_intermediate_move,
			QObject::tr("move selected vertices"),
			d_move_vertex_command_id,
			d_drag_secondary_geometries);
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::move_selected_vertices_to(
		const std::vector<GPlatesMaths::PointOnSphere> &positions,
		bool is_intermediate_move,
		const QString &undo_text,
		UndoRedo::CommandId command_id,
		const secondary_geometry_per_point_seq_type &secondary_geometries_per_point)
{
	if (positions.size() != d_selected_vertex_indices.size() || positions.empty())
	{
		return;
	}

	GeometryBuilderMovePointsUndoCommand::indexed_point_seq_type points_to_move;
	points_to_move.reserve(positions.size());
	std::vector<GPlatesMaths::PointOnSphere>::const_iterator position = positions.begin();
	for (std::set<GeometryBuilder::PointIndex>::const_iterator selected =
				d_selected_vertex_indices.begin();
			selected != d_selected_vertex_indices.end();
			++selected, ++position)
	{
		points_to_move.push_back(std::make_pair(*selected, *position));
	}

	// Carry any snapped vertices in other geometries along with the selection. This is empty when
	// Snap Vertices is off, in which case the command behaves exactly as it did before.
	std::unique_ptr<QUndoCommand> move_vertices_command(
			new GeometryBuilderMovePointsUndoCommand(
					d_geometry_builder,
					points_to_move,
					secondary_geometries_per_point,
					is_intermediate_move));
	std::unique_ptr<QUndoCommand> undo_command(
			new GeometryOperationUndoCommand(
					undo_text,
					std::move(move_vertices_command),
					this,
					d_canvas_tool_workflows,
					command_id));

	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::delete_selected_vertices()
{
	if (!can_delete_selected_vertices())
	{
		return;
	}

	std::vector<GeometryBuilder::PointIndex> point_indices;
	point_indices.reserve(d_selected_vertex_indices.size());
	for (std::set<GeometryBuilder::PointIndex>::const_reverse_iterator selected =
				d_selected_vertex_indices.rbegin();
			selected != d_selected_vertex_indices.rend();
			++selected)
	{
		point_indices.push_back(*selected);
	}

	emit_unhighlight_signal(&d_geometry_builder);
	clear_vertex_selection();
	d_is_vertex_highlighted = false;
	d_is_vertex_selected = false;

	std::unique_ptr<QUndoCommand> delete_vertices_command(
			new GeometryBuilderRemovePointsUndoCommand(
					d_geometry_builder,
					point_indices));
	std::unique_ptr<QUndoCommand> undo_command(
			new GeometryOperationUndoCommand(
					QObject::tr("delete selected vertices"),
					std::move(delete_vertices_command),
					this,
					d_canvas_tool_workflows));

	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::average_selected_vertex_positions()
{
	if (d_selected_vertex_indices.size() < 2)
	{
		return;
	}

	GPlatesMaths::Vector3D position_sum;
	for (std::set<GeometryBuilder::PointIndex>::const_iterator selected =
				d_selected_vertex_indices.begin();
			selected != d_selected_vertex_indices.end();
			++selected)
	{
		position_sum = position_sum +
				GPlatesMaths::Vector3D(
						d_geometry_builder.get_geometry_point(0, *selected).position_vector());
	}

	if (position_sum.is_zero_magnitude())
	{
		return;
	}

	const GPlatesMaths::PointOnSphere average_position(position_sum.get_normalisation());
	std::vector<GPlatesMaths::PointOnSphere> positions(
			d_selected_vertex_indices.size(),
			average_position);
	move_selected_vertices_to(
			positions,
			false,
			QObject::tr("average selected vertex positions"));
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::clear_vertex_selection()
{
	d_selected_vertex_indices.clear();
	if (d_selected_points_layer_ptr)
	{
		d_selected_points_layer_ptr->clear_rendered_geometries();
	}
	publish_vertex_selection_state();
}


bool
GPlatesViewOperations::MoveVertexGeometryOperation::can_delete_selected_vertices() const
{
	if (d_selected_vertex_indices.empty() || d_geometry_builder.get_num_geometries() == 0)
	{
		return false;
	}

	unsigned int minimum_remaining_points = 0;
	switch (d_geometry_builder.get_geometry_build_type())
	{
	case GPlatesMaths::GeometryType::MULTIPOINT:
		minimum_remaining_points = 1;
		break;
	case GPlatesMaths::GeometryType::POLYLINE:
		minimum_remaining_points = 2;
		break;
	case GPlatesMaths::GeometryType::POLYGON:
		minimum_remaining_points = 3;
		break;
	default:
		return false;
	}

	const unsigned int num_points = d_geometry_builder.get_num_points_in_geometry(0);
	return d_selected_vertex_indices.size() <= num_points &&
			num_points - d_selected_vertex_indices.size() >= minimum_remaining_points;
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::publish_vertex_selection_state()
{
	if (d_is_active)
	{
		d_modify_geometry_state.set_vertex_selection_state(
				static_cast<unsigned int>(d_selected_vertex_indices.size()),
				can_delete_selected_vertices());
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::update_rendered_geometries()
{
	// Clear all RenderedGeometry objects from the render layers first.
	d_lines_layer_ptr->clear_rendered_geometries();
	d_points_layer_ptr->clear_rendered_geometries();
	d_highlight_point_layer_ptr->clear_rendered_geometries();
	d_selected_points_layer_ptr->clear_rendered_geometries();

	// Iterate through the internal geometries (currently only one is supported).
	for (GeometryBuilder::GeometryIndex geom_index = 0;
		geom_index < d_geometry_builder.get_num_geometries();
		++geom_index)
	{
		update_rendered_geometry(geom_index);
	}

	update_selected_rendered_points();
	if ((d_is_vertex_selected || d_is_vertex_highlighted) &&
			d_geometry_builder.get_num_geometries() > 0 &&
			d_selected_vertex_index < d_geometry_builder.get_num_points_in_geometry(0))
	{
		update_highlight_rendered_point(d_selected_vertex_index);
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::update_rendered_geometry(
		GeometryBuilder::GeometryIndex geom_index)
{
	// All types of geometry have the points drawn the same.
	add_rendered_points(geom_index);

	const GPlatesMaths::GeometryType::Value actual_geom_type =
		d_geometry_builder.get_actual_type_of_geometry(geom_index);

	switch (actual_geom_type)
	{
	case GPlatesMaths::GeometryType::POLYLINE:
		add_rendered_lines_for_polyline_on_sphere(geom_index);
		break;

	case GPlatesMaths::GeometryType::POLYGON:
		add_rendered_lines_for_polygon_on_sphere(geom_index);
		break;

	default:
		// Do nothing.
		break;
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::add_rendered_lines_for_polyline_on_sphere(
		GeometryBuilder::GeometryIndex geom_index)
{
	// Get start and end of point sequence in current geometry.
	GeometryBuilder::point_const_iterator_type builder_geom_begin =
		d_geometry_builder.get_geometry_point_begin(geom_index);
	GeometryBuilder::point_const_iterator_type builder_geom_end =
		d_geometry_builder.get_geometry_point_end(geom_index);

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline_on_sphere =
		GPlatesMaths::PolylineOnSphere::create(builder_geom_begin, builder_geom_end);

	RenderedGeometry rendered_geom = RenderedGeometryFactory::create_rendered_polyline_on_sphere(
				polyline_on_sphere,
				GeometryOperationParameters::NOT_IN_FOCUS_COLOUR,
				GeometryOperationParameters::LINE_WIDTH_HINT);

	// Add to the lines layer.
	d_lines_layer_ptr->add_rendered_geometry(rendered_geom);
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::add_rendered_lines_for_polygon_on_sphere(
		GeometryBuilder::GeometryIndex geom_index)
{
	// Get start and end of point sequence in current geometry.
	GeometryBuilder::point_const_iterator_type builder_geom_begin =
		d_geometry_builder.get_geometry_point_begin(geom_index);
	GeometryBuilder::point_const_iterator_type builder_geom_end =
		d_geometry_builder.get_geometry_point_end(geom_index);

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon_on_sphere =
		GPlatesMaths::PolygonOnSphere::create(builder_geom_begin, builder_geom_end);

	RenderedGeometry rendered_geom = RenderedGeometryFactory::create_rendered_polygon_on_sphere(
				polygon_on_sphere,
				GeometryOperationParameters::NOT_IN_FOCUS_COLOUR,
				GeometryOperationParameters::LINE_WIDTH_HINT);

	// Add to the lines layer.
	d_lines_layer_ptr->add_rendered_geometry(rendered_geom);
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::add_rendered_points(
		GeometryBuilder::GeometryIndex geom_index)
{
	GeometryBuilder::point_const_iterator_type builder_geom_begin =
		d_geometry_builder.get_geometry_point_begin(geom_index);
	GeometryBuilder::point_const_iterator_type builder_geom_end =
		d_geometry_builder.get_geometry_point_end(geom_index);

	GeometryBuilder::point_const_iterator_type builder_geom_iter;
	for (builder_geom_iter = builder_geom_begin;
		builder_geom_iter != builder_geom_end;
		++builder_geom_iter)
	{
		const GPlatesMaths::PointOnSphere &point_on_sphere = *builder_geom_iter;

		RenderedGeometry rendered_geom = RenderedGeometryFactory::create_rendered_point_on_sphere(
			point_on_sphere,
			GeometryOperationParameters::FOCUS_COLOUR,
			GeometryOperationParameters::LARGE_POINT_SIZE_HINT);

		// Add to the points layer.
		d_points_layer_ptr->add_rendered_geometry(rendered_geom);
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::update_highlight_rendered_point(
		const GeometryBuilder::PointIndex highlight_point_index)
{
	// Clear any geometry before adding.
	d_highlight_point_layer_ptr->clear_rendered_geometries();

	// Currently only one internal geometry is supported so set geometry index to zero.
	const GeometryBuilder::GeometryIndex geometry_index = 0;

	// Get the highlighted point.
	const GPlatesMaths::PointOnSphere &highlight_point_on_sphere =
			d_geometry_builder.get_geometry_point(geometry_index, highlight_point_index);

	RenderedGeometry rendered_geom = RenderedGeometryFactory::create_rendered_point_on_sphere(
		highlight_point_on_sphere,
		GeometryOperationParameters::HIGHLIGHT_COLOUR,
		GeometryOperationParameters::EXTRA_LARGE_POINT_SIZE_HINT);

	d_highlight_point_layer_ptr->add_rendered_geometry(rendered_geom);
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_selected_rendered_points()
{
	d_selected_points_layer_ptr->clear_rendered_geometries();
	if (d_geometry_builder.get_num_geometries() == 0)
	{
		return;
	}

	const unsigned int num_points = d_geometry_builder.get_num_points_in_geometry(0);
	for (std::set<GeometryBuilder::PointIndex>::const_iterator selected =
				d_selected_vertex_indices.begin();
			selected != d_selected_vertex_indices.end();
			++selected)
	{
		if (*selected >= num_points)
		{
			continue;
		}

		d_selected_points_layer_ptr->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_point_on_sphere(
						d_geometry_builder.get_geometry_point(0, *selected),
						GPlatesGui::Colour::get_green(),
						GeometryOperationParameters::EXTRA_LARGE_POINT_SIZE_HINT));
	}
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_lasso_rendered_geometry()
{
	d_lasso_layer_ptr->clear_rendered_geometries();
	if (d_lasso_points.size() < 2)
	{
		return;
	}

	try
	{
		d_lasso_layer_ptr->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polyline_on_sphere(
						GPlatesMaths::PolylineOnSphere::create(d_lasso_points),
						GeometryOperationParameters::HIGHLIGHT_COLOUR,
						GeometryOperationParameters::HIGHLIGHT_LINE_WIDTH_HINT));
	}
	catch (...)
	{
		// Wait for another distinct mouse sample before attempting to draw again.
	}
}

boost::optional<GPlatesViewOperations::MoveVertexGeometryOperation::secondary_geometry_hit_type>
GPlatesViewOperations::MoveVertexGeometryOperation::find_secondary_geometry_near(
	const GPlatesMaths::PointOnSphere &point_on_sphere)
{
	GPlatesViewOperations::sorted_rendered_geometry_proximity_hits_type sorted_hits;
	
	double proximity_inclusion_threshold = d_nearby_vertex_threshold; 

	GPlatesMaths::ProximityCriteria criteria(point_on_sphere, proximity_inclusion_threshold);
	GPlatesViewOperations::test_vertex_proximity(
		sorted_hits,
		d_rendered_geometry_collection,
		GPlatesViewOperations::RenderedGeometryCollection::RECONSTRUCTION_LAYER,
		criteria);
			
	sorted_rendered_geometry_proximity_hits_type::const_iterator 
		it = sorted_hits.begin(),
		end = sorted_hits.end();
		
		
	GPlatesAppLogic::ReconstructionGeometry::maybe_null_ptr_to_const_type focus_rg =
			d_feature_focus.associated_reconstruction_geometry();	
	
	// We may want to extend this to store all geometries that have a vertex inside the proximity
	// threshold, rather than just the geometry which has the closest vertex. 
	boost::optional<RenderedGeometry> closest_non_focus_rendered_geom;
	double closest_closeness = 0.;
	unsigned int closest_vertex_index = 0;
	
	for (; it != end ; ++it)
	{
		RenderedGeometry rg = it->d_rendered_geom_layer->get_rendered_geometry(
			it->d_rendered_geom_index);
		
		ReconstructionGeometryFinder finder;
		rg.accept_visitor(finder);
		boost::optional<GPlatesAppLogic::ReconstructionGeometry::non_null_ptr_to_const_type>
				recon_geom = finder.get_reconstruction_geometry();
			

		// The focus geometry itself will return a hit from the test_vertex_proximity test,
		// so check that it's not the focus geometry before checking the closeness.
		if (recon_geom && *recon_geom != focus_rg)
		{
			if (it->d_proximity_hit_detail->index())
			{
				unsigned int vertex_index = *(it->d_proximity_hit_detail->index());
				//qDebug() << "Found non-focused geometry, vertex no: " << vertex_index;
				if (it->d_proximity_hit_detail->closeness() > closest_closeness)
				{
					closest_non_focus_rendered_geom.reset(rg);
					closest_vertex_index = vertex_index;
					closest_closeness = it->d_proximity_hit_detail->closeness();
				}
			}
			else
			{
				//qDebug() << "Found non-focused geometry, no vertex information";
			}
		}
	}
	
	// We have found a geometry with a vertex in range.
	// FIXME: may want to extend this to return multiple geometries that have
	// a vertex close to the highlighted vertex. Right now we deal only with the geometry that has
	// the closest within-range vertex.
	if (closest_non_focus_rendered_geom)
	{
		ReconstructionGeometryFinder recon_geom_finder;
		closest_non_focus_rendered_geom->accept_visitor(recon_geom_finder);
		boost::optional<GPlatesAppLogic::ReconstructionGeometry::non_null_ptr_to_const_type>
				recon_geom = recon_geom_finder.get_reconstruction_geometry();


		if (recon_geom)
		{
			const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
					GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
							const GPlatesAppLogic::ReconstructedFeatureGeometry *>(recon_geom.get());
			if (rfg)
			{
				boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
						rfg.get()->reconstruction_plate_id();
				if (d_should_use_plate_id_filter)
				{
					if ( d_filter_plate_id &&
						plate_id &&
						(*plate_id == *d_filter_plate_id))
						{
							return std::make_pair(*recon_geom, closest_vertex_index);
						}
				}
				else
				{
				// No plate-id filter selected, so accept the geometry.
						return std::make_pair(*recon_geom, closest_vertex_index);
				}
			}
		}
	}


	return boost::none;
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_secondary_geometries(
	const GPlatesMaths::PointOnSphere &point_on_sphere)
{
	d_geometry_builder.clear_secondary_geometries();

	const boost::optional<secondary_geometry_hit_type> hit =
			find_secondary_geometry_near(point_on_sphere);
	if (hit)
	{
		d_geometry_builder.add_secondary_geometry(hit->first, hit->second);
	}
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_secondary_geometries_for_selection()
{
	// Snapping was previously resolved only for the single highlighted vertex, so dragging a
	// group left coincident vertices in neighbouring geometries behind. Resolve one for every
	// vertex in the selection instead, and remember which vertex each belongs to, so the drag
	// can carry them along.
	d_geometry_builder.clear_secondary_geometries();
	d_drag_secondary_geometries.clear();
	d_drag_secondary_geometries.reserve(d_selected_vertex_indices.size());

	// Iterated in the same order as 'd_drag_original_points' and the points passed to the move
	// command, so the two stay parallel.
	for (std::set<GeometryBuilder::PointIndex>::const_iterator selected =
				d_selected_vertex_indices.begin();
		selected != d_selected_vertex_indices.end();
		++selected)
	{
		std::vector<SecondaryGeometry> secondary_geometries_for_vertex;

		const boost::optional<secondary_geometry_hit_type> hit =
				find_secondary_geometry_near(d_geometry_builder.get_geometry_point(0, *selected));
		if (hit)
		{
			// Let the geometry builder do the conversion to SecondaryGeometry, then take a copy
			// of what it built. It rejects anything that isn't a reconstructed feature geometry,
			// so only record one if it actually added it.
			const std::vector<SecondaryGeometry>::size_type num_before =
					d_geometry_builder.get_secondary_geometries().size();
			d_geometry_builder.add_secondary_geometry(hit->first, hit->second);
			if (d_geometry_builder.get_secondary_geometries().size() > num_before)
			{
				secondary_geometries_for_vertex.push_back(
						d_geometry_builder.get_secondary_geometries().back());
			}
		}

		d_drag_secondary_geometries.push_back(secondary_geometries_for_vertex);
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::release_click()
{
	d_geometry_builder.clear_secondary_geometries();

	if (d_is_vertex_highlighted)
	{
		d_selected_vertex_indices.clear();
		d_selected_vertex_indices.insert(d_selected_vertex_index);
	}
	else
	{
		d_selected_vertex_indices.clear();
	}
	publish_vertex_selection_state();
	
	// This will clear the rendered geometry layers and re-draw the "normal" move vertex geometries.
	update_rendered_geometries();
	if (d_is_vertex_highlighted)
	{
		update_highlight_rendered_point(d_selected_vertex_index);
	}
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::update_rendered_secondary_geometries()
{
	if (d_is_vertex_highlighted)
	{
		// FIXME: We're only grabbing the first of the secondary_geometries here. 
		boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> geom = 
			d_geometry_builder.get_secondary_geometry();

		if (geom)
		{
			RenderedGeometryLayerFiller filler(d_points_layer_ptr,d_lines_layer_ptr);
			(*geom)->accept_visitor(filler);	
		}
	
	}

}

void
GPlatesViewOperations::MoveVertexGeometryOperation::left_press(
		const GPlatesMaths::PointOnSphere &oriented_pos_on_sphere, 
		const double &closeness_inclusion_threshold)
{
#if 0
	qDebug() << "is_vertex_highlighted: " << d_is_vertex_highlighted;
	qDebug() << "should check: " << d_should_check_nearby_vertices;	
#endif
	d_geometry_builder.clear_secondary_geometries();
	d_drag_secondary_geometries.clear();
	// If we're near a vertex in the focused geometry, then check other geometries in the model too.
	if (d_is_vertex_highlighted && d_should_check_nearby_vertices)
	{
		if (d_selected_vertex_indices.size() > 1)
		{
			// Dragging a group. Resolve snapping for every vertex in the selection, not just the
			// highlighted one - a group move is exactly when shared boundaries are most likely to
			// be torn apart, and the most tedious to repair by hand afterwards.
			update_secondary_geometries_for_selection();
		}
		else
		{
			// Use the highlighted point (rather than the mouse point) for searching for secondary geometries.
			const GPlatesMaths::PointOnSphere &highlight_point_on_sphere =
				d_geometry_builder.get_geometry_point(0, d_selected_vertex_index);

			update_secondary_geometries(highlight_point_on_sphere);
		}

		update_rendered_secondary_geometries();
		update_highlight_secondary_vertices();
		//FIXME: find a better colour for highlighting the secondary geometries.
		//FIMXE: highlight the nearest vertex in any of the secondary geometries.
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::handle_snap_vertices_setup_changed(
		bool should_check_nearby_vertices,
		double threshold,
		bool should_use_plate_id,
		GPlatesModel::integer_plate_id_type plate_id)
{
	d_should_check_nearby_vertices = should_check_nearby_vertices;
	d_nearby_vertex_threshold = std::cos(GPlatesMaths::convert_deg_to_rad(threshold));
	d_should_use_plate_id_filter = should_use_plate_id;
	d_filter_plate_id.reset(plate_id);
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::handle_delete_selected_vertices_requested()
{
	if (d_is_active)
	{
		delete_selected_vertices();
	}
}


void
GPlatesViewOperations::MoveVertexGeometryOperation::handle_average_selected_vertex_positions_requested()
{
	if (d_is_active)
	{
		average_selected_vertex_positions();
	}
}

void
GPlatesViewOperations::MoveVertexGeometryOperation::update_highlight_secondary_vertices()
{
	boost::optional<GPlatesMaths::PointOnSphere> point = d_geometry_builder.get_secondary_vertex();
	
	if (point)
	{
		//qDebug() << "Found secondary highlight point";
		RenderedGeometry rendered_geom = RenderedGeometryFactory::create_rendered_point_on_sphere(
			*point,
			GeometryOperationParameters::HIGHLIGHT_COLOUR,
			GeometryOperationParameters::EXTRA_LARGE_POINT_SIZE_HINT);

		d_highlight_point_layer_ptr->add_rendered_geometry(rendered_geom);
	}
}
