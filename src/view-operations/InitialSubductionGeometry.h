/* $Id$ */

/**
 * \file
 * Builds a broad, smoothed subduction-zone trace on the continental margin
 * opposite a newly created mid-ocean ridge.
 */

#ifndef GPLATES_VIEWOPERATIONS_INITIALSUBDUCTIONGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_INITIALSUBDUCTIONGEOMETRY_H

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace InitialSubductionGeometry
	{
		struct Parameters
		{
			Parameters();

			unsigned int anchor_count;
			unsigned int smoothing_passes;
			unsigned int maximum_clearance_adjustment_iterations;
			double width_coverage_fraction;
			double offshore_offset_km;
			double minimum_continental_clearance_km;
			double clearance_adjustment_step_km;
			double endpoint_buffer_km;
			double broad_arc_bow_km;
			double maximum_segment_length_km;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double continent_motion_normal_width_km;
			double trench_motion_normal_span_km;
			double trench_length_km;
			double maximum_segment_length_km;
			double minimum_continental_clearance_km;
			double endpoint_buffer_km;
			unsigned int clearance_adjustment_iterations;
		};

		struct Result
		{
			Result(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &trench_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &motion_arrow_,
					const Metrics &metrics_);

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type trench;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type motion_arrow;
			Metrics metrics;
		};

		/**
		 * Infers the continent's spreading direction from the ridge-to-continent
		 * vector, samples the far-side support envelope across the continent's
		 * motion-normal width, and converts that envelope to a broad offshore arc.
		 */
		Result
		generate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &mid_ocean_ridge,
				const Parameters &parameters = Parameters());
	}
}

#endif // GPLATES_VIEWOPERATIONS_INITIALSUBDUCTIONGEOMETRY_H
