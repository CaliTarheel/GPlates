/* $Id$ */

#include "TripleJunctionGeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include <QObject>

#include "maths/MathsUtils.h"
#include "maths/UnitVector3D.h"


namespace
{
	const double RADIANS_TO_DEGREES = 180.0 / GPlatesMaths::PI;

	double
	angular_distance(
			const GPlatesMaths::PointOnSphere &first,
			const GPlatesMaths::PointOnSphere &second)
	{
		return GPlatesMaths::calculate_distance_on_surface_of_sphere(first, second, 1.0).dval();
	}

	GPlatesMaths::PointOnSphere
	consensus(const std::vector<GPlatesMaths::PointOnSphere> &points)
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
		for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator point_iter =
				points.begin(); point_iter != points.end(); ++point_iter)
		{
			x += point_iter->position_vector().x().dval();
			y += point_iter->position_vector().y().dval();
			z += point_iter->position_vector().z().dval();
		}
		const double magnitude = std::sqrt(x * x + y * y + z * z);
		if (magnitude < 1e-12)
		{
			throw std::runtime_error("The candidate endpoints have no stable spherical consensus.");
		}
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(x / magnitude, y / magnitude, z / magnitude));
	}

	struct Tangent
	{
		double x;
		double y;
		double z;
	};

	Tangent
	branch_tangent(
			const GPlatesMaths::PointOnSphere &junction,
			const GPlatesMaths::PointOnSphere &next_point)
	{
		const double nx = junction.position_vector().x().dval();
		const double ny = junction.position_vector().y().dval();
		const double nz = junction.position_vector().z().dval();
		const double px = next_point.position_vector().x().dval();
		const double py = next_point.position_vector().y().dval();
		const double pz = next_point.position_vector().z().dval();
		const double projection = nx * px + ny * py + nz * pz;
		Tangent tangent = { px - projection * nx, py - projection * ny, pz - projection * nz };
		const double magnitude = std::sqrt(
				tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
		if (magnitude < 1e-12)
		{
			throw std::runtime_error("A ridge has no stable terminal tangent at the junction.");
		}
		tangent.x /= magnitude;
		tangent.y /= magnitude;
		tangent.z /= magnitude;
		return tangent;
	}

	double
	angle(const Tangent &first, const Tangent &second)
	{
		const double cosine = std::max(-1.0, std::min(1.0,
				first.x * second.x + first.y * second.y + first.z * second.z));
		return std::acos(cosine) * RADIANS_TO_DEGREES;
	}
}


GPlatesViewOperations::TripleJunctionGeometry::Result
GPlatesViewOperations::TripleJunctionGeometry::resolve_rrr_endpoints(
		const polyline_seq_type &ridges,
		double maximum_extension_degrees,
		double minimum_stable_branch_angle_degrees,
		double maximum_stable_branch_angle_degrees)
{
	Result result;
	if (ridges.size() != 3 || maximum_extension_degrees < 0.0 ||
			minimum_stable_branch_angle_degrees < 0.0 ||
			maximum_stable_branch_angle_degrees > 180.0 ||
			minimum_stable_branch_angle_degrees >= maximum_stable_branch_angle_degrees)
	{
		result.error = QObject::tr("RRR resolution requires exactly three ridges and valid tolerances.");
		return result;
	}

	std::vector< std::vector<GPlatesMaths::PointOnSphere> > points(3);
	for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
	{
		points[ridge_index].assign(
				ridges[ridge_index]->vertex_begin(), ridges[ridge_index]->vertex_end());
		if (points[ridge_index].size() < 2)
		{
			result.error = QObject::tr("Every selected MOR must contain at least two vertices.");
			return result;
		}
	}

	double best_maximum_distance = std::numeric_limits<double>::max();
	double best_total_distance = std::numeric_limits<double>::max();
	unsigned int best_mask = 0;
	GPlatesMaths::PointOnSphere best_junction = GPlatesMaths::PointOnSphere::north_pole;
	try
	{
		for (unsigned int endpoint_mask = 0; endpoint_mask < 8; ++endpoint_mask)
		{
			std::vector<GPlatesMaths::PointOnSphere> endpoints;
			for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
			{
				endpoints.push_back((endpoint_mask & (1u << ridge_index))
						? points[ridge_index].front() : points[ridge_index].back());
			}
			const GPlatesMaths::PointOnSphere candidate = consensus(endpoints);
			double maximum_distance = 0.0;
			double total_distance = 0.0;
			for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator endpoint_iter =
					endpoints.begin(); endpoint_iter != endpoints.end(); ++endpoint_iter)
			{
				const double distance = angular_distance(*endpoint_iter, candidate);
				maximum_distance = std::max(maximum_distance, distance);
				total_distance += distance;
			}
			if (maximum_distance < best_maximum_distance - 1e-12 ||
					(std::abs(maximum_distance - best_maximum_distance) <= 1e-12 &&
						total_distance < best_total_distance))
			{
				best_maximum_distance = maximum_distance;
				best_total_distance = total_distance;
				best_mask = endpoint_mask;
				best_junction = candidate;
			}
		}
	}
	catch (const std::exception &exception)
	{
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}

	result.maximum_extension_degrees = best_maximum_distance * RADIANS_TO_DEGREES;
	if (result.maximum_extension_degrees > maximum_extension_degrees + 1e-9)
	{
		result.error = QObject::tr(
				"The closest endpoint solution needs %1 degrees of extension; the limit is %2 degrees.")
					.arg(result.maximum_extension_degrees, 0, 'f', 3)
					.arg(maximum_extension_degrees, 0, 'f', 3);
		return result;
	}

	result.junction = best_junction;
	try
	{
		std::vector<Tangent> tangents;
		for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
		{
			const bool junction_at_first = (best_mask & (1u << ridge_index)) != 0;
			std::vector<GPlatesMaths::PointOnSphere> resolved = points[ridge_index];
			if (junction_at_first)
			{
				if (GPlatesMaths::points_are_coincident(resolved.front(), best_junction))
				{
					resolved.front() = best_junction;
				}
				else
				{
					resolved.insert(resolved.begin(), best_junction);
				}
				tangents.push_back(branch_tangent(best_junction, resolved[1]));
			}
			else
			{
				if (GPlatesMaths::points_are_coincident(resolved.back(), best_junction))
				{
					resolved.back() = best_junction;
				}
				else
				{
					resolved.push_back(best_junction);
				}
				tangents.push_back(branch_tangent(best_junction, resolved[resolved.size() - 2]));
			}
			result.resolved_ridges.push_back(GPlatesMaths::PolylineOnSphere::create(resolved));
			result.junction_at_first_vertex.push_back(junction_at_first);
		}

		result.minimum_branch_angle_degrees = 180.0;
		result.maximum_branch_angle_degrees = 0.0;
		for (unsigned int first = 0; first < 3; ++first)
		{
			for (unsigned int second = first + 1; second < 3; ++second)
			{
				const double branch_angle = angle(tangents[first], tangents[second]);
				result.minimum_branch_angle_degrees = std::min(
						result.minimum_branch_angle_degrees, branch_angle);
				result.maximum_branch_angle_degrees = std::max(
						result.maximum_branch_angle_degrees, branch_angle);
			}
		}
	}
	catch (const std::exception &exception)
	{
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}

	if (result.minimum_branch_angle_degrees < minimum_stable_branch_angle_degrees ||
			result.maximum_branch_angle_degrees > maximum_stable_branch_angle_degrees)
	{
		result.error = QObject::tr(
				"The resolved branch angles (%1-%2 degrees) fail the configured RRR stability range (%3-%4 degrees).")
					.arg(result.minimum_branch_angle_degrees, 0, 'f', 2)
					.arg(result.maximum_branch_angle_degrees, 0, 'f', 2)
					.arg(minimum_stable_branch_angle_degrees, 0, 'f', 2)
					.arg(maximum_stable_branch_angle_degrees, 0, 'f', 2);
		result.resolved_ridges.clear();
		result.junction_at_first_vertex.clear();
		return result;
	}

	result.success = true;
	return result;
}
