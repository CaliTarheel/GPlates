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

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

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
