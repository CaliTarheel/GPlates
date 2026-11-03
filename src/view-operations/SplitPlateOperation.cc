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
#include <set>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QObject>
#include <QPushButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "SplitPlateOperation.h"

#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "app-logic/GeometrySetter.h"

#include "file-io/File.h"
#include "file-io/FileInfo.h"

#include "gui/FeatureFocus.h"

#include "maths/GeometryIntersect.h"
#include "maths/GeometryDistance.h"
#include "maths/FiniteRotation.h"
#include "maths/GreatCircleArc.h"
#include "maths/MathsUtils.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/PropertyValueFinder.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GmlPolygon.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	typedef std::vector<GPlatesMaths::PointOnSphere> point_seq_type;
	typedef std::vector<point_seq_type> ring_seq_type;
	typedef GPlatesViewOperations::SplitPlateOperation::polygon_seq_type polygon_seq_type;
	typedef GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type rfg_ptr_type;


	bool
	is_editable_polygon_property(
			const GPlatesModel::TopLevelProperty &property)
	{
		const auto *inline_property = dynamic_cast<const GPlatesModel::TopLevelPropertyInline *>(&property);
		if (!inline_property || inline_property->size() != 1)
		{
			return false;
		}
		const GPlatesModel::PropertyValue::non_null_ptr_to_const_type first_value =
				*inline_property->begin();
		const GPlatesModel::PropertyValue *value = first_value.get();
		while (const auto *constant = dynamic_cast<const GPlatesPropertyValues::GpmlConstantValue *>(value))
		{
			value = constant->value().get();
		}
		// GeometrySetter traverses constant wrappers, but cannot replace a time-window sample.
		return dynamic_cast<const GPlatesPropertyValues::GmlPolygon *>(value) != nullptr;
	}


	QString
	geometry_label(
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg,
			GPlatesAppLogic::ApplicationState &application_state)
	{
		const auto feature = rfg.get_feature_ref();
		const auto name = GPlatesModel::get_property_value<GPlatesPropertyValues::XsString>(
				feature, GPlatesModel::PropertyName::create_gml("name"));
		QString collection_name = QObject::tr("Unsaved collection");
		for (const auto &file : application_state.get_feature_collection_file_state().get_loaded_files())
		{
			if (file.get_file().get_feature_collection().handle_ptr() == feature->parent_ptr())
			{
				collection_name = file.get_file().get_file_info().get_display_name(true);
				break;
			}
		}
		unsigned int property_index = 0;
		for (auto property = feature->begin(); property != feature->end() && property != rfg.property();
				++property, ++property_index)
		{  }
		return QObject::tr("%1 | Plate %2 | %3 | %4 | geometry %5")
				.arg(name ? GPlatesUtils::make_qstring((*name)->get_value()) : QObject::tr("Unnamed"))
				.arg(rfg.reconstruction_plate_id()
						? QString::number(*rfg.reconstruction_plate_id()) : QObject::tr("none"))
				.arg(collection_name)
				.arg(feature->feature_id().get().qstring())
				.arg(property_index + 1);
	}


	void
	get_split_choices(
			std::vector<rfg_ptr_type> &polygons,
			std::vector<rfg_ptr_type> &polylines,
			const GPlatesPresentation::ViewState &view_state)
	{
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
		const auto &layers = view_state.get_visual_layers();
		for (size_t index = 0; index < layers.size(); ++index)
		{
			const auto layer = layers.visual_layer_at(index).lock();
			if (!layer || !layer->is_visible() ||
					!layer->get_reconstruct_graph_layer().is_active() ||
					layer->get_layer_type() !=
							static_cast<unsigned int>(GPlatesAppLogic::LayerTaskType::RECONSTRUCT))
			{
				continue;
			}
			const auto output = layer->get_reconstruct_graph_layer().get_layer_output<
					GPlatesAppLogic::ReconstructLayerProxy>();
			if (!output)
			{
				continue;
			}
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*output)->get_reconstructed_feature_geometries(geometries);
			for (const auto &rfg : geometries)
			{
				if (!rfg->is_valid() || !rfg->property().is_still_valid() ||
						!rfg->feature_handle_ptr()->parent_ptr())
				{
					continue;
				}
				const auto geometry = rfg->reconstructed_geometry();
				const bool is_polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(geometry.get());
				const bool is_polyline = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(geometry.get());
				// Polygon outputs need an invertible reconstruction, including half-stage rotations.
				// A deformed polygon cannot be restored by a rigid plate rotation.
				if ((!is_polygon && !is_polyline) ||
						(is_polygon && (!rfg->finite_rotation_reconstruction() ||
								!is_editable_polygon_property(**rfg->property()))) ||
						!seen_properties.insert((*rfg->property()).get()).second)
				{
					continue;
				}
				(is_polygon ? polygons : polylines).push_back(rfg);
			}
		}
	}


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


	enum CutterStretch
	{
		EMPTY_STRETCH,
		BOUNDARY_STRETCH,
		INSIDE_STRETCH,
		OUTSIDE_STRETCH,
		AMBIGUOUS_STRETCH
	};


	bool
	within_contact_roundoff(
			const GPlatesMaths::UnitVector3D &first,
			const GPlatesMaths::UnitVector3D &second)
	{
		const double dx = first.x().dval() - second.x().dval();
		const double dy = first.y().dval() - second.y().dval();
		const double dz = first.z().dval() - second.z().dval();
		return dx * dx + dy * dy + dz * dz <= 1e-20;
	}


	CutterStretch
	classify_cutter_stretch(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const point_seq_type &path)
	{
		CutterStretch result = EMPTY_STRETCH;
		for (size_t index = 1; index < path.size(); ++index)
		{
			const auto arc = GPlatesMaths::GreatCircleArc::create(path[index - 1], path[index]);
			if (arc.is_zero_length())
			{
				continue;
			}
			const auto sample = arc.point_on_arc(0.5);
			auto closest = sample.position_vector();
			const auto distance = GPlatesMaths::minimum_distance(
					sample, polygon, false, boost::none, closest);
			const bool on_boundary = distance->get_cosine().dval() >=
					GPlatesMaths::GeometryIntersect::Intersection::get_on_segment_start_threshold_cosine();
			if (on_boundary && !within_contact_roundoff(sample.position_vector(), closest))
			{
				// A narrow inside/outside strip is not an existing boundary. Distances near
				// zero lose precision as cosines, so compare the actual closest positions.
				return AMBIGUOUS_STRETCH;
			}
			const CutterStretch location = on_boundary ? BOUNDARY_STRETCH :
					(polygon.is_point_in_polygon(sample, GPlatesMaths::PolygonOnSphere::ADAPTIVE,
							false) ? INSIDE_STRETCH : OUTSIDE_STRETCH);
			if (result != EMPTY_STRETCH && result != location)
			{
				return AMBIGUOUS_STRETCH;
			}
			result = location;
		}
		return result;
	}


	std::vector<CutterStretch>
	classify_cutter_stretches(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Graph &graph)
	{
		// Native intersection events divide the cutter into open stretches with no boundary
		// crossings. Classify their segment midpoints, not arbitrary offsets from a contact.
		// Boundary/empty/mixed stretches remain ambiguous: do not snap away a narrow sliver
		// or assume that a contact at an endpoint is a through-crossing.
		typedef GPlatesMaths::GeometryIntersect::Intersection Intersection;
		Intersection previous(Intersection::SEGMENT_START_ON_SEGMENT_START,
				polyline.get_vertex(0), 0, 0);
		std::vector<CutterStretch> stretches;
		for (unsigned int index : graph.geometry2_ordered_intersections)
		{
			const auto &next = graph.unordered_intersections[index];
			stretches.push_back(classify_cutter_stretch(
					polygon, create_cut_path(polyline, previous, next)));
			previous = next;
		}
		const unsigned int last_vertex = polyline.number_of_vertices() - 1;
		const Intersection end(Intersection::SEGMENT_START_ON_SEGMENT_START,
				polyline.get_vertex(last_vertex), 0, last_vertex);
		stretches.push_back(classify_cutter_stretch(
				polygon, create_cut_path(polyline, previous, end)));
		return stretches;
	}


	bool
	contact_is_geometrically_exact(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Intersection &intersection);


	bool
	cutter_only_follows_boundary(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline)
	{
		GPlatesMaths::GeometryIntersect::Graph graph;
		if (!GPlatesMaths::GeometryIntersect::intersect(graph, polygon, polyline, true))
		{
			return false;
		}
		for (const auto &intersection : graph.unordered_intersections)
		{
			if (intersection.segment_index1 >= polygon.number_of_segments_in_exterior_ring() ||
					!contact_is_geometrically_exact(polygon, polyline, intersection))
			{
				return false;
			}
		}
		bool has_boundary_stretch = false;
		for (const auto location : classify_cutter_stretches(polygon, polyline, graph))
		{
			if (location == INSIDE_STRETCH || location == AMBIGUOUS_STRETCH)
			{
				return false;
			}
			has_boundary_stretch = has_boundary_stretch || location == BOUNDARY_STRETCH;
		}
		// A tangent point or an unrelated outside line is not an already-used boundary.
		return has_boundary_stretch;
	}


	bool
	exactly_same_cutter(
			const GPlatesMaths::PolylineOnSphere &first,
			const GPlatesMaths::PolylineOnSphere &second)
	{
		if (first.number_of_vertices() != second.number_of_vertices())
		{
			return false;
		}
		bool forward = true;
		bool reverse = true;
		const auto identical = [](const GPlatesMaths::PointOnSphere &a,
				const GPlatesMaths::PointOnSphere &b)
		{
			return a.position_vector().x().dval() == b.position_vector().x().dval() &&
					a.position_vector().y().dval() == b.position_vector().y().dval() &&
					a.position_vector().z().dval() == b.position_vector().z().dval();
		};
		for (unsigned int index = 0; index < first.number_of_vertices(); ++index)
		{
			forward = forward && identical(first.get_vertex(index), second.get_vertex(index));
			reverse = reverse && identical(first.get_vertex(index),
					second.get_vertex(second.number_of_vertices() - 1 - index));
		}
		return forward || reverse;
	}


	bool
	cut_path_is_simple(
			QString &error_message,
			const point_seq_type &path)
	{
		std::size_t comparisons = 0;
		for (size_t first = 1; first < path.size(); ++first)
		{
			const auto a = GPlatesMaths::PolylineOnSphere::create(
					point_seq_type{path[first - 1], path[first]});
			for (size_t second = first + 2; second < path.size(); ++second)
			{
				if (++comparisons > 250000)
				{
					error_message = QObject::tr(
							"The cutter is too detailed to validate safely. Simplify it and try again.");
					return false;
				}
				const auto b = GPlatesMaths::PolylineOnSphere::create(
						point_seq_type{path[second - 1], path[second]});
				GPlatesMaths::GeometryIntersect::Graph graph;
				if (GPlatesMaths::GeometryIntersect::intersect(graph, *a, *b))
				{
					error_message = QObject::tr("The cutter crosses or overlaps itself inside the polygon.");
					return false;
				}
			}
		}
		return true;
	}


	bool
	contact_is_geometrically_exact(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Intersection &intersection)
	{
		using GPlatesMaths::GeometryIntersect::Intersection;
		// Native contact thickness is about nine metres on Earth. A through-vertex cut
		// must not turn a merely nearby line into a snapped cut. Allow only numerical
		// coincidence (1e-10 radians), then separately prove the inside/outside transition.
		if (intersection.type == Intersection::SEGMENTS_CROSS)
		{
			return true;
		}
		if (intersection.type == Intersection::SEGMENT_START_ON_SEGMENT_START)
		{
			return within_contact_roundoff(
					polygon.get_exterior_ring_vertex(intersection.segment_index1).position_vector(),
					polyline.get_vertex(intersection.segment_index2).position_vector());
		}
		const auto &segment = intersection.type == Intersection::SEGMENT1_START_ON_SEGMENT2
				? polyline.get_segment(intersection.segment_index2)
				: polygon.get_segment(intersection.segment_index1);
		return !segment.is_zero_length() && std::fabs(GPlatesMaths::dot(
				intersection.position.position_vector(), segment.rotation_axis()).dval()) <= 1e-10;
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
		if (reconstructed_feature_geometry.finite_rotation_reconstruction())
		{
			return GPlatesMaths::get_reverse(
					reconstructed_feature_geometry.finite_rotation_reconstruction()->
							get_reconstruct_method_finite_rotation()->get_finite_rotation()) * point;
		}

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


	typedef GPlatesViewOperations::SplitPlateOperation::SplitPolygonResult SplitPolygonResult;


	boost::optional<SplitPolygonResult>
	split_at_boundary_contacts(
			QString &error_message,
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline,
			const GPlatesMaths::GeometryIntersect::Intersection &intersection1,
			const GPlatesMaths::GeometryIntersect::Intersection &intersection2)
	{
		const point_seq_type cut_path = create_cut_path(polyline, intersection1, intersection2);
		if (!cut_path_is_simple(error_message, cut_path))
		{
			return boost::none;
		}
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
		const double first_area = reconstructed_polygon1->get_area().dval();
		const double second_area = reconstructed_polygon2->get_area().dval();
		if (first_area <= 1e-12 || second_area <= 1e-12 ||
				std::fabs(first_area + second_area - polygon.get_area().dval()) > 1e-8)
		{
			error_message = QObject::tr("The cut would create degenerate or overlapping polygon pieces.");
			return boost::none;
		}

		return SplitPolygonResult(reconstructed_polygon1, reconstructed_polygon2);
	}


	boost::optional<SplitPolygonResult>
	split_polygon_geometry(
			QString &error_message,
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &polyline)
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

		// A vertex contact can be an unambiguous crossing, including a ridge meeting a
		// previous split at its shared vertex. Accept it only when the open stretches on
		// either side switch strictly between inside and outside. A tangent, endpoint,
		// boundary overlap or unresolved near-vertex event must not be turned into a cut.
		const auto stretches = classify_cutter_stretches(polygon, polyline, intersection_graph);
		for (size_t index = 0; index < intersection_graph.geometry2_ordered_intersections.size();
				++index)
		{
			const auto &intersection = intersection_graph.unordered_intersections[
					intersection_graph.geometry2_ordered_intersections[index]];
			if (!contact_is_geometrically_exact(polygon, polyline, intersection) ||
					!((stretches[index] == INSIDE_STRETCH &&
						stretches[index + 1] == OUTSIDE_STRETCH) ||
					(stretches[index] == OUTSIDE_STRETCH &&
						stretches[index + 1] == INSIDE_STRETCH)))
			{
				error_message = QObject::tr(
						"The cutter only touches, ends on, or overlaps the polygon boundary, "
						"or its near-vertex path is ambiguous. It must pass through the boundary "
						"at two distinct points.");
				return boost::none;
			}
		}

		if (intersection_graph.unordered_intersections.size() != 2 ||
				intersection_graph.geometry2_ordered_intersections.size() != 2)
		{
			error_message = QObject::tr(
					"The cutter must cross the polygon's exterior boundary exactly twice. "
					"This polyline produces %1 crossings.")
					.arg(intersection_graph.unordered_intersections.size());
			return boost::none;
		}
		if (stretches[0] != OUTSIDE_STRETCH || stretches[1] != INSIDE_STRETCH ||
				stretches[2] != OUTSIDE_STRETCH)
		{
			// Two transitions can also describe an excursion out of the polygon. Only
			// an interior cut path partitions the selected polygon into its two children.
			error_message = QObject::tr(
					"The cutter must start outside, pass through the polygon, and end outside.");
			return boost::none;
		}

		const GPlatesMaths::GeometryIntersect::Intersection &intersection1 =
				intersection_graph.unordered_intersections[
						intersection_graph.geometry2_ordered_intersections[0]];
		const GPlatesMaths::GeometryIntersect::Intersection &intersection2 =
				intersection_graph.unordered_intersections[
						intersection_graph.geometry2_ordered_intersections[1]];

		return split_at_boundary_contacts(
				error_message, polygon, polyline, intersection1, intersection2);
	}


	class SplitPlateUndoCommand :
			public QUndoCommand
	{
	public:
		SplitPlateUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureHandle::weak_ref &polygon_feature,
				const GPlatesModel::FeatureHandle::iterator &polygon_property,
				const polygon_seq_type &polygons) :
			d_model_interface(model_interface),
			d_polygon_feature(polygon_feature),
			d_polygon_property(polygon_property),
			d_original_geometry_property((*polygon_property)->clone()),
			d_feature_collection(polygon_feature->parent_ptr()->reference())
		{
			setText(QObject::tr("split plate"));
			// Prepare every allocation and snapshot before the command can alter the model.
			for (size_t piece = 0; piece < polygons.size(); ++piece)
			{
				d_piece_properties.push_back(create_geometry_property(polygon_property, polygons[piece]));
				if (piece == 0)
				{
					continue;
				}
				const auto new_feature = GPlatesModel::FeatureHandle::create(polygon_feature->feature_type());
				for (auto property = polygon_feature->begin(); property != polygon_feature->end(); ++property)
				{
					const auto new_property = new_feature->add((*property)->clone());
					if (property == polygon_property)
					{
						d_new_polygon_properties.push_back(new_property);
					}
				}
				d_new_features.push_back(new_feature);
			}
		}

		virtual
		void
		redo()
		{
			if (!d_polygon_feature.is_valid() || !d_polygon_property.is_still_valid() ||
					!d_feature_collection.is_valid() ||
					d_polygon_feature->parent_ptr() != d_feature_collection.handle_ptr())
			{
				return;
			}

			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			// Keep the command's snapshots separate from later in-place property edits.
			d_polygon_feature->set(d_polygon_property, d_piece_properties.front()->clone());
			for (size_t index = 0; index < d_new_features.size(); ++index)
			{
				const auto &feature = d_new_features[index];
				if (!feature->is_active() || !feature->parent_ptr())
				{
					// Removal retains the old parent, and add does not reactivate the feature.
					// Reattach first, then reactivate its property iterators before restoring geometry.
					d_feature_collection->add(feature);
					feature->set_active(true);
					if (d_new_polygon_properties[index].is_still_valid())
					{
						feature->set(
								d_new_polygon_properties[index], d_piece_properties[index + 1]->clone());
					}
					else
					{
						// The feature editor can delete a generated piece's geometry property.
						d_new_polygon_properties[index] = feature->add(d_piece_properties[index + 1]->clone());
					}
				}
			}

			notification_guard.release_guard();
		}

		virtual
		void
		undo()
		{
			if (!d_polygon_feature.is_valid() || !d_polygon_property.is_still_valid() ||
					!d_feature_collection.is_valid() ||
					d_polygon_feature->parent_ptr() != d_feature_collection.handle_ptr())
			{
				return;
			}

			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			d_polygon_feature->set(d_polygon_property, d_original_geometry_property->clone());
			for (const auto &feature : d_new_features)
			{
				if (feature->is_active() && feature->parent_ptr() == d_feature_collection.handle_ptr())
				{
					feature->remove_from_parent();
				}
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
			GPlatesAppLogic::GeometrySetter geometry_setter(polygon);
			geometry_setter.set_geometry(property_clone.get());
			return property_clone;
		}

		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_polygon_feature;
		GPlatesModel::FeatureHandle::iterator d_polygon_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_original_geometry_property;
		std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> d_piece_properties;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_new_features;
		std::vector<GPlatesModel::FeatureHandle::iterator> d_new_polygon_properties;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_feature_collection;
	};
}


boost::optional<GPlatesViewOperations::SplitPlateOperation::SplitPolygonResult>
GPlatesViewOperations::SplitPlateOperation::split_polygon(
		QString &error_message,
		const GPlatesMaths::PolygonOnSphere &polygon,
		const GPlatesMaths::PolylineOnSphere &polyline)
{
	return split_polygon_geometry(error_message, polygon, polyline);
}


boost::optional<GPlatesViewOperations::SplitPlateOperation::SketchSplitResult>
GPlatesViewOperations::SplitPlateOperation::split_polygon_along_sketch(
		QString &error_message,
		const GPlatesMaths::PolygonOnSphere &polygon,
		const GPlatesMaths::PolylineOnSphere &sketch)
{
	using namespace GPlatesMaths;
	const point_seq_type sketch_points(sketch.vertex_begin(), sketch.vertex_end());
	if (!cut_path_is_simple(error_message, sketch_points))
	{
		return boost::none;
	}
	for (unsigned int index = 0; index < sketch.number_of_segments(); ++index)
	{
		const auto &arc = sketch.get_segment(index);
		if (arc.is_zero_length() || (index &&
				dot(arc.rotation_axis(), sketch.get_segment(index - 1).rotation_axis()).dval()
						< -1 + 1e-12))
		{
			error_message = QObject::tr("The sketch has a repeated vertex or doubles back on itself.");
			return boost::none;
		}
	}
	GeometryIntersect::Graph graph;
	GeometryIntersect::intersect(graph, polygon, sketch, true);
	const auto stretches = classify_cutter_stretches(polygon, sketch, graph);
	if (graph.unordered_intersections.empty() ||
			stretches.front() != OUTSIDE_STRETCH || stretches.back() != OUTSIDE_STRETCH)
	{
		error_message = QObject::tr("The sketch must start and end outside each selected polygon.");
		return boost::none;
	}
	if (graph.unordered_intersections.size() > 512)
	{
		error_message = QObject::tr("The sketch makes too many cuts. Use fewer than 256 interior spans.");
		return boost::none;
	}
	for (std::size_t index = 0; index < graph.geometry2_ordered_intersections.size(); ++index)
	{
		const auto &contact = graph.unordered_intersections[graph.geometry2_ordered_intersections[index]];
		if (contact.segment_index1 >= polygon.number_of_segments_in_exterior_ring())
		{
			error_message = QObject::tr("The sketch meets a polygon hole. Move it away from the hole.");
			return boost::none;
		}
		if (!contact_is_geometrically_exact(polygon, sketch, contact) ||
				!((stretches[index] == INSIDE_STRETCH && stretches[index + 1] == OUTSIDE_STRETCH) ||
				(stretches[index] == OUTSIDE_STRETCH && stretches[index + 1] == INSIDE_STRETCH)))
		{
			error_message = QObject::tr("The sketch touches or follows a boundary, or passes too close "
					"to a vertex to identify a clean crossing. No changes were made.");
			return boost::none;
		}
	}
	SketchSplitResult result;
	result.polygons.push_back(polygon.get_non_null_pointer());
	for (std::size_t index = 1; index + 1 < stretches.size(); ++index)
	{
		if (stretches[index] != INSIDE_STRETCH)
		{
			continue;
		}
		const auto &first = graph.unordered_intersections[graph.geometry2_ordered_intersections[index - 1]];
		const auto &last = graph.unordered_intersections[graph.geometry2_ordered_intersections[index]];
		const auto span = PolylineOnSphere::create(create_cut_path(sketch, first, last));
		bool split = false;
		for (std::size_t piece_index = 0; piece_index < result.polygons.size(); ++piece_index)
		{
			const auto &piece = result.polygons[piece_index];
			if (classify_cutter_stretch(*piece,
					point_seq_type(span->vertex_begin(), span->vertex_end())) != INSIDE_STRETCH)
			{
				continue;
			}
			GeometryIntersect::Graph contacts;
			GeometryIntersect::intersect(contacts, *piece, *span, true);
			if (contacts.geometry2_ordered_intersections.size() != 2)
			{
				error_message = QObject::tr("A cut cannot be matched to two unambiguous piece boundaries.");
				return boost::none;
			}
			const auto &entry = contacts.unordered_intersections[contacts.geometry2_ordered_intersections[0]];
			const auto &exit = contacts.unordered_intersections[contacts.geometry2_ordered_intersections[1]];
			if (entry.segment_index1 >= piece->number_of_segments_in_exterior_ring() ||
					exit.segment_index1 >= piece->number_of_segments_in_exterior_ring() ||
					!within_contact_roundoff(entry.position.position_vector(),
							span->get_vertex(0).position_vector()) ||
					!within_contact_roundoff(exit.position.position_vector(),
							span->get_vertex(span->number_of_vertices() - 1).position_vector()))
			{
				error_message = QObject::tr("A cut touches an unexpected boundary. No changes were made.");
				return boost::none;
			}
			const auto children = split_at_boundary_contacts(error_message, *piece, *span, entry, exit);
			if (!children)
			{
				return boost::none;
			}
			result.polygons[piece_index] = children->polygon1;
			result.polygons.push_back(children->polygon2);
			result.cut_spans.push_back(span);
			split = true;
			break;
		}
		if (!split)
		{
			error_message = QObject::tr("An interior sketch span could not split a polygon piece.");
			return boost::none;
		}
	}
	// Every cut edge is shared by exactly two final pieces. Its direction and the ring's
	// signed area identify left/right without centroid guesses or arbitrary sample offsets.
	for (const auto &span : result.cut_spans)
	{
		std::size_t left = result.polygons.size();
		std::size_t right = result.polygons.size();
		for (std::size_t index = 0; index < result.polygons.size(); ++index)
		{
			const auto &piece = result.polygons[index];
			for (auto arc = piece->exterior_ring_begin(); arc != piece->exterior_ring_end(); ++arc)
			{
				const bool forward = within_contact_roundoff(arc->start_point().position_vector(),
						span->get_vertex(0).position_vector()) &&
						within_contact_roundoff(arc->end_point().position_vector(),
								span->get_vertex(1).position_vector());
				const bool reverse = within_contact_roundoff(arc->end_point().position_vector(),
						span->get_vertex(0).position_vector()) &&
						within_contact_roundoff(arc->start_point().position_vector(),
								span->get_vertex(1).position_vector());
				if (forward || reverse)
				{
					const bool on_left = forward == (piece->get_signed_area().dval() > 0);
					(on_left ? left : right) = index;
					break;
				}
			}
		}
		if (left == result.polygons.size() || right == result.polygons.size() || left == right)
		{
			error_message = QObject::tr("The two sides of a cut could not be identified safely.");
			return boost::none;
		}
		result.adjacent_polygons.emplace_back(left, right);
	}
	return result;
}


boost::optional<GPlatesViewOperations::SplitPlateOperation::polygon_seq_type>
GPlatesViewOperations::SplitPlateOperation::split_polygon_with_polylines(
		QString &error_message,
		const GPlatesMaths::PolygonOnSphere &polygon,
		const polyline_seq_type &polylines)
{
	if (polylines.empty())
	{
		error_message = QObject::tr("Select at least one cutting polyline.");
		return boost::none;
	}
	polygon_seq_type pieces(1, polygon.get_non_null_pointer());
	bool split_any_polygon = false;
	for (size_t cutter_index = 0; cutter_index < polylines.size(); ++cutter_index)
	{
		bool duplicate = false;
		for (size_t previous = 0; previous < cutter_index; ++previous)
		{
			duplicate = duplicate || exactly_same_cutter(*polylines[previous], *polylines[cutter_index]);
		}
		// Check the original boundary first, so a previously used ridge remains harmless
		// even after another cutter subdivides that boundary into several child pieces.
		if (duplicate || cutter_only_follows_boundary(polygon, *polylines[cutter_index]))
		{
			continue;
		}
		polygon_seq_type next_pieces;
		bool cut_any_piece = false;
		bool followed_existing_boundary = false;
		for (const auto &piece : pieces)
		{
			GPlatesMaths::GeometryIntersect::Graph intersections;
			if (!GPlatesMaths::GeometryIntersect::intersect(
					intersections, *piece, *polylines[cutter_index], true))
			{
				next_pieces.push_back(piece);
				continue;
			}
			if (cutter_only_follows_boundary(*piece, *polylines[cutter_index]))
			{
				next_pieces.push_back(piece);
				followed_existing_boundary = true;
				continue;
			}
			const auto split = split_polygon(error_message, *piece, *polylines[cutter_index]);
			if (!split)
			{
				error_message = QObject::tr("Polyline %1: %2").arg(cutter_index + 1).arg(error_message);
				return boost::none;
			}
			next_pieces.push_back(split->polygon1);
			next_pieces.push_back(split->polygon2);
			cut_any_piece = true;
		}
		if (!cut_any_piece && !followed_existing_boundary)
		{
			error_message = QObject::tr("Polyline %1 does not split any polygon piece.")
					.arg(cutter_index + 1);
			return boost::none;
		}
		split_any_polygon = split_any_polygon || cut_any_piece;
		pieces.swap(next_pieces);
	}
	if (!split_any_polygon)
	{
		error_message = QObject::tr(
				"The selected polylines only follow existing boundaries; no polygon was split.");
		return boost::none;
	}
	return pieces;
}


std::unique_ptr<QUndoCommand>
GPlatesViewOperations::SplitPlateOperation::create_split_command(
		GPlatesGui::FeatureFocus &,
		GPlatesModel::ModelInterface model_interface,
		const GPlatesModel::FeatureHandle::weak_ref &polygon_feature,
		const GPlatesModel::FeatureHandle::iterator &polygon_property,
		const polygon_seq_type &polygons)
{
	if (polygons.size() < 2 || !polygon_feature.is_valid() ||
			!polygon_property.is_still_valid() ||
			polygon_property.handle_weak_ref() != polygon_feature || !polygon_feature->parent_ptr() ||
			!is_editable_polygon_property(**polygon_property))
	{
		return std::unique_ptr<QUndoCommand>();
	}
	return std::unique_ptr<QUndoCommand>(new SplitPlateUndoCommand(
			model_interface, polygon_feature, polygon_property, polygons));
}


GPlatesViewOperations::SplitPlateOperation::SplitPlateOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::trigger(
		QWidget *parent,
		const GPlatesPresentation::ViewState &view_state)
{
	reset();
	std::vector<rfg_ptr_type> polygons;
	std::vector<rfg_ptr_type> polylines;
	get_split_choices(polygons, polylines, view_state);
	if (polygons.empty() || polylines.empty())
	{
		return Result(OPERATION_ERROR, QObject::tr(
				"Split Plate needs a rigid reconstructed polygon and at least one polyline "
				"in active, visible reconstruct layers at the current time."));
	}
	const double reconstruction_time = d_application_state.get_current_reconstruction_time();
	QDialog dialog(parent);
	dialog.setObjectName("SplitPlateDialog");
	dialog.setWindowTitle(QObject::tr("Split Plate"));
	dialog.resize(850, 480);
	auto *layout = new QVBoxLayout(&dialog);
	layout->addWidget(new QLabel(QObject::tr("Polygon to split:"), &dialog));
	auto *polygon_choice = new QComboBox(&dialog);
	polygon_choice->setObjectName("splitPolygonComboBox");
	for (size_t index = 0; index < polygons.size(); ++index)
	{
		polygon_choice->addItem(geometry_label(*polygons[index], d_application_state));
		if (polygons[index].get() == d_feature_focus.associated_reconstruction_geometry().get())
		{
			polygon_choice->setCurrentIndex(static_cast<int>(index));
		}
	}
	layout->addWidget(polygon_choice);
	auto *instructions = new QLabel(QObject::tr(
			"Select one or more existing cutting polylines (Ctrl-click for multiple). "
			"They are applied in list order. Each new cut must enter and leave a piece; "
			"a clear crossing through a boundary vertex is allowed. Duplicate lines and "
			"lines already following a boundary are skipped. At least one new cut is required. "
			"Tangencies, partial overlaps and cuts through holes are not supported. "
			"The cutting polylines are kept unchanged."), &dialog);
	instructions->setWordWrap(true);
	layout->addWidget(instructions);
	auto *cutter_choices = new QListWidget(&dialog);
	cutter_choices->setObjectName("splitPolylinesListWidget");
	cutter_choices->setSelectionMode(QAbstractItemView::ExtendedSelection);
	for (const auto &polyline : polylines)
	{
		cutter_choices->addItem(geometry_label(*polyline, d_application_state));
	}
	layout->addWidget(cutter_choices);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->setObjectName("splitDialogButtonBox");
	buttons->button(QDialogButtonBox::Ok)->setObjectName("splitApplyButton");
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Split"));
	buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
	QObject::connect(cutter_choices, &QListWidget::itemSelectionChanged, &dialog, [=]()
	{
		buttons->button(QDialogButtonBox::Ok)->setEnabled(!cutter_choices->selectedItems().isEmpty());
	});
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QString());
	}
	if (std::fabs(d_application_state.get_current_reconstruction_time() - reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR, QObject::tr(
				"The reconstruction time changed. Reopen Split Plate to select current geometries."));
	}
	const auto polygon_rfg = polygons[polygon_choice->currentIndex()];
	if (!polygon_rfg->is_valid() || !polygon_rfg->property().is_still_valid() ||
			!polygon_rfg->feature_handle_ptr()->parent_ptr())
	{
		return Result(OPERATION_ERROR, QObject::tr("The selected polygon is no longer available."));
	}
	polyline_seq_type cutters;
	for (int row = 0; row < cutter_choices->count(); ++row)
	{
		if (!cutter_choices->item(row)->isSelected())
		{
			continue;
		}
		const auto &rfg = polylines[row];
		if (!rfg->is_valid() || !rfg->property().is_still_valid())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("A selected cutting polyline is no longer available."));
		}
		const auto *polyline = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
				rfg->reconstructed_geometry().get());
		cutters.push_back(polyline->get_non_null_pointer());
	}
	try
	{
		const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
				polygon_rfg->reconstructed_geometry().get());
		QString error_message;
		const auto pieces = split_polygon_with_polylines(error_message, *polygon, cutters);
		if (!pieces)
		{
			return Result(OPERATION_ERROR, error_message);
		}
		polygon_seq_type source_pieces;
		for (const auto &piece : *pieces)
		{
			source_pieces.push_back(reverse_reconstruct_polygon(*piece, *polygon_rfg));
		}
		auto command = create_split_command(d_feature_focus, d_model_interface,
				polygon_rfg->get_feature_ref(), polygon_rfg->property(), source_pieces);
		if (!command)
		{
			return Result(OPERATION_ERROR, QObject::tr("The selected polygon is no longer editable."));
		}
		// Changing focus clears the geometry editor's undo stack. Do it before push, never
		// inside redo/undo, where it would delete the command currently being executed.
		// Preserve earlier WB commands when the dialog was opened with no focused feature.
		if (d_feature_focus.focused_feature().is_valid() ||
				d_feature_focus.associated_reconstruction_geometry())
		{
			d_feature_focus.unset_focus();
		}
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		return Result(SPLIT_COMPLETED, QObject::tr(
				"Created %1 polygon pieces. The cutting polylines were kept unchanged. "
				"Use Edit > Undo to restore the original polygon.").arg(source_pieces.size()));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not split the polygon: %1").arg(exception.what()));
	}
	catch (...)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not split the polygon because the geometry is invalid."));
	}
}


GPlatesViewOperations::SplitPlateOperation::Result
GPlatesViewOperations::SplitPlateOperation::trigger()
{
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("Select a polygon geometry on the globe first, then choose Split Plate again."));
	}

	boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> reconstructed_feature_geometry =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!reconstructed_feature_geometry)
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("Split Plate currently supports reconstructed polygon and polyline features only."));
	}

	const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type focused_geometry =
			(*reconstructed_feature_geometry)->reconstructed_geometry();
	const GPlatesMaths::PolygonOnSphere *focused_polygon =
			dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(focused_geometry.get());
	const GPlatesMaths::PolylineOnSphere *focused_polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(focused_geometry.get());

	const double current_reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (!d_captured_polygon)
	{
		if (!focused_polygon)
		{
			return Result(
					OPERATION_ERROR,
					QObject::tr("The first selected geometry must be a polygon. Select the plate polygon and try again."));
		}

		const GPlatesModel::FeatureHandle::iterator geometry_property =
				(*reconstructed_feature_geometry)->property();
		if (!geometry_property.is_still_valid())
		{
			return Result(
					OPERATION_ERROR,
					QObject::tr("The selected polygon is not backed by an editable geometry property."));
		}

		d_captured_polygon = CapturedPolygon(
				d_feature_focus.focused_feature(),
				geometry_property,
				(*reconstructed_feature_geometry)->get_non_null_pointer_to_const(),
				focused_polygon->get_non_null_pointer(),
				current_reconstruction_time);

		return Result(
				POLYGON_CAPTURED,
				QObject::tr(
						"Polygon captured. Now select the polyline that should cut it, then choose "
						"World Building > Split Plate again."));
	}

	if (!d_captured_polygon->feature.is_valid() ||
			!d_captured_polygon->geometry_property.is_still_valid())
	{
		reset();
		return Result(
				OPERATION_ERROR,
				QObject::tr("The captured polygon is no longer available. Select it again to restart the operation."));
	}

	if (std::fabs(current_reconstruction_time - d_captured_polygon->reconstruction_time) > 1e-9)
	{
		reset();
		return Result(
				OPERATION_ERROR,
				QObject::tr("The reconstruction time changed. Select the polygon again so both geometries use the same time."));
	}

	if (focused_polygon)
	{
		// Selecting another polygon while a split is pending is a convenient way
		// to correct the first selection.
		const GPlatesModel::FeatureHandle::iterator geometry_property =
				(*reconstructed_feature_geometry)->property();
		if (!geometry_property.is_still_valid())
		{
			return Result(
					OPERATION_ERROR,
					QObject::tr("The replacement polygon is not backed by an editable geometry property."));
		}

		d_captured_polygon = CapturedPolygon(
				d_feature_focus.focused_feature(),
				geometry_property,
				(*reconstructed_feature_geometry)->get_non_null_pointer_to_const(),
				focused_polygon->get_non_null_pointer(),
				current_reconstruction_time);
		return Result(
				POLYGON_CAPTURED,
				QObject::tr("Polygon selection updated. Select the cutting polyline and choose Split Plate again."));
	}

	if (!focused_polyline)
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("The second selected geometry must be a polyline. The captured polygon is still waiting."));
	}

	QString error_message;
	boost::optional<SplitPolygonResult> split_result;
	try
	{
		const boost::optional<SplitPolygonResult> reconstructed_split_result = split_polygon(
				error_message,
				*d_captured_polygon->polygon,
				*focused_polyline);
		if (reconstructed_split_result)
		{
			split_result = SplitPolygonResult(
					reverse_reconstruct_polygon(
							*reconstructed_split_result->polygon1,
							*d_captured_polygon->reconstructed_feature_geometry),
					reverse_reconstruct_polygon(
							*reconstructed_split_result->polygon2,
							*d_captured_polygon->reconstructed_feature_geometry));
		}
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
		return Result(OPERATION_ERROR, error_message);
	}

	std::unique_ptr<QUndoCommand> undo_command = create_split_command(
					d_feature_focus,
					d_model_interface,
					d_captured_polygon->feature,
					d_captured_polygon->geometry_property,
					{ split_result->polygon1, split_result->polygon2 });
	if (!undo_command)
	{
		return Result(OPERATION_ERROR, QObject::tr("The captured polygon is no longer editable."));
	}

	reset();
	// Clear a real focus before push; an unnecessary focus signal would erase undo history.
	if (d_feature_focus.focused_feature().is_valid() ||
			d_feature_focus.associated_reconstruction_geometry())
	{
		d_feature_focus.unset_focus();
	}
	UndoRedo::instance().get_active_undo_stack().push(undo_command.release());

	return Result(
			SPLIT_COMPLETED,
			QObject::tr(
					"Plate split completed. The original feature is one polygon and a cloned feature is the other. "
					"Use Edit > Undo to put them back together."));
}


void
GPlatesViewOperations::SplitPlateOperation::reset()
{
	d_captured_polygon = boost::none;
}
