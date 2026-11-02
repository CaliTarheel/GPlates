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
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "app-logic/FlowlineGeometryPopulator.h"
#include "app-logic/ReconstructedFlowline.h"
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
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/PropertyValueFinder.h"

#include "property-values/GmlPolygon.h"
#include "property-values/XsString.h"

#include "view-operations/MakeOceanCrustOperation.h"
#include "view-operations/OceanCrustSliverBuilder.h"


namespace
{
	using GPlatesViewOperations::OceanCrustSliverBuilder::create_slivers;
	typedef GPlatesModel::FeatureHandle::non_null_ptr_type Feature;
	typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type Polygon;
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
		EXPECT_NEAR(actual.position_vector().x().dval(), expected.position_vector().x().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().y().dval(), expected.position_vector().y().dval(), 1e-10);
		EXPECT_NEAR(actual.position_vector().z().dval(), expected.position_vector().z().dval(), 1e-10);
	}


	Polygon
	stored_polygon(
			const Feature &feature)
	{
		const auto value = GPlatesModel::get_property_value<GPlatesPropertyValues::GmlPolygon>(
				feature->reference(), GPlatesModel::PropertyName::create_gpml("outlineOf"));
		EXPECT_TRUE(value);
		if (!value)
		{
			throw std::runtime_error("Missing OceanicCrust outlineOf.");
		}
		return (*value)->get_polygon();
	}


	class OceanCrustSliverBuilderTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_directory.isValid());
			const QString path = d_directory.filePath("spreading.rot");
			QFile file(path);
			ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
			const QByteArray rotations(
					"201 0 90 0 0 0 ! moving left parent\n"
					"201 1000 90 0 20 0 ! moving left parent\n"
					"501 0 90 0 0 201 ! stationary relative to left\n"
					"501 1000 90 0 0 201 ! stationary relative to left\n"
					"601 0 90 0 0 201 ! numerically unresolved spreading\n"
					"601 1000 90 0 0.000000000001 201 ! unresolved spreading\n"
					"701 0 90 0 0 201 ! changing stage pole\n"
					"701 990 90 0 39.6 201 ! changing stage pole\n"
					"701 995 89.99 0 39.8 201 ! changing stage pole\n"
					"701 1000 90 0 40 201 ! changing stage pole\n"
					"801 0 90 0 0 201 ! right\n"
					"801 1000 90 0 40 201 ! right\n"
					"901 0 90 0 0 201 ! reversing relative motion\n"
					"901 990 90 0 39.6 201 ! reversing relative motion\n"
					"901 995 90 0 39.4 201 ! reversing relative motion\n"
					"901 1000 90 0 40 201 ! reversing relative motion\n"
					"1001 0 90 0 0 201 ! repeated spreading wraps around sphere\n"
					"1001 250 90 0 170 201 ! repeated spreading wraps around sphere\n"
					"1001 500 90 0 340 201 ! repeated spreading wraps around sphere\n"
					"1001 750 90 0 510 201 ! repeated spreading wraps around sphere\n"
					"1001 1000 90 0 680 201 ! repeated spreading wraps around sphere\n");
			ASSERT_EQ(file.write(rotations), rotations.size());
			file.close();
			d_rotations = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_registry.read_feature_collection(d_rotations.get()->get_reference(), errors);
			ASSERT_TRUE(errors.is_empty());
			d_creator = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
					{d_rotations.get()->get_reference().get_feature_collection()});
		}


		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>
		reconstruct(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				double time)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			GPlatesAppLogic::ReconstructUtils::reconstruct(
					geometries, time, GPlatesAppLogic::ReconstructMethodRegistry(), {collection},
					d_creator.get());
			return geometries;
		}

		QTemporaryDir d_directory;
		GPlatesFileIO::FeatureCollectionFileFormat::Registry d_registry;
		boost::optional<GPlatesFileIO::File::non_null_ptr_type> d_rotations;
		boost::optional<GPlatesAppLogic::ReconstructionTreeCreator> d_creator;
	};
}


TEST_F(OceanCrustSliverBuilderTest, FixedPoleProducesKnownPairedPolygonsAndCompletionAges)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-10, 20), point(0, 20), point(10, 20)});
	const auto features = create_slivers(*ridge, 201, 801, d_creator.get(), 1000, 10, 5, "MOR");
	ASSERT_EQ(features.size(), 8u);
	for (std::size_t segment = 0; segment < 2; ++segment)
	{
		for (std::size_t interval = 0; interval < 2; ++interval)
		{
			for (unsigned int side = 0; side < 2; ++side)
			{
				const auto &feature = features[4 * segment + 2 * interval + side];
				SCOPED_TRACE(4 * segment + 2 * interval + side);
				EXPECT_FALSE(feature->parent_ptr());
				EXPECT_EQ(feature->feature_type(), GPlatesModel::FeatureType::create_gpml("OceanicCrust"));
				GPlatesAppLogic::ReconstructionFeatureProperties properties;
				properties.visit_feature(feature->reference());
				ASSERT_TRUE(properties.get_recon_plate_id());
				EXPECT_EQ(*properties.get_recon_plate_id(), side == 0 ? 201u : 801u);
				ASSERT_TRUE(properties.get_time_of_appearance());
				ASSERT_TRUE(properties.get_time_of_disappearance());
				EXPECT_DOUBLE_EQ(properties.get_time_of_appearance()->value(), 990 + 5 * interval);
				EXPECT_DOUBLE_EQ(properties.get_time_of_disappearance()->value(), 0);
				EXPECT_FALSE(properties.get_left_plate_id());
				EXPECT_FALSE(properties.get_reconstruction_method());
				const auto method = GPlatesAppLogic::ReconstructMethodRegistry().
						get_reconstruct_method_type(feature->reference());
				ASSERT_TRUE(method);
				EXPECT_EQ(*method, GPlatesAppLogic::ReconstructMethod::BY_PLATE_ID);
				const auto polygon = stored_polygon(feature);
				ASSERT_EQ(polygon->number_of_vertices_in_exterior_ring(), 4u);
				// Independent north-pole arithmetic: at 990 Ma L=19.8, R=59.4 and
				// ridge=19.6 degrees. Each 5 Myr strip is 0.1 degree wide on each side.
				const double longitude = (side == 0 ? -0.2 : -39.8) +
						(side == 0 ? 0.1 : -0.1) * interval;
				const double older_longitude = longitude + (side == 0 ? 0.1 : -0.1);
				const double latitude = -10 + 10 * static_cast<double>(segment);
				expect_point(polygon->get_exterior_ring_vertex(0), point(latitude, longitude));
				expect_point(polygon->get_exterior_ring_vertex(1), point(latitude + 10, longitude));
				expect_point(polygon->get_exterior_ring_vertex(2), point(latitude + 10, older_longitude));
				expect_point(polygon->get_exterior_ring_vertex(3), point(latitude, older_longitude));
				EXPECT_GT(polygon->get_area().dval(), 0);
			}
		}
	}
	// Adjacent time cells and ridge segments share exactly the same material coordinates.
	expect_point(stored_polygon(features[0])->get_exterior_ring_vertex(2),
			stored_polygon(features[2])->get_exterior_ring_vertex(1));
	expect_point(stored_polygon(features[0])->get_exterior_ring_vertex(1),
			stored_polygon(features[4])->get_exterior_ring_vertex(0));
}


TEST_F(OceanCrustSliverBuilderTest, StoredGeometryIsIndependentOfAnchorAndRidgeDirection)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	const auto base = create_slivers(*ridge, 201, 801, d_creator.get(), 1000, 10, 5, "Base");
	const auto anchored_creator = GPlatesAppLogic::create_cached_reconstruction_tree_adaptor(
			d_creator.get(), 201);
	const auto anchored_ridge = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-5, 0), point(5, 0)});
	const auto anchored = create_slivers(
			*anchored_ridge, 201, 801, anchored_creator, 1000, 10, 5, "Anchor 201");
	const auto reversed_ridge = GPlatesMaths::PolylineOnSphere::create(
			Points{point(5, 20), point(-5, 20)});
	const auto reversed = create_slivers(
			*reversed_ridge, 201, 801, d_creator.get(), 1000, 10, 5, "Reversed");
	ASSERT_EQ(base.size(), anchored.size());
	ASSERT_EQ(base.size(), reversed.size());
	const unsigned int reversed_indices[] = {1, 0, 3, 2};
	for (std::size_t index = 0; index < base.size(); ++index)
	{
		for (unsigned int vertex = 0; vertex < 4; ++vertex)
		{
			expect_point(stored_polygon(base[index])->get_exterior_ring_vertex(vertex),
					stored_polygon(anchored[index])->get_exterior_ring_vertex(vertex));
			expect_point(stored_polygon(base[index])->get_exterior_ring_vertex(vertex),
					stored_polygon(reversed[index])->get_exterior_ring_vertex(reversed_indices[vertex]));
		}
	}
}


TEST_F(OceanCrustSliverBuilderTest, PoleEndpointBecomesValidTriangle)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(80, 20), point(90, 20)});
	const auto features = create_slivers(
			*ridge, 201, 801, d_creator.get(), 1000, 10, 5, "Polar ridge");
	ASSERT_EQ(features.size(), 4u);
	for (const auto &feature : features)
	{
		const auto polygon = stored_polygon(feature);
		EXPECT_EQ(polygon->number_of_vertices_in_exterior_ring(), 3u);
		EXPECT_GT(polygon->get_area().dval(), 1e-8);
		expect_point(polygon->get_exterior_ring_vertex(1), point(90, 0));
	}
}


TEST_F(OceanCrustSliverBuilderTest, ChangingStagePolesMatchNativeLatticeAtFinalSample)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	const auto features = create_slivers(*ridge, 201, 701, d_creator.get(), 1000, 10, 5, "Changing");
	const auto flowline = GPlatesViewOperations::MakeOceanCrustOperation::create_flowline(
			*ridge, 201, 701, d_creator.get(), 1000, 10, 5, "Native");
	std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> native;
	GPlatesAppLogic::FlowlineGeometryPopulator populator(native, d_creator.get(), 990);
	populator.visit_feature(flowline->reference());
	ASSERT_EQ(native.size(), 2u);
	ASSERT_EQ(features.size(), 4u);
	for (unsigned int side = 0; side < 2; ++side)
	{
		const auto correction = d_creator->get_reconstruction_tree(990)->
				get_composed_absolute_rotation(side == 0 ? 201 : 701);
		for (unsigned int interval = 0; interval < 2; ++interval)
		{
			const auto polygon = correction * stored_polygon(features[2 * interval + side]);
			for (unsigned int seed = 0; seed < 2; ++seed)
			{
				const auto *line = dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(
						native[seed].get());
				ASSERT_NE(line, nullptr);
				const auto points = side == 0 ? line->left_flowline_points() : line->right_flowline_points();
				expect_point(polygon->get_exterior_ring_vertex(seed), points->get_vertex(interval));
				expect_point(polygon->get_exterior_ring_vertex(3 - seed), points->get_vertex(interval + 1));
			}
		}
	}
}


TEST_F(OceanCrustSliverBuilderTest, SavedCrustReconstructsWithCompletionAgesAndProvenance)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	const auto features = create_slivers(*ridge, 201, 801, d_creator.get(), 1000, 10, 5, "Saved MOR");
	const QString path = d_directory.filePath("crust.gpml");
	const auto saved = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	for (const auto &feature : features)
	{
		saved->get_reference().get_feature_collection()->add(feature);
	}
	d_registry.write_feature_collection(saved->get_reference());
	const auto loaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	GPlatesFileIO::ReadErrorAccumulation errors;
	d_registry.read_feature_collection(loaded->get_reference(), errors);
	ASSERT_TRUE(errors.is_empty());
	const auto collection = loaded->get_reference().get_feature_collection();
	ASSERT_EQ(collection->size(), 4u);
	EXPECT_TRUE(reconstruct(collection, 996).empty());
	const auto at_completion = reconstruct(collection, 995);
	ASSERT_EQ(at_completion.size(), 2u);
	const auto *completed = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
			at_completion.front()->reconstructed_geometry().get());
	ASSERT_NE(completed, nullptr);
	expect_point(completed->get_exterior_ring_vertex(0), point(-5, 19.8));
	expect_point(completed->get_exterior_ring_vertex(3), point(-5, 19.9));
	const auto at_end = reconstruct(collection, 990);
	ASSERT_EQ(at_end.size(), 4u);
	EXPECT_EQ(reconstruct(collection, 0).size(), 4u);
	unsigned int index = 0;
	for (auto feature = collection->begin(); feature != collection->end(); ++feature, ++index)
	{
		const auto description = GPlatesModel::get_property_value<GPlatesPropertyValues::XsString>(
				(*feature)->reference(), GPlatesModel::PropertyName::create_gml("description"));
		ASSERT_TRUE(description);
		const auto text = (*description)->get_value().get().qstring();
		EXPECT_TRUE(text.contains("Saved MOR"));
		EXPECT_TRUE(text.contains(index < 2 ? "995 to 990 Ma" : "1000 to 995 Ma"));
		const auto stored = stored_polygon(*feature);
		for (unsigned int vertex = 0; vertex < 4; ++vertex)
		{
			expect_point(stored->get_exterior_ring_vertex(vertex),
					stored_polygon(features[index])->get_exterior_ring_vertex(vertex));
		}
	}
	const auto *first = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
			at_end.front()->reconstructed_geometry().get());
	ASSERT_NE(first, nullptr);
	expect_point(first->get_exterior_ring_vertex(0), point(-5, 19.6));
	expect_point(first->get_exterior_ring_vertex(3), point(-5, 19.7));
}


TEST_F(OceanCrustSliverBuilderTest, DatelineAndShortFinalStepRemainValid)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-5, -179.8), point(5, -179.8)});
	const auto features = create_slivers(*ridge, 201, 801, d_creator.get(), 1000, 10, 3, "Dateline");
	ASSERT_EQ(features.size(), 8u);
	GPlatesAppLogic::ReconstructionFeatureProperties properties;
	properties.visit_feature(features.front()->reference());
	ASSERT_TRUE(properties.get_time_of_appearance());
	EXPECT_DOUBLE_EQ(properties.get_time_of_appearance()->value(), 990);
	const auto rotation = d_creator->get_reconstruction_tree(990)->get_composed_absolute_rotation(201);
	const auto polygon = rotation * stored_polygon(features.front());
	expect_point(polygon->get_exterior_ring_vertex(0), point(-5, 179.8));
	expect_point(polygon->get_exterior_ring_vertex(3), point(-5, 179.82));
}


TEST_F(OceanCrustSliverBuilderTest, RejectsZeroAreaTransformMotionAndReversingSpreading)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	EXPECT_THROW(create_slivers(*ridge, 201, 501, d_creator.get(), 1000, 10, 5, "No spreading"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 601, d_creator.get(), 1000, 10, 5, "Unresolved"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 901, d_creator.get(), 1000, 10, 5, "Reversing"),
			std::invalid_argument);
	const auto transform = GPlatesMaths::PolylineOnSphere::create(Points{point(0, 20), point(0, 30)});
	EXPECT_THROW(create_slivers(*transform, 201, 801, d_creator.get(), 1000, 10, 5, "Transform"),
			std::invalid_argument);
	const auto folded = GPlatesMaths::PolylineOnSphere::create(
			Points{point(-5, 20), point(5, 20), point(-5, 20)});
	EXPECT_THROW(create_slivers(*folded, 201, 801, d_creator.get(), 1000, 10, 5, "Folded"),
			std::invalid_argument);
}


TEST_F(OceanCrustSliverBuilderTest, RejectsInvalidTimesMissingRotationsAndExcessiveOutput)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	EXPECT_THROW(create_slivers(*ridge, 201, 201, d_creator.get(), 1000, 10, 5, "Same"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 999, d_creator.get(), 1000, 10, 5, "Missing"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 801, d_creator.get(), 10, 11, 5, "Past present"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 801, d_creator.get(), 1000, 10, 0, "Zero step"),
			std::invalid_argument);
	EXPECT_THROW(create_slivers(*ridge, 201, 801, d_creator.get(),
			1000, std::numeric_limits<double>::quiet_NaN(), 1, "NaN"), std::invalid_argument);
	Points many;
	for (unsigned int index = 0; index < 52; ++index)
	{
		many.push_back(point(-25 + static_cast<double>(index), 20));
	}
	const auto large = GPlatesMaths::PolylineOnSphere::create(many);
	EXPECT_THROW(create_slivers(*large, 201, 801, d_creator.get(), 1000, 1000, 1, "Over limit"),
			std::invalid_argument);
}


TEST_F(OceanCrustSliverBuilderTest, AllowsPresentCompletionButRejectsWrappedOverlappingCrust)
{
	const auto ridge = GPlatesMaths::PolylineOnSphere::create(Points{point(-5, 20), point(5, 20)});
	const auto features = create_slivers(*ridge, 201, 801, d_creator.get(), 10, 10, 5, "To present");
	ASSERT_EQ(features.size(), 4u);
	GPlatesAppLogic::ReconstructionFeatureProperties properties;
	properties.visit_feature(features.front()->reference());
	ASSERT_TRUE(properties.get_time_of_appearance());
	EXPECT_DOUBLE_EQ(properties.get_time_of_appearance()->value(), 0);
	EXPECT_TRUE(properties.is_feature_defined_at_recon_time(0));
	EXPECT_FALSE(properties.is_feature_defined_at_recon_time(0.1));

	// Every individual 85-degree half-stage cell is valid and its winding is consistent,
	// but four such steps on BOTH sides cover overlapping parts of the sphere. This must
	// reach the lattice overlap guard, rather than fail only a local area or fold test.
	try
	{
		create_slivers(*ridge, 201, 1001, d_creator.get(), 1000, 1000, 250, "Wrapped");
		FAIL() << "Expected overlapping crust to be rejected.";
	}
	catch (const std::invalid_argument &error)
	{
		EXPECT_NE(std::string(error.what()).find("overlap"), std::string::npos) << error.what();
	}
}
