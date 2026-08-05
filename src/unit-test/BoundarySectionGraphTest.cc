/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "unit-test/BoundarySectionGraphTest.h"
#include "maths/LatLonPoint.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/BoundarySectionGraph.h"

GPlatesUnitTest::BoundarySectionGraphTestSuite::BoundarySectionGraphTestSuite(unsigned depth) :
	GPlatesTestSuite("BoundarySectionGraphTestSuite") { init(depth); }
void GPlatesUnitTest::BoundarySectionGraphTestSuite::construct_maps()
{
	boost::shared_ptr<BoundarySectionGraphTest> instance(new BoundarySectionGraphTest());
	ADD_TESTCASE(BoundarySectionGraphTest, test_gap_and_successor_plan);
}
void GPlatesUnitTest::BoundarySectionGraphTest::test_gap_and_successor_plan()
{
	typedef GPlatesViewOperations::BoundarySectionGraph Graph;
	std::vector<GPlatesMaths::PointOnSphere> first_points;
	first_points.push_back(GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 0)));
	first_points.push_back(GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 10)));
	std::vector<GPlatesMaths::PointOnSphere> second_points;
	second_points.push_back(GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 11)));
	second_points.push_back(GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 20)));
	std::vector<Graph::Section> sections;
	sections.push_back(Graph::Section("a", "MidOceanRidge", 1, 2, GPlatesMaths::PolylineOnSphere::create(first_points)));
	sections.push_back(Graph::Section("b", "MidOceanRidge", 1, 2, GPlatesMaths::PolylineOnSphere::create(second_points)));
	const std::vector<Graph::Issue> issues = Graph::analyse(sections, 100.0, 0.01, false);
	bool found_gap = false;
	for (std::vector<Graph::Issue>::const_iterator issue = issues.begin(); issue != issues.end(); ++issue)
		found_gap = found_gap || issue->type == Graph::POSSIBLE_MISSING_TRANSFORM;
	BOOST_CHECK(found_gap);
	const std::vector<Graph::SuccessorPlan> plan = Graph::plan_successors(sections, 80.0);
	BOOST_REQUIRE_EQUAL(plan.size(), 2u);
	BOOST_CHECK_EQUAL(plan[0].source_feature_id.toStdString(), "a");
}
