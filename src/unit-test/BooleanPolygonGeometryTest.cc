/* $Id$ */

#include "unit-test/BooleanPolygonGeometryTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "view-operations/SubductionCutterGeometry.h"


namespace
{
	namespace BooleanGeometry = GPlatesViewOperations::SubductionCutterGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	BooleanGeometry::polygon_ptr_type rectangle(
			double minimum_latitude,
			double maximum_latitude,
			double minimum_longitude,
			double maximum_longitude)
	{
		std::vector<GPlatesMaths::PointOnSphere> ring;
		ring.push_back(point(minimum_latitude, minimum_longitude));
		ring.push_back(point(maximum_latitude, minimum_longitude));
		ring.push_back(point(maximum_latitude, maximum_longitude));
		ring.push_back(point(minimum_latitude, maximum_longitude));
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}

	bool any_contains(
			const BooleanGeometry::polygon_seq_type &polygons,
			const GPlatesMaths::PointOnSphere &sample)
	{
		for (BooleanGeometry::polygon_seq_type::const_iterator polygon_iter =
				polygons.begin(); polygon_iter != polygons.end(); ++polygon_iter)
		{
			if ((*polygon_iter)->is_point_in_polygon(sample))
			{
				return true;
			}
		}
		return false;
	}

	BooleanGeometry::polygon_ptr_type first_polygon()
	{
		return rectangle(-10.0, 10.0, -20.0, 10.0);
	}

	BooleanGeometry::polygon_ptr_type overlapping_operand()
	{
		return rectangle(-10.0, 10.0, 0.0, 20.0);
	}
}


GPlatesUnitTest::BooleanPolygonGeometryTestSuite::BooleanPolygonGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("BooleanPolygonGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::BooleanPolygonGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<BooleanPolygonGeometryTest> instance(
			new BooleanPolygonGeometryTest());
	ADD_TESTCASE(BooleanPolygonGeometryTest, test_union_and_property_shape);
	ADD_TESTCASE(BooleanPolygonGeometryTest, test_difference_and_intersection);
	ADD_TESTCASE(BooleanPolygonGeometryTest, test_symmetric_difference_and_empty_result);
}


void
GPlatesUnitTest::BooleanPolygonGeometryTest::test_union_and_property_shape()
{
	BooleanGeometry::polygon_seq_type operands;
	operands.push_back(overlapping_operand());
	const BooleanGeometry::BooleanResult result = BooleanGeometry::apply_polygon_boolean(
			*first_polygon(), operands, BooleanGeometry::POLYGON_UNION);
	BOOST_REQUIRE(result.success);
	BOOST_REQUIRE_EQUAL(result.polygons.size(), 1);
	BOOST_CHECK(any_contains(result.polygons, point(0.0, -15.0)));
	BOOST_CHECK(any_contains(result.polygons, point(0.0, 15.0)));
}


void
GPlatesUnitTest::BooleanPolygonGeometryTest::test_difference_and_intersection()
{
	BooleanGeometry::polygon_seq_type operands;
	operands.push_back(overlapping_operand());
	const BooleanGeometry::BooleanResult difference = BooleanGeometry::apply_polygon_boolean(
			*first_polygon(), operands, BooleanGeometry::POLYGON_DIFFERENCE);
	BOOST_REQUIRE(difference.success);
	BOOST_REQUIRE_EQUAL(difference.polygons.size(), 1);
	BOOST_CHECK(any_contains(difference.polygons, point(0.0, -10.0)));
	BOOST_CHECK(!any_contains(difference.polygons, point(0.0, 5.0)));

	const BooleanGeometry::BooleanResult intersection = BooleanGeometry::apply_polygon_boolean(
			*first_polygon(), operands, BooleanGeometry::POLYGON_INTERSECTION);
	BOOST_REQUIRE(intersection.success);
	BOOST_REQUIRE_EQUAL(intersection.polygons.size(), 1);
	BOOST_CHECK(any_contains(intersection.polygons, point(0.0, 5.0)));
	BOOST_CHECK(!any_contains(intersection.polygons, point(0.0, -10.0)));
}


void
GPlatesUnitTest::BooleanPolygonGeometryTest::test_symmetric_difference_and_empty_result()
{
	BooleanGeometry::polygon_seq_type operands;
	operands.push_back(overlapping_operand());
	const BooleanGeometry::BooleanResult symmetric = BooleanGeometry::apply_polygon_boolean(
			*first_polygon(), operands, BooleanGeometry::POLYGON_SYMMETRIC_DIFFERENCE);
	BOOST_REQUIRE(symmetric.success);
	BOOST_REQUIRE_EQUAL(symmetric.polygons.size(), 2);
	BOOST_CHECK(any_contains(symmetric.polygons, point(0.0, -10.0)));
	BOOST_CHECK(any_contains(symmetric.polygons, point(0.0, 15.0)));
	BOOST_CHECK(!any_contains(symmetric.polygons, point(0.0, 5.0)));

	BooleanGeometry::polygon_seq_type disjoint;
	disjoint.push_back(rectangle(-10.0, 10.0, 30.0, 40.0));
	const BooleanGeometry::BooleanResult empty = BooleanGeometry::apply_polygon_boolean(
			*first_polygon(), disjoint, BooleanGeometry::POLYGON_INTERSECTION);
	BOOST_REQUIRE(empty.success);
	BOOST_CHECK(empty.polygons.empty());
}
