/* $Id$ */

#ifndef GPLATES_UNIT_TEST_TRIPLEJUNCTIONGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_TRIPLEJUNCTIONGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class TripleJunctionGeometryTest
	{
	public:
		void test_clean_and_permuted_rrr();
		void test_near_miss_extension();
		void test_rejects_large_gap();
	};

	class TripleJunctionGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		TripleJunctionGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_TRIPLEJUNCTIONGEOMETRYTEST_H
