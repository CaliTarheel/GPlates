/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_UNIT_TEST_WORLDBUILDINGPROJECTMANIFESTTEST_H
#define GPLATES_UNIT_TEST_WORLDBUILDINGPROJECTMANIFESTTEST_H

#include <boost/test/unit_test.hpp>

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class WorldbuildingProjectManifestTest
	{
	public:
		void test_default_manifest_round_trip();
		void test_missing_files_and_renames();
		void test_duplicate_roles_and_newer_versions_block();
	};

	class WorldbuildingProjectManifestTestSuite :
			public GPlatesTestSuite
	{
	public:
		WorldbuildingProjectManifestTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_WORLDBUILDINGPROJECTMANIFESTTEST_H
