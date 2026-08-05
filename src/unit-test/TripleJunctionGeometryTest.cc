/* $Id$ */

#include "unit-test/TripleJunctionGeometryTest.h"

#include <algorithm>
#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/TripleJunctionGeometry.h"


namespace
{
	namespace JunctionGeometry = GPlatesViewOperations::TripleJunctionGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	JunctionGeometry::polyline_ptr_type line(
			double junction_latitude,
			double junction_longitude,
			double outer_latitude,
			double outer_longitude)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(junction_latitude, junction_longitude));
		points.push_back(point(outer_latitude, outer_longitude));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}

	JunctionGeometry::polyline_seq_type clean_rrr()
	{
		JunctionGeometry::polyline_seq_type ridges;
		ridges.push_back(line(0.0, 0.0, 0.0, -10.0));
		ridges.push_back(line(0.0, 0.0, 10.0, 10.0));
		ridges.push_back(line(0.0, 0.0, -10.0, 10.0));
		return ridges;
	}
}


GPlatesUnitTest::TripleJunctionGeometryTestSuite::TripleJunctionGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("TripleJunctionGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::TripleJunctionGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<TripleJunctionGeometryTest> instance(
			new TripleJunctionGeometryTest());
	ADD_TESTCASE(TripleJunctionGeometryTest, test_clean_and_permuted_rrr);
	ADD_TESTCASE(TripleJunctionGeometryTest, test_near_miss_extension);
	ADD_TESTCASE(TripleJunctionGeometryTest, test_rejects_large_gap);
}


void
GPlatesUnitTest::TripleJunctionGeometryTest::test_clean_and_permuted_rrr()
{
	JunctionGeometry::polyline_seq_type ridges = clean_rrr();
	const JunctionGeometry::Result first =
			JunctionGeometry::resolve_rrr_endpoints(ridges, 1.0);
	BOOST_REQUIRE(first.success);
	BOOST_REQUIRE_EQUAL(first.resolved_ridges.size(), 3);
	BOOST_CHECK_SMALL(first.maximum_extension_degrees, 1e-8);

	std::reverse(ridges.begin(), ridges.end());
	const JunctionGeometry::Result permuted =
			JunctionGeometry::resolve_rrr_endpoints(ridges, 1.0);
	BOOST_REQUIRE(permuted.success);
	BOOST_CHECK_SMALL(permuted.maximum_extension_degrees, 1e-8);
}


void
GPlatesUnitTest::TripleJunctionGeometryTest::test_near_miss_extension()
{
	JunctionGeometry::polyline_seq_type ridges;
	ridges.push_back(line(0.0, -0.20, 0.0, -10.0));
	ridges.push_back(line(0.15, 0.10, 10.0, 10.0));
	ridges.push_back(line(-0.15, 0.10, -10.0, 10.0));
	const JunctionGeometry::Result result =
			JunctionGeometry::resolve_rrr_endpoints(ridges, 1.0);
	BOOST_REQUIRE(result.success);
	BOOST_CHECK_GT(result.maximum_extension_degrees, 0.0);
	BOOST_CHECK_LT(result.maximum_extension_degrees, 1.0);
	for (unsigned int ridge_index = 0; ridge_index < result.resolved_ridges.size(); ++ridge_index)
	{
		BOOST_CHECK(GPlatesMaths::points_are_coincident(
				*result.resolved_ridges[ridge_index]->vertex_begin(), result.junction));
	}
}


void
GPlatesUnitTest::TripleJunctionGeometryTest::test_rejects_large_gap()
{
	JunctionGeometry::polyline_seq_type ridges;
	ridges.push_back(line(0.0, 0.0, 0.0, -10.0));
	ridges.push_back(line(0.0, 0.0, 10.0, 10.0));
	ridges.push_back(line(0.0, 5.0, -10.0, 10.0));
	const JunctionGeometry::Result result =
			JunctionGeometry::resolve_rrr_endpoints(ridges, 1.0);
	BOOST_CHECK(!result.success);
	BOOST_CHECK(!result.error.isEmpty());
}
