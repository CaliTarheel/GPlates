/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/RotationMotionPlannerTest.h"
#include <cmath>
#include "maths/FiniteRotation.h"
#include "maths/UnitQuaternion3D.h"
#include "maths/UnitVector3D.h"
#include "view-operations/RotationMotionPlanner.h"

GPlatesUnitTest::RotationMotionPlannerTestSuite::RotationMotionPlannerTestSuite(unsigned depth) :
	GPlatesTestSuite("RotationMotionPlannerTestSuite") { init(depth); }
void GPlatesUnitTest::RotationMotionPlannerTestSuite::construct_maps()
{
	boost::shared_ptr<RotationMotionPlannerTest> instance(new RotationMotionPlannerTest());
	ADD_TESTCASE(RotationMotionPlannerTest, test_stage_angle);
	ADD_TESTCASE(RotationMotionPlannerTest, test_boundary_decomposition);
}

void GPlatesUnitTest::RotationMotionPlannerTest::test_boundary_decomposition()
{
	double normal = 0.0;
	double parallel = 0.0;
	GPlatesViewOperations::RotationMotionPlanner::decompose_boundary_velocity(
			4.0, 3.0, 0.0, normal, parallel);
	BOOST_CHECK_CLOSE(normal, 3.0, 1e-8);
	BOOST_CHECK_CLOSE(parallel, 4.0, 1e-8);
	GPlatesViewOperations::RotationMotionPlanner::decompose_boundary_velocity(
			4.0, 3.0, 90.0, normal, parallel);
	BOOST_CHECK_CLOSE(normal, -4.0, 1e-8);
	BOOST_CHECK_CLOSE(parallel, 3.0, 1e-8);
}
void GPlatesUnitTest::RotationMotionPlannerTest::test_stage_angle()
{
	const GPlatesMaths::FiniteRotation rotation = GPlatesMaths::FiniteRotation::create(
			GPlatesMaths::UnitQuaternion3D::create_rotation(
					GPlatesMaths::UnitVector3D::zBasis(), 3.14159265358979323846 / 6.0),
			GPlatesMaths::UnitVector3D::zBasis());
	BOOST_CHECK_CLOSE(
			GPlatesViewOperations::RotationMotionPlanner::angle_degrees(rotation), 30.0, 1e-8);
	BOOST_CHECK_SMALL(GPlatesViewOperations::RotationMotionPlanner::angle_degrees(
			GPlatesMaths::FiniteRotation::create_identity_rotation()), 1e-12);
}
