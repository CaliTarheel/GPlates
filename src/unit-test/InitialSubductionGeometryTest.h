/* $Id$ */

#ifndef GPLATES_UNIT_TEST_INITIALSUBDUCTIONGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_INITIALSUBDUCTIONGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class InitialSubductionGeometryTest
	{
	public:
		void test_opposite_margin_and_width();
		void test_smoothed_curved_trench_and_segment_limit();
		void test_clearance_adjustment_and_endpoint_buffer();
	};

	class InitialSubductionGeometryTestSuite :
			public GPlatesTestSuite
	{
	public:
		InitialSubductionGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_INITIALSUBDUCTIONGEOMETRYTEST_H
