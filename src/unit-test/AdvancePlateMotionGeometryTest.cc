/* $Id$ */

#include "unit-test/AdvancePlateMotionGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/AdvancePlateMotionGeometry.h"


namespace
{
	namespace MotionGeometry = GPlatesViewOperations::AdvancePlateMotionGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type meridian(
			double longitude,
			double minimum_latitude = -20,
			double maximum_latitude = 20)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(minimum_latitude, longitude));
		points.push_back(point(0, longitude));
		points.push_back(point(maximum_latitude, longitude));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}
}


GPlatesUnitTest::AdvancePlateMotionGeometryTestSuite::AdvancePlateMotionGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("AdvancePlateMotionGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::AdvancePlateMotionGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<AdvancePlateMotionGeometryTest> instance(new AdvancePlateMotionGeometryTest());
	ADD_TESTCASE(AdvancePlateMotionGeometryTest, test_ridge_push_and_slab_pull_direction);
	ADD_TESTCASE(AdvancePlateMotionGeometryTest, test_previous_rotation_stage_sets_speed);
}


void
GPlatesUnitTest::AdvancePlateMotionGeometryTest::test_ridge_push_and_slab_pull_direction()
{
	std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> ridges;
	ridges.push_back(meridian(-20));
	std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> trenches;
	trenches.push_back(meridian(25));
	const MotionGeometry::Proposal proposal = MotionGeometry::generate_motion(
			point(0, 0), boost::none, 0, ridges, trenches, 50.0);
	const GPlatesMaths::LatLonPoint destination =
			GPlatesMaths::make_lat_lon_point(proposal.destination);

	// A western MOR pushes east and an eastern trench pulls east. Neither
	// feature stores a velocity; only its normalized continent-facing normal is used.
	BOOST_CHECK_GT(destination.longitude(), 0.0);
	BOOST_CHECK_SMALL(destination.latitude(), 1e-6);
	BOOST_CHECK_EQUAL(proposal.ridge_count, 1u);
	BOOST_CHECK_EQUAL(proposal.subduction_count, 1u);
	BOOST_CHECK(!proposal.used_history);
}


void
GPlatesUnitTest::AdvancePlateMotionGeometryTest::test_previous_rotation_stage_sets_speed()
{
	const MotionGeometry::Proposal proposal = MotionGeometry::generate_motion(
			point(0, 0), point(0, -10), 50.0,
			std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>(),
			std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>(),
			50.0);
	const double expected_speed =
			(10.0 * 3.14159265358979323846 / 180.0 * 6371.0088 / 50.0) / 10.0;
	BOOST_CHECK(proposal.used_history);
	BOOST_CHECK_CLOSE(proposal.speed_cm_per_year, expected_speed, 1e-6);
	BOOST_CHECK_GT(GPlatesMaths::make_lat_lon_point(proposal.destination).longitude(), 0.0);
}
