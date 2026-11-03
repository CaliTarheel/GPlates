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

#include "OceanCrustSliverBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

#include "MakeOceanCrustOperation.h"

#include "app-logic/FlowlineUtils.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "maths/FiniteRotation.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/ModelUtils.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	typedef std::array<double, 3> Vector;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;
	const std::size_t MAX_POLYGONS = 100000;
	const double POSITION_EPSILON = 1e-12;
	const double AREA_EPSILON = 1e-14;


	Vector
	position(
			const GPlatesMaths::PointOnSphere &point)
	{
		const auto &v = point.position_vector();
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


	bool
	coincident(
			const GPlatesMaths::PointOnSphere &a,
			const GPlatesMaths::PointOnSphere &b)
	{
		const auto av = position(a);
		const auto bv = position(b);
		double distance_squared = 0;
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			distance_squared += (av[axis] - bv[axis]) * (av[axis] - bv[axis]);
		}
		return distance_squared <= POSITION_EPSILON * POSITION_EPSILON;
	}


	struct Cell
	{
		Points points;
		std::vector<Vector> inward_normals;
		Vector lower;
		Vector upper;
		int orientation;
		std::size_t segment;
		std::size_t interval;
		unsigned int side;
	};


	Cell
	validate_cell(
			const Points &corners,
			std::size_t segment,
			std::size_t interval,
			unsigned int side)
	{
		Cell cell;
		cell.segment = segment;
		cell.interval = interval;
		cell.side = side;
		for (const auto &corner : corners)
		{
			if (cell.points.empty() || !coincident(cell.points.back(), corner))
			{
				cell.points.push_back(corner);
			}
		}
		if (cell.points.size() > 1 && coincident(cell.points.front(), cell.points.back()))
		{
			cell.points.pop_back();
		}
		if (cell.points.size() < 3)
		{
			throw std::invalid_argument("No resolvable crust area; check spreading and ridge geometry.");
		}

		Vector centre = {{0, 0, 0}};
		for (const auto &point : cell.points)
		{
			const auto p = position(point);
			for (unsigned int axis = 0; axis < 3; ++axis)
			{
				centre[axis] += p[axis];
			}
		}
		const double centre_length = std::sqrt(dot(centre, centre));
		if (centre_length < POSITION_EPSILON)
		{
			throw std::invalid_argument("Crust spans a hemisphere; use shorter ridge/time intervals.");
		}
		for (double &component : centre)
		{
			component /= centre_length;
		}

		// In one open hemisphere, great-circle edges become straight lines in a gnomonic
		// projection. Consistent strict half-plane tests prove a simple convex cell, rather
		// than relying on PolygonOnSphere::create (which accepts bowties and zero area).
		cell.orientation = 0;
		double minimum_centre_dot = 1;
		for (std::size_t edge = 0; edge < cell.points.size(); ++edge)
		{
			const auto a = position(cell.points[edge]);
			const auto b = position(cell.points[(edge + 1) % cell.points.size()]);
			minimum_centre_dot = std::min(minimum_centre_dot, dot(centre, a));
			if (minimum_centre_dot <= POSITION_EPSILON || dot(a, b) <= -1 + 1e-12)
			{
				throw std::invalid_argument("Crust has an ambiguous long edge; use smaller intervals.");
			}
			auto normal = cross(a, b);
			const double length = std::sqrt(dot(normal, normal));
			if (length <= POSITION_EPSILON)
			{
				throw std::invalid_argument("Crust contains a duplicate or antipodal edge.");
			}
			const int orientation = dot(normal, centre) > 0 ? 1 : -1;
			if (cell.orientation && orientation != cell.orientation)
			{
				throw std::invalid_argument("Crust folds or crosses itself; use smaller intervals.");
			}
			cell.orientation = orientation;
			for (double &component : normal)
			{
				component *= orientation / length;
			}
			for (std::size_t vertex = 0; vertex < cell.points.size(); ++vertex)
			{
				if (vertex != edge && vertex != (edge + 1) % cell.points.size() &&
						dot(normal, position(cell.points[vertex])) <= POSITION_EPSILON)
				{
					throw std::invalid_argument(
							"Crust is degenerate, folded or non-convex; use smaller intervals.");
				}
			}
			cell.inward_normals.push_back(normal);
		}
		const auto polygon = GPlatesMaths::PolygonOnSphere::create(cell.points);
		if (polygon->get_area().dval() <= AREA_EPSILON)
		{
			throw std::invalid_argument("No resolvable crust area; check spreading and ridge geometry.");
		}

		// A cap of radius < pi/2 about centre bounds both the edges and solid interior of
		// this convex cell. Its enclosing Cartesian box is conservative across the dateline.
		const double chord_radius = std::sqrt(2 * (1 - minimum_centre_dot));
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			cell.lower[axis] = centre[axis] - chord_radius - POSITION_EPSILON;
			cell.upper[axis] = centre[axis] + chord_radius + POSITION_EPSILON;
		}
		return cell;
	}


	bool
	has_separating_edge(
			const Cell &first,
			const Cell &second)
	{
		for (const auto &normal : first.inward_normals)
		{
			bool outside = true;
			for (const auto &point : second.points)
			{
				if (dot(normal, position(point)) > POSITION_EPSILON)
				{
					outside = false;
					break;
				}
			}
			if (outside)
			{
				return true;
			}
		}
		return false;
	}


	void
	validate_overlaps(
			const std::vector<Cell> &cells)
	{
		std::vector<std::size_t> order(cells.size());
		std::iota(order.begin(), order.end(), 0);
		// Sweep the most spread-out coordinate to avoid comparing all cell pairs. Keep
		// an explicit work bound for pathological long/overlapping ridges.
		unsigned int sweep_axis = 0;
		double best_spread = -1;
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			double lower = 2, upper = -2, widths = 0;
			for (const auto &cell : cells)
			{
				lower = std::min(lower, cell.lower[axis]);
				upper = std::max(upper, cell.upper[axis]);
				widths += cell.upper[axis] - cell.lower[axis];
			}
			const double spread = (upper - lower) / widths;
			if (spread > best_spread)
			{
				best_spread = spread;
				sweep_axis = axis;
			}
		}
		std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b)
		{
			return cells[a].lower[sweep_axis] < cells[b].lower[sweep_axis];
		});
		std::size_t comparisons = 0;
		for (std::size_t first = 0; first < order.size(); ++first)
		{
			const auto &a = cells[order[first]];
			for (std::size_t second = first + 1; second < order.size(); ++second)
			{
				const auto &b = cells[order[second]];
				if (b.lower[sweep_axis] > a.upper[sweep_axis])
				{
					break;
				}
				if (++comparisons > 2000000)
				{
					throw std::invalid_argument(
							"Crust overlap validation is too large; use fewer ridge vertices or time steps.");
				}
				bool boxes_overlap = true;
				for (unsigned int axis = 0; axis < 3; ++axis)
				{
					boxes_overlap = boxes_overlap && a.lower[axis] <= b.upper[axis] &&
							b.lower[axis] <= a.upper[axis];
				}
				// Convex spherical half-plane separation permits shared edges and vertices,
				// but rejects positive-area overlap, including identical cells.
				if (boxes_overlap && !has_separating_edge(a, b) && !has_separating_edge(b, a))
				{
					throw std::invalid_argument(QString(
							"Generated crust strips overlap (ridge segments %1/%2, time intervals %3/%4); "
							"shorten the range or simplify the ridge.")
							.arg(a.segment + 1).arg(b.segment + 1)
							.arg(a.interval + 1).arg(b.interval + 1).toStdString());
				}
			}
		}
	}


	void
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, name, value))
		{
			throw std::invalid_argument("Unable to add a required OceanicCrust property.");
		}
	}
}


GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
GPlatesViewOperations::OceanCrustSliverBuilder::validate_patch(
		const std::vector<GPlatesMaths::PointOnSphere> &corners)
{
	return GPlatesMaths::PolygonOnSphere::create(validate_cell(corners, 0, 0, 0).points);
}


void
GPlatesViewOperations::OceanCrustSliverBuilder::validate_patch_overlaps(
		const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &polygons)
{
	std::vector<Cell> cells;
	cells.reserve(polygons.size());
	for (std::size_t index = 0; index < polygons.size(); ++index)
	{
		const auto &polygon = polygons[index];
		cells.push_back(validate_cell(Points(polygon->exterior_ring_vertex_begin(),
				polygon->exterior_ring_vertex_end()), index, 0, 0));
	}
	if (!cells.empty())
	{
		validate_overlaps(cells);
	}
}


GPlatesModel::FeatureHandle::non_null_ptr_type
GPlatesViewOperations::OceanCrustSliverBuilder::create_patch_feature(
		const GPlatesMaths::PolygonOnSphere &polygon_at_birth,
		GPlatesModel::integer_plate_id_type plate_id,
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		double older_age,
		double younger_age,
		const QString &name)
{
	const auto feature = GPlatesModel::FeatureHandle::create(
			GPlatesModel::FeatureType::create_gpml("OceanicCrust"));
	const auto ref = feature->reference();
	add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
			GPlatesPropertyValues::XsString::create(GPlatesUtils::make_icu_string_from_qstring(name)));
	add_property(ref, GPlatesModel::PropertyName::create_gml("description"),
			GPlatesPropertyValues::XsString::create(GPlatesUtils::make_icu_string_from_qstring(
					QString("Triple-junction ocean crust; formation interval %1 to %2 Ma. "
							"Completed rigid patch on plate %3, valid from %2 Ma to 0 Ma. "
							"Sampled symmetric spreading with a shared finite-step junction; "
							"no existing-crust collision or subduction clipping.")
							.arg(older_age, 0, 'g', 15).arg(younger_age, 0, 'g', 15)
							.arg(static_cast<qulonglong>(plate_id)))));
	add_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
			GPlatesModel::ModelUtils::create_gml_time_period(
					GPlatesPropertyValues::GeoTimeInstant(younger_age),
					GPlatesPropertyValues::GeoTimeInstant(0)));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
			GPlatesPropertyValues::GpmlPlateId::create(plate_id));
	const auto tree = tree_creator.get_reconstruction_tree(younger_age);
	const auto stored = GPlatesMaths::get_reverse(tree->get_composed_absolute_rotation(plate_id)) *
			polygon_at_birth.get_non_null_pointer();
	add_property(ref, GPlatesModel::PropertyName::create_gpml("outlineOf"),
			GPlatesAppLogic::GeometryUtils::create_geometry_property_value(stored));
	return feature;
}


std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>
GPlatesViewOperations::OceanCrustSliverBuilder::create_slivers(
		const GPlatesMaths::PolylineOnSphere &reconstructed_ridge,
		GPlatesModel::integer_plate_id_type left_plate_id,
		GPlatesModel::integer_plate_id_type right_plate_id,
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		double start_time,
		double future_duration,
		double step_interval,
		const QString &name)
{
	const auto times = MakeOceanCrustOperation::sample_times(
			start_time, future_duration, step_interval);
	const std::size_t segments = reconstructed_ridge.number_of_vertices() - 1;
	const std::size_t intervals = times.size() - 1;
	if (left_plate_id == right_plate_id)
	{
		throw std::invalid_argument("Ocean crust requires two distinct plate IDs.");
	}
	if (segments > MAX_POLYGONS / 2 / intervals)
	{
		throw std::invalid_argument("Too many crust polygons; the limit is 100,000 per request.");
	}
	const GPlatesModel::integer_plate_id_type plate_ids[] = {left_plate_id, right_plate_id};
	for (double time : times)
	{
		const auto tree = tree_creator.get_reconstruction_tree(time);
		for (auto plate : plate_ids)
		{
			if (!tree->get_composed_absolute_rotation_or_none(plate))
			{
				throw std::invalid_argument(QString("No rotation for plate %1 at %2 Ma.")
						.arg(static_cast<qulonglong>(plate)).arg(time, 0, 'g', 15).toStdString());
			}
		}
	}

	const auto start_tree = tree_creator.get_reconstruction_tree(start_time);
	const auto final_tree = tree_creator.get_reconstruction_tree(times.front());
	std::vector<Points> lattice[2];
	for (unsigned int side = 0; side < 2; ++side)
	{
		std::vector<GPlatesMaths::FiniteRotation> seed_rotations, flowline_rotations;
		GPlatesAppLogic::FlowlineUtils::fill_seed_point_rotations(
				times.front(), times, start_time, plate_ids[side], plate_ids[1 - side],
				tree_creator, seed_rotations);
		GPlatesAppLogic::FlowlineUtils::fill_flowline_rotations(
				times.front(), times, start_time, plate_ids[side], plate_ids[1 - side],
				tree_creator, flowline_rotations);
		const auto reverse_start = GPlatesMaths::get_reverse(
				start_tree->get_composed_absolute_rotation(plate_ids[side]));
		const auto final_rotation = final_tree->get_composed_absolute_rotation(plate_ids[side]);
		for (auto vertex = reconstructed_ridge.vertex_begin();
				vertex != reconstructed_ridge.vertex_end(); ++vertex)
		{
			// inverse(R_side(start)) * ridge is also the native right-frame seed: it equals
			// left-to-right(start) * inverse(R_left(start)) * ridge. Computing each directly
			// avoids constructing a temporary Flowline and permits stationary pole endpoints.
			auto point = GPlatesAppLogic::FlowlineUtils::reconstruct_seed_point(
					reverse_start * *vertex, seed_rotations);
			Points row(1, final_rotation * point);
			for (const auto &rotation : flowline_rotations)
			{
				point = rotation * point;
				row.push_back(final_rotation * point);
			}
			if (row.size() != times.size())
			{
				throw std::invalid_argument("The rotation model produced an incomplete crust lattice.");
			}
			lattice[side].push_back(row);
		}
	}
	// The two youngest rows are the SAME ridge. Independent native half-stage compositions
	// can disagree by a few picoradians even for a fixed pole. Use one exact seam, after
	// checking agreement within 1e-9 radians (about 6 mm on Earth). Do not loosen the overlap
	// test or skip neighbouring cells: those can genuinely fold over one another.
	for (std::size_t seed = 0; seed < lattice[0].size(); ++seed)
	{
		const auto left = position(lattice[0][seed].front());
		const auto right = position(lattice[1][seed].front());
		double distance_squared = 0;
		for (unsigned int axis = 0; axis < 3; ++axis)
		{
			distance_squared += (left[axis] - right[axis]) * (left[axis] - right[axis]);
		}
		if (distance_squared > 1e-18)
		{
			throw std::invalid_argument(
					"The native plate halves do not share a consistent ridge seam; shorten the range.");
		}
		lattice[1][seed].front() = lattice[0][seed].front();
	}

	std::vector<Cell> cells;
	cells.reserve(2 * segments * intervals);
	int orientations[2] = {0, 0};
	for (std::size_t segment = 0; segment < segments; ++segment)
	{
		for (std::size_t interval = 0; interval < intervals; ++interval)
		{
			for (unsigned int side = 0; side < 2; ++side)
			{
				const auto &a = lattice[side][segment];
				const auto &b = lattice[side][segment + 1];
				auto cell = validate_cell({a[interval], b[interval],
						b[interval + 1], a[interval + 1]}, segment, interval, side);
				if (orientations[side] && orientations[side] != cell.orientation)
				{
					throw std::invalid_argument(
							"Spreading reverses or folds along this ridge/range; split the request.");
				}
				orientations[side] = cell.orientation;
				cells.push_back(cell);
			}
			if (orientations[0] == orientations[1])
			{
				throw std::invalid_argument("The two crust sides fold onto the same side of the ridge.");
			}
		}
	}
	validate_overlaps(cells);

	std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> result;
	result.reserve(cells.size());
	for (const auto &cell : cells)
	{
		const auto feature = GPlatesModel::FeatureHandle::create(
				GPlatesModel::FeatureType::create_gpml("OceanicCrust"));
		const auto ref = feature->reference();
		const double younger = times[cell.interval];
		const double older = times[cell.interval + 1];
		const QString label = QString("%1 — %2 crust, segment %3, %4–%5 Ma")
				.arg(name).arg(cell.side == 0 ? "left" : "right")
				.arg(cell.segment + 1).arg(older, 0, 'g', 15).arg(younger, 0, 'g', 15);
		add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(label)));
		add_property(ref, GPlatesModel::PropertyName::create_gml("description"),
				GPlatesPropertyValues::XsString::create(GPlatesUtils::make_icu_string_from_qstring(
						QString("Symmetric (50/50) ocean crust from %1; formation interval %2 to %3 Ma. "
								"Appears as a completed rigid strip at %3 Ma; survives to 0 Ma unless retired. "
								"Source plates %4/%5; ridge segment %6; generated at %7 Ma. "
								"Uses sampled native flowlines; no collision or subduction clipping.")
								.arg(name).arg(older, 0, 'g', 15).arg(younger, 0, 'g', 15)
								.arg(static_cast<qulonglong>(left_plate_id))
								.arg(static_cast<qulonglong>(right_plate_id))
								.arg(cell.segment + 1).arg(times.front(), 0, 'g', 15))));
		add_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(younger),
						GPlatesPropertyValues::GeoTimeInstant(0)));
		add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_ids[cell.side]));
		const auto stored = GPlatesMaths::get_reverse(
				final_tree->get_composed_absolute_rotation(plate_ids[cell.side])) *
				GPlatesMaths::PolygonOnSphere::create(cell.points);
		add_property(ref, GPlatesModel::PropertyName::create_gpml("outlineOf"),
				GPlatesAppLogic::GeometryUtils::create_geometry_property_value(stored));
		result.push_back(feature);
	}
	return result;
}
