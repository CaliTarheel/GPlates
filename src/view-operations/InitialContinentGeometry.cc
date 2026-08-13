/* $Id$ */

/**
 * \file
 * Deterministic spherical geometry generation for the Worldbuilding Pasta
 * "Create Initial Continent" operation.
 */

#include "InitialContinentGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <utility>

#include "maths/GeometryDistance.h"
#include "maths/UnitVector3D.h"


namespace
{
	const double PI = 3.1415926535897932384626433832795;
	const double FOUR_PI = 4.0 * PI;

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

	double
	dot(
			const Vec3 &a,
			const Vec3 &b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vec3
	cross(
			const Vec3 &a,
			const Vec3 &b)
	{
		return Vec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x);
	}

	Vec3
	operator+(
			const Vec3 &a,
			const Vec3 &b)
	{
		return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
	}

	Vec3
	operator*(
			const Vec3 &a,
			double scale)
	{
		return Vec3(a.x * scale, a.y * scale, a.z * scale);
	}

	Vec3
	normalise(
			const Vec3 &value)
	{
		const double magnitude = std::sqrt(dot(value, value));
		if (magnitude <= std::numeric_limits<double>::epsilon())
		{
			throw std::runtime_error("Cannot normalise a zero-length vector.");
		}
		return value * (1.0 / magnitude);
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
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(unit.x, unit.y, unit.z));
	}

	struct LocalFrame
	{
		Vec3 centre;
		Vec3 east;
		Vec3 north;
	};

	LocalFrame
	make_local_frame(
			std::mt19937 &random)
	{
		std::uniform_real_distribution<double> unit_distribution(0.0, 1.0);
		const double z = 2.0 * unit_distribution(random) - 1.0;
		const double longitude = 2.0 * PI * unit_distribution(random);
		const double horizontal = std::sqrt(std::max(0.0, 1.0 - z * z));

		LocalFrame frame;
		frame.centre = Vec3(horizontal * std::cos(longitude), horizontal * std::sin(longitude), z);

		const Vec3 reference = std::fabs(z) < 0.9 ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		frame.east = normalise(cross(reference, frame.centre));
		frame.north = normalise(cross(frame.centre, frame.east));
		return frame;
	}

	GPlatesMaths::PointOnSphere
	unproject_lambert_azimuthal_equal_area(
			const LocalFrame &frame,
			const Vec2 &point)
	{
		const double rho = std::sqrt(point.x * point.x + point.y * point.y);
		if (rho >= 2.0)
		{
			throw std::runtime_error("Generated point is outside the Lambert projection disk.");
		}
		if (rho <= 1e-14)
		{
			return to_point_on_sphere(frame.centre);
		}

		const double angular_distance = 2.0 * std::asin(0.5 * rho);
		const Vec3 direction = frame.east * (point.x / rho) + frame.north * (point.y / rho);
		return to_point_on_sphere(
				frame.centre * std::cos(angular_distance) +
				direction * std::sin(angular_distance));
	}

	GPlatesMaths::PointOnSphere
	slerp(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			double interpolation)
	{
		const Vec3 a = to_vec3(start);
		const Vec3 b = to_vec3(end);
		const double cosine = std::max(-1.0, std::min(1.0, dot(a, b)));
		const double angle = std::acos(cosine);
		if (angle <= 1e-12)
		{
			return start;
		}

		const double sine = std::sin(angle);
		const double weight_a = std::sin((1.0 - interpolation) * angle) / sine;
		const double weight_b = std::sin(interpolation * angle) / sine;
		return to_point_on_sphere(a * weight_a + b * weight_b);
	}

	std::vector<GPlatesMaths::PointOnSphere>
	tessellate_ring(
			const std::vector<GPlatesMaths::PointOnSphere> &ring,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> result;
		for (std::size_t index = 0; index < ring.size(); ++index)
		{
			const GPlatesMaths::PointOnSphere &start = ring[index];
			const GPlatesMaths::PointOnSphere &end = ring[(index + 1) % ring.size()];
			const double cosine = std::max(-1.0, std::min(1.0, dot(to_vec3(start), to_vec3(end))));
			const double angle = std::acos(cosine);
			const unsigned int segment_count = std::max(
					1u,
					static_cast<unsigned int>(std::ceil(angle / maximum_segment_angle)));

			for (unsigned int segment = 0; segment < segment_count; ++segment)
			{
				result.push_back(slerp(start, end, static_cast<double>(segment) / segment_count));
			}
		}
		return result;
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
	create_polygon(
			const LocalFrame &frame,
			const std::vector<Vec2> &plane_ring,
			double scale,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> spherical_ring;
		spherical_ring.reserve(plane_ring.size());
		for (std::vector<Vec2>::const_iterator iter = plane_ring.begin(); iter != plane_ring.end(); ++iter)
		{
			spherical_ring.push_back(unproject_lambert_azimuthal_equal_area(
					frame,
					Vec2(iter->x * scale, iter->y * scale)));
		}

		return GPlatesMaths::PolygonOnSphere::create(
				tessellate_ring(spherical_ring, maximum_segment_angle),
				true);
	}

	double
	polygon_area_fraction(
			const GPlatesMaths::PolygonOnSphere &polygon)
	{
		return polygon.get_area().dval() / FOUR_PI;
	}

	double
	find_scale_for_area(
			const LocalFrame &frame,
			const std::vector<Vec2> &ring,
			double target_area_fraction,
			double maximum_segment_angle,
			double maximum_scale)
	{
		double lower = 0.05;
		double upper = maximum_scale;
		for (unsigned int iteration = 0; iteration < 56; ++iteration)
		{
			const double middle = 0.5 * (lower + upper);
			const double area = polygon_area_fraction(*create_polygon(
					frame, ring, middle, maximum_segment_angle));
			if (area < target_area_fraction)
			{
				lower = middle;
			}
			else
			{
				upper = middle;
			}
		}
		return 0.5 * (lower + upper);
	}

	double
	maximum_radius(
			const std::vector<Vec2> &ring)
	{
		double result = 0;
		for (std::vector<Vec2>::const_iterator iter = ring.begin(); iter != ring.end(); ++iter)
		{
			result = std::max(result, std::sqrt(iter->x * iter->x + iter->y * iter->y));
		}
		return result;
	}

	double
	minimum_radius(
			const std::vector<Vec2> &ring)
	{
		double result = std::numeric_limits<double>::max();
		for (std::vector<Vec2>::const_iterator iter = ring.begin(); iter != ring.end(); ++iter)
		{
			result = std::min(result, std::sqrt(iter->x * iter->x + iter->y * iter->y));
		}
		return result;
	}

	double
	maximum_polygon_segment_length_km(
			const GPlatesMaths::PolygonOnSphere &polygon,
			double planet_radius_km)
	{
		double result = 0;
		for (GPlatesMaths::PolygonOnSphere::ring_const_iterator iter = polygon.exterior_ring_begin();
				iter != polygon.exterior_ring_end();
				++iter)
		{
			result = std::max(result, iter->arc_length().dval() * planet_radius_km);
		}
		return result;
	}

	double
	total_polygon_segment_length_km(
			const GPlatesMaths::PolygonOnSphere &polygon,
			double planet_radius_km)
	{
		double result = 0;
		for (GPlatesMaths::PolygonOnSphere::ring_const_iterator iter = polygon.exterior_ring_begin();
				iter != polygon.exterior_ring_end();
				++iter)
		{
			result += iter->arc_length().dval() * planet_radius_km;
		}
		return result;
	}

	unsigned int
	polygon_segment_count(
			const GPlatesMaths::PolygonOnSphere &polygon)
	{
		return static_cast<unsigned int>(
				std::distance(polygon.exterior_ring_begin(), polygon.exterior_ring_end()));
	}

	unsigned int
	polygon_vertex_count(
			const GPlatesMaths::PolygonOnSphere &polygon)
	{
		return static_cast<unsigned int>(
				std::distance(polygon.exterior_ring_vertex_begin(), polygon.exterior_ring_vertex_end()));
	}

	struct CratonTemplate
	{
		double target_area_fraction;
		std::vector<Vec2> relative_ring;
		double estimated_bound;
	};
}


GPlatesViewOperations::InitialContinentGeometry::Parameters::Parameters() :
	craton_count(10),
	packing(0.5),
	continent_area_fraction(0.25),
	coastline_swiggle(0.55),
	random_seed(1),
	maximum_segment_length_km(180.0),
	planet_radius_km(6371.0088)
{  }


GPlatesViewOperations::InitialContinentGeometry::Metrics::Metrics() :
	continent_area_fraction(0),
	total_craton_area_fraction(0),
	craton_fill_fraction(0),
	continent_perimeter_km(0),
	average_continent_segment_length_km(0),
	maximum_continent_segment_length_km(0),
	average_craton_segment_length_km(0),
	maximum_segment_length_km(0),
	continent_vertex_count(0),
	craton_vertex_count(0)
{  }


GPlatesViewOperations::InitialContinentGeometry::Result::Result(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent_,
		const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons_,
		const GPlatesMaths::PointOnSphere &centre_,
		const Metrics &metrics_) :
	continent(continent_),
	cratons(cratons_),
	centre(centre_),
	metrics(metrics_)
{  }


GPlatesViewOperations::InitialContinentGeometry::Result
GPlatesViewOperations::InitialContinentGeometry::generate(
		const Parameters &parameters)
{
	if (parameters.craton_count < 8 || parameters.craton_count > 12)
	{
		throw std::invalid_argument("Worldbuilding Pasta recommends 8 to 12 cratons for an Earth-like world.");
	}
	if (parameters.packing < 0 || parameters.packing > 1)
	{
		throw std::invalid_argument("Craton packing must be between zero and one.");
	}
	if (parameters.continent_area_fraction < 0.05 || parameters.continent_area_fraction > 0.45)
	{
		throw std::invalid_argument("Continent surface coverage must be between 5 and 45 percent.");
	}
	if (parameters.coastline_swiggle < 0 || parameters.coastline_swiggle > 1)
	{
		throw std::invalid_argument("Coastline swiggle must be between zero and one.");
	}
	if (parameters.maximum_segment_length_km <= 0 || parameters.planet_radius_km <= 0)
	{
		throw std::invalid_argument("Segment length and planet radius must be positive.");
	}

	std::mt19937 random(parameters.random_seed);
	std::uniform_real_distribution<double> unit_distribution(0.0, 1.0);
	std::uniform_real_distribution<double> signed_distribution(-1.0, 1.0);
	std::lognormal_distribution<double> craton_size_distribution(0.0, 0.48);
	const LocalFrame frame = make_local_frame(random);
	const double maximum_segment_angle = parameters.maximum_segment_length_km / parameters.planet_radius_km;

	// Generate a multi-scale, star-shaped coastline in an equal-area local
	// projection. Fine control points add genuine shape information rather than
	// merely tessellating long straight arcs. The binary search below then makes
	// the spherical polygon meet the requested surface fraction exactly.
	const unsigned int coastline_control_point_count = 240;
	const double phase2 = 2.0 * PI * unit_distribution(random);
	const double phase3 = 2.0 * PI * unit_distribution(random);
	const double phase5 = 2.0 * PI * unit_distribution(random);
	const double phase7 = 2.0 * PI * unit_distribution(random);
	const double phase11 = 2.0 * PI * unit_distribution(random);
	const double phase17 = 2.0 * PI * unit_distribution(random);
	std::vector<double> coastline_noise(coastline_control_point_count);
	for (std::vector<double>::iterator noise_iter = coastline_noise.begin();
			noise_iter != coastline_noise.end();
			++noise_iter)
	{
		*noise_iter = signed_distribution(random);
	}
	const double equivalent_continent_radius = 2.0 * std::sqrt(parameters.continent_area_fraction);
	std::vector<Vec2> continent_ring;
	continent_ring.reserve(coastline_control_point_count);
	for (unsigned int index = 0; index < coastline_control_point_count; ++index)
	{
		const double angle = 2.0 * PI * index / coastline_control_point_count;
		const unsigned int previous = (index + coastline_control_point_count - 1) % coastline_control_point_count;
		const unsigned int next = (index + 1) % coastline_control_point_count;
		const double correlated_noise =
				0.25 * coastline_noise[previous] +
				0.5 * coastline_noise[index] +
				0.25 * coastline_noise[next];
		const double radial_factor =
				1.0 +
				0.12 * std::sin(2.0 * angle + phase2) +
				0.07 * std::sin(3.0 * angle + phase3) +
				0.035 * std::sin(5.0 * angle + phase5) +
				parameters.coastline_swiggle * (
						0.035 * std::sin(7.0 * angle + phase7) +
						0.024 * std::sin(11.0 * angle + phase11) +
						0.016 * std::sin(17.0 * angle + phase17) +
						0.04 * correlated_noise);
		const double radius = equivalent_continent_radius * radial_factor;
		continent_ring.push_back(Vec2(radius * std::cos(angle), radius * std::sin(angle)));
	}

	const double maximum_continent_scale = 1.92 / maximum_radius(continent_ring);
	const double continent_scale = find_scale_for_area(
			frame,
			continent_ring,
			parameters.continent_area_fraction,
			maximum_segment_angle,
			maximum_continent_scale);
	for (std::vector<Vec2>::iterator iter = continent_ring.begin(); iter != continent_ring.end(); ++iter)
	{
		iter->x *= continent_scale;
		iter->y *= continent_scale;
	}
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent =
			create_polygon(frame, continent_ring, 1.0, maximum_segment_angle);

	// Craton packing is defined as the fraction of continental area occupied by
	// cratons. Balanced packing intentionally sits near Artifexia's measured 26.39%.
	const double target_craton_fill = 0.18 + 0.16 * parameters.packing;
	const double target_total_craton_area = parameters.continent_area_fraction * target_craton_fill;

	std::vector<double> weights(parameters.craton_count);
	for (std::vector<double>::iterator iter = weights.begin(); iter != weights.end(); ++iter)
	{
		*iter = std::max(0.45, std::min(2.25, craton_size_distribution(random)));
	}
	const double total_weight = std::accumulate(weights.begin(), weights.end(), 0.0);

	std::vector<CratonTemplate> templates(parameters.craton_count);
	for (unsigned int craton_index = 0; craton_index < parameters.craton_count; ++craton_index)
	{
		CratonTemplate &craton = templates[craton_index];
		craton.target_area_fraction = target_total_craton_area * weights[craton_index] / total_weight;
		const double equivalent_radius = 2.0 * std::sqrt(craton.target_area_fraction);
		const unsigned int control_point_count = 8 + static_cast<unsigned int>(random() % 10);
		const unsigned int primary_frequency = 2 + static_cast<unsigned int>(random() % 4);
		const unsigned int secondary_frequency = primary_frequency + 1 + static_cast<unsigned int>(random() % 3);
		const double primary_phase = 2.0 * PI * unit_distribution(random);
		const double secondary_phase = 2.0 * PI * unit_distribution(random);
		const double rotation = 2.0 * PI * unit_distribution(random);
		const double aspect_scale = 0.82 + 0.40 * unit_distribution(random);
		const double primary_amplitude = 0.08 + 0.08 * unit_distribution(random);
		const double secondary_amplitude = 0.025 + 0.045 * unit_distribution(random);
		const double roughness = 0.03 + 0.05 * unit_distribution(random);
		craton.relative_ring.reserve(control_point_count);
		for (unsigned int point_index = 0; point_index < control_point_count; ++point_index)
		{
			const double angle = 2.0 * PI * point_index / control_point_count;
			const double radial_factor =
					1.0 +
					primary_amplitude * std::sin(primary_frequency * angle + primary_phase) +
					secondary_amplitude * std::sin(secondary_frequency * angle + secondary_phase) +
					roughness * signed_distribution(random);
			const double radius = equivalent_radius * radial_factor;
			const double unrotated_x = aspect_scale * radius * std::cos(angle);
			const double unrotated_y = radius * std::sin(angle) / aspect_scale;
			craton.relative_ring.push_back(Vec2(
					unrotated_x * std::cos(rotation) - unrotated_y * std::sin(rotation),
					unrotated_x * std::sin(rotation) + unrotated_y * std::cos(rotation)));
		}
		craton.estimated_bound = maximum_radius(craton.relative_ring) * 1.02;
	}

	std::vector<unsigned int> placement_order(parameters.craton_count);
	for (unsigned int index = 0; index < parameters.craton_count; ++index)
	{
		placement_order[index] = index;
	}
	std::sort(
			placement_order.begin(),
			placement_order.end(),
			[&templates](unsigned int lhs, unsigned int rhs)
			{
				return templates[lhs].estimated_bound > templates[rhs].estimated_bound;
			});

	const double safe_continent_radius = minimum_radius(continent_ring) * 0.96;
	const double sampling_radius = maximum_radius(continent_ring) * 0.98;
	const double requested_gap = safe_continent_radius *
			(0.003 + 0.015 * (1.0 - parameters.packing));
	const double requested_gap_angle = 0.5 * requested_gap;
	typedef std::pair<
			unsigned int,
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> indexed_craton_type;
	std::vector<indexed_craton_type> placed_cratons;

	// Pack the actual irregular polygons across the complete continental shape.
	// A conservative bounding-circle pack wastes the coastline lobes and makes
	// the tight end impossible even though cratons occupy only 34% of the land.
	// Whole-layout retries prevent a greedy early placement from making the
	// result dependent on a lucky final candidate.
	bool layout_placed = false;
	for (unsigned int layout_attempt = 0; layout_attempt < 80 && !layout_placed; ++layout_attempt)
	{
		placed_cratons.clear();
		bool attempt_succeeded = true;
		for (std::vector<unsigned int>::const_iterator order_iter = placement_order.begin();
				order_iter != placement_order.end();
				++order_iter)
		{
			const unsigned int craton_index = *order_iter;
			const double bound = templates[craton_index].estimated_bound;
			const double available_radius = sampling_radius - bound;
			if (available_radius <= 0)
			{
				throw std::runtime_error("The requested cratons are too large for the continent.");
			}

			bool placed = false;
			for (unsigned int attempt = 0; attempt < 1500 && !placed; ++attempt)
			{
				const double radius = available_radius * std::sqrt(unit_distribution(random));
				const double angle = 2.0 * PI * unit_distribution(random);
				const Vec2 candidate(radius * std::cos(angle), radius * std::sin(angle));
				std::vector<Vec2> approximate_ring;
				approximate_ring.reserve(templates[craton_index].relative_ring.size());
				for (std::vector<Vec2>::const_iterator relative_iter =
						templates[craton_index].relative_ring.begin();
						relative_iter != templates[craton_index].relative_ring.end();
						++relative_iter)
				{
					approximate_ring.push_back(Vec2(
							candidate.x + relative_iter->x,
							candidate.y + relative_iter->y));
				}
				if (maximum_radius(approximate_ring) >= 1.98)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type approximate_craton =
						create_polygon(frame, approximate_ring, 1.0, maximum_segment_angle);
				bool clear = true;
				for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
							approximate_craton->exterior_ring_vertex_begin();
						vertex_iter != approximate_craton->exterior_ring_vertex_end();
						++vertex_iter)
				{
					if (!continent->is_point_in_polygon(*vertex_iter))
					{
						clear = false;
						break;
					}
				}
				for (std::vector<indexed_craton_type>::const_iterator placed_iter =
							placed_cratons.begin();
						clear && placed_iter != placed_cratons.end();
						++placed_iter)
				{
					const double separation = GPlatesMaths::minimum_distance(
							*approximate_craton,
							*placed_iter->second,
							true,
							true).calculate_angle().dval();
					clear = separation > requested_gap_angle;
				}
				if (!clear)
				{
					continue;
				}

				// Refine this accepted candidate to its exact requested spherical area.
				double lower = 0.35;
				double upper = 1.65;
				for (unsigned int iteration = 0; iteration < 52; ++iteration)
				{
					const double middle = 0.5 * (lower + upper);
					std::vector<Vec2> scaled_ring;
					scaled_ring.reserve(templates[craton_index].relative_ring.size());
					for (std::vector<Vec2>::const_iterator relative_iter =
							templates[craton_index].relative_ring.begin();
							relative_iter != templates[craton_index].relative_ring.end();
							++relative_iter)
					{
						scaled_ring.push_back(Vec2(
								candidate.x + middle * relative_iter->x,
								candidate.y + middle * relative_iter->y));
					}
					const double area = polygon_area_fraction(*create_polygon(
							frame, scaled_ring, 1.0, maximum_segment_angle));
					if (area < templates[craton_index].target_area_fraction)
					{
						lower = middle;
					}
					else
					{
						upper = middle;
					}
				}

				const double scale = 0.5 * (lower + upper);
				std::vector<Vec2> exact_ring;
				exact_ring.reserve(templates[craton_index].relative_ring.size());
				for (std::vector<Vec2>::const_iterator relative_iter =
						templates[craton_index].relative_ring.begin();
						relative_iter != templates[craton_index].relative_ring.end();
						++relative_iter)
				{
					exact_ring.push_back(Vec2(
							candidate.x + scale * relative_iter->x,
							candidate.y + scale * relative_iter->y));
				}
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exact_craton =
						create_polygon(frame, exact_ring, 1.0, maximum_segment_angle);
				for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
							exact_craton->exterior_ring_vertex_begin();
						clear && vertex_iter != exact_craton->exterior_ring_vertex_end();
						++vertex_iter)
				{
					clear = continent->is_point_in_polygon(*vertex_iter);
				}
				for (std::vector<indexed_craton_type>::const_iterator placed_iter =
							placed_cratons.begin();
						clear && placed_iter != placed_cratons.end();
						++placed_iter)
				{
					const double separation = GPlatesMaths::minimum_distance(
							*exact_craton,
							*placed_iter->second,
							true,
							true).calculate_angle().dval();
					clear = separation > requested_gap_angle;
				}
				if (clear)
				{
					placed_cratons.push_back(indexed_craton_type(craton_index, exact_craton));
					placed = true;
				}
			}
			if (!placed)
			{
				attempt_succeeded = false;
				break;
			}
		}
		layout_placed = attempt_succeeded;
	}
	if (!layout_placed)
	{
		throw std::runtime_error(
				"Unable to place non-overlapping cratons. Reduce packing or continent coverage and try another seed.");
	}

	std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> cratons(
			parameters.craton_count,
			continent);
	for (std::vector<indexed_craton_type>::const_iterator placed_iter = placed_cratons.begin();
			placed_iter != placed_cratons.end();
			++placed_iter)
	{
		cratons[placed_iter->first] = placed_iter->second;
	}

	Metrics metrics;
	metrics.continent_area_fraction = polygon_area_fraction(*continent);
	metrics.continent_vertex_count = polygon_vertex_count(*continent);
	metrics.continent_perimeter_km = total_polygon_segment_length_km(
			*continent, parameters.planet_radius_km);
	const unsigned int continent_segment_count = polygon_segment_count(*continent);
	metrics.average_continent_segment_length_km =
			metrics.continent_perimeter_km / continent_segment_count;
	metrics.maximum_continent_segment_length_km = maximum_polygon_segment_length_km(
			*continent, parameters.planet_radius_km);
	metrics.maximum_segment_length_km = metrics.maximum_continent_segment_length_km;
	double total_craton_boundary_length_km = 0;
	unsigned int total_craton_segment_count = 0;
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator iter =
			cratons.begin();
			iter != cratons.end();
			++iter)
	{
		metrics.total_craton_area_fraction += polygon_area_fraction(**iter);
		metrics.craton_vertex_count += polygon_vertex_count(**iter);
		total_craton_boundary_length_km += total_polygon_segment_length_km(
				**iter, parameters.planet_radius_km);
		total_craton_segment_count += polygon_segment_count(**iter);
		metrics.maximum_segment_length_km = std::max(
				metrics.maximum_segment_length_km,
				maximum_polygon_segment_length_km(**iter, parameters.planet_radius_km));
	}
	metrics.craton_fill_fraction = metrics.total_craton_area_fraction / metrics.continent_area_fraction;
	metrics.average_craton_segment_length_km =
			total_craton_boundary_length_km / total_craton_segment_count;

	return Result(continent, cratons, to_point_on_sphere(frame.centre), metrics);
}
