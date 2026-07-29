/* $Id$ */

#ifndef GPLATES_UNIT_TEST_NATURALIZECOASTLINETEST_H
#define GPLATES_UNIT_TEST_NATURALIZECOASTLINETEST_H

#include "unit-test/GPlatesTestSuite.h"

namespace GPlatesUnitTest
{
	class NaturalizeCoastlineTest
	{
	public:
		void test_deterministic_and_bounded();
		void test_direction_independent();
		void test_shared_segment_detection();
		void test_closed_polygon_ring();
		void test_enhanced_amplitude();
	};

	class NaturalizeCoastlineTestSuite :
			public GPlatesTestSuite
	{
	public:
		NaturalizeCoastlineTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_NATURALIZECOASTLINETEST_H
