/* $Id$ */

#include "unit-test/InitialRiftGeometryTest.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "maths/PolylineOnSphere.h"
#include "maths/UnitVector3D.h"
#include "view-operations/InitialContinentGeometry.h"
#include "view-operations/InitialRiftGeometry.h"


namespace
{
	namespace ContinentGeometry = GPlatesViewOperations::InitialContinentGeometry;
	namespace RiftGeometry = GPlatesViewOperations::InitialRiftGeometry;

	ContinentGeometry::Result
	make_source()
	{
		ContinentGeometry::Parameters parameters;
		parameters.random_seed = 24680;
		parameters.coastline_swiggle = 0.72;
		return ContinentGeometry::generate(parameters);
	}

	void
	check_points_equal(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		BOOST_CHECK_EQUAL(lhs.position_vector().x().dval(), rhs.position_vector().x().dval());
		BOOST_CHECK_EQUAL(lhs.position_vector().y().dval(), rhs.position_vector().y().dval());
		BOOST_CHECK_EQUAL(lhs.position_vector().z().dval(), rhs.position_vector().z().dval());
	}

	void
	check_polylines_equal(
			const GPlatesMaths::PolylineOnSphere &lhs,
			const GPlatesMaths::PolylineOnSphere &rhs)
	{
		BOOST_REQUIRE_EQUAL(lhs.number_of_vertices(), rhs.number_of_vertices());
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator lhs_iter = lhs.vertex_begin();
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator rhs_iter = rhs.vertex_begin();
		for (; lhs_iter != lhs.vertex_end(); ++lhs_iter, ++rhs_iter)
		{
			check_points_equal(*lhs_iter, *rhs_iter);
		}
	}

	GPlatesMaths::PointOnSphere
	segment_midpoint(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end)
	{
		const double x = start.position_vector().x().dval() + end.position_vector().x().dval();
		const double y = start.position_vector().y().dval() + end.position_vector().y().dval();
		const double z = start.position_vector().z().dval() + end.position_vector().z().dval();
		const double length = std::sqrt(x * x + y * y + z * z);
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(x / length, y / length, z / length));
	}

	GPlatesMaths::PointOnSphere
	make_point(double x, double y, double z)
	{
		const double length = std::sqrt(x * x + y * y + z * z);
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(x / length, y / length, z / length));
	}

	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type
	make_line(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(start);
		points.push_back(end);
		return GPlatesMaths::PolylineOnSphere::create(points, true);
	}
}


GPlatesUnitTest::InitialRiftGeometryTestSuite::InitialRiftGeometryTestSuite(
		unsigned level) :
	GPlatesTestSuite("InitialRiftGeometryTestSuite")
{
	init(level);
}


void
GPlatesUnitTest::InitialRiftGeometryTestSuite::construct_maps()
{
	boost::shared_ptr<InitialRiftGeometryTest> instance(new InitialRiftGeometryTest());
	ADD_TESTCASE(InitialRiftGeometryTest, test_deterministic_network);
	ADD_TESTCASE(InitialRiftGeometryTest, test_network_is_clipped_to_continent);
	ADD_TESTCASE(InitialRiftGeometryTest, test_network_excludes_cratons);
	ADD_TESTCASE(InitialRiftGeometryTest, test_voronoi_network_branches);
	ADD_TESTCASE(InitialRiftGeometryTest, test_selected_edges_join_into_one_polyline);
	ADD_TESTCASE(InitialRiftGeometryTest, test_spacing_controls_network_scale);
	ADD_TESTCASE(InitialRiftGeometryTest, test_artifexia_segment_quality);
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_selected_edges_join_into_one_polyline()
{
	const GPlatesMaths::PointOnSphere p0 = make_point(1.0, 0.0, 0.0);
	const GPlatesMaths::PointOnSphere p1 = make_point(1.0, 0.1, 0.0);
	const GPlatesMaths::PointOnSphere p2 = make_point(1.0, 0.2, 0.02);
	const GPlatesMaths::PointOnSphere p3 = make_point(1.0, 0.3, 0.04);
	std::vector<GPlatesMaths::PointOnSphere> nodes;
	nodes.push_back(p0);
	nodes.push_back(p1);
	nodes.push_back(p2);
	nodes.push_back(p3);

	std::vector<RiftGeometry::Edge> edges;
	edges.push_back(RiftGeometry::Edge(make_line(p0, p1), 1.0, false, 0, 1));
	edges.push_back(RiftGeometry::Edge(make_line(p1, p2), 1.0, false, 1, 2));
	// Store the final edge in reverse orientation to verify that joining reorients it.
	edges.push_back(RiftGeometry::Edge(make_line(p3, p2), 1.0, false, 3, 2));
	const RiftGeometry::Result network(
			edges,
			std::vector<GPlatesMaths::PointOnSphere>(),
			nodes,
			RiftGeometry::Metrics());
	std::vector<unsigned int> selected_edges;
	selected_edges.push_back(2);
	selected_edges.push_back(0);
	selected_edges.push_back(1);

	const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> paths =
			RiftGeometry::join_edge_paths(network, selected_edges);
	BOOST_REQUIRE_EQUAL(paths.size(), 1);
	BOOST_REQUIRE_EQUAL(paths[0]->number_of_vertices(), 4);
	GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = paths[0]->vertex_begin();
	check_points_equal(*vertex_iter++, p0);
	check_points_equal(*vertex_iter++, p1);
	check_points_equal(*vertex_iter++, p2);
	check_points_equal(*vertex_iter, p3);
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_network_excludes_cratons()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters parameters;
	parameters.random_seed = 112358;
	const RiftGeometry::Result unculled = RiftGeometry::generate(source.continent, parameters);
	const RiftGeometry::Result culled = RiftGeometry::generate(
			source.continent, source.cratons, parameters);

	BOOST_REQUIRE_GT(culled.metrics.craton_intersection_count, 0);
	BOOST_CHECK_LT(culled.edges.size(), unculled.edges.size());
	for (std::vector<RiftGeometry::Edge>::const_iterator edge_iter = culled.edges.begin();
		edge_iter != culled.edges.end(); ++edge_iter)
	{
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = edge_iter->polyline->vertex_begin();
		GPlatesMaths::PointOnSphere previous = *vertex_iter;
		for (++vertex_iter; vertex_iter != edge_iter->polyline->vertex_end(); ++vertex_iter)
		{
			const GPlatesMaths::PointOnSphere midpoint = segment_midpoint(previous, *vertex_iter);
			for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
					craton_iter = source.cratons.begin(); craton_iter != source.cratons.end(); ++craton_iter)
			{
				BOOST_CHECK(!(*craton_iter)->is_point_in_polygon(midpoint));
			}
			previous = *vertex_iter;
		}
	}
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_deterministic_network()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters parameters;
	parameters.random_seed = 97531;
	const RiftGeometry::Result first = RiftGeometry::generate(source.continent, parameters);
	const RiftGeometry::Result second = RiftGeometry::generate(source.continent, parameters);

	BOOST_REQUIRE_EQUAL(first.sites.size(), second.sites.size());
	BOOST_REQUIRE_EQUAL(first.edges.size(), second.edges.size());
	for (unsigned int site_index = 0; site_index < first.sites.size(); ++site_index)
	{
		check_points_equal(first.sites[site_index], second.sites[site_index]);
	}
	for (unsigned int edge_index = 0; edge_index < first.edges.size(); ++edge_index)
	{
		check_polylines_equal(*first.edges[edge_index].polyline, *second.edges[edge_index].polyline);
		BOOST_CHECK_EQUAL(first.edges[edge_index].start_node, second.edges[edge_index].start_node);
		BOOST_CHECK_EQUAL(first.edges[edge_index].end_node, second.edges[edge_index].end_node);
		BOOST_CHECK_EQUAL(first.edges[edge_index].touches_coast, second.edges[edge_index].touches_coast);
	}
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_network_is_clipped_to_continent()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters parameters;
	parameters.random_seed = 112358;
	const RiftGeometry::Result result = RiftGeometry::generate(source.continent, parameters);

	unsigned int coastal_edges = 0;
	BOOST_REQUIRE_GT(result.edges.size(), 12);
	for (std::vector<RiftGeometry::Edge>::const_iterator edge_iter = result.edges.begin();
		edge_iter != result.edges.end(); ++edge_iter)
	{
		if (edge_iter->touches_coast)
		{
			++coastal_edges;
		}
		for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
				edge_iter->polyline->vertex_begin();
			vertex_iter != edge_iter->polyline->vertex_end(); ++vertex_iter)
		{
			BOOST_CHECK(source.continent->is_point_in_polygon(*vertex_iter));
		}
	}
	BOOST_CHECK_GT(coastal_edges, 2);
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_voronoi_network_branches()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters parameters;
	parameters.random_seed = 314159;
	const RiftGeometry::Result result = RiftGeometry::generate(source.continent, parameters);

	std::vector<unsigned int> node_degrees(result.nodes.size(), 0);
	for (std::vector<RiftGeometry::Edge>::const_iterator edge_iter = result.edges.begin();
		edge_iter != result.edges.end(); ++edge_iter)
	{
		BOOST_REQUIRE_LT(edge_iter->start_node, node_degrees.size());
		BOOST_REQUIRE_LT(edge_iter->end_node, node_degrees.size());
		++node_degrees[edge_iter->start_node];
		++node_degrees[edge_iter->end_node];
	}
	const unsigned int branch_nodes = static_cast<unsigned int>(std::count_if(
			node_degrees.begin(), node_degrees.end(),
			[](unsigned int degree) { return degree >= 3; }));
	BOOST_CHECK_GT(branch_nodes, 3);
	BOOST_CHECK_GT(result.metrics.site_count, 8);
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_spacing_controls_network_scale()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters fine_parameters;
	fine_parameters.random_seed = 271828;
	fine_parameters.target_cell_spacing_km = 1200.0;
	RiftGeometry::Parameters broad_parameters(fine_parameters);
	broad_parameters.target_cell_spacing_km = 2600.0;

	const RiftGeometry::Result fine = RiftGeometry::generate(source.continent, fine_parameters);
	const RiftGeometry::Result broad = RiftGeometry::generate(source.continent, broad_parameters);
	BOOST_CHECK_GT(fine.metrics.site_count, broad.metrics.site_count);
	BOOST_CHECK_GT(fine.metrics.edge_count, broad.metrics.edge_count);
	BOOST_CHECK_LT(fine.metrics.average_edge_length_km, broad.metrics.average_edge_length_km);
}


void
GPlatesUnitTest::InitialRiftGeometryTest::test_artifexia_segment_quality()
{
	const ContinentGeometry::Result source = make_source();
	RiftGeometry::Parameters parameters;
	parameters.maximum_segment_length_km = 130.0;
	parameters.random_seed = 161803;
	const RiftGeometry::Result result = RiftGeometry::generate(source.continent, parameters);

	// Artifexia's active failed-rift segments average 146.8 km at 1000 Ma;
	// keep the selectable network tessellation at least five percent finer.
	BOOST_CHECK_LE(result.metrics.maximum_segment_length_km, 146.8 * 0.95);
}
