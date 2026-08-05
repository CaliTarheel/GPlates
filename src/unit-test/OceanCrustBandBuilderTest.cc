/* $Id$ */

#include "unit-test/OceanCrustBandBuilderTest.h"

#include <vector>

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "view-operations/OceanCrustBandBuilder.h"


namespace
{
	namespace BandBuilder = GPlatesViewOperations::OceanCrustBandBuilder;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	BandBuilder::polygon_ptr_type rectangle(
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
			const BandBuilder::polygon_seq_type &polygons,
			const GPlatesMaths::PointOnSphere &sample)
	{
		for (BandBuilder::polygon_seq_type::const_iterator polygon_iter =
				polygons.begin(); polygon_iter != polygons.end(); ++polygon_iter)
		{
			if ((*polygon_iter)->is_point_in_polygon(sample))
			{
				return true;
			}
		}
		return false;
	}
}


GPlatesUnitTest::OceanCrustBandBuilderTestSuite::OceanCrustBandBuilderTestSuite(
		unsigned level) :
	GPlatesTestSuite("OceanCrustBandBuilderTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::OceanCrustBandBuilderTestSuite::construct_maps()
{
	boost::shared_ptr<OceanCrustBandBuilderTest> instance(
			new OceanCrustBandBuilderTest());
	ADD_TESTCASE(OceanCrustBandBuilderTest, test_subtracts_existing_crust);
	ADD_TESTCASE(OceanCrustBandBuilderTest, test_disjoint_and_fully_filled_bands);
}


void
GPlatesUnitTest::OceanCrustBandBuilderTest::test_subtracts_existing_crust()
{
	const BandBuilder::polygon_ptr_type candidate = rectangle(-10.0, 10.0, -20.0, 20.0);
	BandBuilder::polygon_seq_type existing;
	existing.push_back(rectangle(-10.0, 10.0, 0.0, 20.0));
	const BandBuilder::Result result =
			BandBuilder::subtract_existing_crust(*candidate, existing);
	BOOST_REQUIRE(result.success);
	BOOST_CHECK(result.existing_overlap_removed);
	BOOST_REQUIRE_EQUAL(result.polygons.size(), 1);
	BOOST_CHECK(any_contains(result.polygons, point(0.0, -10.0)));
	BOOST_CHECK(!any_contains(result.polygons, point(0.0, 10.0)));
}


void
GPlatesUnitTest::OceanCrustBandBuilderTest::test_disjoint_and_fully_filled_bands()
{
	const BandBuilder::polygon_ptr_type candidate = rectangle(-10.0, 10.0, -20.0, 20.0);
	BandBuilder::polygon_seq_type disjoint;
	disjoint.push_back(rectangle(-10.0, 10.0, 30.0, 40.0));
	const BandBuilder::Result unchanged =
			BandBuilder::subtract_existing_crust(*candidate, disjoint);
	BOOST_REQUIRE(unchanged.success);
	BOOST_CHECK(!unchanged.existing_overlap_removed);
	BOOST_REQUIRE_EQUAL(unchanged.polygons.size(), 1);

	BandBuilder::polygon_seq_type covering;
	covering.push_back(rectangle(-15.0, 15.0, -25.0, 25.0));
	const BandBuilder::Result empty =
			BandBuilder::subtract_existing_crust(*candidate, covering);
	BOOST_REQUIRE(empty.success);
	BOOST_CHECK(empty.existing_overlap_removed);
	BOOST_CHECK(empty.polygons.empty());
}
