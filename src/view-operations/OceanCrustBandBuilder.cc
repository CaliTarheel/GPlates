/* $Id$ */

/**
 * \file
 * Reusable geometry service for Worldbuilding Pasta ocean-crust generation.
 */

#include "OceanCrustBandBuilder.h"

#include <exception>
#include <stdexcept>
#include <vector>

#include "SubductionCutterGeometry.h"

#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructionTree.h"

#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace
{
	GPlatesViewOperations::OceanCrustBandBuilder::polygon_ptr_type
	create_candidate_band(
			const GPlatesMaths::PolylineOnSphere &current_ridge,
			const GPlatesMaths::PolylineOnSphere &older_ridge,
			GPlatesModel::integer_plate_id_type plate_id,
			const GPlatesAppLogic::ReconstructionTree &older_tree,
			const GPlatesAppLogic::ReconstructionTree &current_tree)
	{
		const std::size_t current_vertex_count = static_cast<std::size_t>(
				std::distance(current_ridge.vertex_begin(), current_ridge.vertex_end()));
		const std::size_t older_vertex_count = static_cast<std::size_t>(
				std::distance(older_ridge.vertex_begin(), older_ridge.vertex_end()));
		if (current_vertex_count < 2 || current_vertex_count != older_vertex_count)
		{
			throw std::runtime_error(
					"The ridge geometry changed vertex topology between the two ages.");
		}

		std::vector<GPlatesMaths::PointOnSphere> advected_older_ridge;
		advected_older_ridge.reserve(older_vertex_count);
		for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
				older_ridge.vertex_begin(); vertex_iter != older_ridge.vertex_end(); ++vertex_iter)
		{
			const GPlatesMaths::PointOnSphere plate_frame_point =
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							*vertex_iter, plate_id, older_tree, true);
			advected_older_ridge.push_back(
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							plate_frame_point, plate_id, current_tree, false));
		}

		bool has_spreading_width = false;
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator current_iter =
				current_ridge.vertex_begin();
		for (std::size_t vertex_index = 0; vertex_index < older_vertex_count;
				++vertex_index, ++current_iter)
		{
			if (!GPlatesMaths::points_are_coincident(
					*current_iter, advected_older_ridge[vertex_index]))
			{
				has_spreading_width = true;
				break;
			}
		}
		if (!has_spreading_width)
		{
			throw std::runtime_error(
					"This plate has no recorded motion across the requested age band.");
		}

		std::vector<GPlatesMaths::PointOnSphere> ring(
				current_ridge.vertex_begin(), current_ridge.vertex_end());
		for (std::vector<GPlatesMaths::PointOnSphere>::const_reverse_iterator vertex_iter =
				advected_older_ridge.rbegin(); vertex_iter != advected_older_ridge.rend(); ++vertex_iter)
		{
			ring.push_back(*vertex_iter);
		}
		return GPlatesMaths::PolygonOnSphere::create(ring);
	}
}


GPlatesViewOperations::OceanCrustBandBuilder::Result
GPlatesViewOperations::OceanCrustBandBuilder::subtract_existing_crust(
		const GPlatesMaths::PolygonOnSphere &candidate,
		const polygon_seq_type &existing_ocean_crust)
{
	Result result;
	const SubductionCutterGeometry::CutResult cut =
			SubductionCutterGeometry::cut_polygon(candidate, existing_ocean_crust);
	if (!cut.success)
	{
		result.success = false;
		result.error = cut.error;
		return result;
	}
	result.existing_overlap_removed = cut.overlap;
	result.polygons = cut.outside;
	return result;
}


GPlatesViewOperations::OceanCrustBandBuilder::Result
GPlatesViewOperations::OceanCrustBandBuilder::build_side_band(
		const GPlatesMaths::PolylineOnSphere &current_ridge,
		const GPlatesMaths::PolylineOnSphere &older_ridge,
		GPlatesModel::integer_plate_id_type plate_id,
		const GPlatesAppLogic::ReconstructionTree &older_tree,
		const GPlatesAppLogic::ReconstructionTree &current_tree,
		const polygon_seq_type &existing_ocean_crust)
{
	try
	{
		const polygon_ptr_type candidate = create_candidate_band(
				current_ridge, older_ridge, plate_id, older_tree, current_tree);
		return subtract_existing_crust(*candidate, existing_ocean_crust);
	}
	catch (const std::exception &exception)
	{
		Result result;
		result.success = false;
		result.error = QString::fromLocal8Bit(exception.what());
		return result;
	}
}


GPlatesViewOperations::OceanCrustBandBuilder::polygon_ptr_type
GPlatesViewOperations::OceanCrustBandBuilder::reverse_reconstruct_polygon(
		const GPlatesMaths::PolygonOnSphere &polygon,
		GPlatesModel::integer_plate_id_type plate_id,
		const GPlatesAppLogic::ReconstructionTree &current_tree)
{
	std::vector<GPlatesMaths::PointOnSphere> stored_ring;
	stored_ring.reserve(static_cast<std::size_t>(std::distance(
			polygon.exterior_ring_vertex_begin(), polygon.exterior_ring_vertex_end())));
	for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
			polygon.exterior_ring_vertex_begin();
			vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
	{
		stored_ring.push_back(GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
				*vertex_iter, plate_id, current_tree, true));
	}
	std::vector< std::vector<GPlatesMaths::PointOnSphere> > stored_interior_rings;
	for (unsigned int ring_index = 0;
			ring_index < polygon.number_of_interior_rings(); ++ring_index)
	{
		stored_interior_rings.push_back(std::vector<GPlatesMaths::PointOnSphere>());
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.interior_ring_vertex_begin(ring_index);
				vertex_iter != polygon.interior_ring_vertex_end(ring_index); ++vertex_iter)
		{
			stored_interior_rings.back().push_back(
					GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
							*vertex_iter, plate_id, current_tree, true));
		}
	}
	return GPlatesMaths::PolygonOnSphere::create(
			stored_ring.begin(), stored_ring.end(),
			stored_interior_rings.begin(), stored_interior_rings.end(), true);
}
