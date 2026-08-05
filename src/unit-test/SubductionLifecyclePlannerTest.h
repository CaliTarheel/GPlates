/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_SUBDUCTIONLIFECYCLEPLANNERTEST_H
#define GPLATES_UNIT_TEST_SUBDUCTIONLIFECYCLEPLANNERTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class SubductionLifecyclePlannerTest { public: void test_reversal_and_retirement(); };
	class SubductionLifecyclePlannerTestSuite : public GPlatesTestSuite
	{
	public: SubductionLifecyclePlannerTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
