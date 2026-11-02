/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The GPlates developers
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

#include <algorithm>
#include <vector>
#include <gtest/gtest.h>
#include <QUndoStack>

#include "gui/FeatureFocus.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureType.h"
#include "model/Model.h"
#include "model/ModelInterface.h"
#include "model/PropertyName.h"
#include "model/PropertyValueFinder.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/GmlLineString.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GpmlPiecewiseAggregation.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "view-operations/RenderedGeometryCollection.h"
#include "view-operations/SplitPlateOperation.h"


namespace
{
	typedef GPlatesViewOperations::SplitPlateOperation SplitPlate;
	typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon_ptr_type;
	typedef std::vector<GPlatesMaths::PointOnSphere> point_seq_type;


	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	point_seq_type
	rectangle_ring(
			double south,
			double north,
			double west,
			double east)
	{
		return { point(south, west), point(south, east), point(north, east), point(north, west) };
	}


	void
	expect_partition(
			const SplitPlate::SplitPolygonResult &result,
			const GPlatesMaths::PolygonOnSphere &original,
			const GPlatesMaths::PointOnSphere &first_sample,
			const GPlatesMaths::PointOnSphere &second_sample)
	{
		const double area1 = result.polygon1->get_area().dval();
		const double area2 = result.polygon2->get_area().dval();
		EXPECT_GT(area1, 0);
		EXPECT_GT(area2, 0);
		// These are exact spherical edges, without the cutter's projection/tessellation.
		EXPECT_NEAR(area1 + area2, original.get_area().dval(), 1e-10);

		const bool first_in_polygon1 = result.polygon1->is_point_in_polygon(first_sample);
		const bool second_in_polygon1 = result.polygon1->is_point_in_polygon(second_sample);
		EXPECT_NE(first_in_polygon1, second_in_polygon1);
		EXPECT_EQ(result.polygon2->is_point_in_polygon(first_sample), !first_in_polygon1);
		EXPECT_EQ(result.polygon2->is_point_in_polygon(second_sample), !second_in_polygon1);
	}


	void
	expect_rejected(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const point_seq_type &cutter_points)
	{
		const auto cutter = GPlatesMaths::PolylineOnSphere::create(cutter_points);
		QString error;
		EXPECT_FALSE(SplitPlate::split_polygon(error, polygon, *cutter));
		EXPECT_FALSE(error.isEmpty());
	}
}


TEST(SplitPlateOperationTest, clean_cut_partitions_polygon_in_either_direction)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	point_seq_type cutter_points = { point(-20, 0), point(20, 0) };
	for (unsigned int direction = 0; direction < 2; ++direction)
	{
		SCOPED_TRACE(direction);
		const auto cutter = GPlatesMaths::PolylineOnSphere::create(cutter_points);
		QString error;
		const auto result = SplitPlate::split_polygon(error, *polygon, *cutter);
		ASSERT_TRUE(result) << error.toStdString();
		expect_partition(*result, *polygon, point(0, -5), point(0, 5));
		EXPECT_NEAR(result->polygon1->get_area().dval(), polygon->get_area().dval() / 2, 1e-10);
		EXPECT_NEAR(result->polygon2->get_area().dval(), polygon->get_area().dval() / 2, 1e-10);
		EXPECT_FALSE(result->polygon1->is_point_in_polygon(point(0, 90)));
		EXPECT_FALSE(result->polygon2->is_point_in_polygon(point(0, 90)));
		std::reverse(cutter_points.begin(), cutter_points.end());
	}
	EXPECT_EQ(polygon->number_of_vertices_in_exterior_ring(), 4u);
	EXPECT_TRUE(polygon->is_point_in_polygon(point(0, -5)));
	EXPECT_TRUE(polygon->is_point_in_polygon(point(0, 5)));
}


TEST(SplitPlateOperationTest, bent_cut_can_enter_and_leave_the_same_boundary_segment)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	point_seq_type cutter_points = { point(-5, 20), point(0, 0), point(5, 20) };
	for (unsigned int direction = 0; direction < 2; ++direction)
	{
		SCOPED_TRACE(direction);
		const auto cutter = GPlatesMaths::PolylineOnSphere::create(cutter_points);
		QString error;
		const auto result = SplitPlate::split_polygon(error, *polygon, *cutter);
		ASSERT_TRUE(result) << error.toStdString();
		expect_partition(*result, *polygon, point(0, -5), point(0, 8));
		// The interior bend must appear in both output boundaries, not be shortcut.
		for (const auto &child : { result->polygon1, result->polygon2 })
		{
			EXPECT_NE(std::find(child->exterior_ring_vertex_begin(),
					child->exterior_ring_vertex_end(), point(0, 0)),
					child->exterior_ring_vertex_end());
		}
		std::reverse(cutter_points.begin(), cutter_points.end());
	}
}


TEST(SplitPlateOperationTest, uncut_holes_remain_in_their_respective_children)
{
	const std::vector<point_seq_type> holes = {
			rectangle_ring(-5, 5, -12, -8), rectangle_ring(-5, 5, 8, 12) };
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-20, 20, -20, 20), holes);
	const auto cutter = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-30, 0), point(30, 0) });
	QString error;
	const auto result = SplitPlate::split_polygon(error, *polygon, *cutter);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*result, *polygon, point(0, -15), point(0, 15));
	for (const auto &child : { result->polygon1, result->polygon2 })
	{
		EXPECT_EQ(child->number_of_interior_rings(), 1u);
		EXPECT_FALSE(child->is_point_in_polygon(point(0, -10)));
		EXPECT_FALSE(child->is_point_in_polygon(point(0, 10)));
	}
	EXPECT_EQ(polygon->number_of_interior_rings(), 2u);
}


TEST(SplitPlateOperationTest, cut_through_hole_is_rejected)
{
	const std::vector<point_seq_type> holes = { rectangle_ring(-5, 5, -5, 5) };
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-20, 20, -20, 20), holes);
	expect_rejected(*polygon, { point(-30, 0), point(30, 0) });
}


TEST(SplitPlateOperationTest, vertex_touch_is_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	expect_rejected(*polygon, { point(-20, -20), point(-10, -10), point(-20, 0) });
}


TEST(SplitPlateOperationTest, boundary_overlap_is_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	expect_rejected(*polygon,
			{ point(-20, -10), point(-10, -10), point(10, -10), point(20, -10) });
}


TEST(SplitPlateOperationTest, near_vertex_contact_is_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	// About one metre from the east corners: safely within GeometryIntersect's
	// roughly nine-metre vertex-contact tolerance, not a platform-sensitive tie.
	expect_rejected(*polygon, { point(-20, 9.99999), point(20, 9.99999) });
}


TEST(SplitPlateOperationTest, cutter_ending_inside_polygon_is_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	expect_rejected(*polygon, { point(-20, 0), point(0, 0) });
}


TEST(SplitPlateOperationTest, four_crossings_are_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	expect_rejected(*polygon,
			{ point(-5, -20), point(-5, 20), point(5, 20), point(5, -20) });
}


TEST(SplitPlateOperationTest, dateline_cut_preserves_spherical_interiors)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, 170, -170));
	const auto cutter = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-20, 180), point(20, 180) });
	QString error;
	const auto result = SplitPlate::split_polygon(error, *polygon, *cutter);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*result, *polygon, point(0, 175), point(0, -175));
	EXPECT_NEAR(result->polygon1->get_area().dval(), polygon->get_area().dval() / 2, 1e-10);
	EXPECT_NEAR(result->polygon2->get_area().dval(), polygon->get_area().dval() / 2, 1e-10);
	EXPECT_FALSE(result->polygon1->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result->polygon2->is_point_in_polygon(point(0, 0)));
}


TEST(SplitPlateOperationTest, multiple_cutters_partition_all_intersected_pieces)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	SplitPlate::polyline_seq_type cutters = {
			GPlatesMaths::PolylineOnSphere::create(point_seq_type{ point(-20, 0), point(20, 0) }),
			GPlatesMaths::PolylineOnSphere::create(point_seq_type{ point(0, -20), point(0, 20) }) };
	for (unsigned int order = 0; order < 2; ++order)
	{
		SCOPED_TRACE(order);
		QString error;
		const auto pieces = SplitPlate::split_polygon_with_polylines(error, *polygon, cutters);
		ASSERT_TRUE(pieces) << error.toStdString();
		ASSERT_EQ(pieces->size(), 4u);
		double total_area = 0;
		for (const auto &piece : *pieces)
		{
			total_area += piece->get_area().dval();
			EXPECT_NEAR(piece->get_area().dval(), polygon->get_area().dval() / 4, 1e-10);
		}
		EXPECT_NEAR(total_area, polygon->get_area().dval(), 1e-10);
		for (double latitude : { -5.0, 5.0 })
		{
			for (double longitude : { -5.0, 5.0 })
			{
				unsigned int containing_pieces = 0;
				for (const auto &piece : *pieces)
				{
					containing_pieces += piece->is_point_in_polygon(point(latitude, longitude));
				}
				EXPECT_EQ(containing_pieces, 1u);
			}
		}
		std::reverse(cutters.begin(), cutters.end());
	}
}


TEST(SplitPlateOperationTest, later_cutter_preserves_disjoint_pieces)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	const SplitPlate::polyline_seq_type cutters = {
			GPlatesMaths::PolylineOnSphere::create(point_seq_type{ point(-20, 0), point(20, 0) }),
			GPlatesMaths::PolylineOnSphere::create(point_seq_type{ point(-20, 5), point(20, 5) }) };
	QString error;
	const auto pieces = SplitPlate::split_polygon_with_polylines(error, *polygon, cutters);
	ASSERT_TRUE(pieces) << error.toStdString();
	ASSERT_EQ(pieces->size(), 3u);
	double total_area = 0;
	for (const auto &piece : *pieces)
	{
		total_area += piece->get_area().dval();
		if (piece->is_point_in_polygon(point(0, -5)))
		{
			EXPECT_NEAR(piece->get_area().dval(), polygon->get_area().dval() / 2, 1e-10);
		}
	}
	EXPECT_NEAR(total_area, polygon->get_area().dval(), 1e-10);
}


TEST(SplitPlateOperationTest, invalid_later_cutter_returns_no_partial_result)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	const auto first = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-20, 0), point(20, 0) });
	const auto second = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-20, 5), point(0, 5) });
	QString error;
	EXPECT_FALSE(SplitPlate::split_polygon_with_polylines(error, *polygon, { first, second }));
	EXPECT_TRUE(error.contains("2"));
	EXPECT_EQ(polygon->number_of_vertices_in_exterior_ring(), 4u);
	EXPECT_EQ(first->number_of_vertices(), 2u);
	EXPECT_EQ(second->get_vertex(1), point(0, 5));
}


TEST(SplitPlateOperationTest, empty_or_non_intersecting_cutter_selection_is_rejected)
{
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	QString error;
	EXPECT_FALSE(SplitPlate::split_polygon_with_polylines(error, *polygon, {}));
	EXPECT_FALSE(error.isEmpty());
	const auto cutter = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-20, 40), point(20, 40) });
	EXPECT_FALSE(SplitPlate::split_polygon_with_polylines(error, *polygon, { cutter }));
	EXPECT_FALSE(error.isEmpty());
}


TEST(SplitPlateOperationTest, one_undo_restores_polygon_metadata_and_preserves_cutters)
{
	GPlatesModel::ModelInterface model;
	const auto collection = GPlatesModel::FeatureCollectionHandle::create(model->root());
	const auto source = GPlatesModel::FeatureHandle::create(
			collection, GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
	const auto cutter_feature = GPlatesModel::FeatureHandle::create(
			collection, GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
	const auto polygon_name = GPlatesModel::PropertyName::create_gpml("outlineOf");
	const auto metadata_name = GPlatesModel::PropertyName::create_gml("description");
	const auto plate_name = GPlatesModel::PropertyName::create_gpml("reconstructionPlateId");
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	const auto source_property = source->add(GPlatesModel::TopLevelPropertyInline::create(
			polygon_name, GPlatesPropertyValues::GmlPolygon::create(polygon)));
	source->add(GPlatesModel::TopLevelPropertyInline::create(
			metadata_name, GPlatesPropertyValues::XsString::create(
					GPlatesUtils::UnicodeString("custom worldbuilding metadata"))));
	source->add(GPlatesModel::TopLevelPropertyInline::create(
			plate_name, GPlatesPropertyValues::GpmlPlateId::create(123)));
	const auto cutter = GPlatesMaths::PolylineOnSphere::create(
			point_seq_type{ point(-20, 0), point(20, 0) });
	const auto cutter_property = cutter_feature->add(GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("centerLineOf"),
			GPlatesPropertyValues::GmlLineString::create(cutter)));
	const auto original_cutter_property = *cutter_property;
	const auto original_cutter_revision = cutter_feature->revision_id();
	const auto source_id = source->feature_id();
	QString error;
	const auto pieces = SplitPlate::split_polygon_with_polylines(error, *polygon,
			{ cutter, GPlatesMaths::PolylineOnSphere::create(
					point_seq_type{ point(0, -20), point(0, 20) }) });
	ASSERT_TRUE(pieces) << error.toStdString();
	ASSERT_EQ(pieces->size(), 4u);
	GPlatesViewOperations::RenderedGeometryCollection rendered_geometries;
	GPlatesGui::FeatureFocus focus(rendered_geometries);
	QUndoStack undo;
	auto command = SplitPlate::create_split_command(focus, model, source, source_property, *pieces);
	ASSERT_TRUE(command);
	EXPECT_EQ(collection->size(), 2u);
	undo.push(command.release());
	EXPECT_EQ(undo.count(), 1);
	EXPECT_EQ(collection->size(), 5u);
	EXPECT_EQ(source->feature_id(), source_id);
	for (auto feature = collection->begin(); feature != collection->end(); ++feature)
	{
		if ((*feature)->reference() == cutter_feature)
		{
			continue;
		}
		const auto metadata = GPlatesModel::get_property_value<GPlatesPropertyValues::XsString>(
				(*feature)->reference(), metadata_name);
		const auto plate_id = GPlatesModel::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
				(*feature)->reference(), plate_name);
		ASSERT_TRUE(metadata);
		ASSERT_TRUE(plate_id);
		EXPECT_EQ((*metadata)->get_value().get().qstring(), "custom worldbuilding metadata");
		EXPECT_EQ((*plate_id)->get_value(), 123);
	}
	undo.undo();
	EXPECT_EQ(collection->size(), 2u);
	const auto restored = GPlatesModel::get_property_value<GPlatesPropertyValues::GmlPolygon>(
			source, polygon_name);
	ASSERT_TRUE(restored);
	EXPECT_EQ((*restored)->get_polygon(), polygon);
	EXPECT_EQ(cutter_feature->revision_id(), original_cutter_revision);
	EXPECT_EQ(*cutter_property, original_cutter_property);
	EXPECT_EQ(cutter_feature->parent_ptr(), collection.handle_ptr());
	undo.redo();
	EXPECT_EQ(collection->size(), 5u);
	EXPECT_EQ(cutter_feature->revision_id(), original_cutter_revision);
	EXPECT_EQ(*cutter_property, original_cutter_property);
}


TEST(SplitPlateOperationTest, unsupported_geometry_container_is_not_silently_cloned)
{
	GPlatesModel::ModelInterface model;
	const auto collection = GPlatesModel::FeatureCollectionHandle::create(model->root());
	const auto source = GPlatesModel::FeatureHandle::create(
			collection, GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
	const polygon_ptr_type polygon = GPlatesMaths::PolygonOnSphere::create(
			rectangle_ring(-10, 10, -10, 10));
	const auto windows = GPlatesPropertyValues::GpmlPiecewiseAggregation::create(
			std::vector<GPlatesPropertyValues::GpmlTimeWindow::non_null_ptr_type>(),
			GPlatesPropertyValues::GmlPolygon::create(polygon)->get_structural_type());
	const auto property = source->add(GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("outlineOf"), windows));
	GPlatesViewOperations::RenderedGeometryCollection rendered_geometries;
	GPlatesGui::FeatureFocus focus(rendered_geometries);
	EXPECT_FALSE(SplitPlate::create_split_command(focus, model, source, property, { polygon, polygon }));
	EXPECT_EQ(collection->size(), 1u);
}
