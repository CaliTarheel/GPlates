/* $Id$ */

#ifndef GPLATES_UNIT_TEST_ADVANCEPLATEMOTIONGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_ADVANCEPLATEMOTIONGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class AdvancePlateMotionGeometryTest
	{
	public:
		void test_ridge_push_and_slab_pull_direction();
		void test_previous_rotation_stage_sets_speed();
	};

	class AdvancePlateMotionGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		AdvancePlateMotionGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_ADVANCEPLATEMOTIONGEOMETRYTEST_H
