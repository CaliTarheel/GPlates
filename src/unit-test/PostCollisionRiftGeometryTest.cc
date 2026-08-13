/* $Id$ */

#include "unit-test/PostCollisionRiftGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "view-operations/PostCollisionRiftGeometry.h"


namespace
{
	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type suture()
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(-14, 0));
		points.push_back(point(-7, 1));
		points.push_back(point(0, -1));
		points.push_back(point(7, 1));
		points.push_back(point(14, 0));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}
}


GPlatesUnitTest::PostCollisionRiftGeometryTestSuite::PostCollisionRiftGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("PostCollisionRiftGeometryTestSuite")
{
	init(level);
}


void GPlatesUnitTest::PostCollisionRiftGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<PostCollisionRiftGeometryTest> instance(
			new PostCollisionRiftGeometryTest());
	ADD_TESTCASE(PostCollisionRiftGeometryTest, test_offset_rift_is_detailed_and_noncoincident);
	ADD_TESTCASE(PostCollisionRiftGeometryTest, test_seed_is_deterministic);
}


void GPlatesUnitTest::PostCollisionRiftGeometryTest::
test_offset_rift_is_detailed_and_noncoincident()
{
	namespace Geometry = GPlatesViewOperations::PostCollisionRiftGeometry;
	Geometry::Parameters parameters;
	parameters.offset_km = 120;
	parameters.end_extension_km = 500;
	const Geometry::Result result = Geometry::generate(suture(), parameters);
	BOOST_CHECK_GT(result.metrics.mean_suture_offset_km, 90.0);
	BOOST_CHECK_GT(result.metrics.minimum_suture_offset_km, 55.0);
	BOOST_CHECK_LE(result.metrics.maximum_segment_length_km, 100.0);
	BOOST_CHECK_GT(result.metrics.path_length_km, 3500.0);
	BOOST_CHECK_GT(result.metrics.vertex_count, 35u);
}


void GPlatesUnitTest::PostCollisionRiftGeometryTest::test_seed_is_deterministic()
{
	namespace Geometry = GPlatesViewOperations::PostCollisionRiftGeometry;
	Geometry::Parameters parameters;
	parameters.random_seed = 42;
	const Geometry::Result first = Geometry::generate(suture(), parameters);
	const Geometry::Result second = Geometry::generate(suture(), parameters);
	BOOST_CHECK_EQUAL(first.rift->number_of_vertices(), second.rift->number_of_vertices());
	BOOST_CHECK_CLOSE(first.metrics.path_length_km, second.metrics.path_length_km, 1e-8);
}
