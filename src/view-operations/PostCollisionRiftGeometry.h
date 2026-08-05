/* $Id$ */

/**
 * \file
 * Deterministic spherical post-collision rifts biased toward, but not coincident
 * with, an inherited suture.
 */

#ifndef GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTGEOMETRY_H

#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace PostCollisionRiftGeometry
	{
		struct Parameters
		{
			Parameters();

			double offset_km;
			double wiggle;
			double maximum_segment_length_km;
			double end_extension_km;
			int side;
			unsigned int random_seed;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double path_length_km;
			double mean_suture_offset_km;
			double minimum_suture_offset_km;
			double maximum_segment_length_km;
			unsigned int vertex_count;
		};

		struct Result
		{
			explicit Result(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &rift_);

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type rift;
			Metrics metrics;
		};

		Result generate(
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &suture,
				const Parameters &parameters);
	}
}

#endif // GPLATES_VIEWOPERATIONS_POSTCOLLISIONRIFTGEOMETRY_H
