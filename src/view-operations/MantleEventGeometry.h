/* $Id$ */

/**
 * \file
 * Deterministic Large Igneous Province placement for the Worldbuilding Pasta
 * LIP and hotspot workflow.
 */

#ifndef GPLATES_VIEWOPERATIONS_MANTLEEVENTGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_MANTLEEVENTGEOMETRY_H

#include <boost/optional.hpp>

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace MantleEventGeometry
	{
		struct Parameters
		{
			Parameters();

			double diameter_km;
			double irregularity;
			double maximum_segment_length_km;
			bool rift_triggered;
			unsigned int random_seed;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double requested_diameter_km;
			double actual_diameter_km;
			double maximum_segment_length_km;
			unsigned int boundary_vertex_count;
			unsigned int containment_reductions;
			bool used_rift;
		};

		struct Result
		{
			Result(
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &lip_outline_,
					const GPlatesMaths::PointOnSphere &hotspot_position_);

			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type lip_outline;
			GPlatesMaths::PointOnSphere hotspot_position;
			Metrics metrics;
		};

		Result generate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &host_continent,
				const boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &rift,
				const Parameters &parameters);
	}
}

#endif // GPLATES_VIEWOPERATIONS_MANTLEEVENTGEOMETRY_H
