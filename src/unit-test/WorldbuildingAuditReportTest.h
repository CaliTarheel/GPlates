/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_WORLDBUILDINGAUDITREPORTTEST_H
#define GPLATES_UNIT_TEST_WORLDBUILDINGAUDITREPORTTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class WorldbuildingAuditReportTest { public: void test_portable_reports(); };
	class WorldbuildingAuditReportTestSuite : public GPlatesTestSuite
	{
	public: WorldbuildingAuditReportTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
