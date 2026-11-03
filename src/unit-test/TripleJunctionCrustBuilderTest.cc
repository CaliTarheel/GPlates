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
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>
#include <boost/optional.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructUtils.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"

#include "model/PropertyValueFinder.h"

#include "property-values/GmlPolygon.h"

#include "view-operations/TripleJunctionCrustBuilder.h"

namespace
{
	namespace Junction = GPlatesViewOperations::TripleJunctionCrustBuilder;
	typedef GPlatesMaths::PointOnSphere Point;
	typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type Polygon;
	typedef GPlatesModel::FeatureHandle::non_null_ptr_type Feature;
	const double PI = 3.14159265358979323846;

	Point
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	void
	expect_point(
			const Point &a,
			const Point &b)
	{
		EXPECT_NEAR(a.position_vector().x().dval(), b.position_vector().x().dval(), 2e-10);
		EXPECT_NEAR(a.position_vector().y().dval(), b.position_vector().y().dval(), 2e-10);
		EXPECT_NEAR(a.position_vector().z().dval(), b.position_vector().z().dval(), 2e-10);
	}

	Polygon
	stored_polygon(
			const Feature &feature)
	{
		const auto value = GPlatesModel::get_property_value<GPlatesPropertyValues::GmlPolygon>(
				feature->reference(), GPlatesModel::PropertyName::create_gpml("outlineOf"));
		if (!value)
		{
			throw std::runtime_error("Missing generated crust polygon.");
		}
		return (*value)->get_polygon();
	}

	std::vector<Junction::RidgeArm>
	arms(
			double endpoint_latitude = 88)
	{
		std::vector<Junction::RidgeArm> result;
		const unsigned int left[] = {101, 202, 303}, right[] = {202, 303, 101};
		const double longitude[] = {60, 180, -60};
		for (unsigned int arm = 0; arm < 3; ++arm)
		{
			result.emplace_back(GPlatesMaths::PolylineOnSphere::create(
					std::vector<Point>{point(80, longitude[arm]), point(endpoint_latitude, longitude[arm])}),
					left[arm], right[arm], QString("Arm %1").arg(arm + 1));
		}
		return result;
	}

	class TripleJunctionCrustBuilderTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_directory.isValid());
			load_model({3, 3, 3});
		}

		void
		load_model(
				const std::vector<double> &angles,
				bool change_poles = false)
		{
			const QString path = d_directory.filePath(QString("rotations-%1.rot").arg(d_models.size()));
			QFile file(path);
			ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
			QString rows;
			for (unsigned int index = 0; index < 3; ++index)
			{
				const unsigned int plate = (index + 1) * 101;
				const double longitude = index * 120 + 90;
				rows += QString("%1 0 0 %2 %3 0 ! final\n%1 950 0 %2 %3 0 ! final\n")
						.arg(plate).arg(longitude).arg(angles[index], 0, 'g', 16);
				if (change_poles)
				{
					rows += QString("%1 975 0.1 %2 %3 0 ! changing pole\n")
							.arg(plate).arg(longitude).arg(angles[index] / 2, 0, 'g', 16);
				}
				rows += QString("%1 1000 0 %2 0 0 ! start\n").arg(plate).arg(longitude);
			}
			const auto data = rows.toUtf8();
			ASSERT_EQ(file.write(data), data.size());
			file.close();
			const auto model = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_registry.read_feature_collection(model->get_reference(), errors);
			ASSERT_TRUE(errors.is_empty());
			d_models.push_back(model);
			d_creator = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
					{model->get_reference().get_feature_collection()});
		}

		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>
		reconstruct(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				double age)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			GPlatesAppLogic::ReconstructUtils::reconstruct(geometries, age,
					GPlatesAppLogic::ReconstructMethodRegistry(), {collection}, d_creator.get());
			return geometries;
		}

		QTemporaryDir d_directory;
		GPlatesFileIO::FeatureCollectionFileFormat::Registry d_registry;
		std::vector<GPlatesFileIO::File::non_null_ptr_type> d_models;
		boost::optional<GPlatesAppLogic::ReconstructionTreeCreator> d_creator;
	};
}


TEST_F(TripleJunctionCrustBuilderTest, SymmetricSpreadingHasKnownCentreCountsAndSharedSeams)
{
	const auto inputs = arms();
	const auto result = Junction::create_crust(inputs, d_creator.get(), 1000, 50, 10);
	ASSERT_EQ(result.d_features.size(), 150u);
	ASSERT_EQ(result.d_centres.size(), 6u);
	EXPECT_EQ(result.d_junction_patch_count, 30u);
	EXPECT_EQ(result.d_step_patch_counts, (std::vector<std::size_t>{18, 24, 30, 36, 42}));
	for (const auto &centre : result.d_centres)
	{
		expect_point(centre, point(90, 0));
	}
	for (unsigned int arm = 0; arm < 3; ++arm)
	{
		EXPECT_EQ(result.d_junction_ends[arm], 1);
		EXPECT_NEAR(result.d_initial_extensions_radians[arm], 2 * PI / 180, 1e-12);
		expect_point(result.d_final_arms[arm]->get_vertex(
				result.d_final_arms[arm]->number_of_vertices() - 1), result.d_centres.back());
		EXPECT_EQ(inputs[arm].d_geometry->number_of_vertices(), 2u);
	}
	std::map<unsigned int, std::size_t> counts;
	for (const auto &feature : result.d_features)
	{
		EXPECT_FALSE(feature->parent_ptr());
		GPlatesAppLogic::ReconstructionFeatureProperties properties;
		properties.visit_feature(feature->reference());
		ASSERT_TRUE(properties.get_recon_plate_id());
		ASSERT_TRUE(properties.get_time_of_appearance());
		ASSERT_TRUE(properties.get_time_of_disappearance());
		EXPECT_GE(properties.get_time_of_appearance()->value(), 950);
		EXPECT_LE(properties.get_time_of_appearance()->value(), 990);
		EXPECT_EQ(properties.get_time_of_disappearance()->value(), 0);
		EXPECT_GT(stored_polygon(feature)->get_area().dval(), 1e-14);
		++counts[*properties.get_recon_plate_id()];
	}
	EXPECT_EQ(counts[101], 50u);
	EXPECT_EQ(counts[202], 50u);
	EXPECT_EQ(counts[303], 50u);
}


TEST_F(TripleJunctionCrustBuilderTest, AsymmetricCentreIsEquidistantFromPlateCarriedCorners)
{
	load_model({3, 3.5, 2.8});
	const auto result = Junction::create_crust(arms(), d_creator.get(), 1000, 10, 10);
	ASSERT_EQ(result.d_centres.size(), 2u);
	const auto tree = d_creator->get_reconstruction_tree(990);
	std::vector<double> cosines;
	for (unsigned int plate : {101, 202, 303})
	{
		const auto carried = tree->get_composed_absolute_rotation(plate) * point(90, 0);
		cosines.push_back(GPlatesMaths::dot(carried.position_vector(),
				result.d_centres.back().position_vector()).dval());
	}
	EXPECT_NEAR(cosines[0], cosines[1], 1e-14);
	EXPECT_NEAR(cosines[0], cosines[2], 1e-14);
	EXPECT_GT(std::fabs(result.d_centres.back().position_vector().x().dval()) +
			std::fabs(result.d_centres.back().position_vector().y().dval()), 1e-4);
	EXPECT_EQ(result.d_features.size(), 18u);
}


TEST_F(TripleJunctionCrustBuilderTest, ReversedRidgesAndNonzeroAnchorPreserveStoredCrust)
{
	const auto original = arms();
	const auto base = Junction::create_crust(original, d_creator.get(), 1000, 20, 10);
	auto reversed = original;
	for (auto &arm : reversed)
	{
		std::vector<Point> vertices(arm.d_geometry->vertex_begin(), arm.d_geometry->vertex_end());
		std::reverse(vertices.begin(), vertices.end());
		arm.d_geometry = GPlatesMaths::PolylineOnSphere::create(vertices);
	}
	const auto reversed_result = Junction::create_crust(reversed, d_creator.get(), 1000, 20, 10);
	const auto anchored_creator = GPlatesAppLogic::create_cached_reconstruction_tree_adaptor(
			d_creator.get(), 202);
	const auto anchored = Junction::create_crust(original, anchored_creator, 1000, 20, 10);
	ASSERT_EQ(base.d_features.size(), anchored.d_features.size());
	ASSERT_EQ(base.d_features.size(), reversed_result.d_features.size());
	for (std::size_t index = 0; index < base.d_features.size(); ++index)
	{
		const auto expected = stored_polygon(base.d_features[index]);
		for (unsigned int vertex = 0; vertex < expected->number_of_vertices_in_exterior_ring(); ++vertex)
		{
			expect_point(expected->get_exterior_ring_vertex(vertex),
					stored_polygon(anchored.d_features[index])->get_exterior_ring_vertex(vertex));
			expect_point(expected->get_exterior_ring_vertex(vertex),
					stored_polygon(reversed_result.d_features[index])->get_exterior_ring_vertex(vertex));
		}
	}
}


TEST_F(TripleJunctionCrustBuilderTest, SavedCrustAppearsAtCompletionAndReconstructsCentralCover)
{
	const auto result = Junction::create_crust(arms(), d_creator.get(), 1000, 20, 10);
	const auto path = d_directory.filePath("junction.gpml");
	const auto saved = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	for (const auto &feature : result.d_features)
	{
		saved->get_reference().get_feature_collection()->add(feature);
	}
	d_registry.write_feature_collection(saved->get_reference());
	const auto loaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	GPlatesFileIO::ReadErrorAccumulation errors;
	d_registry.read_feature_collection(loaded->get_reference(), errors);
	ASSERT_TRUE(errors.is_empty());
	const auto collection = loaded->get_reference().get_feature_collection();
	EXPECT_TRUE(reconstruct(collection, 991).empty());
	EXPECT_EQ(reconstruct(collection, 990).size(), 18u);
	const auto completed = reconstruct(collection, 980);
	ASSERT_EQ(completed.size(), 42u);
	EXPECT_EQ(reconstruct(collection, 0).size(), 42u);
	// A point inside each of the six central sectors must be covered exactly once.
	for (double longitude : {30.0, 90.0, 150.0, -150.0, -90.0, -30.0})
	{
		std::size_t containing = 0;
		for (const auto &geometry : completed)
		{
			const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
					geometry->reconstructed_geometry().get());
			ASSERT_NE(polygon, nullptr);
			containing += polygon->is_point_in_polygon(point(89.95, longitude),
					GPlatesMaths::PolygonOnSphere::ADAPTIVE, false) ? 1 : 0;
		}
		EXPECT_EQ(containing, 1u) << longitude;
	}
}


TEST_F(TripleJunctionCrustBuilderTest, ChangingPolesAndShortFinalStepRetainSharedJunction)
{
	load_model({3, 3.2, 2.9}, true);
	const auto result = Junction::create_crust(arms(), d_creator.get(), 1000, 27, 10);
	EXPECT_EQ(result.d_ages, (std::vector<double>{1000, 990, 980, 973}));
	ASSERT_EQ(result.d_features.size(), 72u);
	for (const auto &arm : result.d_final_arms)
	{
		expect_point(arm->get_vertex(arm->number_of_vertices() - 1), result.d_centres.back());
	}
}


TEST_F(TripleJunctionCrustBuilderTest, RejectsWrongPlateCircuitMissingRotationsAndInvalidTimes)
{
	auto inputs = arms();
	inputs[2].d_left = 999;
	EXPECT_THROW(Junction::create_crust(inputs, d_creator.get(), 1000, 50, 10), std::invalid_argument);
	inputs = arms();
	inputs[1].d_right = 999;
	inputs[2].d_left = 999;
	EXPECT_THROW(Junction::create_crust(inputs, d_creator.get(), 1000, 50, 10), std::invalid_argument);
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 1001, 10), std::invalid_argument);
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 50, 0), std::invalid_argument);
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 501, 1), std::invalid_argument);
}


TEST_F(TripleJunctionCrustBuilderTest, RejectsNonconcurrentArmsAndWrongApproachingEndpoints)
{
	auto inputs = arms();
	inputs[1].d_geometry = GPlatesMaths::PolylineOnSphere::create(
			std::vector<Point>{point(80, 180), point(88, 175)});
	EXPECT_THROW(Junction::create_crust(inputs, d_creator.get(), 1000, 10, 10), std::invalid_argument);
	inputs = arms();
	inputs[0].d_junction_end = 0;
	EXPECT_THROW(Junction::create_crust(inputs, d_creator.get(), 1000, 10, 10), std::invalid_argument);
}


TEST_F(TripleJunctionCrustBuilderTest, RejectsStationaryAndRetreatingJunctionsWithoutOutput)
{
	load_model({0, 0, 0});
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 10, 10), std::invalid_argument);
	// Two large motions and one tiny motion give a 119.667-degree corner. Its
	// circumcentre lies outside the carried-corner triangle, requiring ridge retreat.
	load_model({3, 0.01, 3});
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 10, 10), std::invalid_argument);
	load_model({-3, -3, -3});
	EXPECT_THROW(Junction::create_crust(arms(), d_creator.get(), 1000, 10, 10), std::invalid_argument);
}


TEST_F(TripleJunctionCrustBuilderTest, AllowsHighlyAsymmetricMotionWithAnInteriorCentre)
{
	// In contrast, one large motion and two tiny motions form a narrow acute triangle
	// (89.835, 89.835, 0.330 degrees). Rejecting by rate ratio would discard valid crust.
	load_model({0.01, 0.01, 3});
	const auto result = Junction::create_crust(arms(), d_creator.get(), 1000, 10, 10);
	ASSERT_EQ(result.d_features.size(), 18u);
	ASSERT_EQ(result.d_centres.size(), 2u);
	EXPECT_NEAR(GPlatesMaths::make_lat_lon_point(result.d_centres.back()).latitude(),
			89.7005024958, 1e-8);
	for (const auto &arm : result.d_final_arms)
	{
		expect_point(arm->get_vertex(arm->number_of_vertices() - 1), result.d_centres.back());
	}
}
