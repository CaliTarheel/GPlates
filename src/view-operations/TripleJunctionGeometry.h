/* $Id$ */

/**
 * \file
 * Conservative spherical endpoint resolution for an RRR triple junction.
 */

#ifndef GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONGEOMETRY_H

#include <vector>

#include <QString>

#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace TripleJunctionGeometry
	{
		typedef GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline_ptr_type;
		typedef std::vector<polyline_ptr_type> polyline_seq_type;

		struct Result
		{
			Result() :
				success(false),
				junction(GPlatesMaths::PointOnSphere::north_pole),
				maximum_extension_degrees(0.0),
				minimum_branch_angle_degrees(0.0),
				maximum_branch_angle_degrees(0.0)
			{  }

			bool success;
			QString error;
			GPlatesMaths::PointOnSphere junction;
			polyline_seq_type resolved_ridges;
			std::vector<bool> junction_at_first_vertex;
			double maximum_extension_degrees;
			double minimum_branch_angle_degrees;
			double maximum_branch_angle_degrees;
		};

		/**
		 * Chooses the closest permutation of the three ridge endpoints, derives a
		 * spherical consensus point and adds only the short terminal links needed
		 * to give every ridge the exact same endpoint.
		 */
		Result
		resolve_rrr_endpoints(
				const polyline_seq_type &ridges,
				double maximum_extension_degrees,
				double minimum_stable_branch_angle_degrees = 15.0,
				double maximum_stable_branch_angle_degrees = 170.0);
	}
}

#endif // GPLATES_VIEWOPERATIONS_TRIPLEJUNCTIONGEOMETRY_H
