/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_BOUNDARYSECTIONGRAPHTEST_H
#define GPLATES_UNIT_TEST_BOUNDARYSECTIONGRAPHTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class BoundarySectionGraphTest { public: void test_gap_and_successor_plan(); };
	class BoundarySectionGraphTestSuite : public GPlatesTestSuite
	{
	public: BoundarySectionGraphTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
