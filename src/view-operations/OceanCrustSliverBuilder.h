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

#ifndef GPLATES_VIEWOPERATIONS_OCEANCRUSTSLIVERBUILDER_H
#define GPLATES_VIEWOPERATIONS_OCEANCRUSTSLIVERBUILDER_H

#include <vector>
#include <QString>

#include "model/FeatureHandle.h"
#include "model/types.h"

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"

namespace GPlatesAppLogic
{
	class ReconstructionTreeCreator;
}

namespace GPlatesMaths
{
	class PolylineOnSphere;
}

namespace GPlatesViewOperations
{
	namespace OceanCrustSliverBuilder
	{
		/** Shared strict convex-cell and positive-area overlap checks for crust generators. */
		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type
		validate_patch(
				const std::vector<GPlatesMaths::PointOnSphere> &corners);

		void
		validate_patch_overlaps(
				const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &polygons);

		/** Store a completed patch, supplied at its younger formation age, in its plate frame. */
		GPlatesModel::FeatureHandle::non_null_ptr_type
		create_patch_feature(
				const GPlatesMaths::PolygonOnSphere &polygon_at_birth,
				GPlatesModel::integer_plate_id_type plate_id,
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				double older_age,
				double younger_age,
				const QString &name);

		/**
		 * Create detached OceanicCrust polygons from a symmetric native flowline lattice.
		 *
		 * Each ridge segment and time interval produces one polygon on each plate. Geometry
		 * is evaluated at the final age, then stored in its own plate's present-day frame.
		 * A polygon appears at the interval's younger (completion) age and remains valid to
		 * 0 Ma; the description records both formation bounds. It is a completed, rigid strip,
		 * not a continuously growing polygon or a flowline reconstruction method.
		 *
		 * Output order is segment, youngest-first interval, then left and right plate.
		 * Adjacent coincident corners become triangles. Degenerate, non-convex, folded or
		 * reversing cells are rejected, as are overlaps within this generated lattice.
		 * This does not detect convergence from a directed-ridge convention, test existing
		 * crust, or clip collisions/subduction. The caller supplies a spreading model.
		 *
		 * Throws std::invalid_argument before any model mutation for invalid input or a
		 * request exceeding 100,000 polygons. The caller must retain the source rotation
		 * layer connection and bound aggregate work across multiple ridges.
		 */
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>
		create_slivers(
				const GPlatesMaths::PolylineOnSphere &reconstructed_ridge,
				GPlatesModel::integer_plate_id_type left_plate_id,
				GPlatesModel::integer_plate_id_type right_plate_id,
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				double start_time,
				double future_duration,
				double step_interval,
				const QString &name);
	}
}

#endif // GPLATES_VIEWOPERATIONS_OCEANCRUSTSLIVERBUILDER_H
