/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/CollisionAccretionGuardrailsTest.h"
#include "view-operations/CollisionAccretionGuardrails.h"

GPlatesUnitTest::CollisionAccretionGuardrailsTestSuite::CollisionAccretionGuardrailsTestSuite(unsigned depth) :
	GPlatesTestSuite("CollisionAccretionGuardrailsTestSuite") { init(depth); }
void GPlatesUnitTest::CollisionAccretionGuardrailsTestSuite::construct_maps()
{
	boost::shared_ptr<CollisionAccretionGuardrailsTest> instance(new CollisionAccretionGuardrailsTest());
	ADD_TESTCASE(CollisionAccretionGuardrailsTest, test_explicit_survivor_and_timestamp);
}
void GPlatesUnitTest::CollisionAccretionGuardrailsTest::test_explicit_survivor_and_timestamp()
{
	typedef GPlatesViewOperations::CollisionAccretionGuardrails Guard;
	Guard::Request request;
	request.mode = Guard::RERIFT;
	request.event_time = 100;
	request.project_schedule_available = true;
	request.event_is_project_timestamp = false;
	request.incoming_plate = 1;
	request.receiving_plate = 2;
	request.survivor = Guard::EXPLICIT_CHILD_IDS;
	request.boolean_preview_reviewed = true;
	request.boolean_output_count = 2;
	request.craton_geometry_protected = true;
	request.lineage_will_be_recorded = true;
	request.no_rotation_jump = true;
	BOOST_CHECK(!Guard::validate(request).valid);
	request.event_is_project_timestamp = true;
	BOOST_CHECK(Guard::validate(request).valid);
	BOOST_CHECK(Guard::is_project_timestamp(100, std::vector<double>{200, 100, 0}));
}
