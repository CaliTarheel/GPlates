/* $Id$ */

/**
 * \file
 * Reusable geometry service for one side of an ocean-crust age band.
 */

#ifndef GPLATES_VIEWOPERATIONS_OCEANCRUSTBANDBUILDER_H
#define GPLATES_VIEWOPERATIONS_OCEANCRUSTBANDBUILDER_H

#include <vector>

#include <QString>

#include "maths/PolygonOnSphere.h"
#include "model/types.h"


namespace GPlatesAppLogic
{
	class ReconstructionTree;
}

namespace GPlatesMaths
{
	class PolylineOnSphere;
}

namespace GPlatesViewOperations
{
	namespace OceanCrustBandBuilder
	{
		typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon_ptr_type;
		typedef std::vector<polygon_ptr_type> polygon_seq_type;

		struct Result
		{
			Result() : success(true), existing_overlap_removed(false) {  }

			bool success;
			bool existing_overlap_removed;
			QString error;
			polygon_seq_type polygons;
		};

		/**
		 * Builds a ruled band between the current MOR and the older MOR advected
		 * into the current frame of @a plate_id, then subtracts existing crust.
		 */
		Result
		build_side_band(
				const GPlatesMaths::PolylineOnSphere &current_ridge,
				const GPlatesMaths::PolylineOnSphere &older_ridge,
				GPlatesModel::integer_plate_id_type plate_id,
				const GPlatesAppLogic::ReconstructionTree &older_tree,
				const GPlatesAppLogic::ReconstructionTree &current_tree,
				const polygon_seq_type &existing_ocean_crust);

		/** Subtracts the union of existing crust from a candidate band. */
		Result
		subtract_existing_crust(
				const GPlatesMaths::PolygonOnSphere &candidate,
				const polygon_seq_type &existing_ocean_crust);

		/** Reverse-reconstructs an accepted current-time polygon for storage. */
		polygon_ptr_type
		reverse_reconstruct_polygon(
				const GPlatesMaths::PolygonOnSphere &polygon,
				GPlatesModel::integer_plate_id_type plate_id,
				const GPlatesAppLogic::ReconstructionTree &current_tree);
	}
}

#endif // GPLATES_VIEWOPERATIONS_OCEANCRUSTBANDBUILDER_H
