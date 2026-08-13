/* $Id$ */

#include "unit-test/PacificPlateGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/PacificPlateGeometry.h"


namespace
{
	namespace PacificGeometry = GPlatesViewOperations::PacificPlateGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type line(
			double near_latitude,
			double near_longitude,
			double far_latitude,
			double far_longitude)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(near_latitude, near_longitude));
		points.push_back(point(far_latitude, far_longitude));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}

	PacificGeometry::polyline_seq_type surrounding_ridges()
	{
		PacificGeometry::polyline_seq_type ridges;
		ridges.push_back(line(5.0, -6.0, 25.0, -25.0));
		ridges.push_back(line(5.0, 6.0, 25.0, 25.0));
		ridges.push_back(line(-6.0, 0.0, -30.0, 0.0));
		return ridges;
	}

	GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters parameters()
	{
		GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters result;
		result.maximum_segment_length_km = 250.0;
		result.amplitude_percent = 0.0;
		result.random_seed = 17;
		return result;
	}
}


GPlatesUnitTest::PacificPlateGeometryTestSuite::PacificPlateGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("PacificPlateGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::PacificPlateGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<PacificPlateGeometryTest> instance(
			new PacificPlateGeometryTest());
	ADD_TESTCASE(PacificPlateGeometryTest, test_builds_seeded_local_void);
	ADD_TESTCASE(PacificPlateGeometryTest, test_rejects_seed_outside_void);
	ADD_TESTCASE(PacificPlateGeometryTest, test_rejects_nonlocal_endpoints);
}


void
GPlatesUnitTest::PacificPlateGeometryTest::test_builds_seeded_local_void()
{
	const GPlatesMaths::PointOnSphere seed = point(0.0, 0.0);
	const PacificGeometry::Result result = PacificGeometry::build_local_void(
			surrounding_ridges(), seed, 30.0, parameters(),
			GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type());
	BOOST_REQUIRE_MESSAGE(result.success, result.error.toStdString());
	BOOST_REQUIRE(result.central_crust);
	BOOST_REQUIRE_EQUAL(result.bounding_ridges.size(), 3);
	BOOST_CHECK((*result.central_crust)->is_point_in_polygon(seed));
	BOOST_CHECK_LE(result.actual_maximum_segment_length_km, 250.0 * (1.0 + 1e-9));
	BOOST_CHECK_GT(result.inserted_vertex_count, 0u);
}


void
GPlatesUnitTest::PacificPlateGeometryTest::test_rejects_seed_outside_void()
{
	const PacificGeometry::Result result = PacificGeometry::build_local_void(
			surrounding_ridges(), point(14.0, 0.0), 30.0, parameters(),
			GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type());
	BOOST_CHECK(!result.success);
	BOOST_CHECK(!result.error.isEmpty());
}


void
GPlatesUnitTest::PacificPlateGeometryTest::test_rejects_nonlocal_endpoints()
{
	const PacificGeometry::Result result = PacificGeometry::build_local_void(
			surrounding_ridges(), point(0.0, 0.0), 2.0, parameters(),
			GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type());
	BOOST_CHECK(!result.success);
	BOOST_CHECK(!result.error.isEmpty());
}
