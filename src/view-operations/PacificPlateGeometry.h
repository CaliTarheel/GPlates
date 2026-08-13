/* $Id$ */

/**
 * \file
 * Conservative local-void geometry for Pacific-style plate birth.
 */

#ifndef GPLATES_VIEWOPERATIONS_PACIFICPLATEGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_PACIFICPLATEGEOMETRY_H

#include <vector>

#include <boost/optional.hpp>
#include <QString>

#include "NaturalizeCoastlineGeometry.h"
#include "OceanCrustBandBuilder.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace PacificPlateGeometry
	{
		typedef std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type>
				polyline_seq_type;

		struct Result
		{
			Result();

			bool success;
			bool existing_overlap_removed;
			QString error;
			std::vector<GPlatesMaths::PointOnSphere> selected_endpoints;
			polyline_seq_type bounding_ridges;
			boost::optional<OceanCrustBandBuilder::polygon_ptr_type> central_crust;
			unsigned int inserted_vertex_count;
			double maximum_seed_distance_degrees;
			double actual_maximum_segment_length_km;
		};

		/**
		 * Chooses the endpoint of each ridge nearest the seed, naturalizes the
		 * three connecting MORs, and returns only the uncovered local component
		 * containing the seed. It never computes a global spherical complement.
		 */
		Result
		build_local_void(
				const polyline_seq_type &selected_ridges,
				const GPlatesMaths::PointOnSphere &seed,
				double maximum_seed_distance_degrees,
				const NaturalizeCoastlineGeometry::Parameters &naturalize_parameters,
				const OceanCrustBandBuilder::polygon_seq_type &existing_ocean_crust);
	}
}

#endif // GPLATES_VIEWOPERATIONS_PACIFICPLATEGEOMETRY_H
