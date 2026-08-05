/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/FeatureEventVersionerTest.h"
#include <memory>
#include <QUndoCommand>
#include "feature-visitors/PropertyValueFinder.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/PropertyName.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/XsString.h"
#include "utils/UnicodeStringUtils.h"
#include "view-operations/FeatureEventVersioner.h"
#include "view-operations/PlateEventTransaction.h"

namespace
{
	GPlatesModel::FeatureHandle::non_null_ptr_type make_feature()
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature = GPlatesModel::FeatureHandle::create(
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust"));
		BOOST_REQUIRE(GPlatesModel::ModelUtils::add_property(feature->reference(),
				GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(1000.0),
						GPlatesPropertyValues::GeoTimeInstant(0.0))));
		return feature;
	}
}

GPlatesUnitTest::FeatureEventVersionerTestSuite::FeatureEventVersionerTestSuite(unsigned depth) :
	GPlatesTestSuite("FeatureEventVersionerTestSuite") { init(depth); }

void GPlatesUnitTest::FeatureEventVersionerTestSuite::construct_maps()
{
	boost::shared_ptr<FeatureEventVersionerTest> instance(new FeatureEventVersionerTest());
	ADD_TESTCASE(FeatureEventVersionerTest, test_event_round_trip_and_valid_time);
	ADD_TESTCASE(FeatureEventVersionerTest, test_existing_description_is_preserved);
	ADD_TESTCASE(FeatureEventVersionerTest, test_transaction_participants);
}

void GPlatesUnitTest::FeatureEventVersionerTest::test_event_round_trip_and_valid_time()
{
	typedef GPlatesViewOperations::FeatureEventVersioner Versioner;
	GPlatesModel::FeatureHandle::non_null_ptr_type feature = make_feature();
	const Versioner::EventRecord event = Versioner::make_event(QString::fromLatin1("collision"),
			250.0, QString::fromLatin1("2"), QStringList() << QString::fromLatin1("source-a"),
			QStringList() << QString::fromLatin1("output-b"), QString::fromLatin1("ended-source"), 17);
	Versioner::set_properties(feature->reference(),
			Versioner::properties_for_event(feature->reference(), Versioner::END_AT_EVENT, event));
	const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type> time =
			GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GmlTimePeriod>(
					feature->reference(), GPlatesModel::PropertyName::create_gml("validTime"));
	BOOST_REQUIRE(time);
	BOOST_CHECK_EQUAL((*time)->begin()->get_time_position().value(), 1000.0);
	BOOST_CHECK_EQUAL((*time)->end()->get_time_position().value(), 250.0);
	const std::vector<Versioner::EventRecord> events = Versioner::events(feature->reference());
	BOOST_REQUIRE_EQUAL(events.size(), 1);
	BOOST_CHECK_EQUAL(events[0].event_type.toStdString(), "collision");
	BOOST_CHECK_EQUAL(events[0].source_feature_ids.front().toStdString(), "source-a");
	BOOST_REQUIRE(events[0].seed);
	BOOST_CHECK_EQUAL(*events[0].seed, 17);
}

void GPlatesUnitTest::FeatureEventVersionerTest::test_existing_description_is_preserved()
{
	typedef GPlatesViewOperations::FeatureEventVersioner Versioner;
	GPlatesModel::FeatureHandle::non_null_ptr_type feature = make_feature();
	BOOST_REQUIRE(GPlatesModel::ModelUtils::add_property(feature->reference(),
			GPlatesModel::PropertyName::create_gml("description"),
			GPlatesPropertyValues::XsString::create(
					GPlatesUtils::make_icu_string_from_qstring(QString::fromLatin1("Human note")))));
	const Versioner::EventRecord event = Versioner::make_event(QString::fromLatin1("rerift"),
			100.0, QString::fromLatin1("1"), QStringList(), QStringList(), QString::fromLatin1("successor"));
	Versioner::set_properties(feature->reference(),
			Versioner::properties_for_event(feature->reference(), Versioner::KEEP_VALID_TIME, event));
	BOOST_CHECK(Versioner::description_text(feature->reference()).startsWith(QString::fromLatin1("Human note\n")));
}

void GPlatesUnitTest::FeatureEventVersionerTest::test_transaction_participants()
{
	GPlatesViewOperations::PlateEventTransaction transaction(QString::fromLatin1("test"));
	transaction.add_command(std::unique_ptr<QUndoCommand>(new QUndoCommand()),
			GPlatesViewOperations::PlateEventTransaction::FEATURE_GEOMETRY);
	transaction.add_command(std::unique_ptr<QUndoCommand>(new QUndoCommand()),
			GPlatesViewOperations::PlateEventTransaction::ROTATION_MODEL);
	BOOST_CHECK(!transaction.is_empty());
	BOOST_CHECK_EQUAL(transaction.participant_names().size(), 2);
}
