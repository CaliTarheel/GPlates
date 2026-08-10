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

		struct BoundaryResult
		{
			BoundaryResult();

			bool success;
			QString error;
			std::vector<GPlatesMaths::PointOnSphere> selected_endpoints;
			polyline_seq_type bounding_ridges;
			/** The raw void polygon bounded by the naturalized ridges - existing ocean crust and
			 *  any other plate's ordinary spreading bands have NOT been subtracted yet. Callers
			 *  that build other geometry sharing this boundary (e.g. ordinary side-crust bands
			 *  for the plates the three ridges already belong to) should subtract this polygon
			 *  from their own candidates too, so every piece around the junction terminates at
			 *  the same naturalized edge instead of being trimmed independently and (depending on
			 *  how the two unrelated boundaries happen to fall) leaving a gap or an overlap. */
			boost::optional<OceanCrustBandBuilder::polygon_ptr_type> void_polygon;
			unsigned int inserted_vertex_count;
			double maximum_seed_distance_degrees;
			double actual_maximum_segment_length_km;
		};

		/**
		 * Chooses the endpoint of each ridge nearest the seed, naturalizes the three
		 * connecting MORs, and returns the resulting void polygon before any existing
		 * crust (or another plate's ordinary spreading band) has been subtracted from it.
		 * Split out from @a build_local_void so a caller that also builds other geometry
		 * sharing this same boundary - namely the ordinary side-crust bands for the plates
		 * the three selected ridges already belong to - can make that geometry stop
		 * exactly at this edge, rather than each piece being clipped independently.
		 */
		BoundaryResult
		compute_void_boundary(
				const polyline_seq_type &selected_ridges,
				const GPlatesMaths::PointOnSphere &seed,
				double maximum_seed_distance_degrees,
				const NaturalizeCoastlineGeometry::Parameters &naturalize_parameters);

		/**
		 * Chooses the endpoint of each ridge nearest the seed, naturalizes the
		 * three connecting MORs, and returns only the uncovered local component
		 * containing the seed. It never computes a global spherical complement.
		 *
		 * Equivalent to calling @a compute_void_boundary and subtracting
		 * @a existing_ocean_crust from its @a BoundaryResult::void_polygon - kept as one
		 * call for a caller that has no other geometry to reconcile against the boundary.
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
