/* $Id$ */

#include "unit-test/InitialSubductionGeometryTest.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "maths/GeometryDistance.h"
#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/InitialSubductionGeometry.h"


namespace
{
	namespace SubductionGeometry = GPlatesViewOperations::InitialSubductionGeometry;

	GPlatesMaths::PointOnSphere
	point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	make_continent()
	{
		std::vector<GPlatesMaths::PointOnSphere> ring;
		ring.push_back(point(-20, -30));
		ring.push_back(point(20, -30));
		ring.push_back(point(20, 30));
		ring.push_back(point(10, 28));
		ring.push_back(point(0, 31));
		ring.push_back(point(-10, 27));
		ring.push_back(point(-20, 30));
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type
	make_western_ridge()
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(-18, -30));
		points.push_back(point(0, -32));
		points.push_back(point(18, -30));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	make_sharp_eastern_bulge_continent()
	{
		std::vector<GPlatesMaths::PointOnSphere> ring;
		ring.push_back(point(-20, -30));
		ring.push_back(point(20, -30));
		ring.push_back(point(20, 30));
		ring.push_back(point(4, 30));
		ring.push_back(point(0, 50));
		ring.push_back(point(-4, 30));
		ring.push_back(point(-20, 30));
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}
}


GPlatesUnitTest::InitialSubductionGeometryTestSuite::InitialSubductionGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("InitialSubductionGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::InitialSubductionGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<InitialSubductionGeometryTest> instance(new InitialSubductionGeometryTest());
	ADD_TESTCASE(InitialSubductionGeometryTest, test_opposite_margin_and_width);
	ADD_TESTCASE(InitialSubductionGeometryTest, test_smoothed_curved_trench_and_segment_limit);
	ADD_TESTCASE(InitialSubductionGeometryTest, test_clearance_adjustment_and_endpoint_buffer);
}


void
GPlatesUnitTest::InitialSubductionGeometryTest::test_opposite_margin_and_width()
{
	const SubductionGeometry::Result result = SubductionGeometry::generate(
			make_continent(), make_western_ridge());
	double minimum_latitude = std::numeric_limits<double>::max();
	double maximum_latitude = -std::numeric_limits<double>::max();
	double mean_longitude = 0;
	unsigned int vertex_count = 0;
	for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
			result.trench->vertex_begin(); vertex_iter != result.trench->vertex_end(); ++vertex_iter)
	{
		const GPlatesMaths::LatLonPoint lat_lon = GPlatesMaths::make_lat_lon_point(*vertex_iter);
		minimum_latitude = std::min(minimum_latitude, lat_lon.latitude());
		maximum_latitude = std::max(maximum_latitude, lat_lon.latitude());
		mean_longitude += lat_lon.longitude();
		++vertex_count;
	}
	mean_longitude /= vertex_count;

	// The MOR is on the west side; the generated trench must be on the far east
	// side and span essentially the full north/south silhouette.
	BOOST_CHECK_GT(mean_longitude, 28.0);
	BOOST_CHECK_LT(minimum_latitude, -17.0);
	BOOST_CHECK_GT(maximum_latitude, 17.0);
	BOOST_CHECK_GT(result.metrics.continent_motion_normal_width_km, 4000.0);
	BOOST_CHECK_GT(result.metrics.trench_motion_normal_span_km,
			0.90 * result.metrics.continent_motion_normal_width_km);
	BOOST_CHECK_GE(
			result.metrics.trench_motion_normal_span_km,
			result.metrics.continent_motion_normal_width_km +
					2.0 * SubductionGeometry::Parameters().endpoint_buffer_km - 1e-6);
}


void
GPlatesUnitTest::InitialSubductionGeometryTest::test_smoothed_curved_trench_and_segment_limit()
{
	SubductionGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 140.0;
	const SubductionGeometry::Result result = SubductionGeometry::generate(
			make_continent(), make_western_ridge(), parameters);
	double minimum_longitude = std::numeric_limits<double>::max();
	double maximum_longitude = -std::numeric_limits<double>::max();
	for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
			result.trench->vertex_begin(); vertex_iter != result.trench->vertex_end(); ++vertex_iter)
	{
		const double longitude = GPlatesMaths::make_lat_lon_point(*vertex_iter).longitude();
		minimum_longitude = std::min(minimum_longitude, longitude);
		maximum_longitude = std::max(maximum_longitude, longitude);
	}

	// Even a support margin that is nearly north/south receives a broad bow,
	// while tessellation remains at least as fine as the Artifexia-derived cap.
	BOOST_CHECK_GT(maximum_longitude - minimum_longitude, 1.0);
	BOOST_CHECK_LE(result.metrics.maximum_segment_length_km, 140.0 * (1.0 + 1e-9));
	BOOST_CHECK_GT(result.trench->number_of_vertices(), parameters.anchor_count);
}


void
GPlatesUnitTest::InitialSubductionGeometryTest::test_clearance_adjustment_and_endpoint_buffer()
{
	SubductionGeometry::Parameters parameters;
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent =
			make_sharp_eastern_bulge_continent();
	const SubductionGeometry::Result result = SubductionGeometry::generate(
			continent, make_western_ridge(), parameters);

	// This narrow promontory is intentionally lost by the low-pass coastline
	// filter. The post-tessellation loop must move the complete smooth trench
	// offshore until it clears the original solid polygon.
	const double exact_clearance_km =
			GPlatesMaths::minimum_distance(
					*result.trench,
					*continent,
					true/*polygon_interior_is_solid*/)
					.calculate_angle().dval() * parameters.planet_radius_km;
	BOOST_CHECK_GT(result.metrics.clearance_adjustment_iterations, 0u);
	BOOST_CHECK_GE(exact_clearance_km + 1e-6, parameters.minimum_continental_clearance_km);
	BOOST_CHECK_CLOSE(exact_clearance_km, result.metrics.minimum_continental_clearance_km, 1e-6);
	BOOST_CHECK_GE(
			result.metrics.trench_motion_normal_span_km,
			result.metrics.continent_motion_normal_width_km +
					2.0 * parameters.endpoint_buffer_km - 1e-6);
}
