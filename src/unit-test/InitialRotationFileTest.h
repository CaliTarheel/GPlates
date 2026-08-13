/* $Id$ */

#ifndef GPLATES_UNIT_TEST_INITIALROTATIONFILETEST_H
#define GPLATES_UNIT_TEST_INITIALROTATIONFILETEST_H

#include "GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class InitialRotationFileTest
	{
	public:
		void test_valid_tree_and_text();
		void test_rejects_cycle();
		void test_rejects_missing_parent();
	};

	class InitialRotationFileTestSuite :
			public GPlatesTestSuite
	{
	public:
		InitialRotationFileTestSuite(unsigned level);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_INITIALROTATIONFILETEST_H
