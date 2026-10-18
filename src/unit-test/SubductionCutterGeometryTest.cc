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

#include <vector>
#include <gtest/gtest.h>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"

#include "view-operations/SubductionCutterGeometry.h"


namespace
{
	namespace Cutter = GPlatesViewOperations::SubductionCutterGeometry;


	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	Cutter::polygon_ptr_type
	rectangle(
			double south,
			double north,
			double west,
			double east)
	{
		const std::vector<GPlatesMaths::PointOnSphere> vertices = {
				point(south, west), point(south, east), point(north, east), point(north, west) };
		return GPlatesMaths::PolygonOnSphere::create(vertices);
	}
}


TEST(SubductionCutterGeometryTest, disjoint_cutter_preserves_target)
{
	const Cutter::polygon_ptr_type target = rectangle(-10, 10, -10, 10);
	const Cutter::polygon_seq_type cutters = { rectangle(-10, 10, 40, 60) };
	const Cutter::CutResult result = Cutter::cut_polygon(*target, cutters);

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_FALSE(result.overlap);
	EXPECT_TRUE(result.inside.empty());
	ASSERT_EQ(result.outside.size(), 1u);
	EXPECT_DOUBLE_EQ(result.outside.front()->get_area().dval(), target->get_area().dval());
	EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, 50)));
}


TEST(SubductionCutterGeometryTest, containing_cutter_covers_target)
{
	const Cutter::polygon_ptr_type target = rectangle(-10, 10, -10, 10);
	const Cutter::polygon_seq_type cutters = { rectangle(-20, 20, -20, 20) };
	const Cutter::CutResult result = Cutter::cut_polygon(*target, cutters);

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	EXPECT_TRUE(result.outside.empty());
	ASSERT_EQ(result.inside.size(), 1u);
	const double target_area = target->get_area().dval();
	// Clipping tessellates the spherical edges; allow a small area approximation error.
	EXPECT_NEAR(result.inside.front()->get_area().dval(), target_area, target_area * 1e-5);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, 15)));
}


TEST(SubductionCutterGeometryTest, crossing_cutter_partitions_target)
{
	const Cutter::polygon_ptr_type target = rectangle(-10, 10, -10, 10);
	// The zero-longitude meridian bisects the symmetric target into equal spherical areas.
	const Cutter::polygon_seq_type cutters = { rectangle(-20, 20, 0, 20) };
	const Cutter::CutResult result = Cutter::cut_polygon(*target, cutters);

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	ASSERT_EQ(result.inside.size(), 1u);
	ASSERT_EQ(result.outside.size(), 1u);
	const double target_area = target->get_area().dval();
	const double inside_area = result.inside.front()->get_area().dval();
	const double outside_area = result.outside.front()->get_area().dval();
	EXPECT_NEAR(inside_area, target_area / 2, target_area * 1e-5);
	EXPECT_NEAR(outside_area, target_area / 2, target_area * 1e-5);
	EXPECT_NEAR(inside_area + outside_area, target_area, target_area * 1e-5);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, 5)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, -5)));
	EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, -5)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, 5)));
}
