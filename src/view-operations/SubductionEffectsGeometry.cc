/* $Id$ */

/**
 * \file
 * Spherical proposal geometry for island arcs and active-margin mountains.
 */

#include "SubductionEffectsGeometry.h"

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
		return Vec3(
				lhs.y * rhs.z - lhs.z * rhs.y,
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
			throw std::runtime_error("Cannot normalise a zero-length spherical vector.");
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

	struct Sample
	{
		Sample(const GPlatesMaths::PointOnSphere &point_, const Vec3 &tangent_) :
			point(point_), tangent(tangent_)
		{  }

		GPlatesMaths::PointOnSphere point;
		Vec3 tangent;
	};

	struct SourcePath
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		std::vector<double> cumulative_angles;
		double total_angle;
	};

	SourcePath make_source_path(const GPlatesMaths::PolylineOnSphere &polyline)
	{
		SourcePath path;
		path.total_angle = 0;
		for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
				polyline.vertex_begin(); vertex_iter != polyline.vertex_end(); ++vertex_iter)
		{
			path.points.push_back(*vertex_iter);
		}
		if (path.points.size() < 2)
		{
			throw std::invalid_argument("A subduction-zone proposal requires at least two vertices.");
		}
		path.cumulative_angles.push_back(0);
		for (std::size_t index = 0; index + 1 < path.points.size(); ++index)
		{
			path.total_angle += angular_distance(path.points[index], path.points[index + 1]);
			path.cumulative_angles.push_back(path.total_angle);
		}
		if (path.total_angle <= 1e-12)
		{
			throw std::invalid_argument("The selected subduction zone has no measurable length.");
		}
		return path;
	}

	Sample sample_path(const SourcePath &path, double target_angle)
	{
		target_angle = std::max(0.0, std::min(path.total_angle, target_angle));
		std::size_t segment = 0;
		while (segment + 1 < path.cumulative_angles.size() &&
				path.cumulative_angles[segment + 1] < target_angle)
		{
			++segment;
		}
		segment = std::min(segment, path.points.size() - 2);
		const double segment_angle =
				path.cumulative_angles[segment + 1] - path.cumulative_angles[segment];
		const double fraction = segment_angle <= 1e-14
				? 0.0
				: (target_angle - path.cumulative_angles[segment]) / segment_angle;
		const GPlatesMaths::PointOnSphere point =
				interpolate(path.points[segment], path.points[segment + 1], fraction);
		const Vec3 point_vector = to_vec3(point);
		const Vec3 chord = to_vec3(path.points[segment + 1]) - to_vec3(path.points[segment]);
		return Sample(point, normalise(chord - point_vector * dot(chord, point_vector)));
	}

	GPlatesMaths::PointOnSphere offset_point(
			const GPlatesMaths::PointOnSphere &point,
			const Vec3 &tangent,
			double offset_angle,
			bool left)
	{
		const Vec3 radial = to_vec3(point);
		Vec3 normal = normalise(cross(radial, tangent));
		if (!left)
		{
			normal = normal * -1.0;
		}
		return to_point(radial * std::cos(offset_angle) + normal * std::sin(offset_angle));
	}

	std::vector<Sample> make_selected_samples(
			const SourcePath &path,
			double start_fraction,
			double end_fraction,
			double maximum_segment_angle)
	{
		const double start_angle = path.total_angle * start_fraction;
		const double end_angle = path.total_angle * end_fraction;
		const unsigned int segment_count = std::max(
				1u, static_cast<unsigned int>(std::ceil(
						(end_angle - start_angle) / maximum_segment_angle)));
		std::vector<Sample> samples;
		for (unsigned int segment = 0; segment <= segment_count; ++segment)
		{
			const double fraction = static_cast<double>(segment) / segment_count;
			samples.push_back(sample_path(path, start_angle + fraction * (end_angle - start_angle)));
		}
		return samples;
	}

	GPlatesMaths::PointOnSphere contain_boundary_point(
			const GPlatesMaths::PointOnSphere &centre,
			const GPlatesMaths::PointOnSphere &candidate,
			const GPlatesMaths::PolygonOnSphere &continent)
	{
		if (continent.is_point_in_polygon(candidate))
		{
			return candidate;
		}
		double inside_fraction = 0;
		double outside_fraction = 1;
		for (unsigned int iteration = 0; iteration < 24; ++iteration)
		{
			const double fraction = 0.5 * (inside_fraction + outside_fraction);
			const GPlatesMaths::PointOnSphere probe = interpolate(centre, candidate, fraction);
			if (continent.is_point_in_polygon(probe))
			{
				inside_fraction = fraction;
			}
			else
			{
				outside_fraction = fraction;
			}
		}
		return interpolate(centre, candidate, 0.96 * inside_fraction);
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

	double maximum_polyline_segment_length(
			const GPlatesMaths::PolylineOnSphere &polyline,
			double planet_radius_km)
	{
		double maximum = 0;
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator previous = polyline.vertex_begin();
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = previous;
		++vertex_iter;
		for (; vertex_iter != polyline.vertex_end(); ++vertex_iter, ++previous)
		{
			maximum = std::max(maximum,
					angular_distance(*previous, *vertex_iter) * planet_radius_km);
		}
		return maximum;
	}

	void append_contained_belt(
			const std::vector<Sample> &samples,
			const GPlatesMaths::PolygonOnSphere &land,
			double half_width_angle,
			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &belts)
	{
		if (samples.size() < 2)
		{
			return;
		}
		std::vector<GPlatesMaths::PointOnSphere> left_boundary;
		std::vector<GPlatesMaths::PointOnSphere> right_boundary;
		for (std::vector<Sample>::const_iterator sample_iter = samples.begin();
				sample_iter != samples.end(); ++sample_iter)
		{
			left_boundary.push_back(contain_boundary_point(
					sample_iter->point,
					offset_point(sample_iter->point, sample_iter->tangent, half_width_angle, true),
					land));
			right_boundary.push_back(contain_boundary_point(
					sample_iter->point,
					offset_point(sample_iter->point, sample_iter->tangent, half_width_angle, false),
					land));
		}
		std::vector<GPlatesMaths::PointOnSphere> ring(left_boundary.begin(), left_boundary.end());
		for (std::vector<GPlatesMaths::PointOnSphere>::const_reverse_iterator vertex_iter =
				right_boundary.rbegin(); vertex_iter != right_boundary.rend(); ++vertex_iter)
		{
			ring.push_back(*vertex_iter);
		}
		belts.push_back(GPlatesMaths::PolygonOnSphere::create(ring));
	}
}


GPlatesViewOperations::SubductionEffectsGeometry::Parameters::Parameters() :
	effect_type(ISLAND_ARC),
	overriding_side_is_left(true),
	trim_start_percent(0),
	trim_end_percent(0),
	trench_to_effect_offset_km(220.0),
	maximum_segment_length_km(120.0),
	island_irregularity(0.22),
	belt_width_km(100.0),
	planet_radius_km(6371.0088)
{  }


GPlatesViewOperations::SubductionEffectsGeometry::Metrics::Metrics() :
	source_length_km(0),
	selected_length_km(0),
	overriding_containment_fraction(0),
	maximum_segment_length_km(0),
	island_count(0),
	proposal_vertex_count(0)
{  }


GPlatesViewOperations::SubductionEffectsGeometry::Result::Result()
{  }


double
GPlatesViewOperations::SubductionEffectsGeometry::calculate_overriding_containment_fraction(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &subduction_zone,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &overriding_crust,
		bool overriding_side_is_left,
		double probe_offset_km,
		double planet_radius_km)
{
	if (probe_offset_km <= 0 || planet_radius_km <= 0)
	{
		throw std::invalid_argument("Subduction polarity probe parameters are invalid.");
	}
	const SourcePath path = make_source_path(*subduction_zone);
	const unsigned int sample_count = std::max(5u,
			static_cast<unsigned int>(std::ceil(path.total_angle * planet_radius_km / 250.0)));
	unsigned int inside = 0;
	for (unsigned int sample = 0; sample <= sample_count; ++sample)
	{
		const Sample source = sample_path(
				path, path.total_angle * static_cast<double>(sample) / sample_count);
		if (overriding_crust->is_point_in_polygon(offset_point(
				source.point, source.tangent, probe_offset_km / planet_radius_km,
				overriding_side_is_left)))
		{
			++inside;
		}
	}
	return static_cast<double>(inside) / (sample_count + 1);
}


GPlatesViewOperations::SubductionEffectsGeometry::Result
GPlatesViewOperations::SubductionEffectsGeometry::generate(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &subduction_zone,
		const Parameters &parameters,
		const boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &overriding_crust)
{
	if (parameters.planet_radius_km <= 0 || parameters.maximum_segment_length_km <= 0 ||
			parameters.trench_to_effect_offset_km <= 0 ||
			parameters.trim_start_percent < 0 || parameters.trim_end_percent < 0 ||
		parameters.trim_start_percent + parameters.trim_end_percent >= 90.0 ||
		parameters.island_irregularity < 0 ||
			parameters.island_irregularity > 0.45 || parameters.belt_width_km <= 0)
	{
		throw std::invalid_argument("Subduction-effect proposal parameters are invalid.");
	}
	if (parameters.effect_type != ISLAND_ARC && !overriding_crust)
	{
		throw std::invalid_argument("An overriding ContinentalCrust polygon is required for an orogenic belt.");
	}

	const SourcePath path = make_source_path(*subduction_zone);
	const double start_fraction = parameters.trim_start_percent / 100.0;
	const double end_fraction = 1.0 - parameters.trim_end_percent / 100.0;
	const double maximum_segment_angle = parameters.maximum_segment_length_km / parameters.planet_radius_km;
	const std::vector<Sample> source_samples = make_selected_samples(
			path, start_fraction, end_fraction, maximum_segment_angle);

	Result result;
	result.metrics.source_length_km = path.total_angle * parameters.planet_radius_km;
	result.metrics.selected_length_km =
			path.total_angle * (end_fraction - start_fraction) * parameters.planet_radius_km;
	if (overriding_crust)
	{
		result.metrics.overriding_containment_fraction =
				calculate_overriding_containment_fraction(
						subduction_zone, *overriding_crust,
						parameters.overriding_side_is_left,
						parameters.trench_to_effect_offset_km,
						parameters.planet_radius_km);
	}

	if (parameters.effect_type == ISLAND_ARC)
	{
		// Sample more finely than the requested output ceiling because lateral
		// undulation increases the diagonal distance between neighbouring points.
		const std::vector<Sample> arc_source_samples = make_selected_samples(
				path, start_fraction, end_fraction, 0.45 * maximum_segment_angle);
		std::vector<GPlatesMaths::PointOnSphere> guide_points;
		for (std::size_t sample_index = 0; sample_index < arc_source_samples.size(); ++sample_index)
		{
			const double fraction = arc_source_samples.size() == 1 ? 0.0 :
					static_cast<double>(sample_index) / (arc_source_samples.size() - 1);
			const double undulation =
					0.68 * std::sin(2.0 * 3.14159265358979323846 * (2.0 * fraction + 0.11)) +
					0.32 * std::sin(2.0 * 3.14159265358979323846 * (5.0 * fraction + 0.37));
			const double offset_km = parameters.trench_to_effect_offset_km *
					(1.0 + parameters.island_irregularity * undulation);
			guide_points.push_back(offset_point(
					arc_source_samples[sample_index].point, arc_source_samples[sample_index].tangent,
					offset_km / parameters.planet_radius_km,
					parameters.overriding_side_is_left));
		}
		result.guide = GPlatesMaths::PolylineOnSphere::create(guide_points);
		result.metrics.island_count = 1;
		result.metrics.proposal_vertex_count = guide_points.size();
		result.metrics.maximum_segment_length_km = maximum_polyline_segment_length(
				**result.guide, parameters.planet_radius_km);
		return result;
	}

	std::vector<Sample> belt_samples;
	for (std::vector<Sample>::const_iterator sample_iter = source_samples.begin();
		sample_iter != source_samples.end(); ++sample_iter)
	{
		const GPlatesMaths::PointOnSphere centre = offset_point(
				sample_iter->point, sample_iter->tangent,
				parameters.trench_to_effect_offset_km / parameters.planet_radius_km,
				parameters.overriding_side_is_left);
		if ((*overriding_crust)->is_point_in_polygon(centre))
		{
			belt_samples.push_back(Sample(centre, sample_iter->tangent));
		}
	}
	if (belt_samples.size() < 3)
	{
		throw std::runtime_error(
				"Too little of the proposed belt lies in the selected overriding continental crust. Flip polarity or adjust the offset/trim controls.");
	}

	std::vector<GPlatesMaths::PointOnSphere> left_boundary;
	std::vector<GPlatesMaths::PointOnSphere> right_boundary;
	std::vector<GPlatesMaths::PointOnSphere> guide_points;
	const double half_width_angle =
			0.5 * parameters.belt_width_km / parameters.planet_radius_km;
	for (std::vector<Sample>::const_iterator sample_iter = belt_samples.begin();
		sample_iter != belt_samples.end(); ++sample_iter)
	{
		guide_points.push_back(sample_iter->point);
		left_boundary.push_back(contain_boundary_point(
				sample_iter->point,
				offset_point(sample_iter->point, sample_iter->tangent, half_width_angle, true),
				**overriding_crust));
		right_boundary.push_back(contain_boundary_point(
				sample_iter->point,
				offset_point(sample_iter->point, sample_iter->tangent, half_width_angle, false),
				**overriding_crust));
	}
	std::vector<GPlatesMaths::PointOnSphere> ring(left_boundary.begin(), left_boundary.end());
	for (std::vector<GPlatesMaths::PointOnSphere>::const_reverse_iterator vertex_iter =
			right_boundary.rbegin(); vertex_iter != right_boundary.rend(); ++vertex_iter)
	{
		ring.push_back(*vertex_iter);
	}
	const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type belt =
			GPlatesMaths::PolygonOnSphere::create(ring);
	result.polygons.push_back(belt);
	result.guide = GPlatesMaths::PolylineOnSphere::create(guide_points);
	result.metrics.proposal_vertex_count = belt->number_of_vertices();
	result.metrics.maximum_segment_length_km =
			maximum_polygon_segment_length(*belt, parameters.planet_radius_km);
	return result;
}


std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
GPlatesViewOperations::SubductionEffectsGeometry::generate_land_intersection_belts(
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &arc_line,
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &land,
		double belt_width_km,
		double maximum_segment_length_km,
		double planet_radius_km)
{
	if (belt_width_km <= 0 || maximum_segment_length_km <= 0 || planet_radius_km <= 0)
	{
		throw std::invalid_argument("Land-intersection mountain parameters are invalid.");
	}
	const SourcePath path = make_source_path(*arc_line);
	const std::vector<Sample> samples = make_selected_samples(
			path, 0.0, 1.0, maximum_segment_length_km / planet_radius_km);
	std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> belts;
	std::vector<Sample> inside_run;
	for (std::vector<Sample>::const_iterator sample_iter = samples.begin();
			sample_iter != samples.end(); ++sample_iter)
	{
		if (land->is_point_in_polygon(sample_iter->point))
		{
			inside_run.push_back(*sample_iter);
		}
		else
		{
			append_contained_belt(
					inside_run, *land, 0.5 * belt_width_km / planet_radius_km, belts);
			inside_run.clear();
		}
	}
	append_contained_belt(
			inside_run, *land, 0.5 * belt_width_km / planet_radius_km, belts);
	return belts;
}
