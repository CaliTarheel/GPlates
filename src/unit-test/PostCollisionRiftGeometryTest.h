/* $Id$ */

#ifndef GPLATES_UNIT_TEST_POSTCOLLISIONRIFTGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_POSTCOLLISIONRIFTGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class PostCollisionRiftGeometryTest
	{
	public:
		void test_offset_rift_is_detailed_and_noncoincident();
		void test_seed_is_deterministic();
	};

	class PostCollisionRiftGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		explicit PostCollisionRiftGeometryTestSuite(unsigned level);
	protected:
		virtual void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_POSTCOLLISIONRIFTGEOMETRYTEST_H
