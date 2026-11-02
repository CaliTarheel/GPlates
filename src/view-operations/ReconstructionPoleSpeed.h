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

#ifndef GPLATES_VIEWOPERATIONS_RECONSTRUCTIONPOLESPEED_H
#define GPLATES_VIEWOPERATIONS_RECONSTRUCTIONPOLESPEED_H

#include <vector>
#include <boost/optional.hpp>

#include "maths/FiniteRotation.h"
#include "maths/GeometryOnSphere.h"
#include "maths/Rotation.h"

#include "model/types.h"


namespace GPlatesAppLogic
{
	class ReconstructionTree;
}

namespace GPlatesViewOperations
{
	namespace ReconstructionPoleSpeed
	{
		/** Immutable calculation inputs; refresh after time, anchor or rotation-model changes. */
		struct Reference
		{
			double d_previous_time;
			double d_current_time;
			GPlatesModel::integer_plate_id_type d_anchor_plate_id;
			GPlatesMaths::FiniteRotation d_current_total;
			GPlatesMaths::FiniteRotation d_previous_total;
		};

		/**
		 * Find the preceding enabled keyframe in geological forward time (toward 0 Ma).
		 *
		 * At a crossover, use the sequence actually selected immediately older than the
		 * current time. Both totals use this tree's graph and anchor, including parent motion.
		 * Returns none for a missing plate, the anchor itself, a gap or no earlier keyframe.
		 * This reads the loaded model snapshot; it does not reread or modify a rotation file.
		 */
		boost::optional<Reference>
		prepare_reference(
				const GPlatesAppLogic::ReconstructionTree &tree,
				GPlatesModel::integer_plate_id_type plate_id);

		/**
		 * Return the maximum stage speed in cm/year across the supplied plate geometries.
		 *
		 * Geometries are at the current time before the accumulated adjustment. Includes
		 * segment and filled polygon interiors (respecting holes), not only their vertices.
		 * This includes existing model motion, not just the drag displacement.
		 * Uses GPlates' shortest-stage rotation convention and an explicit planetary radius.
		 * Returns none for an empty geometry collection.
		 * Throws std::invalid_argument for a nonpositive/nonfinite interval or radius.
		 */
		boost::optional<double>
		calculate_maximum_speed(
				const Reference &reference,
				const std::vector<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> &
						unadjusted_plate_geometries,
				const GPlatesMaths::Rotation &adjustment,
				double radius_km);
	}
}

#endif // GPLATES_VIEWOPERATIONS_RECONSTRUCTIONPOLESPEED_H
