/* $Id$ */

/**
 * \file
 * Spherical polygon clipping support for the World Building subduction cutter.
 */

#ifndef GPLATES_VIEWOPERATIONS_SUBDUCTIONCUTTERGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_SUBDUCTIONCUTTERGEOMETRY_H

#include <vector>

#include <QString>

#include "maths/PolygonOnSphere.h"


namespace GPlatesViewOperations
{
	namespace SubductionCutterGeometry
	{
		typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon_ptr_type;
		typedef std::vector<polygon_ptr_type> polygon_seq_type;

		struct CutResult
		{
			CutResult() : success(true), overlap(false) {  }

			bool success;
			bool overlap;
			QString error;
			polygon_seq_type inside;
			polygon_seq_type outside;
		};

		/**
		 * Splits @a target into portions inside and outside the union of @a cutters.
		 *
		 * Input great-circle arcs are tessellated, projected with a locally-centred
		 * Lambert azimuthal equal-area projection, clipped, and mapped back to the
		 * sphere. This keeps the operation dateline-safe and retains polygon holes.
		 */
		CutResult
		cut_polygon(
				const GPlatesMaths::PolygonOnSphere &target,
				const polygon_seq_type &cutters);
	}
}

#endif // GPLATES_VIEWOPERATIONS_SUBDUCTIONCUTTERGEOMETRY_H
