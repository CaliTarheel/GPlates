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

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <boost/optional.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "app-logic/FlowlineGeometryPopulator.h"
#include "app-logic/FlowlineUtils.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ReconstructedFlowline.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "view-operations/MakeOceanCrustOperation.h"


namespace
{
	typedef GPlatesViewOperations::MakeOceanCrustOperation Operation;


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
		// Cartesian component tolerance avoids acos precision loss near coincident points.
		// 1e-10 radians is below a millimetre on an Earth-sized sphere.
		EXPECT_NEAR(actual.position_vector().x().dval(), expected.position_vector().x().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().y().dval(), expected.position_vector().y().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().z().dval(), expected.position_vector().z().dval(), 1e-10);
	}


	class MakeOceanCrustOperationTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_directory.isValid());
			const QString path = d_directory.filePath("spreading.rot");
			QFile rotation_file(path);
			ASSERT_TRUE(rotation_file.open(QIODevice::WriteOnly | QIODevice::Text));
			// Both rotations are about the north pole and linear in geological age.
			// At 1000 Ma: left absolute 20 degrees; right relative 40, absolute 60 degrees.
			// By 990 Ma: left absolute 19.8; right absolute 59.4 degrees. Thus a ridge
			// seed at longitude 20 spreads about longitude 19.6, with oldest-crust ends
			// at longitudes 19.8 and 19.4. These expectations do not use FlowlineUtils.
			const QByteArray rotations(
					"201 0 90 0 0 0 ! left\n"
					"201 1000 90 0 20 0 ! left\n"
					"501 0 90 0 0 201 ! no relative motion\n"
					"501 1000 90 0 0 201 ! no relative motion\n"
					"801 0 90 0 0 201 ! right\n"
					"801 1000 90 0 40 201 ! right\n");
			ASSERT_EQ(rotation_file.write(rotations), rotations.size());
			rotation_file.close();
			d_rotation_file = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_registry.read_feature_collection(d_rotation_file.get()->get_reference(), errors);
			ASSERT_TRUE(errors.is_empty());
			d_tree_creator = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
					std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref>(
							1, d_rotation_file.get()->get_reference().get_feature_collection()));
		}

		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>
		reconstruct(
				const GPlatesModel::FeatureHandle::weak_ref &feature,
				double time)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			GPlatesAppLogic::FlowlineGeometryPopulator populator(geometries, d_tree_creator.get(), time);
			populator.visit_feature(feature);
			return geometries;
		}

		QTemporaryDir d_directory;
		GPlatesFileIO::FeatureCollectionFileFormat::Registry d_registry;
		boost::optional<GPlatesFileIO::File::non_null_ptr_type> d_rotation_file;
		boost::optional<GPlatesAppLogic::ReconstructionTreeCreator> d_tree_creator;
	};
}


TEST(MakeOceanCrustTimesTest, IncludesExactFutureEndpointAndShortFinalInterval)
{
	EXPECT_EQ(Operation::sample_times(1000, 10, 3),
			(std::vector<double>{990, 991, 994, 997, 1000}));
	EXPECT_EQ(Operation::sample_times(1000, 10, 5),
			(std::vector<double>{990, 995, 1000}));
	EXPECT_EQ(Operation::sample_times(10, 10, 20), (std::vector<double>{0, 10}));
	EXPECT_EQ(Operation::sample_times(10, 0.5, 0.25),
			(std::vector<double>{9.5, 9.75, 10}));
}


TEST(MakeOceanCrustTimesTest, RejectsPastPresentAndUnboundedOrUnrepresentableWork)
{
	EXPECT_THROW(Operation::sample_times(10, 11, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(0, 1, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, 0, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, 1, 0), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, -1, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, 1, -1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, 1, 0.000001), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(1e20, 1, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(1e20, 20000, 1), std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, std::numeric_limits<double>::infinity(), 1),
			std::invalid_argument);
	EXPECT_THROW(Operation::sample_times(10, 1, std::numeric_limits<double>::quiet_NaN()),
			std::invalid_argument);
}


TEST_F(MakeOceanCrustOperationTest, EveryRidgeVertexSeedsPairedNativeFlowlines)
{
	const std::vector<GPlatesMaths::PointOnSphere> vertices = {
			point(-10, 20), point(0, 20), point(10, 20)};
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(vertices);
	const auto feature = Operation::create_flowline(
			*ridge, 201, 801, d_tree_creator.get(), 1000, 10, 5, "Test ridge");
	EXPECT_FALSE(feature->parent_ptr());
	EXPECT_EQ(feature->feature_type(), GPlatesModel::FeatureType::create_gpml("Flowline"));
	GPlatesAppLogic::FlowlineUtils::FlowlinePropertyFinder properties;
	properties.visit_feature(feature->reference());
	ASSERT_TRUE(properties.get_geometry_import_time());
	EXPECT_DOUBLE_EQ(*properties.get_geometry_import_time(), 1000);
	EXPECT_EQ(properties.get_times(), (std::vector<double>{990, 995, 1000}));
	ASSERT_TRUE(properties.get_left_plate());
	ASSERT_TRUE(properties.get_right_plate());
	EXPECT_EQ(*properties.get_left_plate(), 201u);
	EXPECT_EQ(*properties.get_right_plate(), 801u);

	const auto at_start = reconstruct(feature->reference(), 1000);
	ASSERT_EQ(at_start.size(), 1u); // The native renderer groups the multipoint seeds at their start.
	std::vector<GPlatesMaths::PointOnSphere> reconstructed_seeds;
	GPlatesAppLogic::GeometryUtils::get_geometry_points(
			*at_start.front()->reconstructed_geometry(), reconstructed_seeds);
	ASSERT_EQ(reconstructed_seeds.size(), vertices.size());
	const auto at_end = reconstruct(feature->reference(), 990);
	ASSERT_EQ(at_end.size(), vertices.size());
	for (std::size_t seed = 0; seed < vertices.size(); ++seed)
	{
		SCOPED_TRACE(seed);
		const double latitude = -10 + 10 * static_cast<double>(seed);
		expect_point(reconstructed_seeds[seed], vertices[seed]);
		expect_point(ridge->get_vertex(static_cast<unsigned int>(seed)), vertices[seed]);
		const auto *flowline =
				dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(at_end[seed].get());
		ASSERT_NE(flowline, nullptr);
		EXPECT_EQ(flowline->left_flowline_points()->number_of_vertices(), 3u);
		EXPECT_EQ(flowline->right_flowline_points()->number_of_vertices(), 3u);
		expect_point(flowline->present_day_seed_point(), point(latitude, 0));
		expect_point(flowline->reconstructed_seed_point(), point(latitude, 19.6));
		for (unsigned int sample = 0; sample < 3; ++sample)
		{
			expect_point(flowline->left_flowline_points()->get_vertex(sample),
					point(latitude, 19.6 + 0.1 * sample));
			expect_point(flowline->right_flowline_points()->get_vertex(sample),
					point(latitude, 19.6 - 0.1 * sample));
		}
	}
}


TEST_F(MakeOceanCrustOperationTest, ShortFinalIntervalAndDatelineUseRequestedSamples)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(
			std::vector<GPlatesMaths::PointOnSphere>{point(-5, -179.8), point(5, -179.8)});
	const auto feature = Operation::create_flowline(
			*ridge, 201, 801, d_tree_creator.get(), 1000, 10, 3, "Dateline ridge");
	const auto geometries = reconstruct(feature->reference(), 990);
	ASSERT_EQ(geometries.size(), 2u);
	for (unsigned int seed = 0; seed < 2; ++seed)
	{
		const auto *flowline =
				dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(geometries[seed].get());
		ASSERT_NE(flowline, nullptr);
		ASSERT_EQ(flowline->left_flowline_points()->number_of_vertices(), 5u);
		ASSERT_EQ(flowline->right_flowline_points()->number_of_vertices(), 5u);
		const double latitude = seed == 0 ? -5 : 5;
		const double ages[] = {990, 991, 994, 997, 1000};
		for (unsigned int sample = 0; sample < 5; ++sample)
		{
			// Centre crosses the dateline to +179.8. Each half spreads 0.02 degrees/Myr.
			expect_point(flowline->left_flowline_points()->get_vertex(sample),
					point(latitude, 179.8 + 0.02 * (ages[sample] - 990)));
			expect_point(flowline->right_flowline_points()->get_vertex(sample),
					point(latitude, 179.8 - 0.02 * (ages[sample] - 990)));
		}
	}
}


TEST_F(MakeOceanCrustOperationTest, RequiresDistinctPlatesAndRotationsThroughoutTheRange)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(
			std::vector<GPlatesMaths::PointOnSphere>{point(-5, 20), point(5, 20)});
	EXPECT_THROW(Operation::create_flowline(
			*ridge, 201, 201, d_tree_creator.get(), 1000, 10, 5, "Same plate"), std::invalid_argument);
	EXPECT_THROW(Operation::create_flowline(
			*ridge, 201, 999, d_tree_creator.get(), 1000, 10, 5, "Missing right"), std::invalid_argument);
	EXPECT_THROW(Operation::create_flowline(
			*ridge, 999, 801, d_tree_creator.get(), 1000, 10, 5, "Missing left"), std::invalid_argument);
	EXPECT_THROW(Operation::create_flowline(
			*ridge, 201, 801, d_tree_creator.get(), 1001, 10, 5, "Beyond rotation range"),
			std::invalid_argument);
	EXPECT_THROW(Operation::create_flowline(
			*ridge, 201, 501, d_tree_creator.get(), 1000, 10, 5, "No spreading"), std::runtime_error);
	EXPECT_NO_THROW(Operation::create_flowline(
			*ridge, 0, 201, d_tree_creator.get(), 1000, 10, 5, "Anchor is a valid plate"));
}


TEST_F(MakeOceanCrustOperationTest, SavedGpmlRetainsSeedsTimesAndBothHalves)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(
			std::vector<GPlatesMaths::PointOnSphere>{point(-5, 20), point(5, 20)});
	const auto feature = Operation::create_flowline(
			*ridge, 201, 801, d_tree_creator.get(), 1000, 10, 3, "Saved ridge");
	const QString path = d_directory.filePath("flowlines.gpml");
	const auto saved = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	saved->get_reference().get_feature_collection()->add(feature);
	d_registry.write_feature_collection(saved->get_reference());
	const auto loaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	GPlatesFileIO::ReadErrorAccumulation errors;
	d_registry.read_feature_collection(loaded->get_reference(), errors);
	ASSERT_TRUE(errors.is_empty());
	const auto collection = loaded->get_reference().get_feature_collection();
	ASSERT_EQ(collection->size(), 1u);
	const auto geometries = reconstruct((*collection->begin())->reference(), 990);
	ASSERT_EQ(geometries.size(), 2u);
	for (unsigned int seed = 0; seed < 2; ++seed)
	{
		const auto *flowline =
				dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(geometries[seed].get());
		ASSERT_NE(flowline, nullptr);
		ASSERT_EQ(flowline->left_flowline_points()->number_of_vertices(), 5u);
		ASSERT_EQ(flowline->right_flowline_points()->number_of_vertices(), 5u);
		const double latitude = seed == 0 ? -5 : 5;
		expect_point(flowline->present_day_seed_point(), point(latitude, 0));
		expect_point(flowline->left_flowline_points()->get_vertex(0), point(latitude, 19.6));
		expect_point(flowline->right_flowline_points()->get_vertex(0), point(latitude, 19.6));
		expect_point(flowline->left_flowline_points()->get_vertex(4), point(latitude, 19.8));
		expect_point(flowline->right_flowline_points()->get_vertex(4), point(latitude, 19.4));
	}
}
