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


#ifndef GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONCRUSTBUILDER_H
#define GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONCRUSTBUILDER_H

#include <vector>
#include <QString>

#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/types.h"

namespace GPlatesAppLogic
{
	class ReconstructionTreeCreator;
}

namespace GPlatesViewOperations
{
	namespace TripleJunctionCrustBuilder
	{
		struct RidgeArm
		{
			RidgeArm(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &geometry,
					GPlatesModel::integer_plate_id_type left,
					GPlatesModel::integer_plate_id_type right,
					const QString &name,
					int junction_end = -1) :
				d_geometry(geometry), d_left(left), d_right(right), d_name(name),
				d_junction_end(junction_end)
			{  }

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type d_geometry;
			GPlatesModel::integer_plate_id_type d_left;
			GPlatesModel::integer_plate_id_type d_right;
			QString d_name;
			// -1: infer a unique local combination; 0: first vertex; 1: last vertex.
			int d_junction_end;
		};

		struct Result
		{
			std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_features;
			std::vector<double> d_ages; // Oldest first, including the start.
			std::vector<GPlatesMaths::PointOnSphere> d_centres;
			std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> d_initial_arms;
			std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> d_final_arms;
			std::vector<double> d_initial_extensions_radians;
			std::vector<int> d_junction_ends;
			std::vector<std::size_t> d_step_patch_counts; // One per completed interval.
			std::size_t d_junction_patch_count = 0;
		};

		/**
		 * Prepare detached crust from three arms forming the A/B, B/C, C/A plate circuit.
		 * Terminal great-circle directions must have one local forward meeting point.
		 * Each finite step advances that point with all three plates, chooses their local
		 * spherical circumcentre, then joins the native half-stage tips to it. The six
		 * central triangles and all arm strips are checked together at every sample.
		 * Rejects configurations requiring retreat, ambiguous intersections or overlaps.
		 *
		 * Source ridges and rotations are never modified. Returned arm/centre samples are
		 * previews, not persisted MOR animation. Crust appears at each completion age;
		 * continuity between samples and collision with pre-existing crust are not implied.
		 * Throws std::invalid_argument before model mutation. Work and output are bounded.
		 */
		Result
		create_crust(
				const std::vector<RidgeArm> &arms,
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				double start_age,
				double duration,
				double interval);
	}
}

#endif // GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONCRUSTBUILDER_H
