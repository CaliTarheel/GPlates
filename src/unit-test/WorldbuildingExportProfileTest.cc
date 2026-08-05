/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/WorldbuildingExportProfileTest.h"

#include "app-logic/WorldbuildingExportProfile.h"
#include "file-io/ExportTemplateFilenameSequence.h"

GPlatesUnitTest::WorldbuildingExportProfileTestSuite::WorldbuildingExportProfileTestSuite(unsigned depth) :
	GPlatesTestSuite("WorldbuildingExportProfileTestSuite") { init(depth); }

void GPlatesUnitTest::WorldbuildingExportProfileTestSuite::construct_maps()
{
	boost::shared_ptr<WorldbuildingExportProfileTest> instance(new WorldbuildingExportProfileTest());
	ADD_TESTCASE(WorldbuildingExportProfileTest, test_exact_project_schedule_and_manifest);
	ADD_TESTCASE(WorldbuildingExportProfileTest, test_explicit_filename_times);
}

void GPlatesUnitTest::WorldbuildingExportProfileTest::test_exact_project_schedule_and_manifest()
{
	typedef GPlatesAppLogic::WorldbuildingExportProfile Profile;
	Profile::Request request;
	request.profile = Profile::default_profiles().front();
	request.schedule_mode = Profile::PROJECT_TIMESTAMPS;
	request.start_time = 600;
	request.end_time = 250;
	request.project_revision = QString::fromLatin1("r12");
	const double exact_times[] = { 600, 560, 520, 480, 440, 400, 370, 340, 310, 280, 250 };
	request.project_timestamps_older_to_younger.assign(exact_times, exact_times + 11);

	const Profile::Plan plan = Profile::build(request);
	BOOST_REQUIRE_EQUAL(plan.reconstruction_times.size(), 11);
	BOOST_CHECK_EQUAL(plan.reconstruction_times[1], 560);
	BOOST_CHECK_EQUAL(plan.reconstruction_times[6], 370);
	BOOST_CHECK_EQUAL(plan.files[6].file_name.toStdString(), "worldbuilding-boundaries-370Ma");
	const QString json = plan.to_json();
	BOOST_CHECK(json.contains(QString::fromLatin1("project-timestamps")));
	BOOST_CHECK(json.contains(QString::fromLatin1("downstream_only")));
	BOOST_CHECK(json.contains(QString::fromLatin1("planet_radius_km")));
	BOOST_CHECK(json.contains(QString::fromLatin1("r12")));
}

void GPlatesUnitTest::WorldbuildingExportProfileTest::test_explicit_filename_times()
{
	std::vector<double> times;
	times.push_back(600);
	times.push_back(560);
	times.push_back(520);
	GPlatesFileIO::ExportTemplateFilenameSequence sequence(
			QString::fromLatin1("frame_%0.2fMa.png"), 0, QString(), times);
	BOOST_REQUIRE_EQUAL(sequence.size(), 3);
	GPlatesFileIO::ExportTemplateFilenameSequence::const_iterator frame = sequence.begin();
	BOOST_CHECK_EQUAL((*frame).toStdString(), "frame_600.00Ma.png");
	++frame;
	BOOST_CHECK_EQUAL((*frame).toStdString(), "frame_560.00Ma.png");
	++frame;
	BOOST_CHECK_EQUAL((*frame).toStdString(), "frame_520.00Ma.png");
}
