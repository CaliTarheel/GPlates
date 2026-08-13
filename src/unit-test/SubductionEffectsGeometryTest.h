/* $Id$ */

#ifndef GPLATES_UNIT_TEST_SUBDUCTIONEFFECTSGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_SUBDUCTIONEFFECTSGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class SubductionEffectsGeometryTest
	{
	public:
		void test_polarity_probe_and_contained_andean_belt();
		void test_island_arc_line_and_land_intersection_mountains();
		void test_trim_controls_and_laramide_width();
	};

	class SubductionEffectsGeometryTestSuite :
			public GPlatesTestSuite
	{
	public:
		SubductionEffectsGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_SUBDUCTIONEFFECTSGEOMETRYTEST_H
