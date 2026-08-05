/* $Id$ */

#ifndef GPLATES_UNIT_TEST_MANTLEEVENTGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_MANTLEEVENTGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class MantleEventGeometryTest
	{
	public:
		void test_lip_stays_inside_host_and_is_detailed();
		void test_rift_trigger_and_seed_are_deterministic();
	};

	class MantleEventGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		explicit MantleEventGeometryTestSuite(unsigned level);
	protected:
		virtual void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_MANTLEEVENTGEOMETRYTEST_H
