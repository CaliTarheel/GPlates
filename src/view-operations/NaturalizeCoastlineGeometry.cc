/* $Id$ */

/**
 * \file
 * Spherical geometry helpers for the World Building "Naturalize Coastline"
 * operation.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

#include "NaturalizeCoastlineGeometry.h"

#include "maths\AngularExtent.h"
#include "maths\GreatCircleArc.h"
#include "maths\MathsUtils.h"
#include "maths\Vector3D.h"

#include "utils\Earth.h"


namespace
{
	const double TWO_PI = 2.0 * GPlatesMaths::PI;
	const double SWIGGLE_POWER_MULTIPLIER = 10.0;


	bool
	point_less(
			const GPlatesMaths::PointOnSphere &point1,
			const GPlatesMaths::PointOnSphere &point2)
	{
		const GPlatesMaths::UnitVector3D &vector1 = point1.position_vector();
		const GPlatesMaths::UnitVector3D &vector2 = point2.position_vector();
		if (vector1.x().dval() < vector2.x().dval())
		{
			return true;
		}
		if (vector1.x().dval() > vector2.x().dval())
		{
			return false;
		}
		if (vector1.y().dval() < vector2.y().dval())
		{
			return true;
		}
		if (vector1.y().dval() > vector2.y().dval())
		{
			return false;
		}
		return vector1.z().dval() < vector2.z().dval();
	}


	boost::uint32_t
	hash_point(
			boost::uint32_t hash,
			const GPlatesMaths::PointOnSphere &point)
	{
		const double scale = 1000000000.0;
		const GPlatesMaths::UnitVector3D &vector = point.position_vector();
		const long long components[3] =
		{
			static_cast<long long>(std::floor(vector.x().dval() * scale + 0.5)),
			static_cast<long long>(std::floor(vector.y().dval() * scale + 0.5)),
			static_cast<long long>(std::floor(vector.z().dval() * scale + 0.5))
		};

		for (unsigned int component = 0; component < 3; ++component)
		{
			boost::uint64_t value = static_cast<boost::uint64_t>(components[component]);
			for (unsigned int byte = 0; byte < 8; ++byte)
			{
				hash ^= static_cast<boost::uint32_t>(value & 0xffu);
				hash *= 16777619u;
				value >>= 8;
			}
		}
		return hash;
	}


	double
	angular_distance(
			const GPlatesMaths::PointOnSphere &point1,
			const GPlatesMaths::PointOnSphere &point2)
	{
		double cosine = GPlatesMaths::dot(
				point1.position_vector(), point2.position_vector()).dval();
		cosine = std::max(-1.0, std::min(1.0, cosine));
		return std::acos(cosine);
	}


	GPlatesMaths::PointOnSphere
	displace_point(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesMaths::UnitVector3D &normal,
			double angular_offset)
	{
		const GPlatesMaths::Vector3D displaced =
				std::cos(angular_offset) * GPlatesMaths::Vector3D(point.position_vector()) +
				std::sin(angular_offset) * GPlatesMaths::Vector3D(normal);
		return GPlatesMaths::PointOnSphere(displaced.get_normalisation());
	}


	bool
	point_lies_on_arc(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesMaths::GreatCircleArc &arc,
			double angular_tolerance)
	{
		GPlatesMaths::real_t closeness;
		return static_cast<bool>(arc.is_close_to(
				point,
				GPlatesMaths::AngularExtent::create_from_angle(angular_tolerance),
				closeness));
	}
}


GPlatesViewOperations::NaturalizeCoastlineGeometry::Parameters::Parameters() :
	maximum_segment_length_km(100.0),
	amplitude_percent(2.0),
	wavelength_km(400.0),
	smoothing_passes(2),
	random_seed(1),
	planet_radius_km(GPlatesUtils::Earth::MEAN_RADIUS_KMS),
	coincidence_tolerance_km(0.01)
{  }


GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult::SegmentResult() :
	inserted_point_count(0),
	maximum_segment_length_km(0.0)
{  }


GPlatesViewOperations::NaturalizeCoastlineGeometry::SegmentResult
GPlatesViewOperations::NaturalizeCoastlineGeometry::naturalize_segment(
		const GPlatesMaths::PointOnSphere &input_start,
		const GPlatesMaths::PointOnSphere &input_end,
		const Parameters &parameters)
{
	SegmentResult result;
	const double segment_length_km =
			angular_distance(input_start, input_end) * parameters.planet_radius_km;
	if (segment_length_km <= parameters.maximum_segment_length_km ||
			parameters.maximum_segment_length_km <= 0.0)
	{
		result.points.push_back(input_start);
		result.points.push_back(input_end);
		result.maximum_segment_length_km = segment_length_km;
		return result;
	}

	const bool reverse_output = point_less(input_end, input_start);
	const GPlatesMaths::PointOnSphere &start = reverse_output ? input_end : input_start;
	const GPlatesMaths::PointOnSphere &end = reverse_output ? input_start : input_end;
	const GPlatesMaths::GreatCircleArc arc = GPlatesMaths::GreatCircleArc::create(start, end);

	boost::uint32_t hash = 2166136261u ^ parameters.random_seed;
	hash = hash_point(hash, start);
	hash = hash_point(hash, end);
	std::mt19937 random(hash);
	std::uniform_real_distribution<double> phase_distribution(0.0, TWO_PI);
	const double phase1 = phase_distribution(random);
	const double phase2 = phase_distribution(random);

	unsigned int subdivision_count = static_cast<unsigned int>(
			std::ceil(segment_length_km / parameters.maximum_segment_length_km));
	subdivision_count = std::max(2u, subdivision_count);

	point_seq_type canonical_points;
	for (unsigned int attempt = 0; attempt < 12; ++attempt)
	{
		std::vector<double> offsets(subdivision_count + 1, 0.0);
		const double amplitude_km =
				parameters.maximum_segment_length_km * parameters.amplitude_percent / 100.0 *
				SWIGGLE_POWER_MULTIPLIER;
		for (unsigned int point_index = 1; point_index < subdivision_count; ++point_index)
		{
			const double fraction = static_cast<double>(point_index) / subdivision_count;
			const double distance_km = fraction * segment_length_km;
			const double wavelength_km = std::max(1e-6, parameters.wavelength_km);
			const double noise =
					(0.65 * std::sin(TWO_PI * distance_km / wavelength_km + phase1) +
					 0.35 * std::sin(2.0 * TWO_PI * distance_km / wavelength_km + phase2)) *
					std::sin(GPlatesMaths::PI * fraction);
			offsets[point_index] = amplitude_km * noise / parameters.planet_radius_km;
		}

		for (unsigned int pass = 0; pass < parameters.smoothing_passes; ++pass)
		{
			std::vector<double> smoothed(offsets);
			for (unsigned int point_index = 1; point_index < subdivision_count; ++point_index)
			{
				smoothed[point_index] =
						0.25 * offsets[point_index - 1] +
						0.5 * offsets[point_index] +
						0.25 * offsets[point_index + 1];
			}
			offsets.swap(smoothed);
		}

		canonical_points.clear();
		canonical_points.reserve(subdivision_count + 1);
		canonical_points.push_back(start);
		for (unsigned int point_index = 1; point_index < subdivision_count; ++point_index)
		{
			const double fraction = static_cast<double>(point_index) / subdivision_count;
			canonical_points.push_back(displace_point(
					arc.point_on_arc(fraction), arc.rotation_axis(), offsets[point_index]));
		}
		canonical_points.push_back(end);

		const double actual_maximum = maximum_segment_length_km(
				canonical_points, false, parameters.planet_radius_km);
		if (actual_maximum <= parameters.maximum_segment_length_km * (1.0 + 1e-10))
		{
			result.maximum_segment_length_km = actual_maximum;
			break;
		}

		const double ratio = actual_maximum / parameters.maximum_segment_length_km;
		subdivision_count = std::max(
				subdivision_count + 1,
				static_cast<unsigned int>(std::ceil(subdivision_count * ratio * 1.01)));
	}

	if (reverse_output)
	{
		result.points.assign(canonical_points.rbegin(), canonical_points.rend());
	}
	else
	{
		result.points = canonical_points;
	}
	result.inserted_point_count = result.points.size() > 2
			? static_cast<unsigned int>(result.points.size() - 2) : 0;
	if (result.maximum_segment_length_km <= 0.0)
	{
		result.maximum_segment_length_km = maximum_segment_length_km(
				result.points, false, parameters.planet_radius_km);
	}
	return result;
}


double
GPlatesViewOperations::NaturalizeCoastlineGeometry::maximum_segment_length_km(
		const point_seq_type &points,
		bool closed,
		double planet_radius_km)
{
	double maximum = 0.0;
	if (points.size() < 2)
	{
		return maximum;
	}
	for (point_seq_type::const_iterator point_iter = points.begin() + 1;
			point_iter != points.end(); ++point_iter)
	{
		maximum = std::max(maximum,
				angular_distance(*(point_iter - 1), *point_iter) * planet_radius_km);
	}
	if (closed)
	{
		maximum = std::max(maximum,
				angular_distance(points.back(), points.front()) * planet_radius_km);
	}
	return maximum;
}


bool
GPlatesViewOperations::NaturalizeCoastlineGeometry::points_are_close(
		const GPlatesMaths::PointOnSphere &point1,
		const GPlatesMaths::PointOnSphere &point2,
		double tolerance_km,
		double planet_radius_km)
{
	return angular_distance(point1, point2) <= tolerance_km / planet_radius_km;
}


bool
GPlatesViewOperations::NaturalizeCoastlineGeometry::segments_match(
		const GPlatesMaths::PointOnSphere &start1,
		const GPlatesMaths::PointOnSphere &end1,
		const GPlatesMaths::PointOnSphere &start2,
		const GPlatesMaths::PointOnSphere &end2,
		double tolerance_km,
		double planet_radius_km,
		bool &reversed)
{
	if (points_are_close(start1, start2, tolerance_km, planet_radius_km) &&
			points_are_close(end1, end2, tolerance_km, planet_radius_km))
	{
		reversed = false;
		return true;
	}
	if (points_are_close(start1, end2, tolerance_km, planet_radius_km) &&
			points_are_close(end1, start2, tolerance_km, planet_radius_km))
	{
		reversed = true;
		return true;
	}
	return false;
}


bool
GPlatesViewOperations::NaturalizeCoastlineGeometry::segments_partially_overlap(
		const GPlatesMaths::PointOnSphere &start1,
		const GPlatesMaths::PointOnSphere &end1,
		const GPlatesMaths::PointOnSphere &start2,
		const GPlatesMaths::PointOnSphere &end2,
		double tolerance_km,
		double planet_radius_km)
{
	bool reversed = false;
	if (segments_match(start1, end1, start2, end2,
			tolerance_km, planet_radius_km, reversed))
	{
		return false;
	}

	const GPlatesMaths::GreatCircleArc arc1 = GPlatesMaths::GreatCircleArc::create(start1, end1);
	const GPlatesMaths::GreatCircleArc arc2 = GPlatesMaths::GreatCircleArc::create(start2, end2);
	if (arc1.is_zero_length() || arc2.is_zero_length())
	{
		return false;
	}

	const double angular_tolerance = tolerance_km / planet_radius_km;
	const double axes_dot = std::fabs(GPlatesMaths::dot(
			arc1.rotation_axis(), arc2.rotation_axis()).dval());
	if (axes_dot < std::cos(angular_tolerance))
	{
		return false;
	}

	const bool start1_in_arc2 = point_lies_on_arc(start1, arc2, angular_tolerance);
	const bool end1_in_arc2 = point_lies_on_arc(end1, arc2, angular_tolerance);
	const bool start2_in_arc1 = point_lies_on_arc(start2, arc1, angular_tolerance);
	const bool end2_in_arc1 = point_lies_on_arc(end2, arc1, angular_tolerance);

	// A common endpoint alone is adjacency, not overlap. At least one endpoint
	// must lie in the other arc's interior, or one whole arc must lie in the other.
	const bool start1_in_arc2_interior = start1_in_arc2 &&
			!points_are_close(start1, start2, tolerance_km, planet_radius_km) &&
			!points_are_close(start1, end2, tolerance_km, planet_radius_km);
	const bool end1_in_arc2_interior = end1_in_arc2 &&
			!points_are_close(end1, start2, tolerance_km, planet_radius_km) &&
			!points_are_close(end1, end2, tolerance_km, planet_radius_km);
	const bool start2_in_arc1_interior = start2_in_arc1 &&
			!points_are_close(start2, start1, tolerance_km, planet_radius_km) &&
			!points_are_close(start2, end1, tolerance_km, planet_radius_km);
	const bool end2_in_arc1_interior = end2_in_arc1 &&
			!points_are_close(end2, start1, tolerance_km, planet_radius_km) &&
			!points_are_close(end2, end1, tolerance_km, planet_radius_km);
	return start1_in_arc2_interior || end1_in_arc2_interior ||
			start2_in_arc1_interior || end2_in_arc1_interior;
}
