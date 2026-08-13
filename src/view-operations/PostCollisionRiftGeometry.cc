/* $Id$ */

#include "PostCollisionRiftGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "maths/GeometryDistance.h"
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

	Vec3 operator-(const Vec3 &value)
	{
		return Vec3(-value.x, -value.y, -value.z);
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
			throw std::runtime_error("Cannot normalise a zero-length rerift vector.");
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

	GPlatesMaths::PointOnSphere interpolate(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			double fraction)
	{
		const double angle = angular_distance(start, end);
		if (angle <= 1e-14)
		{
			return start;
		}
		const double sine = std::sin(angle);
		return to_point(
				to_vec3(start) * (std::sin((1.0 - fraction) * angle) / sine) +
				to_vec3(end) * (std::sin(fraction * angle) / sine));
	}

	std::vector<GPlatesMaths::PointOnSphere> sample_polyline(
			const GPlatesMaths::PolylineOnSphere &polyline,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		for (unsigned int index = 0; index + 1 < polyline.number_of_vertices(); ++index)
		{
			const GPlatesMaths::PointOnSphere &start = polyline.get_vertex(index);
			const GPlatesMaths::PointOnSphere &end = polyline.get_vertex(index + 1);
			const unsigned int pieces = std::max(1u, static_cast<unsigned int>(
					std::ceil(angular_distance(start, end) / maximum_segment_angle)));
			for (unsigned int piece = 0; piece < pieces; ++piece)
			{
				points.push_back(interpolate(start, end,
						static_cast<double>(piece) / pieces));
			}
		}
		points.push_back(polyline.get_vertex(polyline.number_of_vertices() - 1));
		return points;
	}

	std::vector<GPlatesMaths::PointOnSphere> densify_polyline(
			const std::vector<GPlatesMaths::PointOnSphere> &source,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		for (unsigned int index = 0; index + 1 < source.size(); ++index)
		{
			const unsigned int pieces = std::max(1u, static_cast<unsigned int>(
					std::ceil(angular_distance(source[index], source[index + 1]) /
							maximum_segment_angle)));
			for (unsigned int piece = 0; piece < pieces; ++piece)
			{
				points.push_back(interpolate(
						source[index], source[index + 1],
						static_cast<double>(piece) / pieces));
			}
		}
		points.push_back(source.back());
		return points;
	}

	double noise(unsigned int index, unsigned int seed)
	{
		const double value = std::sin(
				(index + 1.0) * 78.233 + (seed + 11.0) * 17.719) * 43758.5453;
		return 2.0 * (value - std::floor(value)) - 1.0;
	}

	GPlatesMaths::PointOnSphere move_along_tangent(
			const GPlatesMaths::PointOnSphere &point,
			const Vec3 &tangent,
			double angle)
	{
		const Vec3 radial = to_vec3(point);
		const Vec3 unit_tangent = normalise(tangent - radial * dot(tangent, radial));
		return to_point(radial * std::cos(angle) + unit_tangent * std::sin(angle));
	}
}


GPlatesViewOperations::PostCollisionRiftGeometry::Parameters::Parameters() :
	offset_km(120.0),
	wiggle(0.25),
	maximum_segment_length_km(90.0),
	end_extension_km(500.0),
	side(1),
	random_seed(1),
	planet_radius_km(6371.0088)
{ }


GPlatesViewOperations::PostCollisionRiftGeometry::Metrics::Metrics() :
	path_length_km(0),
	mean_suture_offset_km(0),
	minimum_suture_offset_km(0),
	maximum_segment_length_km(0),
	vertex_count(0)
{ }


GPlatesViewOperations::PostCollisionRiftGeometry::Result::Result(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &rift_) :
	rift(rift_)
{ }


GPlatesViewOperations::PostCollisionRiftGeometry::Result
GPlatesViewOperations::PostCollisionRiftGeometry::generate(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &suture,
		const Parameters &parameters)
{
	if (parameters.offset_km < 20 || parameters.wiggle < 0 || parameters.wiggle > 0.8 ||
			parameters.maximum_segment_length_km <= 0 || parameters.end_extension_km < 0 ||
			parameters.planet_radius_km <= 0 || (parameters.side != -1 && parameters.side != 1))
	{
		throw std::invalid_argument("Post-collision rerift parameters are invalid.");
	}
	if (suture->number_of_vertices() < 2)
	{
		throw std::invalid_argument("A post-collision rerift needs a suture with at least two vertices.");
	}

	const double maximum_segment_angle =
			parameters.maximum_segment_length_km / parameters.planet_radius_km;
	const std::vector<GPlatesMaths::PointOnSphere> centreline =
			sample_polyline(*suture, maximum_segment_angle);
	std::vector<GPlatesMaths::PointOnSphere> offset_points;
	double offset_sum = 0;
	double minimum_offset = 1e100;
	for (unsigned int index = 0; index < centreline.size(); ++index)
	{
		const unsigned int previous = index == 0 ? 0 : index - 1;
		const unsigned int next = index + 1 == centreline.size() ? index : index + 1;
		const Vec3 radial = to_vec3(centreline[index]);
		const Vec3 chord = to_vec3(centreline[next]) - to_vec3(centreline[previous]);
		const Vec3 tangent = normalise(chord - radial * dot(chord, radial));
		const Vec3 normal = normalise(cross(radial, tangent)) * parameters.side;
		const double wave = 0.60 * std::sin(index * 0.79 + parameters.random_seed * 0.17) +
				0.40 * noise(index, parameters.random_seed);
		const double local_offset_km = parameters.offset_km *
				std::max(0.35, 1.0 + parameters.wiggle * wave);
		offset_points.push_back(move_along_tangent(
				centreline[index], normal, local_offset_km / parameters.planet_radius_km));
		offset_sum += local_offset_km;
		minimum_offset = std::min(minimum_offset, local_offset_km);
	}

	std::vector<GPlatesMaths::PointOnSphere> rift_points;
	if (parameters.end_extension_km > 0)
	{
		const unsigned int pieces = std::max(1u, static_cast<unsigned int>(
				std::ceil(parameters.end_extension_km /
						parameters.maximum_segment_length_km)));
		const Vec3 first_tangent = to_vec3(offset_points[1]) - to_vec3(offset_points[0]);
		for (unsigned int piece = pieces; piece > 0; --piece)
		{
			rift_points.push_back(move_along_tangent(
					offset_points.front(), -first_tangent,
					(parameters.end_extension_km * piece / pieces) /
							parameters.planet_radius_km));
		}
	}
	rift_points.insert(rift_points.end(), offset_points.begin(), offset_points.end());
	if (parameters.end_extension_km > 0)
	{
		const unsigned int pieces = std::max(1u, static_cast<unsigned int>(
				std::ceil(parameters.end_extension_km /
						parameters.maximum_segment_length_km)));
		const Vec3 last_tangent = to_vec3(offset_points.back()) -
				to_vec3(offset_points[offset_points.size() - 2]);
		for (unsigned int piece = 1; piece <= pieces; ++piece)
		{
			rift_points.push_back(move_along_tangent(
					offset_points.back(), last_tangent,
					(parameters.end_extension_km * piece / pieces) /
							parameters.planet_radius_km));
		}
	}
	// The lateral wiggle changes the distance between neighbouring samples.
	// Enforce the requested maximum on the final offset-and-extended path.
	rift_points = densify_polyline(rift_points, maximum_segment_angle);

	Result result(GPlatesMaths::PolylineOnSphere::create(rift_points));
	result.metrics.mean_suture_offset_km = offset_sum / offset_points.size();
	result.metrics.minimum_suture_offset_km = minimum_offset;
	result.metrics.vertex_count = rift_points.size();
	for (unsigned int index = 0; index + 1 < rift_points.size(); ++index)
	{
		const double length_km = angular_distance(
				rift_points[index], rift_points[index + 1]) * parameters.planet_radius_km;
		result.metrics.path_length_km += length_km;
		result.metrics.maximum_segment_length_km = std::max(
				result.metrics.maximum_segment_length_km, length_km);
	}
	return result;
}
