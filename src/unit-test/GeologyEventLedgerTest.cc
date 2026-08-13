/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/GeologyEventLedgerTest.h"
#include "view-operations/GeologyEventLedger.h"

GPlatesUnitTest::GeologyEventLedgerTestSuite::GeologyEventLedgerTestSuite(unsigned depth) :
	GPlatesTestSuite("GeologyEventLedgerTestSuite") { init(depth); }
void GPlatesUnitTest::GeologyEventLedgerTestSuite::construct_maps()
{
	boost::shared_ptr<GeologyEventLedgerTest> instance(new GeologyEventLedgerTest());
	ADD_TESTCASE(GeologyEventLedgerTest, test_chronology_warning);
}
void GPlatesUnitTest::GeologyEventLedgerTest::test_chronology_warning()
{
	typedef GPlatesViewOperations::GeologyEventLedger Ledger;
	Ledger::Entry younger;
	younger.category = Ledger::DERIVED_GEOMETRY;
	younger.feature_id = "feature-a";
	younger.event.event_time = 100;
	younger.event.event_type = "geology.active-orogeny";
	Ledger::Entry older = younger;
	older.event.event_time = 200;
	std::vector<Ledger::Entry> wrong_order;
	wrong_order.push_back(younger);
	wrong_order.push_back(older);
	BOOST_CHECK_EQUAL(Ledger::transition_warnings(wrong_order).size(), 1);
	BOOST_CHECK_EQUAL(Ledger::category_name(Ledger::OBSERVATION).toStdString(), "observation");
}
