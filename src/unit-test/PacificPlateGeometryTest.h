/* $Id$ */

#ifndef GPLATES_UNIT_TEST_PACIFICPLATEGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_PACIFICPLATEGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class PacificPlateGeometryTest
	{
	public:
		void test_builds_seeded_local_void();
		void test_rejects_seed_outside_void();
		void test_rejects_nonlocal_endpoints();
	};

	class PacificPlateGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		PacificPlateGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_PACIFICPLATEGEOMETRYTEST_H
