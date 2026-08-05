/* $Id$ */

#include "unit-test/CollisionGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"
#include "view-operations/CollisionGeometry.h"


namespace
{
	namespace Collision = GPlatesViewOperations::CollisionGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type rectangle(
			double south, double north, double west, double east)
	{
		std::vector<GPlatesMaths::PointOnSphere> ring;
		ring.push_back(point(south, west));
		ring.push_back(point(south, east));
		ring.push_back(point(north, east));
		ring.push_back(point(north, west));
		return GPlatesMaths::PolygonOnSphere::create(ring.begin(), ring.end());
	}
}


GPlatesUnitTest::CollisionGeometryTestSuite::CollisionGeometryTestSuite(unsigned level) :
	GPlatesTestSuite("CollisionGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::CollisionGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<CollisionGeometryTest> instance(new CollisionGeometryTest());
	ADD_TESTCASE(CollisionGeometryTest, test_contact_corridor);
	ADD_TESTCASE(CollisionGeometryTest, test_contact_margin_deformation);
	ADD_TESTCASE(CollisionGeometryTest, test_collision_classification);
	ADD_TESTCASE(CollisionGeometryTest, test_himalayan_width);
}


void
GPlatesUnitTest::CollisionGeometryTest::test_contact_margin_deformation()
{
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type incoming =
			rectangle(-8, 8, -12, -1);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type receiving =
			rectangle(-12, 12, 0, 18);
	Collision::Parameters parameters;
	parameters.contact_threshold_km = 250;
	parameters.deformation_reach_km = 350;
	const Collision::Result result = Collision::generate(incoming, receiving, parameters);
	BOOST_CHECK_LT(result.metrics.post_deformation_gap_km, 1.0);
	BOOST_CHECK_GT(result.deformed_incoming->get_area().dval(),
			0.80 * incoming->get_area().dval());
	BOOST_CHECK_GT(result.deformed_receiving->get_area().dval(),
			0.80 * receiving->get_area().dval());
}


void
GPlatesUnitTest::CollisionGeometryTest::test_contact_corridor()
{
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type incoming =
			rectangle(-8, 8, -12, -1);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type receiving =
			rectangle(-12, 12, 0, 18);
	Collision::Parameters parameters;
	parameters.contact_threshold_km = 250;
	const Collision::Result result = Collision::generate(incoming, receiving, parameters);
	BOOST_CHECK_GT(result.metrics.minimum_gap_km, 90.0);
	BOOST_CHECK_LT(result.metrics.minimum_gap_km, 130.0);
	BOOST_CHECK_GT(result.metrics.contact_length_km, 900.0);
	BOOST_CHECK_GE(result.metrics.contact_sample_count, 8u);
	BOOST_CHECK_LE(result.metrics.maximum_suture_segment_km, 125.0);
	// The two end caps span the full 180 km belt width; longitudinal edges
	// remain governed by the 100 km contact sampling target.
	BOOST_CHECK_LE(result.metrics.maximum_belt_segment_km, 210.0);
}


void
GPlatesUnitTest::CollisionGeometryTest::test_collision_classification()
{
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type small =
			rectangle(-2, 2, -3, 0);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type large =
			rectangle(-12, 12, 0, 20);
	BOOST_CHECK(Collision::recommend_collision_type(small, large, 3.0, 0) ==
			Collision::ARC_OR_TERRANE_ACCRETION);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type peer =
			rectangle(-10, 10, -18, 0);
	BOOST_CHECK(Collision::recommend_collision_type(peer, large, 3.0, 0) ==
			Collision::URAL_OROGENY);
	BOOST_CHECK(Collision::recommend_collision_type(peer, large, 6.0, 0) ==
			Collision::HIMALAYAN_OROGENY);
	BOOST_CHECK(Collision::recommend_collision_type(peer, large, boost::none, 2) ==
			Collision::HIMALAYAN_OROGENY);
}


void
GPlatesUnitTest::CollisionGeometryTest::test_himalayan_width()
{
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type incoming =
			rectangle(-8, 8, -12, -1);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type receiving =
			rectangle(-12, 12, 0, 18);
	Collision::Parameters ural_parameters;
	ural_parameters.contact_threshold_km = 250;
	ural_parameters.belt_width_km = 180;
	const Collision::Result ural = Collision::generate(incoming, receiving, ural_parameters);
	Collision::Parameters himalayan_parameters(ural_parameters);
	himalayan_parameters.collision_type = Collision::HIMALAYAN_OROGENY;
	himalayan_parameters.belt_width_km = 450;
	const Collision::Result himalayan = Collision::generate(
			incoming, receiving, himalayan_parameters);
	BOOST_CHECK_GT(himalayan.orogenic_belt->get_area().dval(),
			2.0 * ural.orogenic_belt->get_area().dval());
}
