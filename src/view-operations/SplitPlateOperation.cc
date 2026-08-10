/* $Id$ */

/**
 * \file
 * Implements the World Building "Split Plate" operation.
 *
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <QObject>
#include <QStringList>
#include <QUndoCommand>

#include "SplitPlateOperation.h"

#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"

#include "maths/GeometryCrossing.h"
#include "maths/GeometryIntersect.h"
#include "maths/MathsUtils.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"


namespace
{
	typedef std::vector<GPlatesMaths::PointOnSphere> point_seq_type;
	typedef std::vector<point_seq_type> ring_seq_type;


	bool
	points_are_close(
			const GPlatesMaths::PointOnSphere &point1,
			const GPlatesMaths::PointOnSphere &point2)
	{
		return GPlatesMaths::dot(
				point1.position_vector(),
				point2.position_vector()).dval() >=
			GPlatesMaths::GeometryIntersect::Intersection::get_on_segment_start_threshold_cosine();
	}


	void
	append_point_if_distinct(
			point_seq_type &points,
			const GPlatesMaths::PointOnSphere &point)
	{
		if (points.empty() || !points_are_close(points.back(), point))
		{
			points.push_back(point);
		}
	}


	void
	remove_duplicate_closing_point(
			point_seq_type &points)
	{
		if (points.size() > 1 && points_are_close(points.front(), points.back()))
		{
			points.pop_back();
		}
	}


	point_seq_type
	create_polygon_boundary_path(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::GeometryIntersect::Intersection &start,
			const GPlatesMaths::GeometryIntersect::Intersection &end)
	{
		point_seq_type path;
		append_point_if_distinct(path, start.position);

		const unsigned int num_vertices = polygon.number_of_vertices_in_exterior_ring();

		// When both intersections are on the same segment and the end follows the
		// start, the forward boundary path contains no original polygon vertices.
		if (start.segment_index1 == end.segment_index1 &&
				start.angle_in_segment1 < end.angle_in_segment1)
		{
			append_point_if_distinct(path, end.position);
			return path;
		}

		unsigned int vertex_index = (start.segment_index1 + 1) % num_vertices;
		for (unsigned int visited = 0; visited < num_vertices; ++visited)
		{
			append_point_if_distinct(path, polygon.get_exterior_ring_vertex(vertex_index));
			if (vertex_index == end.segment_index1)
			{
				break;
			}
			vertex_index = (vertex_index + 1) % num_vertices;
		}

		append_point_if_distinct(path, end.position);
		return path;
	}


	point_seq_type
	create_cut_path(
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Intersection &start,
			const GPlatesMaths::GeometryIntersect::Intersection &end)
	{
		point_seq_type path;
		append_point_if_distinct(path, start.position);

		// A single great-circle segment can enter and leave a polygon. In that
		// common case there are no original polyline vertices between the two
		// intersections.
		if (start.segment_index2 == end.segment_index2 &&
				start.angle_in_segment2 < end.angle_in_segment2)
		{
			append_point_if_distinct(path, end.position);
			return path;
		}

		const unsigned int num_vertices = polyline.number_of_vertices();
		for (unsigned int vertex_index = start.segment_index2 + 1;
				vertex_index <= end.segment_index2 && vertex_index < num_vertices;
				++vertex_index)
		{
			append_point_if_distinct(path, polyline.get_vertex(vertex_index));
		}

		append_point_if_distinct(path, end.position);
		return path;
	}


	point_seq_type
	join_boundary_and_cut(
			const point_seq_type &boundary_path,
			const point_seq_type &cut_path,
			bool reverse_cut_path)
	{
		point_seq_type ring;
		for (point_seq_type::const_iterator point_iter = boundary_path.begin();
				point_iter != boundary_path.end();
				++point_iter)
		{
			append_point_if_distinct(ring, *point_iter);
		}

		if (reverse_cut_path)
		{
			for (point_seq_type::const_reverse_iterator point_iter = cut_path.rbegin();
					point_iter != cut_path.rend();
					++point_iter)
			{
				append_point_if_distinct(ring, *point_iter);
			}
		}
		else
		{
			for (point_seq_type::const_iterator point_iter = cut_path.begin();
					point_iter != cut_path.end();
					++point_iter)
			{
				append_point_if_distinct(ring, *point_iter);
			}
		}

		remove_duplicate_closing_point(ring);
		return ring;
	}


	GPlatesMaths::PointOnSphere
	reverse_reconstruct_point(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstructed_feature_geometry)
	{
		const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id =
				reconstructed_feature_geometry.reconstruction_plate_id();
		if (!plate_id)
		{
			return point;
		}

		return GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
				point,
				*plate_id,
				*reconstructed_feature_geometry.get_reconstruction_tree(),
				true /* reverse_reconstruct */);
	}


	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	reverse_reconstruct_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &reconstructed_feature_geometry)
	{
		point_seq_type exterior_ring;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
					polygon.exterior_ring_vertex_begin();
				point_iter != polygon.exterior_ring_vertex_end();
				++point_iter)
		{
			exterior_ring.push_back(reverse_reconstruct_point(*point_iter, reconstructed_feature_geometry));
		}

		ring_seq_type interior_rings;
		for (unsigned int ring_index = 0;
				ring_index < polygon.number_of_interior_rings();
				++ring_index)
		{
			point_seq_type interior_ring;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
						polygon.interior_ring_vertex_begin(ring_index);
					point_iter != polygon.interior_ring_vertex_end(ring_index);
					++point_iter)
			{
				interior_ring.push_back(reverse_reconstruct_point(*point_iter, reconstructed_feature_geometry));
			}
			interior_rings.push_back(interior_ring);
		}

		return GPlatesMaths::PolygonOnSphere::create(exterior_ring, interior_rings, true);
	}


	struct SplitPolygonResult
	{
		SplitPolygonResult(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon1_,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon2_) :
			polygon1(polygon1_),
			polygon2(polygon2_)
		{  }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon1;
		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon2;
	};


	boost::optional<SplitPolygonResult>
	split_polygon(
			QString &error_message,
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &polygon_reconstruction)
	{
		GPlatesMaths::GeometryIntersect::Graph intersection_graph;
		if (!GPlatesMaths::GeometryIntersect::intersect(
				intersection_graph,
				polygon,
				polyline,
				true /* include polygon interior rings */))
		{
			error_message = QObject::tr(
					"The selected polyline does not cross the polygon boundary. "
					"The cutter must pass completely through the polygon.");
			return boost::none;
		}

		const unsigned int num_exterior_segments = polygon.number_of_segments_in_exterior_ring();
		for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator intersection_iter =
					intersection_graph.unordered_intersections.begin();
				intersection_iter != intersection_graph.unordered_intersections.end();
				++intersection_iter)
		{
			if (intersection_iter->segment_index1 >= num_exterior_segments)
			{
				error_message = QObject::tr(
						"The cutter intersects an interior ring (a polygon hole). "
						"This first version can preserve holes, but cannot cut through one.");
				return boost::none;
			}
		}

		GPlatesMaths::GeometryIntersect::Graph crossing_graph =
				GPlatesMaths::GeometryCrossing::find_crossings(intersection_graph);

		if (crossing_graph.unordered_intersections.size() != 2 ||
				crossing_graph.geometry2_ordered_intersections.size() != 2)
		{
			error_message = QObject::tr(
					"The cutter must cross the polygon's exterior boundary exactly twice. "
					"This polyline produces %1 crossings.")
					.arg(crossing_graph.unordered_intersections.size());
			return boost::none;
		}

		// Reject boundary overlaps and tangential touches. They are ambiguous when
		// constructing two closed child rings.
		if (intersection_graph.unordered_intersections.size() !=
				crossing_graph.unordered_intersections.size())
		{
			error_message = QObject::tr(
					"The cutter touches or overlaps the polygon boundary. "
					"Move it so it crosses the boundary cleanly at two points.");
			return boost::none;
		}

		const GPlatesMaths::GeometryIntersect::Intersection &intersection1 =
				crossing_graph.unordered_intersections[
						crossing_graph.geometry2_ordered_intersections[0]];
		const GPlatesMaths::GeometryIntersect::Intersection &intersection2 =
				crossing_graph.unordered_intersections[
						crossing_graph.geometry2_ordered_intersections[1]];

		const point_seq_type cut_path = create_cut_path(polyline, intersection1, intersection2);
		const point_seq_type boundary_path1 =
				create_polygon_boundary_path(polygon, intersection1, intersection2);
		const point_seq_type boundary_path2 =
				create_polygon_boundary_path(polygon, intersection2, intersection1);

		const point_seq_type exterior_ring1 =
				join_boundary_and_cut(boundary_path1, cut_path, true);
		const point_seq_type exterior_ring2 =
				join_boundary_and_cut(boundary_path2, cut_path, false);

		if (GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					exterior_ring1, true) != GPlatesMaths::PolygonOnSphere::VALID ||
				GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					exterior_ring2, true) != GPlatesMaths::PolygonOnSphere::VALID)
		{
			error_message = QObject::tr(
					"The cut would create an invalid polygon. "
					"Try a cutter whose crossings are farther from polygon vertices.");
			return boost::none;
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exterior_polygon1 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring1, true);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exterior_polygon2 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring2, true);

		// Interior rings that are not cut remain intact and belong to whichever
		// new exterior contains their first vertex.
		ring_seq_type interior_rings1;
		ring_seq_type interior_rings2;
		for (unsigned int ring_index = 0;
				ring_index < polygon.number_of_interior_rings();
				++ring_index)
		{
			point_seq_type interior_ring(
					polygon.interior_ring_vertex_begin(ring_index),
					polygon.interior_ring_vertex_end(ring_index));
			if (exterior_polygon1->is_point_in_polygon(interior_ring.front()))
			{
				interior_rings1.push_back(interior_ring);
			}
			else if (exterior_polygon2->is_point_in_polygon(interior_ring.front()))
			{
				interior_rings2.push_back(interior_ring);
			}
			else
			{
				error_message = QObject::tr(
						"An interior ring could not be assigned to either output polygon.");
				return boost::none;
			}
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type reconstructed_polygon1 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring1, interior_rings1, true);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type reconstructed_polygon2 =
				GPlatesMaths::PolygonOnSphere::create(exterior_ring2, interior_rings2, true);

		return SplitPolygonResult(
				reverse_reconstruct_polygon(*reconstructed_polygon1, polygon_reconstruction),
				reverse_reconstruct_polygon(*reconstructed_polygon2, polygon_reconstruction));
	}


	class SplitPlateUndoCommand :
			public QUndoCommand
	{
	public:
		/**
		 * One polygon's worth of split work: which feature/property to overwrite, and the two
		 * resulting polygons (the first stays on the original feature, the second becomes a
		 * clone). Grouping all of a batch's splits into one QUndoCommand means Undo reverses
		 * every polygon in the batch together, not one split at a time.
		 */
		struct Split
		{
			Split(
					const GPlatesModel::FeatureHandle::weak_ref &polygon_feature_,
					const GPlatesModel::FeatureHandle::iterator &polygon_property_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon1_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon2_) :
				polygon_feature(polygon_feature_),
				polygon_property(polygon_property_),
				polygon1(polygon1_),
				polygon2(polygon2_)
			{  }

			GPlatesModel::FeatureHandle::weak_ref polygon_feature;
			GPlatesModel::FeatureHandle::iterator polygon_property;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon1;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon2;
		};

		SplitPlateUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<Split> &splits) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_first_redo(true)
		{
			setText(splits.size() == 1
					? QObject::tr("split plate")
					: QObject::tr("split %1 plates").arg(static_cast<unsigned int>(splits.size())));
			for (std::vector<Split>::const_iterator split_iter = splits.begin();
					split_iter != splits.end(); ++split_iter)
			{
				d_entries.push_back(Entry(*split_iter));
			}
		}

		virtual
		void
		redo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			for (std::vector<Entry>::iterator entry_iter = d_entries.begin();
					entry_iter != d_entries.end(); ++entry_iter)
			{
				Entry &entry = *entry_iter;
				if (!entry.split.polygon_feature.is_valid() ||
						!entry.split.polygon_property.is_still_valid())
				{
					continue;
				}

				GPlatesModel::FeatureCollectionHandle *feature_collection =
						entry.split.polygon_feature->parent_ptr();
				if (!feature_collection)
				{
					continue;
				}

				if (d_first_redo)
				{
					GPlatesModel::FeatureHandle::non_null_ptr_type new_feature =
							GPlatesModel::FeatureHandle::create(entry.split.polygon_feature->feature_type());

					for (GPlatesModel::FeatureHandle::iterator property_iter =
							entry.split.polygon_feature->begin();
							property_iter != entry.split.polygon_feature->end();
							++property_iter)
					{
						GPlatesModel::FeatureHandle::iterator new_property_iter =
								new_feature->add((*property_iter)->clone());
						if (property_iter == entry.split.polygon_property)
						{
							entry.new_polygon_property = new_property_iter;
						}
					}

					new_feature->set(
							entry.new_polygon_property,
							create_geometry_property(entry.split.polygon_property, entry.split.polygon2));
					entry.new_feature = new_feature;
					entry.feature_collection = feature_collection->reference();
				}

				entry.split.polygon_feature->set(
						entry.split.polygon_property,
						create_geometry_property(entry.split.polygon_property, entry.split.polygon1));
				entry.feature_collection->add(*entry.new_feature);
			}
			d_first_redo = false;

			notification_guard.release_guard();
		}

		virtual
		void
		undo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			for (std::vector<Entry>::reverse_iterator entry_iter = d_entries.rbegin();
					entry_iter != d_entries.rend(); ++entry_iter)
			{
				Entry &entry = *entry_iter;
				if (!entry.split.polygon_feature.is_valid() ||
						!entry.split.polygon_property.is_still_valid() || !entry.new_feature)
				{
					continue;
				}

				entry.split.polygon_feature->set(
						entry.split.polygon_property, entry.original_geometry_property);
				(*entry.new_feature)->remove_from_parent();
			}

			notification_guard.release_guard();
		}

	private:
		static
		GPlatesModel::TopLevelProperty::non_null_ptr_type
		create_geometry_property(
				const GPlatesModel::FeatureHandle::iterator &property,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
		{
			GPlatesModel::TopLevelProperty::non_null_ptr_type property_clone = (*property)->clone();
			GPlatesFeatureVisitors::GeometrySetter geometry_setter(polygon);
			geometry_setter.set_geometry(property_clone.get());
			return property_clone;
		}

		struct Entry
		{
			explicit Entry(
					const Split &split_) :
				split(split_),
				original_geometry_property((*split_.polygon_property)->clone())
			{  }

			Split split;
			GPlatesModel::TopLevelProperty::non_null_ptr_type original_geometry_property;
			boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> new_feature;
			GPlatesModel::FeatureHandle::iterator new_polygon_property;
			GPlatesModel::FeatureCollectionHandle::weak_ref feature_collection;
		};

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<Entry> d_entries;
		bool d_first_redo;
	};
}


GPlatesViewOperations::SplitPlateOperation::SplitPlateOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_model_interface(application_state.get_model_interface()),
	d_selection_mode(NOT_SELECTING)
{  }


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::arm_polygon_selection()
{
	d_selection_mode = SELECTING_POLYGON;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the polygon to split on the globe or map."));
}


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::arm_polyline_selection()
{
	if (d_captured_polygons.empty())
	{
		return Result(OPERATION_ERROR, QObject::tr("Select at least one polygon before choosing a cutting polyline."));
	}
	d_selection_mode = SELECTING_POLYLINE;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the polyline that cuts the polygon(s)."));
}


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}

	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selection has no editable reconstructed geometry. Selection remains armed."));
	}

	boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> reconstructed_feature_geometry =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!reconstructed_feature_geometry)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Split Plate supports reconstructed polygon and polyline features only. Selection remains armed."));
	}

	const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type focused_geometry =
			(*reconstructed_feature_geometry)->reconstructed_geometry();
	const double current_reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (d_selection_mode == SELECTING_POLYGON)
	{
		const GPlatesMaths::PolygonOnSphere *focused_polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(focused_geometry.get());
		if (!focused_polygon)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selection must be a polygon. Selection remains armed."));
		}

		const GPlatesModel::FeatureHandle::iterator geometry_property =
				(*reconstructed_feature_geometry)->property();
		if (!geometry_property.is_still_valid())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected polygon is not backed by an editable geometry property."));
		}

		if (!d_captured_polygons.empty() &&
				std::fabs(current_reconstruction_time - d_captured_polygons.front().reconstruction_time) > 1e-9)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The View time changed since the first polygon was selected. Return to %1 Ma or clear the polygons and start again.")
							.arg(d_captured_polygons.front().reconstruction_time, 0, 'f', 2));
		}

		const GPlatesModel::FeatureHandle::weak_ref focused_feature = d_feature_focus.focused_feature();
		for (std::vector<CapturedPolygon>::const_iterator polygon_iter = d_captured_polygons.begin();
				polygon_iter != d_captured_polygons.end(); ++polygon_iter)
		{
			if (polygon_iter->feature == focused_feature &&
					(*polygon_iter->geometry_property).get() == (*geometry_property).get())
			{
				return Result(OPERATION_ERROR,
						QObject::tr("That polygon is already selected. Selection remains armed."));
			}
		}

		d_captured_polygons.push_back(CapturedPolygon(
				focused_feature,
				geometry_property,
				(*reconstructed_feature_geometry)->get_non_null_pointer_to_const(),
				focused_polygon->get_non_null_pointer(),
				current_reconstruction_time));
		// A newly added polygon invalidates any previously captured polyline, since it was
		// only ever meant to cut the polygon set it was picked alongside.
		d_captured_polyline = boost::none;
		d_selection_mode = NOT_SELECTING;

		return Result(POLYGON_CAPTURED,
				QObject::tr("Polygon %1 captured. Add another, or choose the cutting polyline.")
						.arg(static_cast<unsigned int>(d_captured_polygons.size())));
	}

	// SELECTING_POLYLINE.
	if (d_captured_polygons.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The captured polygons are no longer available. Select them again to restart."));
	}
	if (std::fabs(current_reconstruction_time - d_captured_polygons.front().reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The View time changed after the polygon(s) were selected. Return to %1 Ma or select the polygons again.")
						.arg(d_captured_polygons.front().reconstruction_time, 0, 'f', 2));
	}

	const GPlatesMaths::PolylineOnSphere *focused_polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(focused_geometry.get());
	if (!focused_polyline)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The cutting selection must be a polyline. Selection remains armed."));
	}

	d_captured_polyline = CapturedPolyline(
			focused_polyline->get_non_null_pointer(),
			current_reconstruction_time);
	d_selection_mode = NOT_SELECTING;

	return Result(POLYLINE_CAPTURED,
			QObject::tr("Cutting polyline captured. Press Commit to split the polygon(s)."));
}


void
GPlatesViewOperations::SplitPlateOperation::clear_polygons()
{
	d_captured_polygons.clear();
	// The polyline was only ever meant to cut the polygon set that is now gone.
	d_captured_polyline = boost::none;
}


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::commit()
{
	if (d_captured_polygons.empty() || !d_captured_polyline)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select at least one polygon and a cutting polyline before committing."));
	}
	for (std::vector<CapturedPolygon>::const_iterator polygon_iter = d_captured_polygons.begin();
			polygon_iter != d_captured_polygons.end(); ++polygon_iter)
	{
		if (!polygon_iter->feature.is_valid() || !polygon_iter->geometry_property.is_still_valid())
		{
			reset();
			return Result(OPERATION_ERROR,
					QObject::tr("A captured polygon is no longer available. The Split Plate selection was reset."));
		}
	}
	if (std::fabs(d_captured_polyline->reconstruction_time - d_captured_polygons.front().reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The View time changed between selecting the polygon(s) and the polyline. Select them again at the same time."));
	}

	// Every polygon must split cleanly before anything commits - a batch either all succeeds
	// or nothing changes, rather than leaving some plates split and others not because a later
	// one in the list happened to fail.
	std::vector<SplitPlateUndoCommand::Split> splits;
	for (std::vector<CapturedPolygon>::const_iterator polygon_iter = d_captured_polygons.begin();
			polygon_iter != d_captured_polygons.end(); ++polygon_iter)
	{
		QString error_message;
		boost::optional<SplitPolygonResult> split_result;
		try
		{
			split_result = split_polygon(
					error_message,
					*polygon_iter->polygon,
					*d_captured_polyline->polyline,
					*polygon_iter->reconstructed_feature_geometry);
		}
		catch (const std::exception &exception)
		{
			error_message = QObject::tr("Could not construct the two output polygons: %1").arg(exception.what());
		}
		catch (...)
		{
			error_message = QObject::tr("Could not construct the two output polygons because the geometry is invalid.");
		}

		if (!split_result)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Polygon %1: %2").arg(polygon_iter->feature->feature_id().get().qstring()).arg(error_message));
		}

		splits.push_back(SplitPlateUndoCommand::Split(
				polygon_iter->feature,
				polygon_iter->geometry_property,
				split_result->polygon1,
				split_result->polygon2));
	}

	const unsigned int split_count = static_cast<unsigned int>(splits.size());
	std::unique_ptr<QUndoCommand> undo_command(
			new SplitPlateUndoCommand(d_feature_focus, d_model_interface, splits));

	reset();
	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());

	return Result(
			SPLIT_COMPLETED,
			split_count == 1
					? QObject::tr(
							"Plate split completed. The original feature is one polygon and a cloned feature is the other. "
							"Use Edit > Undo to put them back together.")
					: QObject::tr(
							"%1 plates split as one edit. Each original feature is one polygon and a cloned feature is the "
							"other. Use Edit > Undo to put them all back together.").arg(split_count));
}


void
GPlatesViewOperations::SplitPlateOperation::reset()
{
	d_selection_mode = NOT_SELECTING;
	d_captured_polygons.clear();
	d_captured_polyline = boost::none;
}


QString
GPlatesViewOperations::SplitPlateOperation::polygon_status() const
{
	if (d_captured_polygons.empty())
	{
		return QObject::tr("No polygon selected.");
	}
	if (d_captured_polygons.size() == 1)
	{
		return QObject::tr("Polygon: %1 at %2 Ma")
				.arg(d_captured_polygons.front().feature->feature_id().get().qstring())
				.arg(d_captured_polygons.front().reconstruction_time, 0, 'f', 2);
	}
	QStringList ids;
	for (std::vector<CapturedPolygon>::const_iterator polygon_iter = d_captured_polygons.begin();
			polygon_iter != d_captured_polygons.end(); ++polygon_iter)
	{
		ids << polygon_iter->feature->feature_id().get().qstring();
	}
	return QObject::tr("%1 polygons selected at %2 Ma: %3")
			.arg(static_cast<unsigned int>(d_captured_polygons.size()))
			.arg(d_captured_polygons.front().reconstruction_time, 0, 'f', 2)
			.arg(ids.join(", "));
}


QString
GPlatesViewOperations::SplitPlateOperation::polyline_status() const
{
	return d_captured_polyline
			? QObject::tr("Cutting polyline selected.")
			: QObject::tr("No cutting polyline selected.");
}
