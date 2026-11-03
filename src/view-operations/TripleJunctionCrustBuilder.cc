/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include "TripleJunctionCrustBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <boost/optional.hpp>

#include "MakeOceanCrustOperation.h"
#include "OceanCrustSliverBuilder.h"

#include "app-logic/FlowlineUtils.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "maths/FiniteRotation.h"
#include "maths/GreatCircleArc.h"
#include "maths/PolygonOnSphere.h"
#include "maths/UnitVector3D.h"

namespace
{
	typedef GPlatesViewOperations::TripleJunctionCrustBuilder::RidgeArm Arm;
	typedef GPlatesMaths::PointOnSphere Point;
	typedef std::vector<Point> Points;
	typedef std::array<double, 3> Vector;
	typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type Polygon;
	typedef GPlatesModel::integer_plate_id_type Plate;
	namespace Crust = GPlatesViewOperations::OceanCrustSliverBuilder;
	const double PI = 3.14159265358979323846;
	const double COINCIDENCE = 1e-10;
	const std::size_t MAX_PATCHES = 100000;
	const std::size_t MAX_VALIDATED_PATCHES = 1000000;

	Vector
	position(
			const Point &p)
	{
		const auto &v = p.position_vector();
		return {{v.x().dval(), v.y().dval(), v.z().dval()}};
	}

	double
	dot(
			const Vector &a,
			const Vector &b)
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	Vector
	cross(
			const Vector &a,
			const Vector &b)
	{
		return {{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
				a[0] * b[1] - a[1] * b[0]}};
	}

	Vector
	subtract(
			const Vector &a,
			const Vector &b)
	{
		return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}};
	}

	double
	length(
			const Vector &a)
	{
		return std::sqrt(dot(a, a));
	}

	Vector
	normalise(
			Vector v)
	{
		const double magnitude = length(v);
		if (magnitude < 1e-14)
		{
			throw std::invalid_argument("The junction direction is unresolved or degenerate.");
		}
		for (auto &component : v)
		{
			component /= magnitude;
		}
		return v;
	}

	Point
	point(
			const Vector &v)
	{
		const auto unit = normalise(v);
		return Point(GPlatesMaths::UnitVector3D(unit[0], unit[1], unit[2]));
	}

	double
	distance(
			const Point &a,
			const Point &b)
	{
		return std::atan2(length(cross(position(a), position(b))), dot(position(a), position(b)));
	}

	struct InitialJunction
	{
		InitialJunction(
				const Point &centre,
				const std::vector<Points> &arms,
				const std::vector<double> &extensions,
				const std::vector<int> &ends) :
			d_centre(centre), d_arms(arms), d_extensions(extensions), d_ends(ends)
		{  }
		Point d_centre;
		std::vector<Points> d_arms;
		std::vector<double> d_extensions;
		std::vector<int> d_ends;
	};

	InitialJunction
	find_initial_junction(
			const std::vector<Arm> &arms)
	{
		boost::optional<InitialJunction> best;
		double best_cost = 1e100;
		// Only eight endpoint combinations. Explicit endpoint choices constrain this search.
		for (unsigned int mask = 0; mask < 8; ++mask)
		{
			std::vector<Points> oriented;
			std::vector<Vector> normals;
			std::vector<int> ends;
			bool allowed = true;
			for (unsigned int arm = 0; arm < 3; ++arm)
			{
				const int end = (mask >> arm) & 1;
				if (arms[arm].d_junction_end != -1 && arms[arm].d_junction_end != end)
				{
					allowed = false;
					break;
				}
				Points vertices(arms[arm].d_geometry->vertex_begin(),
						arms[arm].d_geometry->vertex_end());
				if (end == 0)
				{
					std::reverse(vertices.begin(), vertices.end());
				}
				const auto normal = cross(position(vertices[vertices.size() - 2]),
						position(vertices.back()));
				if (length(normal) < 1e-6)
				{
					allowed = false;
					break;
				}
				normals.push_back(normalise(normal));
				oriented.push_back(vertices);
				ends.push_back(end);
			}
			if (!allowed)
			{
				continue;
			}
			Vector direction = {{0, 0, 0}};
			for (unsigned int pair = 0; pair < 3; ++pair)
			{
				const auto candidate = cross(normals[pair], normals[(pair + 1) % 3]);
				if (length(candidate) > length(direction))
				{
					direction = candidate;
				}
			}
			if (length(direction) < 1e-4)
			{
				continue;
			}
			direction = normalise(direction);
			for (int sign : {-1, 1})
			{
				const Point centre = point({{sign * direction[0], sign * direction[1],
						sign * direction[2]}});
				std::vector<double> extensions;
				double cost = 0;
				bool valid = true;
				for (unsigned int arm = 0; arm < 3; ++arm)
				{
					const auto &endpoint = oriented[arm].back();
					const double extension = distance(endpoint, centre);
					const auto tangent = cross(normals[arm], position(endpoint));
					// Do not bend a nonconcurrent arm into an invented intersection. The
					// tolerance is sub-metre on Earth and only covers coordinate roundoff.
					if (std::fabs(dot(normals[arm], position(centre))) > 1e-7 ||
							extension > PI / 6 ||
							(extension > COINCIDENCE && dot(tangent, position(centre)) <= 0))
					{
						valid = false;
						break;
					}
					extensions.push_back(extension);
					cost += extension;
				}
				if (!valid)
				{
					continue;
				}
				if (best && distance(best->d_centre, centre) > 1e-7)
				{
					throw std::invalid_argument(
							"More than one local junction is possible. Choose the approaching endpoints explicitly.");
				}
				if (cost < best_cost)
				{
					auto extended = oriented;
					for (unsigned int arm = 0; arm < 3; ++arm)
					{
						if (extensions[arm] <= COINCIDENCE)
						{
							extended[arm].back() = centre;
						}
						else
						{
							extended[arm].push_back(centre);
						}
					}
					best = InitialJunction(centre, extended, extensions, ends);
					best_cost = cost;
				}
			}
		}
		if (!best)
		{
			throw std::invalid_argument(
					"The three ridge-end directions do not meet at a unique nearby forward junction. "
					"Check the endpoints and terminal segments (maximum extension 30 degrees).");
		}
		return *best;
	}

	struct Patch
	{
		Patch(
				const Polygon &polygon,
				Plate plate,
				double older,
				double younger,
				const QString &name) :
			d_birth(polygon), d_current(polygon), d_plate(plate), d_older(older),
			d_younger(younger), d_name(name)
		{  }
		Polygon d_birth;
		Polygon d_current;
		Plate d_plate;
		double d_older;
		double d_younger;
		QString d_name;
	};
}


GPlatesViewOperations::TripleJunctionCrustBuilder::Result
GPlatesViewOperations::TripleJunctionCrustBuilder::create_crust(
		const std::vector<RidgeArm> &arms,
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		double start_age,
		double duration,
		double interval)
{
	if (arms.size() != 3)
	{
		throw std::invalid_argument("Triple Junction requires exactly three ridge arms.");
	}
	std::set<Plate> plates;
	std::set<std::pair<Plate, Plate>> pairs;
	for (const auto &arm : arms)
	{
		if (arm.d_left == arm.d_right || arm.d_junction_end < -1 || arm.d_junction_end > 1)
		{
			throw std::invalid_argument("Each arm needs distinct plate IDs and a valid endpoint choice.");
		}
		plates.insert(arm.d_left);
		plates.insert(arm.d_right);
		pairs.insert(std::minmax(arm.d_left, arm.d_right));
	}
	if (plates.size() != 3 || pairs.size() != 3)
	{
		throw std::invalid_argument("The three ridges must form plate pairs A/B, B/C and C/A.");
	}
	Result result;
	result.d_ages = MakeOceanCrustOperation::sample_times(start_age, duration, interval);
	std::reverse(result.d_ages.begin(), result.d_ages.end());
	if (result.d_ages.size() > 501)
	{
		throw std::invalid_argument("Triple Junction is limited to 500 steps; increase the interval.");
	}
	for (double age : result.d_ages)
	{
		const auto tree = tree_creator.get_reconstruction_tree(age);
		for (Plate plate : plates)
		{
			if (!tree->get_composed_absolute_rotation_or_none(plate))
			{
				throw std::invalid_argument(QString("No rotation for plate %1 at %2 Ma.")
						.arg(static_cast<qulonglong>(plate)).arg(age, 0, 'g', 15).toStdString());
			}
		}
	}
	const auto initial = find_initial_junction(arms);
	auto working = initial.d_arms;
	std::size_t segment_count = 0;
	for (const auto &vertices : working)
	{
		segment_count += vertices.size() - 1;
	}
	const std::size_t steps = result.d_ages.size() - 1;
	if (segment_count > MAX_PATCHES / (2 * steps) ||
			2 * segment_count * steps + 3 * steps * (steps + 1) > MAX_PATCHES)
	{
		throw std::invalid_argument("More than 100,000 junction patches requested; simplify the inputs.");
	}
	result.d_centres.push_back(initial.d_centre);
	result.d_initial_extensions_radians = initial.d_extensions;
	result.d_junction_ends = initial.d_ends;
	for (const auto &vertices : working)
	{
		result.d_initial_arms.push_back(GPlatesMaths::PolylineOnSphere::create(vertices));
	}
	std::vector<Patch> patches;
	std::size_t validation_work = 0;
	for (std::size_t step = 1; step < result.d_ages.size(); ++step)
	{
		const double older = result.d_ages[step - 1], younger = result.d_ages[step];
		try
		{
			const std::size_t previous_patch_count = patches.size();
			const auto old_tree = tree_creator.get_reconstruction_tree(older);
			const auto new_tree = tree_creator.get_reconstruction_tree(younger);
			std::map<Plate, GPlatesMaths::FiniteRotation> stages;
			std::map<Plate, Point> corners;
			for (Plate plate : plates)
			{
				const auto stage = GPlatesMaths::compose(new_tree->get_composed_absolute_rotation(plate),
						GPlatesMaths::get_reverse(old_tree->get_composed_absolute_rotation(plate)));
				stages.emplace(plate, stage);
				corners.emplace(plate, stage * result.d_centres.back());
			}
			auto corner = corners.begin();
			const auto a = position((corner++)->second);
			const auto b = position((corner++)->second);
			const auto c = position(corner->second);
			const auto ab = subtract(a, b), ac = subtract(a, c);
			auto axis = cross(ab, ac);
			if (length(ab) < 1e-7 || length(ac) < 1e-7 ||
					length(axis) <= 1e-4 * length(ab) * length(ac))
			{
				throw std::invalid_argument("Plate motions give a nearly collinear or stationary junction.");
			}
			axis = normalise(axis);
			if (dot(axis, {{a[0] + b[0] + c[0], a[1] + b[1] + c[1], a[2] + b[2] + c[2]}}) < 0)
			{
				for (auto &value : axis)
				{
					value = -value;
				}
			}
			const Point centre = point(axis);
			const auto gap = Crust::validate_patch({point(a), point(b), point(c)});
			if (!gap->is_point_in_polygon(centre, GPlatesMaths::PolygonOnSphere::ADAPTIVE, false))
			{
				throw std::invalid_argument(
						"This junction needs ridge retreat or a topology change, not extension alone.");
			}
			for (auto &patch : patches)
			{
				patch.d_current = stages.at(patch.d_plate) * patch.d_current;
			}
			for (std::size_t arm = 0; arm < 3; ++arm)
			{
				std::vector<GPlatesMaths::FiniteRotation> half_steps;
				GPlatesAppLogic::FlowlineUtils::fill_seed_point_rotations(younger,
						{younger, older}, older, arms[arm].d_left, arms[arm].d_right,
						tree_creator, half_steps);
				const auto reverse_old = GPlatesMaths::get_reverse(
						old_tree->get_composed_absolute_rotation(arms[arm].d_left));
				const auto new_left = new_tree->get_composed_absolute_rotation(arms[arm].d_left);
				Points young_ridge;
				for (const auto &vertex : working[arm])
				{
					young_ridge.push_back(new_left * GPlatesAppLogic::FlowlineUtils::reconstruct_seed_point(
							reverse_old * vertex, half_steps));
				}
				for (Plate plate : {arms[arm].d_left, arms[arm].d_right})
				{
					Points carried;
					for (const auto &vertex : working[arm])
					{
						carried.push_back(stages.at(plate) * vertex);
					}
					// Reuse this exact endpoint across both incident arms and their central fan.
					carried.back() = corners.at(plate);
					for (std::size_t segment = 1; segment < carried.size(); ++segment)
					{
						const auto polygon = Crust::validate_patch({young_ridge[segment - 1],
								young_ridge[segment], carried[segment], carried[segment - 1]});
						patches.emplace_back(polygon, plate, older, younger,
								QString("%1 — junction crust, %2–%3 Ma, segment %4")
										.arg(arms[arm].d_name).arg(older).arg(younger).arg(segment));
					}
					const auto fan = Crust::validate_patch({corners.at(plate), young_ridge.back(), centre});
					patches.emplace_back(fan, plate, older, younger,
							QString("%1 — junction centre crust, %2–%3 Ma")
									.arg(arms[arm].d_name).arg(older).arg(younger));
					++result.d_junction_patch_count;
				}
				if (distance(young_ridge.back(), centre) <= COINCIDENCE)
				{
					throw std::invalid_argument("A junction arm has no resolvable extension at this step.");
				}
				young_ridge.push_back(centre);
				working[arm] = young_ridge;
			}
			if (patches.size() > MAX_PATCHES || (validation_work += patches.size()) > MAX_VALIDATED_PATCHES)
			{
				throw std::invalid_argument("Too much junction geometry; reduce the span or use a larger step.");
			}
			std::vector<Polygon> current;
			for (const auto &patch : patches)
			{
				current.push_back(patch.d_current);
			}
			Crust::validate_patch_overlaps(current);
			result.d_centres.push_back(centre);
			result.d_step_patch_counts.push_back(patches.size() - previous_patch_count);
		}
		catch (const std::exception &error)
		{
			throw std::invalid_argument(QString("Triple junction, %1 to %2 Ma: %3")
					.arg(older).arg(younger).arg(error.what()).toStdString());
		}
	}
	for (const auto &vertices : working)
	{
		result.d_final_arms.push_back(GPlatesMaths::PolylineOnSphere::create(vertices));
	}
	for (const auto &patch : patches)
	{
		result.d_features.push_back(Crust::create_patch_feature(*patch.d_birth,
				patch.d_plate, tree_creator, patch.d_older, patch.d_younger, patch.d_name));
	}
	return result;
}
