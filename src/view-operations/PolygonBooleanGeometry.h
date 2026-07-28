/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_POLYGONBOOLEANGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_POLYGONBOOLEANGEOMETRY_H

#include <vector>

#include "maths/PolygonOnSphere.h"


namespace GPlatesViewOperations
{
	namespace PolygonBooleanGeometry
	{
		enum Operation
		{
			UNION,
			DIFFERENCE,
			INTERSECTION,
			SYMMETRIC_DIFFERENCE
		};

		typedef GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon_ptr_type;
		typedef std::vector<polygon_ptr_type> polygon_seq_type;

		/**
		 * Performs exact Boolean set operations on great-circle polygons on the
		 * unit sphere. Inputs and outputs may contain holes, and an operation may
		 * produce any number of disconnected polygons.
		 */
		polygon_seq_type
		apply(
				Operation operation,
				const polygon_seq_type &subjects,
				const polygon_seq_type &modifiers);
	}
}

#endif // GPLATES_VIEWOPERATIONS_POLYGONBOOLEANGEOMETRY_H
