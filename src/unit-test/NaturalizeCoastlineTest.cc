/* $Id$ */

#include "unit-test/NaturalizeCoastlineTest.h"

#include <cmath>

#include "maths/GeometryIntersect.h"
#include "maths/GreatCircleArc.h"
#include "maths/LatLonPoint.h"
#include "maths/MathsUtils.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/NaturalizeCoastlineGeometry.h"


namespace
{
	namespace NaturalizeGeometry = GPlatesViewOperations::NaturalizeCoastlineGeometry;

	GPlatesMaths::PointOnSphere
	point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	bool
	has_self_intersection(
			const GPlatesViewOperations::NaturalizeCoastlineGeometry::point_seq_type &ring)
	{
		GPlatesViewOperations::NaturalizeCoastlineGeometry::point_seq_type closed_ring(ring);
		closed_ring.push_back(ring.front());
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline1 =
				GPlatesMaths::PolylineOnSphere::create(closed_ring);
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline2 =
				GPlatesMaths::PolylineOnSphere::create(closed_ring);
		GPlatesMaths::GeometryIntersect::Graph graph;
		if (!GPlatesMaths::GeometryIntersect::intersect(graph, *polyline1, *polyline2))
		{
			return false;
		}
		const unsigned int segment_count = polyline1->number_of_segments();
		const unsigned int last_segment = segment_count - 1;
		for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator
				intersection_iter = graph.unordered_intersections.begin();
				intersection_iter != graph.unordered_intersections.end(); ++intersection_iter)
		{
			const unsigned int segment1 = intersection_iter->segment_index1 == segment_count
					? 0 : intersection_iter->segment_index1;
			const unsigned int segment2 = intersection_iter->segment_index2 == segment_count
					? 0 : intersection_iter->segment_index2;
			if (segment1 == segment2 || segment1 + 1 == segment2 || segment2 + 1 == segment1 ||
					(segment1 == 0 && segment2 == last_segment) ||
					(segment2 == 0 && segment1 == last_segment))
			{
				continue;
			}
			return true;
		}
		return false;
	}


	double
	maximum_lateral_offset_km(
			const NaturalizeGeometry::point_seq_type &points,
			const GPlatesMaths::GreatCircleArc &original_arc,
			double planet_radius_km)
	{
		double maximum_offset = 0.0;
		for (NaturalizeGeometry::point_seq_type::const_iterator point_iter = points.begin();
				point_iter != points.end(); ++point_iter)
		{
			const double sine_offset = std::min(1.0, std::fabs(GPlatesMaths::dot(
					point_iter->position_vector(), original_arc.rotation_axis()).dval()));
			maximum_offset = std::max(maximum_offset, std::asin(sine_offset) * planet_radius_km);
		}
		return maximum_offset;
	}
}


GPlatesUnitTest::NaturalizeCoastlineTestSuite::NaturalizeCoastlineTestSuite(
		unsigned level) :
	GPlatesTestSuite("NaturalizeCoastlineTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::NaturalizeCoastlineTestSuite::construct_maps()
{
	boost::shared_ptr<NaturalizeCoastlineTest> instance(new NaturalizeCoastlineTest());
	ADD_TESTCASE(NaturalizeCoastlineTest, test_deterministic_and_bounded);
	ADD_TESTCASE(NaturalizeCoastlineTest, test_direction_independent);
	ADD_TESTCASE(NaturalizeCoastlineTest, test_shared_segment_detection);
	ADD_TESTCASE(NaturalizeCoastlineTest, test_closed_polygon_ring);
	ADD_TESTCASE(NaturalizeCoastlineTest, test_enhanced_amplitude);
}


void
GPlatesUnitTest::NaturalizeCoastlineTest::test_deterministic_and_bounded()
{
	GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 100.0;
	parameters.amplitude_percent = 20.0;
	parameters.wavelength_km = 350.0;
	parameters.random_seed = 8675309;

	const GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult result1 =
			GPlatesViewOperations::NaturalizeCoastlineGeometry::naturalize_segment(
					point(0.0, 0.0), point(0.0, 10.0), parameters);
	const GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult result2 =
			GPlatesViewOperations::NaturalizeCoastlineGeometry::naturalize_segment(
					point(0.0, 0.0), point(0.0, 10.0), parameters);

	BOOST_REQUIRE(result1.points.size() > 2);
	BOOST_REQUIRE_EQUAL(result1.points.size(), result2.points.size());
	BOOST_CHECK_LE(result1.maximum_segment_length_km,
			parameters.maximum_segment_length_km * (1.0 + 1e-9));
	for (unsigned int point_index = 0; point_index < result1.points.size(); ++point_index)
	{
		BOOST_CHECK_EQUAL(
				result1.points[point_index].position_vector().x().dval(),
				result2.points[point_index].position_vector().x().dval());
		BOOST_CHECK_EQUAL(
				result1.points[point_index].position_vector().y().dval(),
				result2.points[point_index].position_vector().y().dval());
		BOOST_CHECK_EQUAL(
				result1.points[point_index].position_vector().z().dval(),
				result2.points[point_index].position_vector().z().dval());
	}
}


void
GPlatesUnitTest::NaturalizeCoastlineTest::test_direction_independent()
{
	GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 75.0;
	parameters.random_seed = 42;
	const GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult forward =
			GPlatesViewOperations::NaturalizeCoastlineGeometry::naturalize_segment(
					point(-10.0, 20.0), point(5.0, 35.0), parameters);
	const GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult reverse =
			GPlatesViewOperations::NaturalizeCoastlineGeometry::naturalize_segment(
					point(5.0, 35.0), point(-10.0, 20.0), parameters);

	BOOST_REQUIRE_EQUAL(forward.points.size(), reverse.points.size());
	for (unsigned int point_index = 0; point_index < forward.points.size(); ++point_index)
	{
		const GPlatesMaths::UnitVector3D &forward_vector =
				forward.points[point_index].position_vector();
		const GPlatesMaths::UnitVector3D &reverse_vector =
				reverse.points[reverse.points.size() - 1 - point_index].position_vector();
		BOOST_CHECK_EQUAL(forward_vector.x().dval(), reverse_vector.x().dval());
		BOOST_CHECK_EQUAL(forward_vector.y().dval(), reverse_vector.y().dval());
		BOOST_CHECK_EQUAL(forward_vector.z().dval(), reverse_vector.z().dval());
	}
}


void
GPlatesUnitTest::NaturalizeCoastlineTest::test_shared_segment_detection()
{
	const GPlatesMaths::PointOnSphere a = point(0.0, 0.0);
	const GPlatesMaths::PointOnSphere b = point(0.0, 10.0);
	const GPlatesMaths::PointOnSphere middle = point(0.0, 5.0);
	const GPlatesMaths::PointOnSphere c = point(0.0, 15.0);
	bool reversed = false;
	BOOST_CHECK(GPlatesViewOperations::NaturalizeCoastlineGeometry::segments_match(
			a, b, b, a, 0.01, 6371.0, reversed));
	BOOST_CHECK(reversed);
	BOOST_CHECK(GPlatesViewOperations::NaturalizeCoastlineGeometry::segments_partially_overlap(
			a, b, a, middle, 0.01, 6371.0));
	BOOST_CHECK(!GPlatesViewOperations::NaturalizeCoastlineGeometry::segments_partially_overlap(
			a, b, b, c, 0.01, 6371.0));
}


void
GPlatesUnitTest::NaturalizeCoastlineTest::test_closed_polygon_ring()
{
	NaturalizeGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 100.0;
	parameters.amplitude_percent = 1.0;
	parameters.wavelength_km = 400.0;
	parameters.smoothing_passes = 2;
	parameters.random_seed = 12345;

	NaturalizeGeometry::point_seq_type original;
	original.push_back(point(-10.0, -20.0));
	original.push_back(point(-10.0, 0.0));
	original.push_back(point(10.0, 0.0));
	original.push_back(point(10.0, -20.0));

	NaturalizeGeometry::point_seq_type naturalized;
	naturalized.push_back(original.front());
	for (unsigned int segment_index = 0; segment_index < original.size(); ++segment_index)
	{
		const NaturalizeGeometry::SegmentResult segment = NaturalizeGeometry::naturalize_segment(
				original[segment_index], original[(segment_index + 1) % original.size()], parameters);
		BOOST_REQUIRE(segment.points.size() > 2);
		naturalized.insert(naturalized.end(), segment.points.begin() + 1, segment.points.end());
	}
	BOOST_REQUIRE(naturalized.front() == naturalized.back());
	naturalized.pop_back();

	BOOST_CHECK_EQUAL(
			GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(naturalized, true),
			GPlatesMaths::PolygonOnSphere::VALID);
	BOOST_CHECK(!has_self_intersection(naturalized));
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon =
			GPlatesMaths::PolygonOnSphere::create(naturalized, true);
	BOOST_CHECK(polygon->get_area().dval() > 0.0);
}


void
GPlatesUnitTest::NaturalizeCoastlineTest::test_enhanced_amplitude()
{
	NaturalizeGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 200.0;
	parameters.amplitude_percent = 1.0;
	parameters.wavelength_km = 1000.0;
	parameters.smoothing_passes = 2;
	parameters.random_seed = 8675309;

	const GPlatesMaths::PointOnSphere start = point(0.0, 0.0);
	const GPlatesMaths::PointOnSphere end = point(0.0, 10.0);
	const NaturalizeGeometry::SegmentResult result =
			NaturalizeGeometry::naturalize_segment(start, end, parameters);
	const double maximum_offset_km = maximum_lateral_offset_km(
			result.points, GPlatesMaths::GreatCircleArc::create(start, end),
			parameters.planet_radius_km);

	// Without the enhanced 10x scale, a 1% setting could never exceed 2 km
	// for this 200 km segment limit.
	BOOST_CHECK_GT(maximum_offset_km, 2.0);
}
