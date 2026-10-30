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

#include <cmath>
#include <vector>
#include <gtest/gtest.h>

#include "maths/LatLonPoint.h"
#include "maths/MathsUtils.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/UnitVector3D.h"

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


	Cutter::polygon_ptr_type
	with_hole(
			const GPlatesMaths::PolygonOnSphere &exterior,
			const GPlatesMaths::PolygonOnSphere &hole)
	{
		const std::vector<std::vector<GPlatesMaths::PointOnSphere> > interior_rings = {
				std::vector<GPlatesMaths::PointOnSphere>(
						hole.exterior_ring_vertex_begin(), hole.exterior_ring_vertex_end()) };
		return GPlatesMaths::PolygonOnSphere::create(
				exterior.exterior_ring_vertex_begin(), exterior.exterior_ring_vertex_end(),
				interior_rings.begin(), interior_rings.end());
	}


	Cutter::polygon_ptr_type
	wrapping_band()
	{
		// The narrow 576-degree helix from PointInPolygonTest: a simple ring whose
		// interior contains the antipode of its boundary centroid.
		const unsigned int num_points_per_side = 54;
		std::vector<GPlatesMaths::PointOnSphere> vertices;
		for (unsigned int side = 0; side < 2; ++side)
		{
			for (unsigned int k = 0; k < num_points_per_side; ++k)
			{
				const unsigned int index = side == 0 ? k : num_points_per_side - 1 - k;
				const double fraction = double(index) / (num_points_per_side - 1);
				const double colatitude = 0.779 + 1.579 * fraction +
						(side == 0 ? 0.065 / 2 : -0.065 / 2);
				const double longitude = 576 * GPlatesMaths::PI / 180 * fraction;
				vertices.push_back(GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(
						std::sin(colatitude) * std::cos(longitude),
						std::sin(colatitude) * std::sin(longitude), std::cos(colatitude))));
			}
		}
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


TEST(SubductionCutterGeometryTest, cutter_inside_target_hole_preserves_target)
{
	const Cutter::polygon_ptr_type target = with_hole(
			*rectangle(-20, 20, -20, 20), *rectangle(-10, 10, -10, 10));
	const Cutter::polygon_seq_type cutters = { rectangle(-5, 5, -5, 5) };
	const Cutter::CutResult result = Cutter::cut_polygon(*target, cutters);

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_FALSE(result.overlap);
	EXPECT_TRUE(result.inside.empty());
	ASSERT_EQ(result.outside.size(), 1u);
	EXPECT_EQ(result.outside.front()->number_of_interior_rings(), 1u);
	EXPECT_DOUBLE_EQ(result.outside.front()->get_area().dval(), target->get_area().dval());
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, 15)));
}


TEST(SubductionCutterGeometryTest, interior_cutter_leaves_a_hole_in_survivor)
{
	const Cutter::polygon_ptr_type target = rectangle(-20, 20, -20, 20);
	const Cutter::polygon_ptr_type cutter = rectangle(-5, 5, -5, 5);
	const Cutter::CutResult result = Cutter::cut_polygon(*target, { cutter });

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	ASSERT_EQ(result.inside.size(), 1u);
	ASSERT_EQ(result.outside.size(), 1u);
	EXPECT_EQ(result.inside.front()->number_of_interior_rings(), 0u);
	EXPECT_EQ(result.outside.front()->number_of_interior_rings(), 1u);
	const double target_area = target->get_area().dval();
	const double cutter_area = cutter->get_area().dval();
	const double inside_area = result.inside.front()->get_area().dval();
	const double outside_area = result.outside.front()->get_area().dval();
	// Tessellated clipping preserves spherical areas to a small relative approximation error.
	EXPECT_NEAR(inside_area, cutter_area, cutter_area * 1e-5);
	EXPECT_NEAR(outside_area, target_area - cutter_area, target_area * 1e-5);
	EXPECT_NEAR(inside_area + outside_area, target_area, target_area * 1e-5);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, 15)));
	EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, 15)));
}


TEST(SubductionCutterGeometryTest, dateline_crossing_cutter_partitions_target)
{
	const Cutter::polygon_ptr_type target = rectangle(-10, 10, 170, -170);
	// The dateline bisects the target; the cutter contains its eastern half.
	const Cutter::polygon_seq_type cutters = { rectangle(-20, 20, 180, -160) };
	const Cutter::CutResult result = Cutter::cut_polygon(*target, cutters);

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	ASSERT_EQ(result.inside.size(), 1u);
	ASSERT_EQ(result.outside.size(), 1u);
	const double target_area = target->get_area().dval();
	const double inside_area = result.inside.front()->get_area().dval();
	const double outside_area = result.outside.front()->get_area().dval();
	// Use the same relative area tolerance as the equivalent cut at longitude zero.
	EXPECT_NEAR(inside_area, target_area / 2, target_area * 1e-5);
	EXPECT_NEAR(outside_area, target_area / 2, target_area * 1e-5);
	EXPECT_NEAR(inside_area + outside_area, target_area, target_area * 1e-5);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, -175)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, 175)));
	EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, 175)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, -175)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, 0)));
}


TEST(SubductionCutterGeometryTest, wrapping_target_preserves_its_spherical_interior)
{
	const Cutter::polygon_ptr_type target = wrapping_band();
	// The band centre at half its length is near latitude zero, longitude -72 degrees.
	const Cutter::polygon_ptr_type cutter = rectangle(-0.25, 0.25, -72.25, -71.75);
	const GPlatesMaths::PointOnSphere cut_point = point(0, -72);
	ASSERT_TRUE(target->is_point_in_polygon(cut_point));
	ASSERT_FALSE(target->is_point_in_polygon(point(30, -72)));
	const Cutter::CutResult result = Cutter::cut_polygon(*target, { cutter });

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	ASSERT_EQ(result.inside.size(), 1u);
	ASSERT_EQ(result.outside.size(), 1u);
	EXPECT_EQ(result.outside.front()->number_of_interior_rings(), 1u);
	const double target_area = target->get_area().dval();
	const double cutter_area = cutter->get_area().dval();
	const double inside_area = result.inside.front()->get_area().dval();
	const double outside_area = result.outside.front()->get_area().dval();
	// A globe-spanning outline is more distorted by the tessellated projection than a local cut.
	EXPECT_NEAR(inside_area, cutter_area, cutter_area * 1e-4);
	EXPECT_NEAR(outside_area, target_area - cutter_area, target_area * 1e-4);
	EXPECT_NEAR(inside_area + outside_area, target_area, target_area * 1e-4);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(cut_point));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(cut_point));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(30, -72)));
	EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(30, -72)));
}


TEST(SubductionCutterGeometryTest, cutter_containing_projection_antipode_covers_target)
{
	const Cutter::polygon_ptr_type target = rectangle(-5, 5, -5, 5);
	// Under spherical ring parity these disjoint rings are both filled, as in
	// PointInPolygonTest.antipodal_rings. One covers the target, the other its antipode.
	const Cutter::polygon_ptr_type cutter = with_hole(
			*rectangle(-10, 10, -10, 10), *rectangle(-10, 10, 170, -170));
	ASSERT_TRUE(cutter->is_point_in_polygon(point(0, 0)));
	ASSERT_TRUE(cutter->is_point_in_polygon(
			GPlatesMaths::PointOnSphere(target->get_outside_point())));
	const Cutter::CutResult result = Cutter::cut_polygon(*target, { cutter });

	ASSERT_TRUE(result.success) << result.error.toStdString();
	EXPECT_TRUE(result.overlap);
	ASSERT_EQ(result.inside.size(), 1u);
	EXPECT_TRUE(result.outside.empty());
	const double target_area = target->get_area().dval();
	EXPECT_NEAR(result.inside.front()->get_area().dval(), target_area, target_area * 1e-5);
	EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, 0)));
	EXPECT_FALSE(result.inside.front()->is_point_in_polygon(point(0, 180)));
}


TEST(SubductionCutterGeometryTest, complemented_and_ordinary_cutters_form_a_union)
{
	const Cutter::polygon_ptr_type target = rectangle(-10, 10, -10, 10);
	const Cutter::polygon_ptr_type complemented_cutter = with_hole(
			*rectangle(-20, 20, -20, 0), *rectangle(-10, 10, 170, -170));
	const Cutter::polygon_ptr_type ordinary_cutter = rectangle(-20, 20, 0, 20);
	ASSERT_TRUE(complemented_cutter->is_point_in_polygon(
			GPlatesMaths::PointOnSphere(target->get_outside_point())));
	ASSERT_FALSE(ordinary_cutter->is_point_in_polygon(
			GPlatesMaths::PointOnSphere(target->get_outside_point())));
	const std::vector<Cutter::polygon_seq_type> cutter_sets = {
			{ complemented_cutter },
			{ complemented_cutter, ordinary_cutter },
			{ ordinary_cutter, complemented_cutter } };
	for (unsigned int index = 0; index < cutter_sets.size(); ++index)
	{
		SCOPED_TRACE(index);
		const Cutter::CutResult result = Cutter::cut_polygon(*target, cutter_sets[index]);
		ASSERT_TRUE(result.success) << result.error.toStdString();
		EXPECT_TRUE(result.overlap);
		ASSERT_EQ(result.inside.size(), 1u);
		const double target_area = target->get_area().dval();
		const double expected_inside_area = index == 0 ? target_area / 2 : target_area;
		EXPECT_NEAR(
				result.inside.front()->get_area().dval(), expected_inside_area, target_area * 1e-5);
		EXPECT_TRUE(result.inside.front()->is_point_in_polygon(point(0, -5)));
		EXPECT_EQ(result.inside.front()->is_point_in_polygon(point(0, 5)), index != 0);
		if (index == 0)
		{
			ASSERT_EQ(result.outside.size(), 1u);
			EXPECT_TRUE(result.outside.front()->is_point_in_polygon(point(0, 5)));
			EXPECT_FALSE(result.outside.front()->is_point_in_polygon(point(0, -5)));
		}
		else
		{
			EXPECT_TRUE(result.outside.empty());
		}
	}
}
