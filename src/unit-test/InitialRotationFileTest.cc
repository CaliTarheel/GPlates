/* $Id$ */

#include "InitialRotationFileTest.h"

#include <boost/test/unit_test.hpp>

#include "view-operations/InitialRotationFile.h"


void
GPlatesUnitTest::InitialRotationFileTest::test_valid_tree_and_text()
{
	using namespace GPlatesViewOperations::InitialRotationFile;
	plate_circuit_seq_type entries;
	entries.push_back(PlateCircuitEntry(300, 200));
	entries.push_back(PlateCircuitEntry(100, 0));
	entries.push_back(PlateCircuitEntry(200, 100));
	QString error_message;
	BOOST_CHECK(validate_plate_circuit(error_message, entries));
	BOOST_CHECK(error_message.isEmpty());

	const QString text = create_legacy_rotation_text(entries, 1000.0);
	BOOST_CHECK(text.startsWith("100  0.0  90.0  0.0  0.0  000"));
	BOOST_CHECK(text.contains("200  1000.0  90.0  0.0  0.0  100"));
	BOOST_CHECK(text.contains("300  1000.0  90.0  0.0  0.0  200"));
	BOOST_CHECK_EQUAL(text.count('\n'), 6);
}

void
GPlatesUnitTest::InitialRotationFileTest::test_rejects_cycle()
{
	using namespace GPlatesViewOperations::InitialRotationFile;
	plate_circuit_seq_type entries;
	entries.push_back(PlateCircuitEntry(100, 200));
	entries.push_back(PlateCircuitEntry(200, 100));
	QString error_message;
	BOOST_CHECK(!validate_plate_circuit(error_message, entries));
	BOOST_CHECK(error_message.contains("cycle"));
}

void
GPlatesUnitTest::InitialRotationFileTest::test_rejects_missing_parent()
{
	using namespace GPlatesViewOperations::InitialRotationFile;
	plate_circuit_seq_type entries;
	entries.push_back(PlateCircuitEntry(100, 0));
	entries.push_back(PlateCircuitEntry(200, 999));
	QString error_message;
	BOOST_CHECK(!validate_plate_circuit(error_message, entries));
	BOOST_CHECK(error_message.contains("missing"));
}


GPlatesUnitTest::InitialRotationFileTestSuite::InitialRotationFileTestSuite(
		unsigned level) :
	GPlatesTestSuite("InitialRotationFileTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::InitialRotationFileTestSuite::construct_maps()
{
	boost::shared_ptr<InitialRotationFileTest> instance(new InitialRotationFileTest());
	ADD_TESTCASE(InitialRotationFileTest, test_valid_tree_and_text);
	ADD_TESTCASE(InitialRotationFileTest, test_rejects_cycle);
	ADD_TESTCASE(InitialRotationFileTest, test_rejects_missing_parent);
}
