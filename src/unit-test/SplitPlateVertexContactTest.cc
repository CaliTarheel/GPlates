/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
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

#include "maths/FiniteRotation.h"
#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "view-operations/SplitPlateOperation.h"


namespace
{
	typedef GPlatesViewOperations::SplitPlateOperation Split;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;


	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	square()
	{
		return GPlatesMaths::PolygonOnSphere::create(
				Points{point(-10, -10), point(-10, 10), point(10, 10), point(10, -10)});
	}


	Points
	vertex_cutter_points()
	{
		return {point(-20, 0), point(0, 5), point(10, 10), point(20, 15)};
	}


	Points
	boundary_cutter_points()
	{
		return {point(-20, -10), point(-10, -10), point(10, -10), point(20, -10)};
	}


	void
	expect_partition(
			const GPlatesMaths::PolygonOnSphere &original,
			const Split::polygon_seq_type &pieces,
			const GPlatesMaths::PointOnSphere &left_sample,
			const GPlatesMaths::PointOnSphere &right_sample)
	{
		ASSERT_EQ(pieces.size(), 2u);
		EXPECT_GT(pieces[0]->get_area().dval(), 0);
		EXPECT_GT(pieces[1]->get_area().dval(), 0);
		EXPECT_NEAR(pieces[0]->get_area().dval() + pieces[1]->get_area().dval(),
				original.get_area().dval(), 1e-10);
		EXPECT_NE(pieces[0]->is_point_in_polygon(left_sample),
				pieces[0]->is_point_in_polygon(right_sample));
		EXPECT_EQ(pieces[0]->is_point_in_polygon(left_sample),
				!pieces[1]->is_point_in_polygon(left_sample));
		EXPECT_EQ(pieces[0]->is_point_in_polygon(right_sample),
				!pieces[1]->is_point_in_polygon(right_sample));
	}
}


TEST(SplitPlateVertexContactTest, SharedVertexThroughCrossingPartitionsInBothDirections)
{
	const auto polygon = square();
	auto points = vertex_cutter_points();
	for (unsigned int direction = 0; direction < 2; ++direction)
	{
		const auto cutter = GPlatesMaths::PolylineOnSphere::create(points);
		QString error;
		const auto result = Split::split_polygon(error, *polygon, *cutter);
		ASSERT_TRUE(result) << error.toStdString();
		expect_partition(*polygon, {result->polygon1, result->polygon2}, point(0, -5), point(0, 8));
		for (const auto &piece : {result->polygon1, result->polygon2})
		{
			EXPECT_NE(std::find(piece->exterior_ring_vertex_begin(),
					piece->exterior_ring_vertex_end(), point(10, 10)), piece->exterior_ring_vertex_end());
		}
		EXPECT_EQ(Points(cutter->vertex_begin(), cutter->vertex_end()), points);
		std::reverse(points.begin(), points.end());
	}
}


TEST(SplitPlateVertexContactTest, PolygonVertexOnCutterSegmentAndCutterVertexOnPolygonSegment)
{
	const auto triangle = GPlatesMaths::PolygonOnSphere::create(
			Points{point(-10, -10), point(-10, 10), point(10, 0)});
	const auto meridian = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-20, 0), point(20, 0)});
	QString error;
	const auto through_polygon_vertex = Split::split_polygon(error, *triangle, *meridian);
	ASSERT_TRUE(through_polygon_vertex) << error.toStdString();
	expect_partition(*triangle,
			{through_polygon_vertex->polygon1, through_polygon_vertex->polygon2},
			point(-5, -3), point(-5, 3));

	const auto polygon = square();
	const auto equator = GPlatesMaths::PolylineOnSphere::create(
			Points{point(0, -20), point(0, -10), point(0, 0), point(0, 10), point(0, 20)});
	const auto through_cutter_vertices = Split::split_polygon(error, *polygon, *equator);
	ASSERT_TRUE(through_cutter_vertices) << error.toStdString();
	expect_partition(*polygon,
			{through_cutter_vertices->polygon1, through_cutter_vertices->polygon2},
			point(-5, 0), point(5, 0));
}


TEST(SplitPlateVertexContactTest, VertexCrossingIsIndependentOfGlobeOrientation)
{
	const auto rotation = GPlatesMaths::FiniteRotation::create(
			point(30, 70), 2.5);
	const auto polygon = rotation * square();
	const auto cutter = rotation * GPlatesMaths::PolylineOnSphere::create(vertex_cutter_points());
	QString error;
	const auto result = Split::split_polygon(error, *polygon, *cutter);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*polygon, {result->polygon1, result->polygon2},
			rotation * point(0, -5), rotation * point(0, 8));
}


TEST(SplitPlateVertexContactTest, BoundaryAndDuplicateCuttersAreNoOpsOnlyWithARealSplit)
{
	const auto polygon = square();
	const auto boundary = GPlatesMaths::PolylineOnSphere::create(boundary_cutter_points());
	const auto through = GPlatesMaths::PolylineOnSphere::create(vertex_cutter_points());
	auto reversed_points = vertex_cutter_points();
	std::reverse(reversed_points.begin(), reversed_points.end());
	const auto reversed = GPlatesMaths::PolylineOnSphere::create(reversed_points);
	for (const Split::polyline_seq_type &cutters : {
			Split::polyline_seq_type{boundary, through, through, reversed},
			Split::polyline_seq_type{through, boundary, reversed}})
	{
		QString error;
		const auto pieces = Split::split_polygon_with_polylines(error, *polygon, cutters);
		ASSERT_TRUE(pieces) << error.toStdString();
		expect_partition(*polygon, *pieces, point(0, -5), point(0, 8));
	}
	EXPECT_EQ(Points(boundary->vertex_begin(), boundary->vertex_end()), boundary_cutter_points());
	EXPECT_EQ(Points(through->vertex_begin(), through->vertex_end()), vertex_cutter_points());
	QString error;
	EXPECT_FALSE(Split::split_polygon_with_polylines(error, *polygon, {boundary, boundary}));
	EXPECT_TRUE(error.contains("no polygon was split"));
	EXPECT_FALSE(Split::split_polygon(error, *polygon, *boundary));
}


TEST(SplitPlateVertexContactTest, NearParallelInsideOrOutsideCutterIsNeverSkippedAsBoundary)
{
	const auto polygon = square();
	const auto through = GPlatesMaths::PolylineOnSphere::create(vertex_cutter_points());
	for (double longitude : {-9.99999, -10.00001})
	{
		SCOPED_TRACE(longitude);
		// About one metre from the existing edge: within native contact thickness,
		// but not coincidence. Ignoring this cutter would silently lose a narrow strip.
		const auto near_boundary = GPlatesMaths::PolylineOnSphere::create(
				Points{point(-20, longitude), point(20, longitude)});
		QString error;
		EXPECT_FALSE(Split::split_polygon_with_polylines(
				error, *polygon, {near_boundary, through}));
		EXPECT_FALSE(error.isEmpty());
	}
}


TEST(SplitPlateVertexContactTest, TangenciesEndpointsAndUnrelatedOutsideCuttersRemainErrors)
{
	const auto polygon = square();
	const auto through = GPlatesMaths::PolylineOnSphere::create(vertex_cutter_points());
	const std::vector<Points> invalid = {
			{point(-20, -20), point(-10, -10), point(-20, 0)},
			{point(-20, 0), point(0, 0), point(10, 10)},
			{point(-10, -10), point(0, 0), point(20, 0)},
			{point(-20, 30), point(20, 30)}};
	for (const auto &points : invalid)
	{
		const auto cutter = GPlatesMaths::PolylineOnSphere::create(points);
		QString error;
		EXPECT_FALSE(Split::split_polygon(error, *polygon, *cutter));
		EXPECT_FALSE(Split::split_polygon_with_polylines(error, *polygon, {cutter, through}));
	}
}


TEST(SplitPlateVertexContactTest, OverlapMixedWithInteriorAndHoleContactsRemainErrors)
{
	const auto polygon = square();
	const auto overlap = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-20, -10), point(-10, -10), point(0, -10), point(0, 0), point(20, 0)});
	const auto through = GPlatesMaths::PolylineOnSphere::create(vertex_cutter_points());
	QString error;
	EXPECT_FALSE(Split::split_polygon(error, *polygon, *overlap));
	EXPECT_FALSE(Split::split_polygon_with_polylines(error, *polygon, {overlap, through}));
	const auto with_hole = GPlatesMaths::PolygonOnSphere::create(
			Points{point(-10, -10), point(-10, 10), point(10, 10), point(10, -10)},
			std::vector<Points>{{point(-2, -2), point(-2, 2), point(2, 0)}});
	const auto hole_vertex = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-20, 0), point(2, 0), point(20, 0)});
	EXPECT_FALSE(Split::split_polygon(error, *with_hole, *hole_vertex));
	EXPECT_TRUE(error.contains("hole"));
}


TEST(SplitPlateVertexContactTest, SelfCrossingInteriorPathIsRejected)
{
	const auto polygon = square();
	const auto crossing = GPlatesMaths::PolylineOnSphere::create(Points{
			point(-20, 0), point(-5, -5), point(5, 5), point(-5, 5), point(5, -5), point(20, 0)});
	QString error;
	EXPECT_FALSE(Split::split_polygon(error, *polygon, *crossing));
	EXPECT_TRUE(error.contains("itself")) << error.toStdString();
}


TEST(SplitPlateVertexContactTest, InsideEndpointsWithAnOutsideExcursionDoNotCutThePolygon)
{
	const auto polygon = square();
	const auto excursion = GPlatesMaths::PolylineOnSphere::create(Points{
			point(-5, 0), point(-20, -5), point(-20, 5), point(5, 0)});
	QString error;
	EXPECT_FALSE(Split::split_polygon(error, *polygon, *excursion));
	EXPECT_TRUE(error.contains("start outside")) << error.toStdString();
}


TEST(SplitPlateVertexContactTest, DetailedCutValidationHasABoundedWorkload)
{
	const auto polygon = square();
	Points points;
	for (unsigned int index = 0; index <= 900; ++index)
	{
		points.push_back(point(-12 + 24.0 * index / 900, 0));
	}
	const auto cutter = GPlatesMaths::PolylineOnSphere::create(points);
	QString error;
	EXPECT_FALSE(Split::split_polygon(error, *polygon, *cutter));
	EXPECT_TRUE(error.contains("too detailed")) << error.toStdString();
}
