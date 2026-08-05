/* $Id$ */

#ifndef GPLATES_UNIT_TEST_BOOLEANPOLYGONGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_BOOLEANPOLYGONGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class BooleanPolygonGeometryTest
	{
	public:
		void test_union_and_property_shape();
		void test_difference_and_intersection();
		void test_symmetric_difference_and_empty_result();
	};

	class BooleanPolygonGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		BooleanPolygonGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_BOOLEANPOLYGONGEOMETRYTEST_H
