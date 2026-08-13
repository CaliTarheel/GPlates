/* $Id$ */

/**
 * \file
 * Deterministic, continent-clipped Voronoi networks for rift planning.
 */

#include "InitialRiftGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include "maths/UnitVector3D.h"


namespace
{
	struct Vec2
	{
		Vec2() : x(0), y(0) { }
		Vec2(double x_, double y_) : x(x_), y(y_) { }

		double x;
		double y;
	};

	struct Vec3
	{
		Vec3() : x(0), y(0), z(0) { }
		Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) { }

		double x;
		double y;
		double z;
	};

	Vec2 operator+(const Vec2 &lhs, const Vec2 &rhs) { return Vec2(lhs.x + rhs.x, lhs.y + rhs.y); }
	Vec2 operator-(const Vec2 &lhs, const Vec2 &rhs) { return Vec2(lhs.x - rhs.x, lhs.y - rhs.y); }
	Vec2 operator*(const Vec2 &value, double scale) { return Vec2(value.x * scale, value.y * scale); }
	Vec2 operator/(const Vec2 &value, double scale) { return Vec2(value.x / scale, value.y / scale); }

	double dot(const Vec2 &lhs, const Vec2 &rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }
	double cross(const Vec2 &lhs, const Vec2 &rhs) { return lhs.x * rhs.y - lhs.y * rhs.x; }
	double magnitude(const Vec2 &value) { return std::sqrt(dot(value, value)); }
	double distance_squared(const Vec2 &lhs, const Vec2 &rhs) { return dot(lhs - rhs, lhs - rhs); }

	Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) { return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z); }
	Vec3 operator*(const Vec3 &value, double scale) { return Vec3(value.x * scale, value.y * scale, value.z * scale); }
	double dot(const Vec3 &lhs, const Vec3 &rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }

	Vec3
	cross(
			const Vec3 &lhs,
			const Vec3 &rhs)
	{
		return Vec3(
				lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	Vec3
	normalise(
			const Vec3 &value)
	{
		const double length = std::sqrt(dot(value, value));
		if (length <= 1e-14)
		{
			throw std::runtime_error("Cannot normalise a zero-length spherical vector.");
		}
		return value * (1.0 / length);
	}

	Vec3
	to_vec3(
			const GPlatesMaths::PointOnSphere &point)
	{
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		return Vec3(vector.x().dval(), vector.y().dval(), vector.z().dval());
	}

	GPlatesMaths::PointOnSphere
	to_point_on_sphere(
			const Vec3 &vector)
	{
		const Vec3 unit = normalise(vector);
		return GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(unit.x, unit.y, unit.z));
	}

	struct LocalFrame
	{
		Vec3 centre;
		Vec3 east;
		Vec3 north;
	};

	GPlatesMaths::PointOnSphere
	polygon_centre(
			const GPlatesMaths::PolygonOnSphere &polygon)
	{
		Vec3 sum;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.exterior_ring_vertex_begin();
			vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
		{
			sum = sum + to_vec3(*vertex_iter);
		}
		return to_point_on_sphere(sum);
	}

	LocalFrame
	make_local_frame(
			const GPlatesMaths::PolygonOnSphere &continent)
	{
		LocalFrame frame;
		frame.centre = to_vec3(polygon_centre(continent));
		const Vec3 reference = std::fabs(frame.centre.z) < 0.9 ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		frame.east = normalise(cross(reference, frame.centre));
		frame.north = normalise(cross(frame.centre, frame.east));
		return frame;
	}

	Vec2
	project_lambert_azimuthal_equal_area(
			const LocalFrame &frame,
			const GPlatesMaths::PointOnSphere &point)
	{
		const Vec3 vector = to_vec3(point);
		const double denominator = 1.0 + dot(frame.centre, vector);
		if (denominator <= 1e-10)
		{
			throw std::runtime_error("The selected continent reaches the antipode of its projection centre.");
		}
		const double scale = std::sqrt(2.0 / denominator);
		return Vec2(scale * dot(frame.east, vector), scale * dot(frame.north, vector));
	}

	GPlatesMaths::PointOnSphere
	unproject_lambert_azimuthal_equal_area(
			const LocalFrame &frame,
			const Vec2 &point)
	{
		const double radius = magnitude(point);
		if (radius >= 2.0)
		{
			throw std::runtime_error("A Voronoi point lies outside the Lambert projection disk.");
		}
		if (radius <= 1e-14)
		{
			return to_point_on_sphere(frame.centre);
		}
		const double angular_distance = 2.0 * std::asin(0.5 * radius);
		const Vec3 direction = frame.east * (point.x / radius) + frame.north * (point.y / radius);
		return to_point_on_sphere(
				frame.centre * std::cos(angular_distance) +
				direction * std::sin(angular_distance));
	}

	double
	polygon_area(
			const std::vector<Vec2> &ring)
	{
		double twice_area = 0;
		for (std::size_t index = 0; index < ring.size(); ++index)
		{
			twice_area += cross(ring[index], ring[(index + 1) % ring.size()]);
		}
		return 0.5 * std::fabs(twice_area);
	}

	double
	distance_to_segment(
			const Vec2 &point,
			const Vec2 &start,
			const Vec2 &end)
	{
		const Vec2 segment = end - start;
		const double length_squared = dot(segment, segment);
		if (length_squared <= 1e-20)
		{
			return magnitude(point - start);
		}
		const double interpolation = std::max(
				0.0,
				std::min(1.0, dot(point - start, segment) / length_squared));
		return magnitude(point - (start + segment * interpolation));
	}

	double
	distance_to_ring(
			const Vec2 &point,
			const std::vector<Vec2> &ring)
	{
		double distance = std::numeric_limits<double>::max();
		for (std::size_t index = 0; index < ring.size(); ++index)
		{
			distance = std::min(distance, distance_to_segment(
					point, ring[index], ring[(index + 1) % ring.size()]));
		}
		return distance;
	}

	bool
	point_in_ring(
			const Vec2 &point,
			const std::vector<Vec2> &ring)
	{
		if (distance_to_ring(point, ring) <= 1e-10)
		{
			return true;
		}

		bool inside = false;
		for (std::size_t current = 0, previous = ring.size() - 1;
			current < ring.size(); previous = current++)
		{
			const Vec2 &a = ring[current];
			const Vec2 &b = ring[previous];
			if (((a.y > point.y) != (b.y > point.y)) &&
				point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
			{
				inside = !inside;
			}
		}
		return inside;
	}

	std::vector<Vec2>
	clip_cell_to_half_plane(
			const std::vector<Vec2> &cell,
			const Vec2 &normal,
			double offset)
	{
		std::vector<Vec2> clipped;
		if (cell.empty())
		{
			return clipped;
		}

		for (std::size_t index = 0; index < cell.size(); ++index)
		{
			const Vec2 start = cell[index];
			const Vec2 end = cell[(index + 1) % cell.size()];
			const double start_value = dot(start, normal) - offset;
			const double end_value = dot(end, normal) - offset;
			const bool start_inside = start_value <= 1e-12;
			const bool end_inside = end_value <= 1e-12;

			if (start_inside)
			{
				clipped.push_back(start);
			}
			if (start_inside != end_inside)
			{
				const double denominator = start_value - end_value;
				if (std::fabs(denominator) > 1e-16)
				{
					const double interpolation = start_value / denominator;
					clipped.push_back(start + (end - start) * interpolation);
				}
			}
		}
		return clipped;
	}

	std::vector<std::pair<Vec2, Vec2> >
	clip_segment_to_ring(
			const Vec2 &start,
			const Vec2 &end,
			const std::vector<Vec2> &ring)
	{
		std::vector<double> cuts;
		cuts.push_back(0.0);
		cuts.push_back(1.0);
		const Vec2 direction = end - start;
		for (std::size_t edge_index = 0; edge_index < ring.size(); ++edge_index)
		{
			const Vec2 edge_start = ring[edge_index];
			const Vec2 edge_direction = ring[(edge_index + 1) % ring.size()] - edge_start;
			const double denominator = cross(direction, edge_direction);
			if (std::fabs(denominator) <= 1e-14)
			{
				continue;
			}
			const Vec2 relative = edge_start - start;
			const double segment_t = cross(relative, edge_direction) / denominator;
			const double edge_t = cross(relative, direction) / denominator;
			if (segment_t > 1e-12 && segment_t < 1.0 - 1e-12 &&
				edge_t >= -1e-12 && edge_t <= 1.0 + 1e-12)
			{
				cuts.push_back(segment_t);
			}
		}

		std::sort(cuts.begin(), cuts.end());
		cuts.erase(std::unique(cuts.begin(), cuts.end(),
				[](double lhs, double rhs) { return std::fabs(lhs - rhs) <= 1e-10; }), cuts.end());

		std::vector<std::pair<Vec2, Vec2> > intervals;
		for (std::size_t cut_index = 0; cut_index + 1 < cuts.size(); ++cut_index)
		{
			const double first = cuts[cut_index];
			const double second = cuts[cut_index + 1];
			if (second - first <= 1e-10)
			{
				continue;
			}
			const Vec2 midpoint = start + direction * (0.5 * (first + second));
			if (point_in_ring(midpoint, ring))
			{
				intervals.push_back(std::make_pair(
						start + direction * first,
						start + direction * second));
			}
		}
		return intervals;
	}

	struct PointKey
	{
		PointKey() : x(0), y(0) { }
		explicit PointKey(const Vec2 &point) :
			x(static_cast<long long>(std::llround(point.x * 1e8))),
			y(static_cast<long long>(std::llround(point.y * 1e8)))
		{  }

		bool operator<(const PointKey &other) const
		{
			return x < other.x || (x == other.x && y < other.y);
		}

		long long x;
		long long y;
	};

	struct EdgeKey
	{
		EdgeKey(const Vec2 &start, const Vec2 &end)
		{
			const PointKey first(start);
			const PointKey second(end);
			if (second < first)
			{
				a = second;
				b = first;
			}
			else
			{
				a = first;
				b = second;
			}
		}

		bool operator<(const EdgeKey &other) const
		{
			return a < other.a || (!(other.a < a) && b < other.b);
		}

		PointKey a;
		PointKey b;
	};

	struct PlanarEdge
	{
		Vec2 start;
		Vec2 end;
		bool touches_coast;
	};

	double
	angular_distance(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		return std::acos(std::max(-1.0, std::min(1.0, dot(to_vec3(lhs), to_vec3(rhs)))));
	}

	std::vector<GPlatesMaths::PointOnSphere>
	tessellate_planar_segment(
			const LocalFrame &frame,
			const Vec2 &start,
			const Vec2 &end,
			double maximum_segment_angle)
	{
		unsigned int segment_count = std::max(
				1u,
				static_cast<unsigned int>(std::ceil(magnitude(end - start) / maximum_segment_angle)));
		for (;;)
		{
			std::vector<GPlatesMaths::PointOnSphere> points;
			points.reserve(segment_count + 1);
			for (unsigned int segment = 0; segment <= segment_count; ++segment)
			{
				const double interpolation = static_cast<double>(segment) / segment_count;
				points.push_back(unproject_lambert_azimuthal_equal_area(
						frame, start + (end - start) * interpolation));
			}

			bool within_limit = true;
			for (std::size_t point_index = 0; point_index + 1 < points.size(); ++point_index)
			{
				if (angular_distance(points[point_index], points[point_index + 1]) >
					maximum_segment_angle * (1.0 + 1e-12))
				{
					within_limit = false;
					break;
				}
			}
			if (within_limit)
			{
				return points;
			}
			segment_count *= 2;
		}
	}

	std::vector<Vec2>
	generate_sites(
			const std::vector<Vec2> &ring,
			unsigned int site_count,
			double target_spacing,
			std::mt19937 &random)
	{
		double minimum_x = std::numeric_limits<double>::max();
		double maximum_x = -std::numeric_limits<double>::max();
		double minimum_y = std::numeric_limits<double>::max();
		double maximum_y = -std::numeric_limits<double>::max();
		for (std::vector<Vec2>::const_iterator vertex_iter = ring.begin(); vertex_iter != ring.end(); ++vertex_iter)
		{
			minimum_x = std::min(minimum_x, vertex_iter->x);
			maximum_x = std::max(maximum_x, vertex_iter->x);
			minimum_y = std::min(minimum_y, vertex_iter->y);
			maximum_y = std::max(maximum_y, vertex_iter->y);
		}

		std::uniform_real_distribution<double> x_distribution(minimum_x, maximum_x);
		std::uniform_real_distribution<double> y_distribution(minimum_y, maximum_y);
		std::vector<Vec2> sites;
		double minimum_separation = 0.52 * target_spacing;
		for (unsigned int relaxation = 0; relaxation < 9 && sites.size() < site_count; ++relaxation)
		{
			const unsigned int attempt_limit = site_count * 500;
			for (unsigned int attempt = 0; attempt < attempt_limit && sites.size() < site_count; ++attempt)
			{
				const Vec2 candidate(x_distribution(random), y_distribution(random));
				if (!point_in_ring(candidate, ring))
				{
					continue;
				}
				bool separated = true;
				for (std::vector<Vec2>::const_iterator site_iter = sites.begin(); site_iter != sites.end(); ++site_iter)
				{
					if (distance_squared(candidate, *site_iter) < minimum_separation * minimum_separation)
					{
						separated = false;
						break;
					}
				}
				if (separated)
				{
					sites.push_back(candidate);
				}
			}
			minimum_separation *= 0.82;
		}
		if (sites.size() < 4)
		{
			throw std::runtime_error("Could not place enough Voronoi sites inside the selected continent.");
		}
		return sites;
	}
}


GPlatesViewOperations::InitialRiftGeometry::Parameters::Parameters() :
	random_seed(1),
	target_cell_spacing_km(1800.0),
	minimum_edge_length_factor(0.16),
	maximum_segment_length_km(130.0),
	planet_radius_km(6371.0088)
{  }


GPlatesViewOperations::InitialRiftGeometry::Edge::Edge(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline_,
		double length_km_,
		bool touches_coast_,
		unsigned int start_node_,
		unsigned int end_node_) :
	polyline(polyline_),
	length_km(length_km_),
	touches_coast(touches_coast_),
	start_node(start_node_),
	end_node(end_node_)
{  }


GPlatesViewOperations::InitialRiftGeometry::Metrics::Metrics() :
	site_count(0),
	edge_count(0),
	craton_intersection_count(0),
	average_edge_length_km(0),
	maximum_edge_length_km(0),
	maximum_segment_length_km(0)
{  }


GPlatesViewOperations::InitialRiftGeometry::Result::Result(
		const std::vector<Edge> &edges_,
		const std::vector<GPlatesMaths::PointOnSphere> &sites_,
		const std::vector<GPlatesMaths::PointOnSphere> &nodes_,
		const Metrics &metrics_) :
	edges(edges_),
	sites(sites_),
	nodes(nodes_),
	metrics(metrics_)
{  }


std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>
GPlatesViewOperations::InitialRiftGeometry::join_edge_paths(
		const Result &network,
		const std::vector<unsigned int> &edge_indices)
{
	std::vector<std::vector<unsigned int> > node_edges(network.nodes.size());
	std::vector<bool> selected(network.edges.size(), false);
	for (std::vector<unsigned int>::const_iterator edge_index_iter = edge_indices.begin();
		edge_index_iter != edge_indices.end(); ++edge_index_iter)
	{
		if (*edge_index_iter >= network.edges.size())
		{
			throw std::out_of_range("Selected rift edge index is outside the generated network.");
		}
		if (selected[*edge_index_iter])
		{
			continue;
		}
		const Edge &edge = network.edges[*edge_index_iter];
		if (edge.start_node >= network.nodes.size() || edge.end_node >= network.nodes.size())
		{
			throw std::out_of_range("Selected rift edge references a missing network node.");
		}
		selected[*edge_index_iter] = true;
		node_edges[edge.start_node].push_back(*edge_index_iter);
		node_edges[edge.end_node].push_back(*edge_index_iter);
	}

	std::vector<bool> used(network.edges.size(), false);
	std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> joined_paths;

	auto append_edge = [&](std::vector<GPlatesMaths::PointOnSphere> &points,
			unsigned int edge_index, unsigned int from_node)
	{
		const Edge &edge = network.edges[edge_index];
		std::vector<GPlatesMaths::PointOnSphere> edge_points(
				edge.polyline->vertex_begin(), edge.polyline->vertex_end());
		if (from_node == edge.end_node)
		{
			std::reverse(edge_points.begin(), edge_points.end());
		}
		else if (from_node != edge.start_node)
		{
			throw std::runtime_error("Selected rift path is not incident to its current node.");
		}
		points.insert(
				points.end(),
				edge_points.begin() + (points.empty() ? 0 : 1),
				edge_points.end());
	};

	auto walk_path = [&](unsigned int start_node, unsigned int first_edge)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		unsigned int current_node = start_node;
		unsigned int current_edge = first_edge;
		while (true)
		{
			append_edge(points, current_edge, current_node);
			used[current_edge] = true;
			const Edge &edge = network.edges[current_edge];
			current_node = current_node == edge.start_node ? edge.end_node : edge.start_node;
			if (node_edges[current_node].size() != 2)
			{
				break;
			}
			const unsigned int first_candidate = node_edges[current_node][0];
			const unsigned int second_candidate = node_edges[current_node][1];
			const unsigned int next_edge = !used[first_candidate]
					? first_candidate : second_candidate;
			if (used[next_edge])
			{
				break;
			}
			current_edge = next_edge;
		}
		if (points.size() >= 2)
		{
			joined_paths.push_back(GPlatesMaths::PolylineOnSphere::create(points, true));
		}
	};

	// Start at endpoints and branch nodes. This consumes every maximal path that
	// can be represented without doubling back through a branch.
	for (unsigned int node_index = 0; node_index < node_edges.size(); ++node_index)
	{
		if (node_edges[node_index].size() == 2)
		{
			continue;
		}
		for (std::vector<unsigned int>::const_iterator edge_iter = node_edges[node_index].begin();
			edge_iter != node_edges[node_index].end(); ++edge_iter)
		{
			if (!used[*edge_iter])
			{
				walk_path(node_index, *edge_iter);
			}
		}
	}

	// Any remaining edges form degree-two cycles.
	for (unsigned int edge_index = 0; edge_index < selected.size(); ++edge_index)
	{
		if (selected[edge_index] && !used[edge_index])
		{
			walk_path(network.edges[edge_index].start_node, edge_index);
		}
	}

	return joined_paths;
}


GPlatesViewOperations::InitialRiftGeometry::Result
GPlatesViewOperations::InitialRiftGeometry::generate(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
		const Parameters &parameters)

{
	return generate(
			continent,
			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>(),
			parameters);
}


GPlatesViewOperations::InitialRiftGeometry::Result
GPlatesViewOperations::InitialRiftGeometry::generate(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
		const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &exclusion_polygons,
		const Parameters &parameters)
{
	if (parameters.target_cell_spacing_km < 300.0 || parameters.target_cell_spacing_km > 6000.0)
	{
		throw std::invalid_argument("Voronoi cell spacing must be between 300 and 6000 km.");
	}
	if (parameters.minimum_edge_length_factor < 0.0 || parameters.minimum_edge_length_factor > 0.8)
	{
		throw std::invalid_argument("The minimum Voronoi edge factor must be between 0 and 0.8.");
	}
	if (parameters.maximum_segment_length_km <= 0.0 || parameters.planet_radius_km <= 0.0)
	{
		throw std::invalid_argument("Segment length and planet radius must be positive.");
	}

	const LocalFrame frame = make_local_frame(*continent);
	std::vector<Vec2> ring;
	for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
			continent->exterior_ring_vertex_begin();
		vertex_iter != continent->exterior_ring_vertex_end(); ++vertex_iter)
	{
		ring.push_back(project_lambert_azimuthal_equal_area(frame, *vertex_iter));
	}
	if (ring.size() < 3)
	{
		throw std::invalid_argument("The selected continent needs at least three boundary vertices.");
	}
	std::vector<std::vector<Vec2> > exclusion_rings;
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
			exclusion_iter = exclusion_polygons.begin(); exclusion_iter != exclusion_polygons.end(); ++exclusion_iter)
	{
		std::vector<Vec2> exclusion_ring;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				(*exclusion_iter)->exterior_ring_vertex_begin();
			vertex_iter != (*exclusion_iter)->exterior_ring_vertex_end(); ++vertex_iter)
		{
			exclusion_ring.push_back(project_lambert_azimuthal_equal_area(frame, *vertex_iter));
		}
		if (exclusion_ring.size() >= 3)
		{
			exclusion_rings.push_back(exclusion_ring);
		}
	}

	const double target_spacing = parameters.target_cell_spacing_km / parameters.planet_radius_km;
	const double area = polygon_area(ring);
	const unsigned int requested_site_count = std::max(
			8u,
			std::min(96u, static_cast<unsigned int>(std::llround(
					area / (0.82 * target_spacing * target_spacing)))));
	std::mt19937 random(parameters.random_seed);
	const std::vector<Vec2> sites = generate_sites(
			ring, requested_site_count, target_spacing, random);

	double minimum_x = std::numeric_limits<double>::max();
	double maximum_x = -std::numeric_limits<double>::max();
	double minimum_y = std::numeric_limits<double>::max();
	double maximum_y = -std::numeric_limits<double>::max();
	for (std::vector<Vec2>::const_iterator vertex_iter = ring.begin(); vertex_iter != ring.end(); ++vertex_iter)
	{
		minimum_x = std::min(minimum_x, vertex_iter->x);
		maximum_x = std::max(maximum_x, vertex_iter->x);
		minimum_y = std::min(minimum_y, vertex_iter->y);
		maximum_y = std::max(maximum_y, vertex_iter->y);
	}
	const double margin = target_spacing + 0.1 * std::max(maximum_x - minimum_x, maximum_y - minimum_y);
	const std::vector<Vec2> bounding_cell = {
		Vec2(minimum_x - margin, minimum_y - margin),
		Vec2(maximum_x + margin, minimum_y - margin),
		Vec2(maximum_x + margin, maximum_y + margin),
		Vec2(minimum_x - margin, maximum_y + margin)};

	std::map<EdgeKey, PlanarEdge> unique_edges;
	std::set<EdgeKey> rejected_edges;
	unsigned int craton_intersection_count = 0;
	const double coast_tolerance = std::max(1e-7, target_spacing * 1e-5);
	const double minimum_edge_length = parameters.minimum_edge_length_factor * target_spacing;
	for (std::size_t site_index = 0; site_index < sites.size(); ++site_index)
	{
		std::vector<Vec2> cell(bounding_cell);
		for (std::size_t other_index = 0; other_index < sites.size() && !cell.empty(); ++other_index)
		{
			if (site_index == other_index)
			{
				continue;
			}
			const Vec2 normal = sites[other_index] - sites[site_index];
			const double offset = 0.5 * (
					dot(sites[other_index], sites[other_index]) -
					dot(sites[site_index], sites[site_index]));
			cell = clip_cell_to_half_plane(cell, normal, offset);
		}

		for (std::size_t edge_index = 0; edge_index < cell.size(); ++edge_index)
		{
			const Vec2 cell_start = cell[edge_index];
			const Vec2 cell_end = cell[(edge_index + 1) % cell.size()];
			const std::vector<std::pair<Vec2, Vec2> > clipped =
					clip_segment_to_ring(cell_start, cell_end, ring);
			for (std::vector<std::pair<Vec2, Vec2> >::const_iterator clipped_iter = clipped.begin();
				clipped_iter != clipped.end(); ++clipped_iter)
			{
				Vec2 start = clipped_iter->first;
				Vec2 end = clipped_iter->second;
				if (magnitude(end - start) < minimum_edge_length)
				{
					continue;
				}
				bool intersects_exclusion = false;
				for (std::vector<std::vector<Vec2> >::const_iterator exclusion_iter =
						exclusion_rings.begin(); exclusion_iter != exclusion_rings.end(); ++exclusion_iter)
				{
					if (!clip_segment_to_ring(start, end, *exclusion_iter).empty())
					{
						intersects_exclusion = true;
						break;
					}
				}
				if (intersects_exclusion)
				{
					const EdgeKey rejected_key(start, end);
					if (rejected_edges.insert(rejected_key).second)
					{
						++craton_intersection_count;
					}
					continue;
				}
				const bool start_on_coast = distance_to_ring(start, ring) <= coast_tolerance;
				const bool end_on_coast = distance_to_ring(end, ring) <= coast_tolerance;
				// Keep points numerically inside the spherical polygon while retaining
				// the topological fact that the edge reaches the coast.
				if (start_on_coast)
				{
					start = start + (end - start) * 1e-6;
				}
				if (end_on_coast)
				{
					end = end + (start - end) * 1e-6;
				}

				PlanarEdge edge;
				edge.start = start;
				edge.end = end;
				edge.touches_coast = start_on_coast || end_on_coast;
				unique_edges.insert(std::make_pair(EdgeKey(start, end), edge));
			}
		}
	}
	if (unique_edges.size() < 3)
	{
		throw std::runtime_error("The clipped Voronoi network did not contain enough usable edges.");
	}

	std::map<PointKey, unsigned int> node_indices;
	std::vector<Vec2> node_points;
	std::vector<Edge> edges;
	Metrics metrics;
	double total_edge_length = 0;
	const double maximum_segment_angle = parameters.maximum_segment_length_km / parameters.planet_radius_km;
	for (std::map<EdgeKey, PlanarEdge>::const_iterator edge_iter = unique_edges.begin();
		edge_iter != unique_edges.end(); ++edge_iter)
	{
		const PlanarEdge &planar_edge = edge_iter->second;
		unsigned int endpoint_nodes[2];
		const Vec2 endpoint_points[2] = {planar_edge.start, planar_edge.end};
		for (unsigned int endpoint = 0; endpoint < 2; ++endpoint)
		{
			const PointKey key(endpoint_points[endpoint]);
			std::map<PointKey, unsigned int>::const_iterator found = node_indices.find(key);
			if (found == node_indices.end())
			{
				endpoint_nodes[endpoint] = static_cast<unsigned int>(node_points.size());
				node_indices.insert(std::make_pair(key, endpoint_nodes[endpoint]));
				node_points.push_back(endpoint_points[endpoint]);
			}
			else
			{
				endpoint_nodes[endpoint] = found->second;
			}
		}

		const std::vector<GPlatesMaths::PointOnSphere> points = tessellate_planar_segment(
				frame, planar_edge.start, planar_edge.end, maximum_segment_angle);
		double edge_length = 0;
		double maximum_segment = 0;
		for (std::size_t point_index = 0; point_index + 1 < points.size(); ++point_index)
		{
			const double segment_length = angular_distance(
					points[point_index], points[point_index + 1]) * parameters.planet_radius_km;
			edge_length += segment_length;
			maximum_segment = std::max(maximum_segment, segment_length);
		}
		edges.push_back(Edge(
				GPlatesMaths::PolylineOnSphere::create(points, true),
				edge_length,
				planar_edge.touches_coast,
				endpoint_nodes[0],
				endpoint_nodes[1]));
		total_edge_length += edge_length;
		metrics.maximum_edge_length_km = std::max(metrics.maximum_edge_length_km, edge_length);
		metrics.maximum_segment_length_km = std::max(metrics.maximum_segment_length_km, maximum_segment);
	}

	std::vector<GPlatesMaths::PointOnSphere> spherical_sites;
	for (std::vector<Vec2>::const_iterator site_iter = sites.begin(); site_iter != sites.end(); ++site_iter)
	{
		spherical_sites.push_back(unproject_lambert_azimuthal_equal_area(frame, *site_iter));
	}
	std::vector<GPlatesMaths::PointOnSphere> spherical_nodes;
	for (std::vector<Vec2>::const_iterator node_iter = node_points.begin(); node_iter != node_points.end(); ++node_iter)
	{
		spherical_nodes.push_back(unproject_lambert_azimuthal_equal_area(frame, *node_iter));
	}

	metrics.site_count = static_cast<unsigned int>(sites.size());
	metrics.edge_count = static_cast<unsigned int>(edges.size());
	metrics.craton_intersection_count = craton_intersection_count;
	metrics.average_edge_length_km = total_edge_length / edges.size();
	return Result(edges, spherical_sites, spherical_nodes, metrics);
}
