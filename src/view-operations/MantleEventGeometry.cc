/* $Id$ */

#include "MantleEventGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "maths/UnitVector3D.h"


namespace
{
	struct Vec3
	{
		Vec3() : x(0), y(0), z(0) { }
		Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) { }
		double x;
		double y;
		double z;
	};

	Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
	}

	Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z);
	}

	Vec3 operator*(const Vec3 &value, double scale)
	{
		return Vec3(value.x * scale, value.y * scale, value.z * scale);
	}

	double dot(const Vec3 &lhs, const Vec3 &rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	Vec3 cross(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	double magnitude(const Vec3 &value)
	{
		return std::sqrt(dot(value, value));
	}

	Vec3 normalise(const Vec3 &value)
	{
		const double length = magnitude(value);
		if (length <= 1e-14)
		{
			throw std::runtime_error("Cannot normalise a zero-length mantle-event vector.");
		}
		return value * (1.0 / length);
	}

	Vec3 to_vec3(const GPlatesMaths::PointOnSphere &point)
	{
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		return Vec3(vector.x().dval(), vector.y().dval(), vector.z().dval());
	}

	GPlatesMaths::PointOnSphere to_point(const Vec3 &vector)
	{
		const Vec3 unit = normalise(vector);
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(unit.x, unit.y, unit.z));
	}

	double angular_distance(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		return std::acos(std::max(-1.0, std::min(1.0,
				dot(to_vec3(lhs), to_vec3(rhs)))));
	}

	GPlatesMaths::PointOnSphere interpolate_great_circle(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			double fraction)
	{
		const Vec3 start_vector = to_vec3(start);
		const Vec3 end_vector = to_vec3(end);
		const double angle = angular_distance(start, end);
		if (angle <= 1e-12)
		{
			return start;
		}
		const double inverse_sine = 1.0 / std::sin(angle);
		return to_point(
				start_vector * (std::sin((1.0 - fraction) * angle) * inverse_sine) +
				end_vector * (std::sin(fraction * angle) * inverse_sine));
	}

	double noise(unsigned int index, unsigned int seed)
	{
		const double value = std::sin(
				(index + 3.0) * 91.733 + (seed + 7.0) * 13.127) * 43758.5453;
		return 2.0 * (value - std::floor(value)) - 1.0;
	}

	GPlatesMaths::PointOnSphere offset_point(
			const GPlatesMaths::PointOnSphere &centre,
			double azimuth,
			double angular_radius)
	{
		const Vec3 radial = to_vec3(centre);
		const Vec3 reference = std::fabs(radial.z) < 0.85
				? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		const Vec3 east = normalise(cross(reference, radial));
		const Vec3 north = normalise(cross(radial, east));
		const Vec3 direction = east * std::cos(azimuth) + north * std::sin(azimuth);
		return to_point(radial * std::cos(angular_radius) +
				direction * std::sin(angular_radius));
	}

	GPlatesMaths::PointOnSphere choose_centre(
			const GPlatesMaths::PolygonOnSphere &host,
			const boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &rift,
			const GPlatesViewOperations::MantleEventGeometry::Parameters &parameters,
			bool &used_rift)
	{
		used_rift = false;
		if (parameters.rift_triggered && rift)
		{
			std::vector<GPlatesMaths::PointOnSphere> candidates;
			for (unsigned int index = 0; index < (*rift)->number_of_vertices(); ++index)
			{
				const GPlatesMaths::PointOnSphere &point = (*rift)->get_vertex(index);
				if (host.is_point_in_polygon(point))
				{
					candidates.push_back(point);
				}
			}
			if (!candidates.empty())
			{
				used_rift = true;
				return candidates[parameters.random_seed % candidates.size()];
			}
		}

		const GPlatesMaths::PointOnSphere interior(host.get_interior_centroid());
		// Prefer a repeatable, non-central placement where the polygon permits it.
		for (unsigned int attempt = 0; attempt < 48; ++attempt)
		{
			const double azimuth = 6.283185307179586 *
					(0.618033988749895 * (attempt + parameters.random_seed + 1));
			const double radius = (120.0 + 35.0 * (attempt % 8)) /
					parameters.planet_radius_km;
			const GPlatesMaths::PointOnSphere candidate =
					offset_point(interior, azimuth, radius);
			if (host.is_point_in_polygon(candidate))
			{
				return candidate;
			}
		}
		return interior;
	}

	std::vector<GPlatesMaths::PointOnSphere> create_outline(
			const GPlatesMaths::PointOnSphere &centre,
			double radius_km,
			const GPlatesViewOperations::MantleEventGeometry::Parameters &parameters)
	{
		const unsigned int vertex_count = std::max(16u, static_cast<unsigned int>(
				std::ceil(6.283185307179586 * radius_km /
						parameters.maximum_segment_length_km)));
		std::vector<GPlatesMaths::PointOnSphere> coarse_outline;
		coarse_outline.reserve(vertex_count);
		for (unsigned int index = 0; index < vertex_count; ++index)
		{
			const double angle = 6.283185307179586 * index / vertex_count;
			const double wave = 0.55 * std::sin(3.0 * angle + parameters.random_seed * 0.21) +
					0.30 * std::sin(5.0 * angle + parameters.random_seed * 0.37) +
					0.15 * noise(index, parameters.random_seed);
			const double local_radius = radius_km *
					std::max(0.55, 1.0 + parameters.irregularity * wave);
			coarse_outline.push_back(offset_point(
					centre, angle, local_radius / parameters.planet_radius_km));
		}

		// Irregular radial displacement can make neighbouring vertices farther
		// apart than the nominal circumference-based sampling interval.  Densify
		// each great-circle edge so the requested maximum is an actual bound.
		std::vector<GPlatesMaths::PointOnSphere> outline;
		for (unsigned int index = 0; index < coarse_outline.size(); ++index)
		{
			const GPlatesMaths::PointOnSphere &start = coarse_outline[index];
			const GPlatesMaths::PointOnSphere &end =
					coarse_outline[(index + 1) % coarse_outline.size()];
			const double length_km = angular_distance(start, end) *
					parameters.planet_radius_km;
			const unsigned int segment_count = std::max(1u,
					static_cast<unsigned int>(std::ceil(
							length_km / parameters.maximum_segment_length_km)));
			outline.push_back(start);
			for (unsigned int segment = 1; segment < segment_count; ++segment)
			{
				outline.push_back(interpolate_great_circle(
						start, end, static_cast<double>(segment) / segment_count));
			}
		}
		return outline;
	}

	bool outline_inside(
			const GPlatesMaths::PolygonOnSphere &host,
			const std::vector<GPlatesMaths::PointOnSphere> &outline)
	{
		for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point_iter =
				outline.begin(); point_iter != outline.end(); ++point_iter)
		{
			if (!host.is_point_in_polygon(*point_iter))
			{
				return false;
			}
		}
		return true;
	}
}


GPlatesViewOperations::MantleEventGeometry::Parameters::Parameters() :
	diameter_km(700.0),
	irregularity(0.28),
	maximum_segment_length_km(75.0),
	rift_triggered(true),
	random_seed(1),
	planet_radius_km(6371.0088)
{ }


GPlatesViewOperations::MantleEventGeometry::Metrics::Metrics() :
	requested_diameter_km(0),
	actual_diameter_km(0),
	maximum_segment_length_km(0),
	boundary_vertex_count(0),
	containment_reductions(0),
	used_rift(false)
{ }


GPlatesViewOperations::MantleEventGeometry::Result::Result(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &lip_outline_,
		const GPlatesMaths::PointOnSphere &hotspot_position_) :
	lip_outline(lip_outline_),
	hotspot_position(hotspot_position_)
{ }


GPlatesViewOperations::MantleEventGeometry::Result
GPlatesViewOperations::MantleEventGeometry::generate(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &host_continent,
		const boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &rift,
		const Parameters &parameters)
{
	if (parameters.diameter_km < 50 || parameters.diameter_km > 4000 ||
			parameters.irregularity < 0 || parameters.irregularity > 0.75 ||
			parameters.maximum_segment_length_km <= 0 || parameters.planet_radius_km <= 0)
	{
		throw std::invalid_argument("Mantle-event geometry parameters are invalid.");
	}

	bool used_rift = false;
	const GPlatesMaths::PointOnSphere centre = choose_centre(
			*host_continent, rift, parameters, used_rift);
	double radius_km = 0.5 * parameters.diameter_km;
	unsigned int reductions = 0;
	std::vector<GPlatesMaths::PointOnSphere> outline =
			create_outline(centre, radius_km, parameters);
	while (!outline_inside(*host_continent, outline) && reductions < 18)
	{
		radius_km *= 0.84;
		++reductions;
		outline = create_outline(centre, radius_km, parameters);
	}
	if (!outline_inside(*host_continent, outline) || radius_km < 25.0)
	{
		throw std::runtime_error(
				"The selected continental crust is too narrow around the proposed mantle event. Choose another seed, a smaller LIP, or a different host.");
	}

	Result result(GPlatesMaths::PolygonOnSphere::create(outline), centre);
	result.metrics.requested_diameter_km = parameters.diameter_km;
	result.metrics.actual_diameter_km = 2.0 * radius_km;
	result.metrics.boundary_vertex_count = outline.size();
	result.metrics.containment_reductions = reductions;
	result.metrics.used_rift = used_rift;
	for (unsigned int index = 0; index < outline.size(); ++index)
	{
		const double length_km = angular_distance(
				outline[index], outline[(index + 1) % outline.size()]) *
				parameters.planet_radius_km;
		result.metrics.maximum_segment_length_km = std::max(
				result.metrics.maximum_segment_length_km, length_km);
	}
	return result;
}
