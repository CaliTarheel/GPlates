/* $Id$ */

#include "unit-test/MantleEventGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "view-operations/MantleEventGeometry.h"


namespace
{
	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type host()
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(-20, -30));
		points.push_back(point(-20, 30));
		points.push_back(point(20, 30));
		points.push_back(point(20, -30));
		return GPlatesMaths::PolygonOnSphere::create(points);
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type rift()
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(-12, -5));
		points.push_back(point(-4, -2));
		points.push_back(point(4, 1));
		points.push_back(point(12, 4));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}
}


GPlatesUnitTest::MantleEventGeometryTestSuite::MantleEventGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("MantleEventGeometryTestSuite")
{
	init(level);
}


void GPlatesUnitTest::MantleEventGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<MantleEventGeometryTest> instance(new MantleEventGeometryTest());
	ADD_TESTCASE(MantleEventGeometryTest, test_lip_stays_inside_host_and_is_detailed);
	ADD_TESTCASE(MantleEventGeometryTest, test_rift_trigger_and_seed_are_deterministic);
}


void GPlatesUnitTest::MantleEventGeometryTest::
test_lip_stays_inside_host_and_is_detailed()
{
	namespace Geometry = GPlatesViewOperations::MantleEventGeometry;
	Geometry::Parameters parameters;
	parameters.rift_triggered = false;
	parameters.diameter_km = 900;
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent = host();
	const Geometry::Result result = Geometry::generate(continent, boost::none, parameters);
	BOOST_CHECK(continent->is_point_in_polygon(result.hotspot_position));
	for (unsigned int index = 0; index < result.lip_outline->number_of_vertices(); ++index)
	{
		BOOST_CHECK(continent->is_point_in_polygon(result.lip_outline->get_vertex(index)));
	}
	BOOST_CHECK_GE(result.metrics.actual_diameter_km, 300.0);
	BOOST_CHECK_GT(result.metrics.boundary_vertex_count, 20u);
	BOOST_CHECK_LE(result.metrics.maximum_segment_length_km, 100.0);
}


void GPlatesUnitTest::MantleEventGeometryTest::
test_rift_trigger_and_seed_are_deterministic()
{
	namespace Geometry = GPlatesViewOperations::MantleEventGeometry;
	Geometry::Parameters parameters;
	parameters.rift_triggered = true;
	parameters.random_seed = 17;
	const Geometry::Result first = Geometry::generate(host(), rift(), parameters);
	const Geometry::Result second = Geometry::generate(host(), rift(), parameters);
	BOOST_CHECK(first.metrics.used_rift);
	BOOST_CHECK_EQUAL(first.lip_outline->number_of_vertices(),
			second.lip_outline->number_of_vertices());
	BOOST_CHECK_CLOSE(first.metrics.actual_diameter_km,
			second.metrics.actual_diameter_km, 1e-8);
	BOOST_CHECK_CLOSE(first.metrics.maximum_segment_length_km,
			second.metrics.maximum_segment_length_km, 1e-8);
}
