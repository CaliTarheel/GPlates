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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "maths/LatLonPoint.h"
#include "maths/MathsUtils.h"
#include "maths/MultiPointOnSphere.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureVisitor.h"

#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlIrregularSampling.h"

#include "view-operations/ReconstructionPoleSpeed.h"


namespace
{
	namespace Speed = GPlatesViewOperations::ReconstructionPoleSpeed;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;
	typedef std::vector<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> Geometries;
	const double RADIUS_KM = 6000.0;

	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::FiniteRotation
	total_rotation(
			double degrees)
	{
		return GPlatesMaths::FiniteRotation::create(
				point(90, 0), GPlatesMaths::convert_deg_to_rad(degrees));
	}

	GPlatesMaths::Rotation
	adjustment(
			double degrees)
	{
		return GPlatesMaths::Rotation::create(
				point(90, 0).position_vector(), GPlatesMaths::convert_deg_to_rad(degrees));
	}

	double
	equatorial_speed(
			double degrees,
			double interval)
	{
		return std::fabs(degrees) * GPlatesMaths::PI / 180.0 / interval * RADIUS_KM * 0.1;
	}

	Speed::Reference
	reference()
	{
		return Speed::Reference{10, 0, 0, total_rotation(10), total_rotation(0)};
	}

	class DisableSample :
			public GPlatesModel::FeatureVisitor
	{
	public:
		explicit
		DisableSample(
				double time) :
			d_time(time)
		{  }

	private:
		void
		visit_gpml_constant_value(
				GPlatesPropertyValues::GpmlConstantValue &value) override
		{
			value.value()->accept_visitor(*this);
		}

		void
		visit_gpml_irregular_sampling(
				GPlatesPropertyValues::GpmlIrregularSampling &sampling) override
		{
			for (GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type sample : sampling.time_samples())
			{
				if (sample->valid_time()->get_time_position().value() == d_time)
				{
					sample->set_disabled(true);
				}
			}
		}

		double d_time;
	};

	class ReconstructionPoleSpeedTest :
			public ::testing::Test
	{
	protected:
		GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type
		load_tree(
				const QByteArray &rotations,
				double time,
				GPlatesModel::integer_plate_id_type anchor = 0,
				boost::optional<double> disabled_time = boost::none)
		{
			const QString path = d_directory.filePath("speed.rot");
			QFile output(path);
			if (!d_directory.isValid() || !output.open(QIODevice::WriteOnly | QIODevice::Text) ||
					output.write(rotations) != rotations.size())
			{
				throw std::runtime_error("Unable to create temporary rotation fixture.");
			}
			output.close();
			const auto file = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_registry.read_feature_collection(file->get_reference(), errors);
			EXPECT_TRUE(errors.is_empty());
			const auto collection = file->get_reference().get_feature_collection();
			if (disabled_time)
			{
				DisableSample disable(*disabled_time);
				for (auto feature = collection->begin(); feature != collection->end(); ++feature)
				{
					disable.visit_feature((*feature)->reference());
				}
			}
			return GPlatesAppLogic::create_cached_reconstruction_tree_creator(
					std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref>(1, collection))
					.get_reconstruction_tree(time, anchor);
		}

		QTemporaryDir d_directory;
		GPlatesFileIO::FeatureCollectionFileFormat::Registry d_registry;
	};
}


TEST_F(ReconstructionPoleSpeedTest, ChoosesStrictlyOlderEnabledKeyframeFromLoadedModel)
{
	const QByteArray poles(
			"201 0 90 0 0 0 ! present\n"
			"201 990 90 0 9 0 ! current keyframe\n"
			"201 995 90 0 9.5 0 ! disabled in loaded model\n"
			"201 1000 90 0 10 0 ! previous keyframe\n");
	const auto tree = load_tree(poles, 990, 0, 995);
	const auto result = Speed::prepare_reference(*tree, 201);
	ASSERT_TRUE(result);
	EXPECT_DOUBLE_EQ(result->d_previous_time, 1000);
	EXPECT_DOUBLE_EQ(result->d_current_time, 990);
	EXPECT_EQ(result->d_anchor_plate_id, 0u);
	const Geometries geometries{point(0, 9).get_geometry_on_sphere()};
	const auto baseline = Speed::calculate_maximum_speed(
			*result, geometries, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(baseline);
	EXPECT_NEAR(*baseline, equatorial_speed(1, 10), 1e-9);
	// Existing model motion is -1 degree. Adding +3 makes +2, not +3 degrees.
	const auto preview = Speed::calculate_maximum_speed(*result, geometries, adjustment(3), RADIUS_KM);
	ASSERT_TRUE(preview);
	EXPECT_NEAR(*preview, equatorial_speed(2, 10), 1e-9);
	const auto cancelled = Speed::calculate_maximum_speed(
			*result, geometries, adjustment(1), RADIUS_KM);
	ASSERT_TRUE(cancelled);
	EXPECT_NEAR(*cancelled, 0.0, 1e-9);
	EXPECT_FALSE(Speed::prepare_reference(*load_tree(poles, 1000), 201));
	EXPECT_FALSE(Speed::prepare_reference(*tree, 999));
	EXPECT_FALSE(Speed::prepare_reference(*tree, 0));
}


TEST_F(ReconstructionPoleSpeedTest, IncludesParentMotionAndRespectsNonzeroAnchor)
{
	const QByteArray poles(
			"201 0 90 0 0 0 ! parent\n"
			"201 1000 90 0 20 0 ! parent\n"
			"801 0 90 0 0 201 ! moving relative to parent\n"
			"801 1000 90 0 40 201 ! moving relative to parent\n");
	const auto absolute = Speed::prepare_reference(*load_tree(poles, 990), 801);
	const auto relative = Speed::prepare_reference(*load_tree(poles, 990, 201), 801);
	const auto reversed = Speed::prepare_reference(*load_tree(poles, 990, 801), 201);
	ASSERT_TRUE(absolute);
	ASSERT_TRUE(relative);
	ASSERT_TRUE(reversed);
	EXPECT_EQ(relative->d_anchor_plate_id, 201u);
	EXPECT_EQ(reversed->d_anchor_plate_id, 801u);
	const Geometries equator{point(0, 0).get_geometry_on_sphere()};
	EXPECT_NEAR(*Speed::calculate_maximum_speed(*absolute, equator, adjustment(0), RADIUS_KM),
			equatorial_speed(0.6, 10), 1e-9);
	EXPECT_NEAR(*Speed::calculate_maximum_speed(*relative, equator, adjustment(0), RADIUS_KM),
			equatorial_speed(0.4, 10), 1e-9);
	EXPECT_NEAR(*Speed::calculate_maximum_speed(*reversed, equator, adjustment(0), RADIUS_KM),
			equatorial_speed(0.4, 10), 1e-9);
}


TEST_F(ReconstructionPoleSpeedTest, CrossoverUsesOlderBranchRegardlessOfFileOrder)
{
	const QByteArray parent(
			"201 0 90 0 0 0 ! parent\n"
			"201 1000 90 0 10 0 ! parent\n"
			"201 1100 90 0 11 0 ! parent\n");
	const QByteArray younger(
			"501 0 90 0 0 0 ! young branch\n"
			"501 1000 90 0 20 0 ! crossover\n");
	const QByteArray older(
			"501 1000 90 0 10 201 ! crossover: absolute 20\n"
			"501 1100 90 0 20 201 ! absolute 31\n");
	for (const QByteArray &poles : {parent + younger + older, older + parent + younger,
			younger + older + parent})
	{
		const auto result = Speed::prepare_reference(*load_tree(poles, 1000), 501);
		ASSERT_TRUE(result);
		EXPECT_DOUBLE_EQ(result->d_previous_time, 1100);
		const auto speed = Speed::calculate_maximum_speed(*result,
				Geometries{point(0, 20).get_geometry_on_sphere()}, adjustment(0), RADIUS_KM);
		ASSERT_TRUE(speed);
		EXPECT_NEAR(*speed, equatorial_speed(11, 100), 1e-9);
	}
}


TEST_F(ReconstructionPoleSpeedTest, DoesNotBridgeRotationGapsOrMissingParentAtKeyframe)
{
	const QByteArray gap(
			"201 0 90 0 0 0 ! parent\n"
			"201 1100 90 0 0 0 ! parent\n"
			"501 0 90 0 0 0 ! younger\n"
			"501 1000 90 0 20 0 ! younger end\n"
			"501 1020 90 0 20 201 ! gap before older branch\n"
			"501 1100 90 0 30 201 ! older\n");
	EXPECT_FALSE(Speed::prepare_reference(*load_tree(gap, 1000), 501));
	const QByteArray missing_parent(
			"201 0 90 0 0 0 ! parent\n"
			"201 995 90 0 10 0 ! no parent at 1000\n"
			"501 0 90 0 0 201 ! child\n"
			"501 1000 90 0 20 201 ! older child keyframe\n");
	EXPECT_FALSE(Speed::prepare_reference(*load_tree(missing_parent, 990), 501));
}


TEST(ReconstructionPoleMaximumSpeedTest, PointMultipointAndRadiusGiveLocationSpecificMaximum)
{
	const auto values = reference();
	const Geometries points{GPlatesMaths::MultiPointOnSphere::create(
			Points{point(80, 30), point(60, -170)})};
	const auto speed = Speed::calculate_maximum_speed(values, points, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(speed);
	EXPECT_NEAR(*speed, equatorial_speed(10, 10) * 0.5, 1e-9);
	const auto doubled = Speed::calculate_maximum_speed(values, points, adjustment(0), RADIUS_KM * 2);
	ASSERT_TRUE(doubled);
	EXPECT_NEAR(*doubled, *speed * 2, 1e-9);
	const auto pole = Speed::calculate_maximum_speed(values,
			Geometries{point(90, 0).get_geometry_on_sphere()}, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(pole);
	EXPECT_NEAR(*pole, 0.0, 1e-9);
}


TEST(ReconstructionPoleMaximumSpeedTest, FindsMaximumInsideArcRatherThanOnlyAtVertices)
{
	const auto line = GPlatesMaths::PolylineOnSphere::create(Points{point(-30, 25), point(30, 25)});
	const auto speed = Speed::calculate_maximum_speed(reference(), Geometries{line},
			adjustment(0), RADIUS_KM);
	ASSERT_TRUE(speed);
	// Both vertices have only cos(30 degrees) of the maximum. The arc crosses the equator.
	EXPECT_NEAR(*speed, equatorial_speed(10, 10), 1e-9);
}


TEST(ReconstructionPoleMaximumSpeedTest, PolygonWithHoleAndMultipleGeometries)
{
	const Points exterior{point(-20, -20), point(-20, 20), point(20, 20), point(20, -20)};
	const std::vector<Points> holes{
			Points{point(-5, -5), point(-5, 5), point(5, 5), point(5, -5)}};
	const auto polygon = GPlatesMaths::PolygonOnSphere::create(exterior, holes);
	const Geometries geometries{point(80, 0).get_geometry_on_sphere(), polygon};
	const auto speed = Speed::calculate_maximum_speed(
			reference(), geometries, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(speed);
	// The hole excludes the centre, but solid regions and outer edges still reach the equator.
	EXPECT_NEAR(*speed, equatorial_speed(10, 10), 1e-9);
}


TEST(ReconstructionPoleMaximumSpeedTest, NoncommutingPreviewRotatesBothModelAndGeometry)
{
	const Speed::Reference values{10, 0, 0, total_rotation(90), total_rotation(0)};
	const auto rotate_x = GPlatesMaths::Rotation::create(
			point(0, 0).position_vector(), GPlatesMaths::PI * 0.5);
	const auto speed = Speed::calculate_maximum_speed(values,
			Geometries{point(0, 45).get_geometry_on_sphere()}, rotate_x, RADIUS_KM);
	ASSERT_TRUE(speed);
	// Rx(90) Rz(90) is 120 degrees about (1,-1,1)/sqrt(3). The preview point is
	// (1,0,1)/sqrt(2), giving sin(colatitude)=1/sqrt(3). Leaving geometry unrotated
	// would incorrectly report the full equatorial speed.
	EXPECT_NEAR(*speed, equatorial_speed(120, 10) / std::sqrt(3.0), 1e-9);
}


TEST(ReconstructionPoleMaximumSpeedTest, ShortestStageAndIdentityAreWellDefined)
{
	Speed::Reference values{10, 0, 0, total_rotation(-170), total_rotation(170)};
	const Geometries equator{point(0, 0).get_geometry_on_sphere()};
	const auto short_stage = Speed::calculate_maximum_speed(values, equator, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(short_stage);
	EXPECT_NEAR(*short_stage, equatorial_speed(20, 10), 1e-9);
	values.d_current_total = values.d_previous_total;
	const auto stationary = Speed::calculate_maximum_speed(values, equator, adjustment(0), RADIUS_KM);
	ASSERT_TRUE(stationary);
	EXPECT_DOUBLE_EQ(*stationary, 0.0);
	EXPECT_FALSE(Speed::calculate_maximum_speed(values, Geometries(), adjustment(0), RADIUS_KM));
}


TEST(ReconstructionPoleMaximumSpeedTest, RejectsInvalidIntervalsAndRadii)
{
	auto values = reference();
	const Geometries equator{point(0, 0).get_geometry_on_sphere()};
	EXPECT_THROW(Speed::calculate_maximum_speed(values, equator, adjustment(0), 0),
			std::invalid_argument);
	EXPECT_THROW(Speed::calculate_maximum_speed(values, equator, adjustment(0),
			std::numeric_limits<double>::infinity()), std::invalid_argument);
	values.d_previous_time = values.d_current_time;
	EXPECT_THROW(Speed::calculate_maximum_speed(values, equator, adjustment(0), RADIUS_KM),
			std::invalid_argument);
	values.d_previous_time = std::numeric_limits<double>::quiet_NaN();
	EXPECT_THROW(Speed::calculate_maximum_speed(values, equator, adjustment(0), RADIUS_KM),
			std::invalid_argument);
}
