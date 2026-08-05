/* $Id$ */

/**
 * \file
 * Implements spherical contact, suture and collisional-orogen proposals.
 */

#include "CollisionGeometry.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include <boost/ref.hpp>

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
			throw std::runtime_error("Cannot normalise a zero-length collision vector.");
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
		return std::acos(std::max(-1.0, std::min(1.0, dot(to_vec3(lhs), to_vec3(rhs)))));
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

	std::vector<GPlatesMaths::PointOnSphere> sample_exterior_ring(
			const GPlatesMaths::PolygonOnSphere &polygon,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> samples;
		const unsigned int vertex_count = polygon.number_of_vertices_in_exterior_ring();
		for (unsigned int index = 0; index < vertex_count; ++index)
		{
			const GPlatesMaths::PointOnSphere &start = polygon.get_exterior_ring_vertex(index);
			const GPlatesMaths::PointOnSphere &end =
					polygon.get_exterior_ring_vertex((index + 1) % vertex_count);
			const unsigned int pieces = std::max(1u, static_cast<unsigned int>(
					std::ceil(angular_distance(start, end) / maximum_segment_angle)));
			for (unsigned int piece = 0; piece < pieces; ++piece)
			{
				samples.push_back(interpolate(start, end,
						static_cast<double>(piece) / pieces));
			}
		}
		return samples;
	}

	struct ContactSample
	{
		ContactSample() :
			accepted(false),
			distance_km(0),
			incoming(GPlatesMaths::UnitVector3D::xBasis()),
			receiving(GPlatesMaths::UnitVector3D::xBasis()),
			midpoint(GPlatesMaths::UnitVector3D::xBasis())
		{ }
		bool accepted;
		double distance_km;
		GPlatesMaths::PointOnSphere incoming;
		GPlatesMaths::PointOnSphere receiving;
		GPlatesMaths::PointOnSphere midpoint;
	};

	std::vector<unsigned int> longest_cyclic_run(
			const std::vector<ContactSample> &samples)
	{
		std::vector<unsigned int> best;
		if (samples.empty())
		{
			return best;
		}
		unsigned int first_rejected = samples.size();
		for (unsigned int index = 0; index < samples.size(); ++index)
		{
			if (!samples[index].accepted)
			{
				first_rejected = index;
				break;
			}
		}
		if (first_rejected == samples.size())
		{
			for (unsigned int index = 0; index < samples.size(); ++index)
			{
				best.push_back(index);
			}
			return best;
		}

		std::vector<unsigned int> current;
		for (unsigned int offset = 1; offset <= samples.size(); ++offset)
		{
			const unsigned int index = (first_rejected + offset) % samples.size();
			if (samples[index].accepted)
			{
				current.push_back(index);
			}
			else
			{
				if (current.size() > best.size())
				{
					best = current;
				}
				current.clear();
			}
		}
		if (current.size() > best.size())
		{
			best = current;
		}
		return best;
	}

	std::vector<GPlatesMaths::PointOnSphere> smooth_path(
			std::vector<GPlatesMaths::PointOnSphere> points,
			unsigned int iterations)
	{
		for (unsigned int iteration = 0; iteration < iterations && points.size() > 2; ++iteration)
		{
			std::vector<GPlatesMaths::PointOnSphere> smoothed(points);
			for (unsigned int index = 1; index + 1 < points.size(); ++index)
			{
				smoothed[index] = to_point(
						to_vec3(points[index - 1]) * 0.25 +
						to_vec3(points[index]) * 0.50 +
						to_vec3(points[index + 1]) * 0.25);
			}
			points.swap(smoothed);
		}
		return points;
	}

	std::vector<GPlatesMaths::PointOnSphere> remove_near_duplicates(
			const std::vector<GPlatesMaths::PointOnSphere> &points,
			double minimum_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> result;
		for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point_iter =
				points.begin(); point_iter != points.end(); ++point_iter)
		{
			if (result.empty() || angular_distance(result.back(), *point_iter) >= minimum_angle)
			{
				result.push_back(*point_iter);
			}
		}
		return result;
	}

	void remove_duplicate_closing_point(
			std::vector<GPlatesMaths::PointOnSphere> &points,
			double minimum_angle)
	{
		if (points.size() > 1 && angular_distance(points.front(), points.back()) < minimum_angle)
		{
			points.pop_back();
		}
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type deform_margin_to_suture(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PolylineOnSphere &suture,
			double reach_angle,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> exterior =
				sample_exterior_ring(polygon, maximum_segment_angle);
		const double fully_welded_angle = 0.35 * reach_angle;
		for (std::vector<GPlatesMaths::PointOnSphere>::iterator point_iter = exterior.begin();
			point_iter != exterior.end(); ++point_iter)
		{
			GPlatesMaths::UnitVector3D closest = GPlatesMaths::UnitVector3D::xBasis();
			const double distance = GPlatesMaths::minimum_distance(
					*point_iter, suture, boost::none,
					boost::optional<GPlatesMaths::UnitVector3D &>(closest)).calculate_angle().dval();
			if (distance >= reach_angle)
			{
				continue;
			}
			double weight = 1.0;
			if (distance > fully_welded_angle)
			{
				const double linear = (reach_angle - distance) /
						std::max(1e-12, reach_angle - fully_welded_angle);
				weight = linear * linear * (3.0 - 2.0 * linear);
			}
			*point_iter = interpolate(*point_iter, GPlatesMaths::PointOnSphere(closest), weight);
		}
		exterior = remove_near_duplicates(exterior, 0.1 / 6371.0088);
		remove_duplicate_closing_point(exterior, 0.1 / 6371.0088);
		if (exterior.size() < 3)
		{
			throw std::runtime_error("Collision deformation collapsed a continental polygon.");
		}

		typedef std::vector<GPlatesMaths::PointOnSphere> ring_type;
		std::vector<ring_type> interior_rings;
		for (unsigned int ring_index = 0; ring_index < polygon.number_of_interior_rings(); ++ring_index)
		{
			ring_type interior;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
						polygon.interior_ring_vertex_begin(ring_index);
				point_iter != polygon.interior_ring_vertex_end(ring_index); ++point_iter)
			{
				interior.push_back(*point_iter);
			}
			interior_rings.push_back(interior);
		}
		return GPlatesMaths::PolygonOnSphere::create(exterior, interior_rings, true);
	}

	double path_length(const std::vector<GPlatesMaths::PointOnSphere> &points)
	{
		double length = 0;
		for (unsigned int index = 0; index + 1 < points.size(); ++index)
		{
			length += angular_distance(points[index], points[index + 1]);
		}
		return length;
	}

	double deterministic_noise(unsigned int index)
	{
		const double value = std::sin((index + 7) * 91.733) * 43758.5453;
		return 2.0 * (value - std::floor(value)) - 1.0;
	}

	GPlatesMaths::PointOnSphere offset_point(
			const GPlatesMaths::PointOnSphere &point,
			const Vec3 &tangent,
			double offset_angle)
	{
		const Vec3 radial = to_vec3(point);
		const Vec3 normal = normalise(cross(radial, tangent));
		return to_point(radial * std::cos(offset_angle) + normal * std::sin(offset_angle));
	}

	GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type make_corridor(
			const std::vector<GPlatesMaths::PointOnSphere> &centreline,
			double width_angle,
			double irregularity)
	{
		std::vector<GPlatesMaths::PointOnSphere> left;
		std::vector<GPlatesMaths::PointOnSphere> right;
		for (unsigned int index = 0; index < centreline.size(); ++index)
		{
			const unsigned int previous = index == 0 ? 0 : index - 1;
			const unsigned int next = index + 1 == centreline.size() ? index : index + 1;
			const Vec3 radial = to_vec3(centreline[index]);
			const Vec3 chord = to_vec3(centreline[next]) - to_vec3(centreline[previous]);
			const Vec3 tangent = normalise(chord - radial * dot(chord, radial));
			const double local_half_width = 0.5 * width_angle *
					(1.0 + irregularity * deterministic_noise(index));
			left.push_back(offset_point(centreline[index], tangent, local_half_width));
			right.push_back(offset_point(centreline[index], tangent, -local_half_width));
		}
		std::vector<GPlatesMaths::PointOnSphere> ring(left);
		for (std::vector<GPlatesMaths::PointOnSphere>::reverse_iterator point_iter =
				right.rbegin(); point_iter != right.rend(); ++point_iter)
		{
			ring.push_back(*point_iter);
		}
		return GPlatesMaths::PolygonOnSphere::create(ring.begin(), ring.end());
	}

	double maximum_polygon_segment_length(
			const GPlatesMaths::PolygonOnSphere &polygon,
			double planet_radius_km)
	{
		double maximum = 0;
		GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator previous =
				polygon.exterior_ring_vertex_end();
		--previous;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.exterior_ring_vertex_begin();
			vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
		{
			maximum = std::max(maximum,
					angular_distance(*previous, *vertex_iter) * planet_radius_km);
			previous = vertex_iter;
		}
		return maximum;
	}
}


GPlatesViewOperations::CollisionGeometry::Parameters::Parameters() :
	collision_type(URAL_OROGENY),
	contact_threshold_km(250.0),
	belt_width_km(180.0),
	maximum_segment_length_km(100.0),
	smoothing_iterations(2),
	belt_irregularity(0.12),
	deformation_reach_km(350.0),
	planet_radius_km(6371.0088)
{ }


GPlatesViewOperations::CollisionGeometry::Metrics::Metrics() :
	minimum_gap_km(0),
	contact_length_km(0),
	incoming_to_receiving_area_ratio(0),
	incoming_overlap_fraction(0),
	receiving_overlap_fraction(0),
	maximum_suture_segment_km(0),
	maximum_belt_segment_km(0),
	post_deformation_gap_km(0),
	contact_sample_count(0)
{ }


GPlatesViewOperations::CollisionGeometry::Result::Result(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &suture_,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &orogenic_belt_,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &deformed_incoming_,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &deformed_receiving_,
		const GPlatesMaths::PointOnSphere &incoming_contact_,
		const GPlatesMaths::PointOnSphere &receiving_contact_) :
	suture(suture_),
	orogenic_belt(orogenic_belt_),
	deformed_incoming(deformed_incoming_),
	deformed_receiving(deformed_receiving_),
	incoming_contact(incoming_contact_),
	receiving_contact(receiving_contact_)
{ }


GPlatesViewOperations::CollisionGeometry::CollisionType
GPlatesViewOperations::CollisionGeometry::recommend_collision_type(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &incoming,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &receiving,
		const boost::optional<double> &closing_speed_cm_per_year,
		unsigned int precursor_collision_count)
{
	const double area_ratio = incoming->get_area().dval() /
			std::max(1e-12, receiving->get_area().dval());
	if (area_ratio < 0.18)
	{
		return ARC_OR_TERRANE_ACCRETION;
	}
	if ((closing_speed_cm_per_year && *closing_speed_cm_per_year >= 5.0) ||
			precursor_collision_count >= 2)
	{
		return HIMALAYAN_OROGENY;
	}
	return URAL_OROGENY;
}


double
GPlatesViewOperations::CollisionGeometry::default_belt_width_km(
		CollisionType collision_type)
{
	switch (collision_type)
	{
	case ARC_OR_TERRANE_ACCRETION: return 120.0;
	case HIMALAYAN_OROGENY: return 450.0;
	case URAL_OROGENY: return 180.0;
	}
	return 180.0;
}


GPlatesViewOperations::CollisionGeometry::Result
GPlatesViewOperations::CollisionGeometry::generate(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &incoming,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &receiving,
		const Parameters &parameters)
{
	if (parameters.contact_threshold_km <= 0 || parameters.belt_width_km <= 0 ||
			parameters.maximum_segment_length_km <= 0 || parameters.planet_radius_km <= 0 ||
			parameters.belt_irregularity < 0 || parameters.belt_irregularity > 0.35 ||
			parameters.deformation_reach_km <= 0 ||
			parameters.smoothing_iterations > 8)
	{
		throw std::invalid_argument("Collision proposal parameters are invalid.");
	}

	GPlatesMaths::UnitVector3D closest_incoming = GPlatesMaths::UnitVector3D::xBasis();
	GPlatesMaths::UnitVector3D closest_receiving = GPlatesMaths::UnitVector3D::xBasis();
	const GPlatesMaths::AngularDistance minimum_distance =
			GPlatesMaths::minimum_distance(
					*incoming, *receiving, true, true, boost::none,
					boost::make_tuple(boost::ref(closest_incoming), boost::ref(closest_receiving)));
	const double minimum_gap_km = minimum_distance.calculate_angle().dval() *
			parameters.planet_radius_km;
	if (minimum_gap_km > parameters.contact_threshold_km + 1e-6)
	{
		throw std::runtime_error("The selected continents are outside the collision contact threshold.");
	}

	const double sample_angle = parameters.maximum_segment_length_km / parameters.planet_radius_km;
	const std::vector<GPlatesMaths::PointOnSphere> incoming_samples =
			sample_exterior_ring(*incoming, sample_angle);
	std::vector<ContactSample> contacts(incoming_samples.size());
	unsigned int incoming_overlap_count = 0;
	for (unsigned int index = 0; index < incoming_samples.size(); ++index)
	{
		GPlatesMaths::UnitVector3D closest = GPlatesMaths::UnitVector3D::xBasis();
		const GPlatesMaths::AngularDistance outline_distance =
				GPlatesMaths::minimum_distance(
						incoming_samples[index], *receiving, false, boost::none,
						boost::optional<GPlatesMaths::UnitVector3D &>(closest));
		const bool inside = receiving->is_point_in_polygon(incoming_samples[index]);
		if (inside)
		{
			++incoming_overlap_count;
		}
		contacts[index].distance_km = outline_distance.calculate_angle().dval() *
				parameters.planet_radius_km;
		contacts[index].accepted = inside ||
				contacts[index].distance_km <= parameters.contact_threshold_km;
		contacts[index].incoming = incoming_samples[index];
		contacts[index].receiving = GPlatesMaths::PointOnSphere(closest);
		contacts[index].midpoint = interpolate(
				contacts[index].incoming, contacts[index].receiving, 0.5);
	}
	const std::vector<unsigned int> run = longest_cyclic_run(contacts);
	if (run.size() < 2)
	{
		throw std::runtime_error(
				"The selected continents touch at too little of their margins to derive a collision corridor.");
	}

	std::vector<GPlatesMaths::PointOnSphere> suture_points;
	for (std::vector<unsigned int>::const_iterator index_iter = run.begin();
		index_iter != run.end(); ++index_iter)
	{
		suture_points.push_back(contacts[*index_iter].midpoint);
	}
	suture_points = remove_near_duplicates(suture_points, 15.0 / parameters.planet_radius_km);
	suture_points = smooth_path(suture_points, parameters.smoothing_iterations);
	if (suture_points.size() < 2)
	{
		throw std::runtime_error("Collision contact collapsed to a single point after smoothing.");
	}

	const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type suture =
			GPlatesMaths::PolylineOnSphere::create(suture_points);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type belt =
			make_corridor(suture_points,
					parameters.belt_width_km / parameters.planet_radius_km,
					parameters.belt_irregularity);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type deformed_incoming =
			deform_margin_to_suture(*incoming, *suture,
					parameters.deformation_reach_km / parameters.planet_radius_km,
					sample_angle);
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type deformed_receiving =
			deform_margin_to_suture(*receiving, *suture,
					parameters.deformation_reach_km / parameters.planet_radius_km,
					sample_angle);
	Result result(
			suture, belt, deformed_incoming, deformed_receiving,
			GPlatesMaths::PointOnSphere(closest_incoming),
			GPlatesMaths::PointOnSphere(closest_receiving));
	result.metrics.minimum_gap_km = minimum_gap_km;
	result.metrics.contact_length_km = path_length(suture_points) * parameters.planet_radius_km;
	result.metrics.incoming_to_receiving_area_ratio = incoming->get_area().dval() /
			std::max(1e-12, receiving->get_area().dval());
	result.metrics.incoming_overlap_fraction = incoming_samples.empty() ? 0.0 :
			static_cast<double>(incoming_overlap_count) / incoming_samples.size();
	const std::vector<GPlatesMaths::PointOnSphere> receiving_samples =
			sample_exterior_ring(*receiving, sample_angle);
	unsigned int receiving_overlap_count = 0;
	for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point_iter =
		receiving_samples.begin(); point_iter != receiving_samples.end(); ++point_iter)
	{
		if (incoming->is_point_in_polygon(*point_iter))
		{
			++receiving_overlap_count;
		}
	}
	result.metrics.receiving_overlap_fraction = receiving_samples.empty() ? 0.0 :
			static_cast<double>(receiving_overlap_count) / receiving_samples.size();
	result.metrics.contact_sample_count = suture_points.size();
	for (unsigned int index = 0; index + 1 < suture_points.size(); ++index)
	{
		result.metrics.maximum_suture_segment_km = std::max(
				result.metrics.maximum_suture_segment_km,
				angular_distance(suture_points[index], suture_points[index + 1]) *
						parameters.planet_radius_km);
	}
	result.metrics.maximum_belt_segment_km =
			maximum_polygon_segment_length(*result.orogenic_belt, parameters.planet_radius_km);
	result.metrics.post_deformation_gap_km = GPlatesMaths::minimum_distance(
			*result.deformed_incoming, *result.deformed_receiving,
			false, false).calculate_angle().dval() * parameters.planet_radius_km;
	return result;
}
