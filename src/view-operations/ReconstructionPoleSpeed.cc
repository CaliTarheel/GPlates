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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <vector>

#include "ReconstructionPoleSpeed.h"

#include "app-logic/ReconstructionGraph.h"
#include "app-logic/ReconstructionTree.h"

#include "maths/CalculateVelocity.h"
#include "maths/GeometryDistance.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "property-values/GeoTimeInstant.h"


namespace
{
	/** Find a time boundary without relying on arbitrary time increments or cache tolerances. */
	boost::optional<double>
	find_next_older_time(
			const GPlatesAppLogic::ReconstructionGraph::Plate &initial_plate,
			const GPlatesPropertyValues::GeoTimeInstant &current_time)
	{
		boost::optional<double> next_time;
		std::set<GPlatesModel::integer_plate_id_type> visited;
		std::vector<const GPlatesAppLogic::ReconstructionGraph::Plate *> pending(1, &initial_plate);
		while (!pending.empty())
		{
			const auto *plate = pending.back();
			pending.pop_back();
			if (!visited.insert(plate->get_plate_id()).second)
			{
				continue;
			}
			const auto inspect_edge = [&](const GPlatesAppLogic::ReconstructionGraph::Edge &edge)
			{
				for (const auto &sample : edge.get_pole())
				{
					const auto &time = sample.get_time_instant();
					if (time.is_real() && time.is_strictly_earlier_than(current_time) &&
							(!next_time || time.value() < *next_time))
					{
						next_time = time.value();
					}
				}
			};
			for (const auto &edge : plate->get_incoming_edges())
			{
				inspect_edge(edge);
				pending.push_back(&edge.get_fixed_plate());
			}
			for (const auto &edge : plate->get_outgoing_edges())
			{
				inspect_edge(edge);
				pending.push_back(&edge.get_moving_plate());
			}
		}
		return next_time;
	}
}


boost::optional<GPlatesViewOperations::ReconstructionPoleSpeed::Reference>
GPlatesViewOperations::ReconstructionPoleSpeed::prepare_reference(
		const GPlatesAppLogic::ReconstructionTree &tree,
		GPlatesModel::integer_plate_id_type plate_id)
{
	const double current_time = tree.get_reconstruction_time();
	if (!std::isfinite(current_time) || !tree.get_edge(plate_id))
	{
		return boost::none;
	}
	const auto graph = tree.get_reconstruction_graph();
	const auto graph_plate = graph->get_plate(plate_id);
	if (!graph_plate)
	{
		return boost::none;
	}
	const GPlatesPropertyValues::GeoTimeInstant current_instant(current_time);
	const auto next_time = find_next_older_time(*graph_plate, current_instant);
	if (!next_time)
	{
		return boost::none;
	}

	// At a crossover the exact boundary can select either branch according to graph order.
	// Resolve inside the immediately older interval instead. No sequence boundary lies between
	// current_time and next_time, so this never guesses which inactive sequence is relevant.
	// Using a midpoint also avoids a nextafter-sized offset being treated as the same GeoTime.
	const double probe_time = current_time + (*next_time - current_time) * 0.5;
	const GPlatesPropertyValues::GeoTimeInstant probe_instant(probe_time);
	if (!probe_instant.is_strictly_earlier_than(current_instant) ||
			!probe_instant.is_strictly_later_than(GPlatesPropertyValues::GeoTimeInstant(*next_time)))
	{
		return boost::none;
	}
	const auto probe_tree = GPlatesAppLogic::ReconstructionTree::create(
			graph, probe_time, tree.get_anchor_plate_id());
	const auto previous_edge = probe_tree->get_edge(plate_id);
	if (!previous_edge)
	{
		// Do not bridge a period where this plate has no reconstruction circuit.
		return boost::none;
	}
	boost::optional<double> previous_time;
	for (const auto &sample : previous_edge->get_graph_edge().get_pole())
	{
		const auto &time = sample.get_time_instant();
		if (time.is_real() && time.is_strictly_earlier_than(current_instant) &&
				(!previous_time || time.value() < *previous_time))
		{
			previous_time = time.value();
		}
	}
	if (!previous_time)
	{
		return boost::none;
	}
	const auto previous_tree = GPlatesAppLogic::ReconstructionTree::create(
			graph, *previous_time, tree.get_anchor_plate_id());
	const auto current_total = tree.get_composed_absolute_rotation_or_none(plate_id);
	const auto previous_total = previous_tree->get_composed_absolute_rotation_or_none(plate_id);
	if (!current_total || !previous_total)
	{
		return boost::none;
	}
	return Reference{*previous_time, current_time, tree.get_anchor_plate_id(),
			*current_total, *previous_total};
}


boost::optional<double>
GPlatesViewOperations::ReconstructionPoleSpeed::calculate_maximum_speed(
		const Reference &reference,
		const std::vector<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> &
				unadjusted_plate_geometries,
		const GPlatesMaths::Rotation &adjustment,
		double radius_km)
{
	const double interval = reference.d_previous_time - reference.d_current_time;
	if (!std::isfinite(interval) || interval <= 0.0 ||
			!std::isfinite(radius_km) || radius_km <= 0.0)
	{
		throw std::invalid_argument("Speed requires a positive finite time interval and radius.");
	}
	if (unadjusted_plate_geometries.empty())
	{
		return boost::none;
	}
	const auto preview_total = GPlatesMaths::compose(adjustment, reference.d_current_total);
	const auto stage = GPlatesMaths::calculate_stage_rotation(
			preview_total, reference.d_previous_total);
	if (GPlatesMaths::represents_identity_rotation(stage.unit_quat()))
	{
		return 0.0;
	}
	const auto params = stage.unit_quat().get_rotation_params(boost::none);
	// Speed is omega * radius * |axis cross point|. Its maximum over a geometry is
	// cos(minimum distance to the stage equator) times omega * radius. Four quarter-circle
	// arcs represent that entire equator exactly, including maxima inside geometry segments
	// and filled polygon interiors. GeometryDistance supplies the same hole semantics as
	// reconstruction. Rotate the axis back instead of allocating every preview geometry.
	const auto axis = adjustment.get_reverse() * params.axis;
	const auto u = GPlatesMaths::generate_perpendicular(axis);
	const auto v = GPlatesMaths::cross(axis, u).get_normalisation();
	const auto equator = GPlatesMaths::PolylineOnSphere::create(
			std::vector<GPlatesMaths::PointOnSphere>{GPlatesMaths::PointOnSphere(u),
					GPlatesMaths::PointOnSphere(v), GPlatesMaths::PointOnSphere(-u),
					GPlatesMaths::PointOnSphere(-v), GPlatesMaths::PointOnSphere(u)});
	double maximum_factor = 0.0;
	for (const auto &geometry : unadjusted_plate_geometries)
	{
		const auto distance = GPlatesMaths::minimum_distance(*geometry, *equator, true, false);
		// No distance threshold was supplied, so the distance always exists.
		maximum_factor = std::max(maximum_factor,
				std::max(0.0, std::min(1.0, distance->get_cosine().dval())));
		if (maximum_factor == 1.0)
		{
			break;
		}
	}
	return std::fabs(params.angle.dval()) / interval * radius_km * 0.1 * maximum_factor;
}
