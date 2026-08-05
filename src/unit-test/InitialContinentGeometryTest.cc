/* $Id$ */

#include "unit-test/InitialContinentGeometryTest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "maths/GeometryDistance.h"
#include "maths/PolygonOnSphere.h"
#include "view-operations/InitialContinentGeometry.h"


namespace
{
	namespace InitialGeometry = GPlatesViewOperations::InitialContinentGeometry;

	void
	check_polygons_equal(
			const GPlatesMaths::PolygonOnSphere &polygon1,
			const GPlatesMaths::PolygonOnSphere &polygon2)
	{
		BOOST_REQUIRE_EQUAL(
				polygon1.number_of_vertices_in_exterior_ring(),
				polygon2.number_of_vertices_in_exterior_ring());
		GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex1 =
				polygon1.exterior_ring_vertex_begin();
		GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex2 =
				polygon2.exterior_ring_vertex_begin();
		for (; vertex1 != polygon1.exterior_ring_vertex_end(); ++vertex1, ++vertex2)
		{
			BOOST_CHECK_EQUAL(
					vertex1->position_vector().x().dval(),
					vertex2->position_vector().x().dval());
			BOOST_CHECK_EQUAL(
					vertex1->position_vector().y().dval(),
					vertex2->position_vector().y().dval());
			BOOST_CHECK_EQUAL(
					vertex1->position_vector().z().dval(),
					vertex2->position_vector().z().dval());
		}
	}

	double
	polygon_perimeter_radians(
			const GPlatesMaths::PolygonOnSphere &polygon)
	{
		double perimeter = 0;
		for (GPlatesMaths::PolygonOnSphere::ring_const_iterator segment_iter =
					polygon.exterior_ring_begin();
				segment_iter != polygon.exterior_ring_end();
				++segment_iter)
		{
			perimeter += segment_iter->arc_length().dval();
		}
		return perimeter;
	}
}


GPlatesUnitTest::InitialContinentGeometryTestSuite::InitialContinentGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("InitialContinentGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::InitialContinentGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<InitialContinentGeometryTest> instance(new InitialContinentGeometryTest());
	ADD_TESTCASE(InitialContinentGeometryTest, test_deterministic_generation);
	ADD_TESTCASE(InitialContinentGeometryTest, test_worldbuilding_pasta_craton_range);
	ADD_TESTCASE(InitialContinentGeometryTest, test_area_and_segment_quality);
	ADD_TESTCASE(InitialContinentGeometryTest, test_coastline_swiggle_control);
	ADD_TESTCASE(InitialContinentGeometryTest, test_craton_shape_variation);
	ADD_TESTCASE(InitialContinentGeometryTest, test_packing_controls_craton_fill);
	ADD_TESTCASE(InitialContinentGeometryTest, test_cratons_are_contained_and_separate);
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_deterministic_generation()
{
	InitialGeometry::Parameters parameters;
	parameters.random_seed = 8675309;
	const InitialGeometry::Result result1 = InitialGeometry::generate(parameters);
	const InitialGeometry::Result result2 = InitialGeometry::generate(parameters);

	BOOST_REQUIRE_EQUAL(result1.cratons.size(), result2.cratons.size());
	check_polygons_equal(*result1.continent, *result2.continent);
	for (unsigned int index = 0; index < result1.cratons.size(); ++index)
	{
		check_polygons_equal(*result1.cratons[index], *result2.cratons[index]);
	}
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_worldbuilding_pasta_craton_range()
{
	InitialGeometry::Parameters parameters;
	parameters.random_seed = 42;
	parameters.craton_count = 8;
	parameters.packing = 0.0;
	parameters.continent_area_fraction = 0.05;
	BOOST_CHECK_EQUAL(InitialGeometry::generate(parameters).cratons.size(), 8);
	parameters.craton_count = 12;
	parameters.packing = 1.0;
	parameters.continent_area_fraction = 0.45;
	BOOST_CHECK_EQUAL(InitialGeometry::generate(parameters).cratons.size(), 12);

	parameters.craton_count = 7;
	BOOST_CHECK_THROW(InitialGeometry::generate(parameters), std::invalid_argument);
	parameters.craton_count = 13;
	BOOST_CHECK_THROW(InitialGeometry::generate(parameters), std::invalid_argument);
	parameters.craton_count = 10;
	parameters.coastline_swiggle = 1.01;
	BOOST_CHECK_THROW(InitialGeometry::generate(parameters), std::invalid_argument);
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_area_and_segment_quality()
{
	InitialGeometry::Parameters parameters;
	parameters.continent_area_fraction = 0.25;
	parameters.maximum_segment_length_km = 180.0;
	parameters.random_seed = 314159;
	const InitialGeometry::Result result = InitialGeometry::generate(parameters);

	BOOST_CHECK_SMALL(result.metrics.continent_area_fraction - 0.25, 1e-8);
	BOOST_CHECK_LE(result.metrics.maximum_segment_length_km, 180.0 * (1.0 + 1e-9));
	// Artifexia's active ContinentalCrust features average 196.3 km per
	// segment at 1000 Ma. Stay at least 5% finer at the default quality.
	BOOST_CHECK_LE(result.metrics.average_continent_segment_length_km, 196.3 * 0.95);
	BOOST_CHECK_GT(result.metrics.continent_vertex_count, 240);
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_coastline_swiggle_control()
{
	InitialGeometry::Parameters smooth_parameters;
	smooth_parameters.coastline_swiggle = 0.0;
	smooth_parameters.random_seed = 424242;
	InitialGeometry::Parameters wild_parameters = smooth_parameters;
	wild_parameters.coastline_swiggle = 1.0;

	const InitialGeometry::Result smooth = InitialGeometry::generate(smooth_parameters);
	const InitialGeometry::Result wild = InitialGeometry::generate(wild_parameters);
	BOOST_CHECK_SMALL(smooth.metrics.continent_area_fraction - wild.metrics.continent_area_fraction, 1e-8);
	BOOST_CHECK_GT(wild.metrics.continent_perimeter_km, 1.02 * smooth.metrics.continent_perimeter_km);
	BOOST_CHECK_LE(wild.metrics.maximum_continent_segment_length_km,
			wild_parameters.maximum_segment_length_km * (1.0 + 1e-9));
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_craton_shape_variation()
{
	InitialGeometry::Parameters parameters;
	parameters.random_seed = 13579;
	const InitialGeometry::Result result = InitialGeometry::generate(parameters);

	double minimum_shape_index = std::numeric_limits<double>::max();
	double maximum_shape_index = 0;
	for (unsigned int index = 0; index < result.cratons.size(); ++index)
	{
		const double shape_index = polygon_perimeter_radians(*result.cratons[index]) /
				std::sqrt(result.cratons[index]->get_area().dval());
		minimum_shape_index = std::min(minimum_shape_index, shape_index);
		maximum_shape_index = std::max(maximum_shape_index, shape_index);
	}
	BOOST_CHECK_GT(maximum_shape_index - minimum_shape_index, 0.15);
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_packing_controls_craton_fill()
{
	InitialGeometry::Parameters loose_parameters;
	loose_parameters.packing = 0.0;
	loose_parameters.random_seed = 271828;
	InitialGeometry::Parameters tight_parameters = loose_parameters;
	tight_parameters.packing = 1.0;

	const InitialGeometry::Result loose = InitialGeometry::generate(loose_parameters);
	const InitialGeometry::Result tight = InitialGeometry::generate(tight_parameters);
	BOOST_CHECK_SMALL(loose.metrics.craton_fill_fraction - 0.18, 1e-8);
	BOOST_CHECK_SMALL(tight.metrics.craton_fill_fraction - 0.34, 1e-8);
	BOOST_CHECK_GT(tight.metrics.craton_fill_fraction, loose.metrics.craton_fill_fraction);
}


void
GPlatesUnitTest::InitialContinentGeometryTest::test_cratons_are_contained_and_separate()
{
	InitialGeometry::Parameters parameters;
	parameters.craton_count = 12;
	parameters.packing = 1.0;
	parameters.random_seed = 1618033;
	const InitialGeometry::Result result = InitialGeometry::generate(parameters);

	for (unsigned int craton_index = 0; craton_index < result.cratons.size(); ++craton_index)
	{
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
					result.cratons[craton_index]->exterior_ring_vertex_begin();
				vertex_iter != result.cratons[craton_index]->exterior_ring_vertex_end();
				++vertex_iter)
		{
			BOOST_CHECK(result.continent->is_point_in_polygon(*vertex_iter));
		}

		for (unsigned int other_index = craton_index + 1;
				other_index < result.cratons.size();
				++other_index)
		{
			const double separation = GPlatesMaths::minimum_distance(
					*result.cratons[craton_index],
					*result.cratons[other_index],
					true,
					true).calculate_angle().dval();
			BOOST_CHECK_GT(separation, 1e-9);
		}
	}
}
