/* $Id$ */

/**
 * \file
 * Motion-informed opposite-margin subduction-zone geometry.
 */

#include "InitialSubductionGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>

#include "maths/GeometryDistance.h"
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

	Vec3 operator+(const Vec3 &lhs, const Vec3 &rhs) { return Vec3(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z); }
	Vec3 operator-(const Vec3 &lhs, const Vec3 &rhs) { return Vec3(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z); }
	Vec3 operator*(const Vec3 &value, double scale) { return Vec3(value.x * scale, value.y * scale, value.z * scale); }

	double dot(const Vec2 &lhs, const Vec2 &rhs) { return lhs.x * rhs.x + lhs.y * rhs.y; }
	double magnitude(const Vec2 &value) { return std::sqrt(dot(value, value)); }
	double dot(const Vec3 &lhs, const Vec3 &rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }

	Vec3
	cross(const Vec3 &lhs, const Vec3 &rhs)
	{
		return Vec3(
				lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	Vec2
	normalise(const Vec2 &value)
	{
		const double length = magnitude(value);
		if (length <= 1e-12)
		{
			throw std::runtime_error("Cannot infer spreading direction because the ridge is centred inside the continent.");
		}
		return value * (1.0 / length);
	}

	Vec3
	normalise(const Vec3 &value)
	{
		const double length = std::sqrt(dot(value, value));
		if (length <= 1e-14)
		{
			throw std::runtime_error("Cannot normalise a zero-length spherical vector.");
		}
		return value * (1.0 / length);
	}

	Vec3
	to_vec3(const GPlatesMaths::PointOnSphere &point)
	{
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		return Vec3(vector.x().dval(), vector.y().dval(), vector.z().dval());
	}

	GPlatesMaths::PointOnSphere
	to_point_on_sphere(const Vec3 &vector)
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

	LocalFrame
	make_local_frame(const GPlatesMaths::PolygonOnSphere &continent)
	{
		Vec3 centre_sum;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				continent.exterior_ring_vertex_begin();
			vertex_iter != continent.exterior_ring_vertex_end(); ++vertex_iter)
		{
			centre_sum = centre_sum + to_vec3(*vertex_iter);
		}
		LocalFrame frame;
		frame.centre = normalise(centre_sum);
		const Vec3 reference = std::fabs(frame.centre.z) < 0.9 ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
		frame.east = normalise(cross(reference, frame.centre));
		frame.north = normalise(cross(frame.centre, frame.east));
		return frame;
	}

	Vec2
	project_lambert(
			const LocalFrame &frame,
			const GPlatesMaths::PointOnSphere &point)
	{
		const Vec3 vector = to_vec3(point);
		const double denominator = 1.0 + dot(frame.centre, vector);
		if (denominator <= 1e-10)
		{
			throw std::runtime_error("The selected continent or ridge reaches the antipode of its projection centre.");
		}
		const double scale = std::sqrt(2.0 / denominator);
		return Vec2(scale * dot(frame.east, vector), scale * dot(frame.north, vector));
	}

	GPlatesMaths::PointOnSphere
	unproject_lambert(
			const LocalFrame &frame,
			const Vec2 &point)
	{
		const double radius = magnitude(point);
		if (radius >= 2.0)
		{
			throw std::runtime_error("The generated trench lies outside the Lambert projection disk.");
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
	angular_distance(
			const GPlatesMaths::PointOnSphere &lhs,
			const GPlatesMaths::PointOnSphere &rhs)
	{
		return std::acos(std::max(-1.0, std::min(1.0, dot(to_vec3(lhs), to_vec3(rhs)))));
	}

	GPlatesMaths::PointOnSphere
	spherical_interpolate(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			double interpolation)
	{
		const Vec3 a = to_vec3(start);
		const Vec3 b = to_vec3(end);
		const double angle = angular_distance(start, end);
		if (angle <= 1e-12)
		{
			return start;
		}
		const double sine = std::sin(angle);
		return to_point_on_sphere(
				a * (std::sin((1.0 - interpolation) * angle) / sine) +
				b * (std::sin(interpolation * angle) / sine));
	}

	std::vector<GPlatesMaths::PointOnSphere>
	tessellate(
			const std::vector<GPlatesMaths::PointOnSphere> &anchors,
			double maximum_segment_angle)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		points.push_back(anchors.front());
		for (std::size_t anchor_index = 0; anchor_index + 1 < anchors.size(); ++anchor_index)
		{
			const double angle = angular_distance(anchors[anchor_index], anchors[anchor_index + 1]);
			const unsigned int segment_count = std::max(
					1u, static_cast<unsigned int>(std::ceil(angle / maximum_segment_angle)));
			for (unsigned int segment = 1; segment <= segment_count; ++segment)
			{
				points.push_back(spherical_interpolate(
						anchors[anchor_index], anchors[anchor_index + 1],
						static_cast<double>(segment) / segment_count));
			}
		}
		return points;
	}

	double
	far_margin_at_lateral(
			const std::vector<Vec2> &motion_ring,
			double lateral)
	{
		double far_margin = -std::numeric_limits<double>::max();
		bool found = false;
		for (std::size_t index = 0; index < motion_ring.size(); ++index)
		{
			const Vec2 &a = motion_ring[index];
			const Vec2 &b = motion_ring[(index + 1) % motion_ring.size()];
			if (std::fabs(a.y - b.y) <= 1e-12)
			{
				if (std::fabs(lateral - a.y) <= 1e-10)
				{
					far_margin = std::max(far_margin, std::max(a.x, b.x));
					found = true;
				}
				continue;
			}
			if (lateral < std::min(a.y, b.y) - 1e-10 ||
					lateral > std::max(a.y, b.y) + 1e-10)
			{
				continue;
			}
			const double interpolation = (lateral - a.y) / (b.y - a.y);
			if (interpolation >= -1e-10 && interpolation <= 1.0 + 1e-10)
			{
				far_margin = std::max(far_margin, a.x + interpolation * (b.x - a.x));
				found = true;
			}
		}
		if (!found)
		{
			throw std::runtime_error("Could not sample the opposite continental margin.");
		}
		return far_margin;
	}
}


GPlatesViewOperations::InitialSubductionGeometry::Parameters::Parameters() :
	anchor_count(17),
	smoothing_passes(3),
	maximum_clearance_adjustment_iterations(64),
	width_coverage_fraction(1.0),
	offshore_offset_km(120.0),
	minimum_continental_clearance_km(50.0),
	clearance_adjustment_step_km(100.0),
	endpoint_buffer_km(500.0),
	broad_arc_bow_km(180.0),
	maximum_segment_length_km(140.0),
	planet_radius_km(6371.0088)
{  }


GPlatesViewOperations::InitialSubductionGeometry::Metrics::Metrics() :
	continent_motion_normal_width_km(0),
	trench_motion_normal_span_km(0),
	trench_length_km(0),
	maximum_segment_length_km(0),
	minimum_continental_clearance_km(0),
	endpoint_buffer_km(0),
	clearance_adjustment_iterations(0)
{  }


GPlatesViewOperations::InitialSubductionGeometry::Result::Result(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &trench_,
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &motion_arrow_,
		const Metrics &metrics_) :
	trench(trench_),
	motion_arrow(motion_arrow_),
	metrics(metrics_)
{  }


GPlatesViewOperations::InitialSubductionGeometry::Result
GPlatesViewOperations::InitialSubductionGeometry::generate(
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &mid_ocean_ridge,
		const Parameters &parameters)
{
	if (parameters.anchor_count < 7 || parameters.planet_radius_km <= 0 ||
			parameters.maximum_segment_length_km <= 0 ||
			parameters.maximum_clearance_adjustment_iterations == 0 ||
			parameters.offshore_offset_km < 0 ||
			parameters.minimum_continental_clearance_km < 0 ||
			parameters.clearance_adjustment_step_km <= 0 ||
			parameters.endpoint_buffer_km < 0 ||
			parameters.width_coverage_fraction < 0.5 ||
			parameters.width_coverage_fraction > 1.0)
	{
		throw std::invalid_argument("Opposite-margin trench parameters are invalid.");
	}

	const LocalFrame frame = make_local_frame(*continent);
	std::vector<Vec2> projected_ring;
	for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
			continent->exterior_ring_vertex_begin();
		vertex_iter != continent->exterior_ring_vertex_end(); ++vertex_iter)
	{
		projected_ring.push_back(project_lambert(frame, *vertex_iter));
	}

	Vec2 projected_ridge_centre;
	unsigned int ridge_vertex_count = 0;
	for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
			mid_ocean_ridge->vertex_begin();
		vertex_iter != mid_ocean_ridge->vertex_end(); ++vertex_iter)
	{
		projected_ridge_centre = projected_ridge_centre + project_lambert(frame, *vertex_iter);
		++ridge_vertex_count;
	}
	projected_ridge_centre = projected_ridge_centre * (1.0 / ridge_vertex_count);

	// In the continent-centred frame the ridge-to-continent vector points away
	// from the ridge. This is the side on which a far-margin trench belongs.
	const Vec2 motion = normalise(projected_ridge_centre * -1.0);
	const Vec2 lateral(-motion.y, motion.x);
	std::vector<Vec2> motion_ring;
	double minimum_lateral = std::numeric_limits<double>::max();
	double maximum_lateral = -std::numeric_limits<double>::max();
	for (std::vector<Vec2>::const_iterator vertex_iter = projected_ring.begin();
		vertex_iter != projected_ring.end(); ++vertex_iter)
	{
		const Vec2 point(dot(*vertex_iter, motion), dot(*vertex_iter, lateral));
		motion_ring.push_back(point);
		minimum_lateral = std::min(minimum_lateral, point.y);
		maximum_lateral = std::max(maximum_lateral, point.y);
	}
	if (maximum_lateral - minimum_lateral <= 1e-8)
	{
		throw std::runtime_error("The selected continent has no measurable width normal to its spreading direction.");
	}

	std::vector<double> laterals;
	std::vector<double> far_margins;
	const double full_lateral_width = maximum_lateral - minimum_lateral;
	const double lateral_trim =
			0.5 * (1.0 - parameters.width_coverage_fraction) * full_lateral_width;
	const double endpoint_buffer = parameters.endpoint_buffer_km / parameters.planet_radius_km;
	const double trench_minimum_lateral = minimum_lateral + lateral_trim - endpoint_buffer;
	const double trench_maximum_lateral = maximum_lateral - lateral_trim + endpoint_buffer;
	for (unsigned int anchor = 0; anchor < parameters.anchor_count; ++anchor)
	{
		const double fraction = static_cast<double>(anchor) / (parameters.anchor_count - 1);
		const double target_lateral =
				trench_minimum_lateral + fraction *
				(trench_maximum_lateral - trench_minimum_lateral);
		laterals.push_back(target_lateral);
		// Outside the continental silhouette, continue the endpoint margin while the
		// lateral coordinate supplies the requested ocean-side overhang.
		const double margin_sample_lateral =
				std::max(minimum_lateral, std::min(maximum_lateral, target_lateral));
		far_margins.push_back(far_margin_at_lateral(motion_ring, margin_sample_lateral));
	}

	// Low-pass filtering deliberately discards short coastal coves and promontories.
	// The final broad bow prevents a mathematically straight support edge from
	// becoming an implausibly ruler-straight trench.
	for (unsigned int pass = 0; pass < parameters.smoothing_passes; ++pass)
	{
		std::vector<double> smoothed = far_margins;
		for (std::size_t index = 1; index + 1 < far_margins.size(); ++index)
		{
			smoothed[index] =
					0.25 * far_margins[index - 1] +
					0.50 * far_margins[index] +
					0.25 * far_margins[index + 1];
		}
		far_margins.swap(smoothed);
	}

	const double broad_arc_bow = parameters.broad_arc_bow_km / parameters.planet_radius_km;
	const double maximum_segment_angle =
			parameters.maximum_segment_length_km / parameters.planet_radius_km;
	boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> trench;
	std::vector<GPlatesMaths::PointOnSphere> trench_points;
	double additional_offshore_offset_km = 0;
	double minimum_continental_clearance_km = 0;
	unsigned int clearance_adjustment_iterations = 0;
	for (unsigned int iteration = 0;
		iteration < parameters.maximum_clearance_adjustment_iterations;
		++iteration)
	{
		const double offshore_offset =
				(parameters.offshore_offset_km + additional_offshore_offset_km) /
				parameters.planet_radius_km;
		std::vector<GPlatesMaths::PointOnSphere> trench_anchors;
		for (std::size_t index = 0; index < far_margins.size(); ++index)
		{
			const double normalised_lateral =
					2.0 * static_cast<double>(index) / (far_margins.size() - 1) - 1.0;
			const double broad_arc =
					broad_arc_bow * (1.0 - normalised_lateral * normalised_lateral);
			const Vec2 projected =
					motion * (far_margins[index] + offshore_offset + broad_arc) +
					lateral * laterals[index];
			trench_anchors.push_back(unproject_lambert(frame, projected));
		}

		trench_points = tessellate(trench_anchors, maximum_segment_angle);
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type candidate_trench =
				GPlatesMaths::PolylineOnSphere::create(trench_points);
		minimum_continental_clearance_km =
				GPlatesMaths::minimum_distance(
						*candidate_trench,
						*continent,
						true/*polygon_interior_is_solid*/)
						.calculate_angle().dval() * parameters.planet_radius_km;
		if (minimum_continental_clearance_km + 1e-6 >=
				parameters.minimum_continental_clearance_km)
		{
			trench = candidate_trench;
			clearance_adjustment_iterations = iteration;
			break;
		}

		// If the candidate intersects the continent then its solid-polygon distance
		// is zero and does not reveal the penetration depth. Move outward by a
		// bounded increment and repeat. Once outside, close any remaining clearance
		// deficit in one pass (with the same increment as a small safety margin).
		additional_offshore_offset_km += std::max(
				parameters.clearance_adjustment_step_km,
				parameters.minimum_continental_clearance_km -
						minimum_continental_clearance_km +
						parameters.clearance_adjustment_step_km);
	}
	if (!trench)
	{
		throw std::runtime_error(
				"Could not move the proposed trench fully outside the selected continental crust.");
	}

	const double arrow_length = std::min(
			0.10, 0.28 * magnitude(projected_ridge_centre));
	std::vector<GPlatesMaths::PointOnSphere> arrow_points;
	arrow_points.push_back(unproject_lambert(frame, projected_ridge_centre));
	arrow_points.push_back(unproject_lambert(frame, projected_ridge_centre + motion * arrow_length));
	const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type motion_arrow =
			GPlatesMaths::PolylineOnSphere::create(arrow_points);

	Metrics metrics;
	metrics.continent_motion_normal_width_km =
			full_lateral_width * parameters.planet_radius_km;
	metrics.trench_motion_normal_span_km =
			(trench_maximum_lateral - trench_minimum_lateral) * parameters.planet_radius_km;
	metrics.minimum_continental_clearance_km = minimum_continental_clearance_km;
	metrics.endpoint_buffer_km = parameters.endpoint_buffer_km;
	metrics.clearance_adjustment_iterations = clearance_adjustment_iterations;
	for (std::size_t index = 0; index + 1 < trench_points.size(); ++index)
	{
		const double segment_length =
				angular_distance(trench_points[index], trench_points[index + 1]) * parameters.planet_radius_km;
		metrics.trench_length_km += segment_length;
		metrics.maximum_segment_length_km =
				std::max(metrics.maximum_segment_length_km, segment_length);
	}
	return Result(*trench, motion_arrow, metrics);
}
