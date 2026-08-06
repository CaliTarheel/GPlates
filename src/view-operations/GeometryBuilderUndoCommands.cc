/* $Id$ */

/**
 * \file 
 * $Revision$
 * $Date$ 
 * 
 * Copyright (C) 2010 The University of Sydney, Australia
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
#include "GeometryBuilderUndoCommands.h"

	
void
GPlatesViewOperations::GeometryBuilderInsertPointUndoCommand::redo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// Add point to geometry builder.
	// This will also cause GeometryBuilder to emit a signal to
	// its observers.
	d_undo_operation = d_geometry_builder.insert_point_into_current_geometry(
		d_point_index_to_insert_at,
		d_oriented_pos_on_globe);
}


void
GPlatesViewOperations::GeometryBuilderInsertPointUndoCommand::undo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// The undo operation will also cause GeometryBuilder to emit
	// a signal to its observers.
	d_geometry_builder.undo(d_undo_operation);
}


void
GPlatesViewOperations::GeometryBuilderRemovePointUndoCommand::redo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// Remove point from geometry builder.
	// This will also cause GeometryBuilder to emit a signal to
	// its observers.
	d_undo_operation = d_geometry_builder.remove_point_from_current_geometry(
		d_point_index_to_remove_at);
}


void
GPlatesViewOperations::GeometryBuilderRemovePointUndoCommand::undo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// The undo operation will also cause GeometryBuilder to emit
	// a signal to its observers.
	d_geometry_builder.undo(d_undo_operation);
}


void
GPlatesViewOperations::GeometryBuilderMovePointUndoCommand::redo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// Move point in geometry builder.
	// This will also cause GeometryBuilder to emit a signal to
	// its observers.
	std::vector<GPlatesMaths::PointOnSphere> secondary_points;
	for (int i = 0; i < static_cast<int>(d_secondary_geometries.size()) ; ++i)
	{
		secondary_points.push_back(d_oriented_pos_on_globe);
	}

	d_undo_operation = d_geometry_builder.move_point_in_current_geometry(
		d_point_index_to_move,
		d_oriented_pos_on_globe,
		d_secondary_geometries,
		secondary_points,
		d_is_intermediate_move);

}


void
GPlatesViewOperations::GeometryBuilderMovePointUndoCommand::undo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// The undo operation will also cause GeometryBuilder to emit
	// a signal to its observers.
	d_geometry_builder.undo(d_undo_operation);
}


bool
GPlatesViewOperations::GeometryBuilderMovePointUndoCommand::mergeWith(
		const QUndoCommand *other_command)
{
	// If other command is same type as us then coalesce its command into us.
	const GeometryBuilderMovePointUndoCommand *other_move_command =
		dynamic_cast<const GeometryBuilderMovePointUndoCommand *>(other_command);

	if (other_move_command != NULL)
	{
		//
		// Merge the other move vertex command with ours.
		//

		// Use the other command's destination vertex position.
		d_oriented_pos_on_globe = other_move_command->d_oriented_pos_on_globe;

		// If the other command is not an intermediate move then the merged
		// command is also not an intermediate move.
		if (!other_move_command->d_is_intermediate_move)
		{
			d_is_intermediate_move = false;
		}

		// But keep our undo operation.

		return true;
	}

	return false;
}


void
GPlatesViewOperations::GeometryBuilderMovePointsUndoCommand::redo()
{
	RenderedGeometryCollection::UpdateGuard update_guard;

	d_undo_operations.clear();
	d_undo_operations.reserve(d_points_to_move.size());

	for (indexed_point_seq_type::size_type point = 0; point < d_points_to_move.size(); ++point)
	{
		// Any geometries snapped to this point. Copied because 'move_point_in_current_geometry'
		// takes them by non-const reference, and because redo() may run more than once.
		//
		// If Snap Vertices is off, or nothing was within the threshold of this point, this is
		// empty and the call below behaves exactly as it did before snapping was considered.
		std::vector<GPlatesViewOperations::SecondaryGeometry> secondary_geometries;
		if (point < d_secondary_geometries_per_point.size())
		{
			secondary_geometries = d_secondary_geometries_per_point[point];
		}

		// A snapped vertex is coincident with the vertex it is snapped to, so it moves to that
		// vertex's new position. This matches what GeometryBuilderMovePointUndoCommand does for
		// a single-vertex drag.
		std::vector<GPlatesMaths::PointOnSphere> secondary_points(
				secondary_geometries.size(),
				d_points_to_move[point].second);

		const bool is_intermediate_move =
				d_is_intermediate_move || point + 1 < d_points_to_move.size();

		d_undo_operations.push_back(
				d_geometry_builder.move_point_in_current_geometry(
						d_points_to_move[point].first,
						d_points_to_move[point].second,
						secondary_geometries,
						secondary_points,
						is_intermediate_move));
	}
}


void
GPlatesViewOperations::GeometryBuilderMovePointsUndoCommand::undo()
{
	RenderedGeometryCollection::UpdateGuard update_guard;

	for (std::vector<GeometryBuilder::UndoOperation>::reverse_iterator undo = d_undo_operations.rbegin();
			undo != d_undo_operations.rend();
			++undo)
	{
		d_geometry_builder.undo(*undo);
	}
}


bool
GPlatesViewOperations::GeometryBuilderMovePointsUndoCommand::mergeWith(
		const QUndoCommand *other_command)
{
	const GeometryBuilderMovePointsUndoCommand *other_move_command =
			dynamic_cast<const GeometryBuilderMovePointsUndoCommand *>(other_command);
	if (other_move_command == NULL ||
			&other_move_command->d_geometry_builder != &d_geometry_builder ||
			other_move_command->d_points_to_move.size() != d_points_to_move.size())
	{
		return false;
	}

	for (indexed_point_seq_type::size_type point = 0; point < d_points_to_move.size(); ++point)
	{
		if (other_move_command->d_points_to_move[point].first != d_points_to_move[point].first)
		{
			return false;
		}
	}

	d_points_to_move = other_move_command->d_points_to_move;
	// Take the later command's snapped geometries too. They are resolved once when the drag starts
	// and so are normally identical, but the merged command should not keep a stale copy.
	d_secondary_geometries_per_point = other_move_command->d_secondary_geometries_per_point;
	if (!other_move_command->d_is_intermediate_move)
	{
		d_is_intermediate_move = false;
	}

	return true;
}


void
GPlatesViewOperations::GeometryBuilderRemovePointsUndoCommand::redo()
{
	RenderedGeometryCollection::UpdateGuard update_guard;

	d_undo_operations.clear();
	d_undo_operations.reserve(d_point_indices_to_remove.size());
	for (std::vector<GeometryBuilder::PointIndex>::const_iterator point_index =
				d_point_indices_to_remove.begin();
			point_index != d_point_indices_to_remove.end();
			++point_index)
	{
		d_undo_operations.push_back(
				d_geometry_builder.remove_point_from_current_geometry(*point_index));
	}
}


void
GPlatesViewOperations::GeometryBuilderRemovePointsUndoCommand::undo()
{
	RenderedGeometryCollection::UpdateGuard update_guard;

	for (std::vector<GeometryBuilder::UndoOperation>::reverse_iterator undo = d_undo_operations.rbegin();
			undo != d_undo_operations.rend();
			++undo)
	{
		d_geometry_builder.undo(*undo);
	}
}


bool
GPlatesViewOperations::GeometryBuilderSetGeometryTypeUndoCommand::mergeWith(
		const QUndoCommand *other_command)
{
	// Is other command same type as us ?
	const GeometryBuilderSetGeometryTypeUndoCommand *other_set_geom_type_command =
		dynamic_cast<const GeometryBuilderSetGeometryTypeUndoCommand *>(other_command);

	if (other_set_geom_type_command != NULL)
	{
		// We use our undo operation for undo'ing but use their geometry type
		// for redo'ing.
		d_geom_type_to_build = other_set_geom_type_command->d_geom_type_to_build;

		return true;
	}

	return false;
}


void
GPlatesViewOperations::GeometryBuilderSetGeometryTypeUndoCommand::redo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// Set geometry type to build in geometry builder.
	// This will also cause GeometryBuilder to emit a signal to
	// its observers.
	d_undo_operation = d_geometry_builder.set_geometry_type_to_build(
		d_geom_type_to_build);
}


void
GPlatesViewOperations::GeometryBuilderSetGeometryTypeUndoCommand::undo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	// The undo operation will also cause GeometryBuilder to emit
	// a signal to its observers.
	d_geometry_builder.undo(d_undo_operation);
}


void
GPlatesViewOperations::GeometryBuilderClearAllGeometries::redo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	d_undo_operation = d_geometry_builder.clear_all_geometries();
}


void
GPlatesViewOperations::GeometryBuilderClearAllGeometries::undo()
{
	// Delay any notification of changes to the rendered geometry collection
	// until end of current scope block.
	RenderedGeometryCollection::UpdateGuard update_guard;

	d_geometry_builder.undo(d_undo_operation);
}



