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
#include "maths/FiniteRotation.h"
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

		// GeometryIntersect reports contacts as well as crossings. The former
		// GeometryCrossing helper only retained SEGMENTS_CROSS, and we rejected
		// cuts if it had discarded any contacts. Keep that restriction directly:
		// vertex contacts, tangencies and overlaps cannot form unambiguous child rings.
		for (const auto &intersection : intersection_graph.unordered_intersections)
		{
			if (intersection.type != GPlatesMaths::GeometryIntersect::Intersection::SEGMENTS_CROSS)
			{
				error_message = QObject::tr(
						"The cutter touches or overlaps the polygon boundary. "
						"Move it so it crosses the boundary cleanly at two points.");
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

		const GPlatesMaths::GeometryIntersect::Intersection &intersection1 =
				intersection_graph.unordered_intersections[
						intersection_graph.geometry2_ordered_intersections[0]];
		const GPlatesMaths::GeometryIntersect::Intersection &intersection2 =
				intersection_graph.unordered_intersections[
						intersection_graph.geometry2_ordered_intersections[1]];

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

		return SplitPolygonResult(reconstructed_polygon1, reconstructed_polygon2);
	}


	class SplitPlateUndoCommand :
			public QUndoCommand
	{
	public:
		SplitPlateUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureHandle::weak_ref &polygon_feature,
				const GPlatesModel::FeatureHandle::iterator &polygon_property,
				const polygon_seq_type &polygons) :
			d_feature_focus(feature_focus),
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
					!d_feature_collection.is_valid())
			{
				return;
			}

			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			// Keep the command's snapshots separate from later in-place property edits.
			d_polygon_feature->set(d_polygon_property, d_piece_properties.front()->clone());
			for (size_t index = 0; index < d_new_features.size(); ++index)
			{
				d_new_features[index]->set(
						d_new_polygon_properties[index], d_piece_properties[index + 1]->clone());
				d_feature_collection->add(d_new_features[index]);
			}

			notification_guard.release_guard();
		}

		virtual
		void
		undo()
		{
			if (!d_polygon_feature.is_valid() || !d_polygon_property.is_still_valid())
			{
				return;
			}

			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());

			d_polygon_feature->set(d_polygon_property, d_original_geometry_property->clone());
			for (const auto &feature : d_new_features)
			{
				feature->remove_from_parent();
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

		GPlatesGui::FeatureFocus &d_feature_focus;
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
	for (size_t cutter_index = 0; cutter_index < polylines.size(); ++cutter_index)
	{
		polygon_seq_type next_pieces;
		bool cut_any_piece = false;
		for (const auto &piece : pieces)
		{
			GPlatesMaths::GeometryIntersect::Graph intersections;
			if (!GPlatesMaths::GeometryIntersect::intersect(
					intersections, *piece, *polylines[cutter_index], true))
			{
				next_pieces.push_back(piece);
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
		if (!cut_any_piece)
		{
			error_message = QObject::tr("Polyline %1 does not split any polygon piece.")
					.arg(cutter_index + 1);
			return boost::none;
		}
		pieces.swap(next_pieces);
	}
	return pieces;
}


std::unique_ptr<QUndoCommand>
GPlatesViewOperations::SplitPlateOperation::create_split_command(
		GPlatesGui::FeatureFocus &feature_focus,
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
			feature_focus, model_interface, polygon_feature, polygon_property, polygons));
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
			"They are applied in list order. Each must cross the pieces cleanly, "
			"without touching a vertex, overlapping a boundary or crossing a hole. "
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
