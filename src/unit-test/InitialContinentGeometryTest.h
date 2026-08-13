/* $Id$ */

#ifndef GPLATES_UNIT_TEST_INITIALCONTINENTGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_INITIALCONTINENTGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class InitialContinentGeometryTest
	{
	public:
		void test_deterministic_generation();
		void test_worldbuilding_pasta_craton_range();
		void test_area_and_segment_quality();
		void test_coastline_swiggle_control();
		void test_craton_shape_variation();
		void test_packing_controls_craton_fill();
		void test_cratons_are_contained_and_separate();
	};

	class InitialContinentGeometryTestSuite :
			public GPlatesTestSuite
	{
	public:
		InitialContinentGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_INITIALCONTINENTGEOMETRYTEST_H
