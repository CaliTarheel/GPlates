/* $Id$ */

#ifndef GPLATES_UNIT_TEST_OCEANCRUSTBANDBUILDERTEST_H
#define GPLATES_UNIT_TEST_OCEANCRUSTBANDBUILDERTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class OceanCrustBandBuilderTest
	{
	public:
		void test_subtracts_existing_crust();
		void test_disjoint_and_fully_filled_bands();
	};

	class OceanCrustBandBuilderTestSuite : public GPlatesTestSuite
	{
	public:
		OceanCrustBandBuilderTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_OCEANCRUSTBANDBUILDERTEST_H
