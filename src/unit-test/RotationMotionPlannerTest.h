/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_ROTATIONMOTIONPLANNERTEST_H
#define GPLATES_UNIT_TEST_ROTATIONMOTIONPLANNERTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class RotationMotionPlannerTest { public: void test_stage_angle(); };
	class RotationMotionPlannerTestSuite : public GPlatesTestSuite
	{
	public: RotationMotionPlannerTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
