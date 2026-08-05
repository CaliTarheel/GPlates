/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_WORLDBUILDINGEXPORTPROFILETEST_H
#define GPLATES_UNIT_TEST_WORLDBUILDINGEXPORTPROFILETEST_H

#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"

namespace GPlatesUnitTest
{
	class WorldbuildingExportProfileTest
	{
	public:
		void test_exact_project_schedule_and_manifest();
		void test_explicit_filename_times();
	};

	class WorldbuildingExportProfileTestSuite : public GPlatesTestSuite
	{
	public:
		WorldbuildingExportProfileTestSuite(unsigned depth);
	protected:
		void construct_maps();
	};
}
#endif
