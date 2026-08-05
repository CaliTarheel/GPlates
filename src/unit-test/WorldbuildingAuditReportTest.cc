/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/WorldbuildingAuditReportTest.h"
#include "view-operations/WorldbuildingAuditReport.h"

GPlatesUnitTest::WorldbuildingAuditReportTestSuite::WorldbuildingAuditReportTestSuite(unsigned depth) :
	GPlatesTestSuite("WorldbuildingAuditReportTestSuite") { init(depth); }
void GPlatesUnitTest::WorldbuildingAuditReportTestSuite::construct_maps()
{
	boost::shared_ptr<WorldbuildingAuditReportTest> instance(new WorldbuildingAuditReportTest());
	ADD_TESTCASE(WorldbuildingAuditReportTest, test_portable_reports);
}
void GPlatesUnitTest::WorldbuildingAuditReportTest::test_portable_reports()
{
	typedef GPlatesViewOperations::WorldbuildingAuditReport Audit;
	Audit::Report report;
	report.request.project_revision = "r7";
	Audit::Finding finding;
	finding.severity = "warning"; finding.domain = "crust"; finding.code = "old-ocean-crust";
	finding.feature_id = "crust-1"; finding.message = "review"; finding.suggested_repair = "inspect";
	report.findings.push_back(finding);
	Audit::CrustRecord crust;
	crust.feature_id = "crust-1"; crust.created_time = 300; crust.status = "surviving";
	crust.age_at_current_time = 250; crust.old_crust_advisory = true;
	report.crust.push_back(crust);
	const QString markdown = report.to_markdown(std::vector<int>(1, 0));
	BOOST_CHECK(markdown.contains("crust-1"));
	BOOST_CHECK(markdown.contains("User-promoted repair queue"));
	const QString json = report.to_json(std::vector<int>(1, 0));
	BOOST_CHECK(json.contains("promoted_to_repair_queue"));
	BOOST_CHECK(json.contains("r7"));
}
