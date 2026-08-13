/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_FEATUREEVENTVERSIONERTEST_H
#define GPLATES_UNIT_TEST_FEATUREEVENTVERSIONERTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class FeatureEventVersionerTest
	{
	public:
		void test_event_round_trip_and_valid_time();
		void test_existing_description_is_preserved();
		void test_transaction_participants();
	};
	class FeatureEventVersionerTestSuite : public GPlatesTestSuite
	{
	public:
		FeatureEventVersionerTestSuite(unsigned depth);
	protected:
		void construct_maps();
	};
}
#endif
