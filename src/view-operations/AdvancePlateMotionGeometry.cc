/* $Id$ */

#include "AdvancePlateMotionGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

	Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) { return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z); }
	Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs) { return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z); }
	Vec3 operator*(const Vec3 &value, double scale) { return Vec3(value.x * scale, value.y * scale, value.z * scale); }

	double dot(const Vec3 &lhs, const Vec3 &rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }
	double magnitude(const Vec3 &value) { return std::sqrt(dot(value, value)); }

	Vec3 cross(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(
				lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	Vec3 normalise(const Vec3 &value)
	{
		const double length = magnitude(value);
		if (length <= 1e-14)
		{
			throw std::runtime_error("Cannot normalise a zero-length plate-motion vector.");
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
		return GPlatesMaths::PointOnSphere(GPlatesMaths::UnitVector3D(unit.x, unit.y, unit.z));
	}

	double angular_distance(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		return std::acos(std::max(-1.0, std::min(1.0, dot(to_vec3(lhs), to_vec3(rhs)))));
	}

	GPlatesMaths::PointOnSphere spherical_interpolate(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			double fraction)
	{
		const Vec3 a = to_vec3(start);
		const Vec3 b = to_vec3(end);
		const double angle = angular_distance(start, end);
		if (angle <= 1e-12)
		{
			return start;
		}
		const double sine = std::sin(angle);
		return to_point(
				a * (std::sin((1.0 - fraction) * angle) / sine) +
				b * (std::sin(fraction * angle) / sine));
	}

	boost::optional<Vec3> tangent_toward(
			const GPlatesMaths::PointOnSphere &origin,
			const GPlatesMaths::PointOnSphere &target)
	{
		const Vec3 r = to_vec3(origin);
		const Vec3 q = to_vec3(target);
		const Vec3 tangent = q - r * dot(q, r);
		if (magnitude(tangent) <= 1e-12)
		{
			return boost::none;
		}
		return normalise(tangent);
	}

	boost::optional<Vec3> continuing_history_tangent(
			const GPlatesMaths::PointOnSphere &previous,
			const GPlatesMaths::PointOnSphere &current)
	{
		const Vec3 previous_vector = to_vec3(previous);
		const Vec3 current_vector = to_vec3(current);
		const Vec3 axis = cross(previous_vector, current_vector);
		if (magnitude(axis) <= 1e-12)
		{
			return boost::none;
		}
		return normalise(cross(normalise(axis), current_vector));
	}

}


GPlatesViewOperations::AdvancePlateMotionGeometry::Parameters::Parameters() :
	history_direction_weight(1.5),
	ridge_push_weight(1.0),
	slab_pull_weight(2.0),
	default_speed_cm_per_year(4.0),
	minimum_speed_cm_per_year(0.5),
	maximum_speed_cm_per_year(10.0),
	motion_path_segments(8),
	planet_radius_km(6371.0088)
{ }


GPlatesViewOperations::AdvancePlateMotionGeometry::Proposal::Proposal(
		const GPlatesMaths::FiniteRotation &motion_rotation_,
		const GPlatesMaths::PointOnSphere &destination_,
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &motion_path_,
		double speed_cm_per_year_,
		bool used_history_,
		unsigned int ridge_count_,
		unsigned int subduction_count_) :
	motion_rotation(motion_rotation_),
	destination(destination_),
	motion_path(motion_path_),
	speed_cm_per_year(speed_cm_per_year_),
	used_history(used_history_),
	ridge_count(ridge_count_),
	subduction_count(subduction_count_)
{ }


GPlatesViewOperations::AdvancePlateMotionGeometry::Proposal
GPlatesViewOperations::AdvancePlateMotionGeometry::generate_motion(
		const GPlatesMaths::PointOnSphere &continent_centre,
		const boost::optional<GPlatesMaths::PointOnSphere> &previous_continent_centre,
		double previous_interval_ma,
		const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &ridges,
		const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &subduction_zones,
		double interval_ma,
		const Parameters &parameters)
{
	if (interval_ma <= 0 || parameters.planet_radius_km <= 0 ||
			parameters.default_speed_cm_per_year <= 0 ||
			parameters.minimum_speed_cm_per_year <= 0 ||
			parameters.maximum_speed_cm_per_year < parameters.minimum_speed_cm_per_year ||
			parameters.motion_path_segments < 1 ||
			parameters.history_direction_weight < 0 ||
			parameters.ridge_push_weight < 0 ||
			parameters.slab_pull_weight < 0)
	{
		throw std::invalid_argument("Plate-motion proposal parameters are invalid.");
	}

	Vec3 direction_sum;
	bool used_history = false;
	double speed_cm_per_year = parameters.default_speed_cm_per_year;
	if (previous_continent_centre && previous_interval_ma > 0)
	{
		const boost::optional<Vec3> history_direction = continuing_history_tangent(
				*previous_continent_centre, continent_centre);
		const double previous_angle = angular_distance(*previous_continent_centre, continent_centre);
		if (history_direction && previous_angle > 1e-10)
		{
			direction_sum = direction_sum + *history_direction * parameters.history_direction_weight;
			speed_cm_per_year =
					(previous_angle * parameters.planet_radius_km / previous_interval_ma) / 10.0;
			used_history = true;
		}
	}

	for (std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>::const_iterator
			ridge_iter = ridges.begin(); ridge_iter != ridges.end(); ++ridge_iter)
	{
		const GPlatesMaths::PointOnSphere ridge_centre((*ridge_iter)->get_centroid());
		const boost::optional<Vec3> toward_ridge = tangent_toward(continent_centre, ridge_centre);
		if (toward_ridge)
		{
			direction_sum = direction_sum + *toward_ridge * -parameters.ridge_push_weight;
		}
	}

	for (std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>::const_iterator
			trench_iter = subduction_zones.begin(); trench_iter != subduction_zones.end(); ++trench_iter)
	{
		const GPlatesMaths::PointOnSphere trench_centre((*trench_iter)->get_centroid());
		const boost::optional<Vec3> toward_trench = tangent_toward(continent_centre, trench_centre);
		if (toward_trench)
		{
			direction_sum = direction_sum + *toward_trench * parameters.slab_pull_weight;
		}
	}

	if (magnitude(direction_sum) <= 1e-10)
	{
		throw std::runtime_error(
				"The historical, ridge-push and slab-pull directions cancel; edit the boundaries or weights.");
	}

	speed_cm_per_year = std::max(
			parameters.minimum_speed_cm_per_year,
			std::min(parameters.maximum_speed_cm_per_year, speed_cm_per_year));
	const Vec3 direction = normalise(direction_sum);
	const Vec3 centre = to_vec3(continent_centre);
	const Vec3 rotation_axis = normalise(cross(centre, direction));
	const double distance_km = speed_cm_per_year * 10.0 * interval_ma;
	const double rotation_angle = std::min(3.14159265358979323846 - 1e-6,
			distance_km / parameters.planet_radius_km);
	const GPlatesMaths::FiniteRotation motion_rotation = GPlatesMaths::FiniteRotation::create(
			GPlatesMaths::PointOnSphere(
					GPlatesMaths::UnitVector3D(rotation_axis.x, rotation_axis.y, rotation_axis.z)),
			rotation_angle);
	const GPlatesMaths::PointOnSphere destination = motion_rotation * continent_centre;

	std::vector<GPlatesMaths::PointOnSphere> path_points;
	for (unsigned int segment = 0; segment <= parameters.motion_path_segments; ++segment)
	{
		path_points.push_back(spherical_interpolate(
				continent_centre, destination,
				static_cast<double>(segment) / parameters.motion_path_segments));
	}
	return Proposal(
			motion_rotation,
			destination,
			GPlatesMaths::PolylineOnSphere::create(path_points),
			speed_cm_per_year,
			used_history,
			static_cast<unsigned int>(ridges.size()),
			static_cast<unsigned int>(subduction_zones.size()));
}
