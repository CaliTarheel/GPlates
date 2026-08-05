/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/SubductionLifecyclePlannerTest.h"
#include "view-operations/SubductionLifecyclePlanner.h"

GPlatesUnitTest::SubductionLifecyclePlannerTestSuite::SubductionLifecyclePlannerTestSuite(unsigned depth) :
	GPlatesTestSuite("SubductionLifecyclePlannerTestSuite") { init(depth); }
void GPlatesUnitTest::SubductionLifecyclePlannerTestSuite::construct_maps()
{
	boost::shared_ptr<SubductionLifecyclePlannerTest> instance(new SubductionLifecyclePlannerTest());
	ADD_TESTCASE(SubductionLifecyclePlannerTest, test_reversal_and_retirement);
}
void GPlatesUnitTest::SubductionLifecyclePlannerTest::test_reversal_and_retirement()
{
	typedef GPlatesViewOperations::SubductionLifecyclePlanner Planner;
	Planner::Request request;
	request.event_type = Planner::POLARITY_REVERSAL;
	request.event_time = 100;
	request.trench_start_time = 200;
	request.overriding_plate = 1;
	request.subducting_plate = 2;
	request.polarity_left = true;
	request.successor_polarity_left = true;
	BOOST_CHECK(!Planner::plan(request).valid);
	request.successor_polarity_left = false;
	BOOST_CHECK(Planner::plan(request).valid);
	request.event_type = Planner::FINAL_RETIREMENT;
	const Planner::Plan retirement = Planner::plan(request);
	BOOST_CHECK(retirement.valid);
	BOOST_CHECK(!retirement.creates_successor_trench);
	BOOST_CHECK(retirement.retires_ocean_crust);
	request.event_type = Planner::FLAT_SLAB;
	request.duration_ma = 0;
	BOOST_CHECK(!Planner::plan(request).valid);
	request.duration_ma = 20;
	BOOST_CHECK(Planner::plan(request).valid);
}
