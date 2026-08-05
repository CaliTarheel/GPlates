/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_GEOLOGYEVENTLEDGERTEST_H
#define GPLATES_UNIT_TEST_GEOLOGYEVENTLEDGERTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class GeologyEventLedgerTest { public: void test_chronology_warning(); };
	class GeologyEventLedgerTestSuite : public GPlatesTestSuite
	{
	public: GeologyEventLedgerTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
