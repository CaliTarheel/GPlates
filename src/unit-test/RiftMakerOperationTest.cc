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
#include <stdexcept>
#include <vector>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "app-logic/GeometryUtils.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "maths/GeometryDistance.h"
#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/PropertyValueFinder.h"

#include "property-values/GmlLineString.h"
#include "property-values/GmlOrientableCurve.h"

#include "view-operations/RiftMakerOperation.h"
#include "view-operations/SplitPlateOperation.h"

namespace
{
	typedef GPlatesViewOperations::RiftMakerOperation Operation;
	typedef GPlatesViewOperations::SplitPlateOperation Split;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;

	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	void
	expect_point(
			const GPlatesMaths::PointOnSphere &actual,
			const GPlatesMaths::PointOnSphere &expected)
	{
		// Component differences remain accurate where acos(dot) loses small-angle precision.
		EXPECT_NEAR(actual.position_vector().x().dval(), expected.position_vector().x().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().y().dval(), expected.position_vector().y().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().z().dval(), expected.position_vector().z().dval(), 1e-10);
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	square()
	{
		return GPlatesMaths::PolygonOnSphere::create(
				Points{point(-10, -10), point(-10, 10), point(10, 10), point(10, -10)});
	}

	void
	expect_partition(
			const GPlatesMaths::PolygonOnSphere &original,
			const Split::SketchSplitResult &result)
	{
		ASSERT_EQ(result.polygons.size(), result.cut_spans.size() + 1);
		ASSERT_EQ(result.adjacent_polygons.size(), result.cut_spans.size());
		double area = 0;
		for (const auto &piece : result.polygons)
		{
			EXPECT_GT(piece->get_area().dval(), 1e-12);
			area += piece->get_area().dval();
		}
		EXPECT_NEAR(area, original.get_area().dval(), 1e-10);
		for (std::size_t index = 0; index < result.cut_spans.size(); ++index)
		{
			const auto &span = result.cut_spans[index];
			const auto sides = result.adjacent_polygons[index];
			ASSERT_LT(sides.first, result.polygons.size());
			ASSERT_LT(sides.second, result.polygons.size());
			EXPECT_NE(sides.first, sides.second);
			for (auto arc = span->begin(); arc != span->end(); ++arc)
			{
				const auto middle = arc->point_on_arc(0.5);
				EXPECT_TRUE(original.is_point_in_polygon(middle,
						GPlatesMaths::PolygonOnSphere::ADAPTIVE, false));
				for (const auto side : {sides.first, sides.second})
				{
					auto closest = middle.position_vector();
					GPlatesMaths::minimum_distance(middle, *result.polygons[side], false,
							boost::none, closest);
					expect_point(GPlatesMaths::PointOnSphere(closest), middle);
				}
			}
		}
	}
}

TEST(RiftMakerGeometryTest, WavySketchProducesOnlySharedInteriorSpan)
{
	const auto polygon = square();
	const Points points{point(0, -20), point(2, -5), point(-2, 5), point(0, 20)};
	const auto sketch = GPlatesMaths::PolylineOnSphere::create(points);
	QString error;
	const auto result = Split::split_polygon_along_sketch(error, *polygon, *sketch);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*polygon, *result);
	ASSERT_EQ(result->cut_spans.size(), 1u);
	const auto span = result->cut_spans.front();
	ASSERT_EQ(span->number_of_vertices(), 4u);
	expect_point(span->get_vertex(1), points[1]);
	expect_point(span->get_vertex(2), points[2]);
	EXPECT_LT(span->get_arc_length().dval(), sketch->get_arc_length().dval());
	for (unsigned int index = 0; index < points.size(); ++index)
	{
		expect_point(sketch->get_vertex(index), points[index]);
	}
}

TEST(RiftMakerGeometryTest, ConcaveReentryMakesSeparateMorsAndThreePieces)
{
	const auto polygon = GPlatesMaths::PolygonOnSphere::create(Points{
			point(-15, -20), point(-15, 20), point(15, 20), point(15, 5),
			point(-5, 5), point(-5, -5), point(15, -5), point(15, -20)});
	const auto sketch = GPlatesMaths::PolylineOnSphere::create(
			Points{point(0, -30), point(0, 0), point(0, 30)});
	QString error;
	const auto result = Split::split_polygon_along_sketch(error, *polygon, *sketch);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*polygon, *result);
	ASSERT_EQ(result->cut_spans.size(), 2u);
	expect_point(result->cut_spans[0]->get_vertex(0), point(0, -20));
	expect_point(result->cut_spans[0]->get_vertex(1), point(0, -5));
	expect_point(result->cut_spans[1]->get_vertex(0), point(0, 5));
	expect_point(result->cut_spans[1]->get_vertex(1), point(0, 20));
	EXPECT_EQ(result->adjacent_polygons[0].second, result->adjacent_polygons[1].second);
	EXPECT_TRUE(result->polygons[result->adjacent_polygons[0].first]->is_point_in_polygon(
			point(8, -12)));
	EXPECT_TRUE(result->polygons[result->adjacent_polygons[1].first]->is_point_in_polygon(
			point(8, 12)));
	EXPECT_TRUE(result->polygons[result->adjacent_polygons[0].second]->is_point_in_polygon(
			point(-10, 0)));
}

TEST(RiftMakerGeometryTest, ReversingSketchSwapsSidesAndPreservesPartition)
{
	const auto polygon = square();
	for (const bool reverse : {false, true})
	{
		Points points{point(0, -20), point(0, 20)};
		if (reverse)
		{
			std::reverse(points.begin(), points.end());
		}
		QString error;
		const auto result = Split::split_polygon_along_sketch(error, *polygon,
				*GPlatesMaths::PolylineOnSphere::create(points));
		ASSERT_TRUE(result) << error.toStdString();
		expect_partition(*polygon, *result);
		const auto sides = result->adjacent_polygons.front();
		EXPECT_TRUE(result->polygons[sides.first]->is_point_in_polygon(point(reverse ? -5 : 5, 0)));
		EXPECT_TRUE(result->polygons[sides.second]->is_point_in_polygon(point(reverse ? 5 : -5, 0)));
	}
}

TEST(RiftMakerGeometryTest, PreservesUncutHoleAndRejectsHoleCrossing)
{
	const auto outer = square();
	const Points hole{point(3, -3), point(3, 3), point(7, 3), point(7, -3)};
	const auto polygon = GPlatesMaths::PolygonOnSphere::create(
			Points(outer->exterior_ring_vertex_begin(), outer->exterior_ring_vertex_end()),
			std::vector<Points>{hole});
	QString error;
	const auto result = Split::split_polygon_along_sketch(error, *polygon,
			*GPlatesMaths::PolylineOnSphere::create(Points{point(0, -20), point(0, 20)}));
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*polygon, *result);
	unsigned int holes = 0;
	for (const auto &piece : result->polygons)
	{
		holes += piece->number_of_interior_rings();
		EXPECT_FALSE(piece->is_point_in_polygon(point(5, 0)));
	}
	EXPECT_EQ(holes, 1u);
	EXPECT_FALSE(Split::split_polygon_along_sketch(error, *polygon,
			*GPlatesMaths::PolylineOnSphere::create(Points{point(-20, 0), point(20, 0)})));
	EXPECT_TRUE(error.contains("hole"));
	EXPECT_EQ(polygon->number_of_interior_rings(), 1u);
}

TEST(RiftMakerGeometryTest, RejectsDanglingTangentialAndSelfCrossingSketches)
{
	const auto polygon = square();
	const std::vector<Points> bad{
			{point(0, 0), point(0, 20)},
			{point(-20, -10), point(-10, -10), point(10, -10), point(20, -10)},
			{point(-20, -20), point(5, 5), point(5, -5), point(-20, 20)},
			{point(0, -20), point(0, 0), point(0, -20)}};
	for (const auto &points : bad)
	{
		QString error;
		EXPECT_FALSE(Split::split_polygon_along_sketch(error, *polygon,
				*GPlatesMaths::PolylineOnSphere::create(points)));
		EXPECT_FALSE(error.isEmpty());
	}
}

TEST(RiftMakerGeometryTest, DatelineCrossingNeedsNoPlanarProjection)
{
	const auto polygon = GPlatesMaths::PolygonOnSphere::create(Points{
			point(-10, 170), point(-10, -170), point(10, -170), point(10, 170)});
	const auto sketch = GPlatesMaths::PolylineOnSphere::create(
			Points{point(0, 160), point(0, 180), point(0, -160)});
	QString error;
	const auto result = Split::split_polygon_along_sketch(error, *polygon, *sketch);
	ASSERT_TRUE(result) << error.toStdString();
	expect_partition(*polygon, *result);
	ASSERT_EQ(result->cut_spans.size(), 1u);
	ASSERT_EQ(result->cut_spans[0]->number_of_vertices(), 3u);
	expect_point(result->cut_spans[0]->get_vertex(0), point(0, 170));
	expect_point(result->cut_spans[0]->get_vertex(2), point(0, -170));
}

namespace
{
	class RiftMakerOperationTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_directory.isValid());
			const QString path = d_directory.filePath("plates.rot");
			QFile rotations(path);
			ASSERT_TRUE(rotations.open(QIODevice::WriteOnly | QIODevice::Text));
			const QByteArray data("201 0 90 0 0 0 ! left\n201 1000 90 0 20 0 ! left\n"
					"801 0 90 0 0 201 ! right\n801 1000 90 0 40 201 ! right\n");
			ASSERT_EQ(rotations.write(data), data.size());
			rotations.close();
			d_rotation_file = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_registry.read_feature_collection(d_rotation_file.get()->get_reference(), errors);
			ASSERT_TRUE(errors.is_empty());
			d_tree = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
					std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref>{
							d_rotation_file.get()->get_reference().get_feature_collection()});
		}
		QTemporaryDir d_directory;
		GPlatesFileIO::FeatureCollectionFileFormat::Registry d_registry;
		boost::optional<GPlatesFileIO::File::non_null_ptr_type> d_rotation_file;
		boost::optional<GPlatesAppLogic::ReconstructionTreeCreator> d_tree;
	};
}

TEST_F(RiftMakerOperationTest, NativeRidgeReconstructsToExactCreationSpan)
{
	const Points points{point(-5, 20), point(0, 22), point(5, 20)};
	const auto span = GPlatesMaths::PolylineOnSphere::create(points);
	const auto ridge = Operation::create_mid_ocean_ridge(*span, 201, 801, *d_tree, 1000, 0, "Rift");
	EXPECT_FALSE(ridge->parent_ptr());
	const auto curve = GPlatesModel::get_property_value<GPlatesPropertyValues::GmlOrientableCurve>(
			ridge->reference(), GPlatesModel::PropertyName::create_gpml("centerLineOf"));
	ASSERT_TRUE(curve);
	const auto stored = (*curve)->base_curve()->get_polyline();
	const auto reconstructed = GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
			stored, GPlatesAppLogic::ReconstructMethodRegistry(), ridge->reference(),
			1000, *d_tree);
	const auto *line = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(reconstructed.get());
	ASSERT_NE(line, nullptr);
	ASSERT_EQ(line->number_of_vertices(), points.size());
	for (unsigned int index = 0; index < points.size(); ++index)
	{
		expect_point(line->get_vertex(index), points[index]);
		// At the import age Version3 starts in the left plate's 20-degree frame.
		const auto coordinates = GPlatesMaths::make_lat_lon_point(points[index]);
		expect_point(stored->get_vertex(index),
				point(coordinates.latitude(), coordinates.longitude() - 20));
	}
	GPlatesAppLogic::ReconstructionFeatureProperties properties;
	properties.visit_feature(ridge->reference());
	EXPECT_FALSE(properties.is_feature_defined_at_recon_time(1001));
	EXPECT_TRUE(properties.is_feature_defined_at_recon_time(1000));
	EXPECT_TRUE(properties.is_feature_defined_at_recon_time(0));
	ASSERT_TRUE(properties.get_left_plate_id());
	ASSERT_TRUE(properties.get_right_plate_id());
	EXPECT_EQ(*properties.get_left_plate_id(), 201);
	EXPECT_EQ(*properties.get_right_plate_id(), 801);
}

TEST_F(RiftMakerOperationTest, RefusesMissingIdenticalPlatesAndInvalidLifetime)
{
	const auto span = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	EXPECT_THROW(Operation::create_mid_ocean_ridge(*span, 201, 201, *d_tree, 1000, 0, "bad"),
			std::invalid_argument);
	EXPECT_THROW(Operation::create_mid_ocean_ridge(*span, 201, 999, *d_tree, 1000, 0, "bad"),
			std::invalid_argument);
	EXPECT_THROW(Operation::create_mid_ocean_ridge(*span, 201, 801, *d_tree, 1000, 1001, "bad"),
			std::invalid_argument);
}
