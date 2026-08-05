/* $Id$ */

#include "unit-test/SubductionEffectsGeometryTest.h"

#include <vector>

#include "maths/GeometryDistance.h"
#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "view-operations/SubductionEffectsGeometry.h"


namespace
{
	namespace Effects = GPlatesViewOperations::SubductionEffectsGeometry;

	GPlatesMaths::PointOnSphere point(double latitude, double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(
				GPlatesMaths::LatLonPoint(latitude, longitude));
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type make_eastward_trench()
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(point(0, -24));
		points.push_back(point(1.5, -12));
		points.push_back(point(0, 0));
		points.push_back(point(-1.5, 12));
		points.push_back(point(0, 24));
		return GPlatesMaths::PolylineOnSphere::create(points);
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type make_northern_continent()
	{
		std::vector<GPlatesMaths::PointOnSphere> ring;
		ring.push_back(point(1, -30));
		ring.push_back(point(20, -30));
		ring.push_back(point(24, 0));
		ring.push_back(point(20, 30));
		ring.push_back(point(1, 30));
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}
}


GPlatesUnitTest::SubductionEffectsGeometryTestSuite::SubductionEffectsGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("SubductionEffectsGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::SubductionEffectsGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<SubductionEffectsGeometryTest> instance(new SubductionEffectsGeometryTest());
	ADD_TESTCASE(SubductionEffectsGeometryTest, test_polarity_probe_and_contained_andean_belt);
	ADD_TESTCASE(SubductionEffectsGeometryTest, test_island_arc_line_and_land_intersection_mountains);
	ADD_TESTCASE(SubductionEffectsGeometryTest, test_trim_controls_and_laramide_width);
}


void
GPlatesUnitTest::SubductionEffectsGeometryTest::test_polarity_probe_and_contained_andean_belt()
{
	const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type trench = make_eastward_trench();
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent = make_northern_continent();
	const double left_fraction = Effects::calculate_overriding_containment_fraction(
			trench, continent, true, 180.0);
	const double right_fraction = Effects::calculate_overriding_containment_fraction(
			trench, continent, false, 180.0);
	// The bowed test trench extends beyond the polygon at each end, but the
	// left side must still be strongly preferred over the right side.
	BOOST_CHECK_GT(left_fraction, 0.60);
	BOOST_CHECK_LT(right_fraction, 0.20);

	Effects::Parameters parameters;
	parameters.effect_type = Effects::ANDEAN_OROGENY;
	parameters.overriding_side_is_left = true;
	parameters.trench_to_effect_offset_km = 220.0;
	parameters.belt_width_km = 100.0;
	const Effects::Result result = Effects::generate(trench, parameters, continent);
	BOOST_REQUIRE_EQUAL(result.polygons.size(), 1u);
	BOOST_CHECK(result.guide);
	for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
			result.polygons.front()->exterior_ring_vertex_begin();
		vertex_iter != result.polygons.front()->exterior_ring_vertex_end(); ++vertex_iter)
	{
		BOOST_CHECK(continent->is_point_in_polygon(*vertex_iter));
	}
}


void
GPlatesUnitTest::SubductionEffectsGeometryTest::test_island_arc_line_and_land_intersection_mountains()
{
	Effects::Parameters parameters;
	parameters.effect_type = Effects::ISLAND_ARC;
	parameters.overriding_side_is_left = true;
	parameters.island_irregularity = 0.25;
	const Effects::Result first = Effects::generate(make_eastward_trench(), parameters);
	const Effects::Result second = Effects::generate(make_eastward_trench(), parameters);
	BOOST_CHECK(first.polygons.empty());
	BOOST_REQUIRE(first.guide);
	BOOST_REQUIRE(second.guide);
	BOOST_CHECK_EQUAL((*first.guide)->number_of_vertices(), (*second.guide)->number_of_vertices());
	BOOST_CHECK_EQUAL(first.metrics.proposal_vertex_count, second.metrics.proposal_vertex_count);
	BOOST_CHECK_EQUAL(first.metrics.island_count, 1u);
	BOOST_CHECK_LE(first.metrics.maximum_segment_length_km, parameters.maximum_segment_length_km + 5.0);

	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent =
			make_northern_continent();
	const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> belts =
			Effects::generate_land_intersection_belts(*first.guide, continent, 100.0);
	BOOST_REQUIRE(!belts.empty());
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
			belt_iter = belts.begin(); belt_iter != belts.end(); ++belt_iter)
	{
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				(*belt_iter)->exterior_ring_vertex_begin();
				vertex_iter != (*belt_iter)->exterior_ring_vertex_end(); ++vertex_iter)
		{
			BOOST_CHECK(continent->is_point_in_polygon(*vertex_iter));
		}
	}
}


void
GPlatesUnitTest::SubductionEffectsGeometryTest::test_trim_controls_and_laramide_width()
{
	Effects::Parameters full_parameters;
	full_parameters.effect_type = Effects::ANDEAN_OROGENY;
	full_parameters.overriding_side_is_left = true;
	full_parameters.trench_to_effect_offset_km = 260.0;
	full_parameters.belt_width_km = 100.0;
	const Effects::Result full = Effects::generate(
			make_eastward_trench(), full_parameters, make_northern_continent());

	Effects::Parameters trimmed_parameters = full_parameters;
	trimmed_parameters.effect_type = Effects::LARAMIDE_OROGENY;
	trimmed_parameters.trim_start_percent = 20.0;
	trimmed_parameters.trim_end_percent = 20.0;
	trimmed_parameters.belt_width_km = 350.0;
	const Effects::Result trimmed = Effects::generate(
			make_eastward_trench(), trimmed_parameters, make_northern_continent());
	BOOST_CHECK_CLOSE(trimmed.metrics.selected_length_km,
			0.60 * trimmed.metrics.source_length_km, 1e-6);
	BOOST_CHECK_LT(trimmed.metrics.proposal_vertex_count, full.metrics.proposal_vertex_count);
	// The cross-belt closing edge records the requested broad Laramide scale.
	BOOST_CHECK_GT(trimmed.metrics.maximum_segment_length_km, 300.0);
}
