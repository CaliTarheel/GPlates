/* $Id$ */

/**
 * \file
 * Spherical geometry helpers for the World Building "Naturalize Coastline"
 * operation.
 */

#ifndef GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEGEOMETRY_H

#include <vector>
#include <boost/cstdint.hpp>

#include "maths/PointOnSphere.h"


namespace GPlatesViewOperations
{
	namespace NaturalizeCoastlineGeometry
	{
		typedef std::vector<GPlatesMaths::PointOnSphere> point_seq_type;

		struct Parameters
		{
			Parameters();

			double maximum_segment_length_km;
			// Percentage of the maximum segment length, with the operation's 10x
			// swiggle-power scale applied to make low percentage values visible.
			double amplitude_percent;
			double wavelength_km;
			unsigned int smoothing_passes;
			boost::uint32_t random_seed;
			double planet_radius_km;
			double coincidence_tolerance_km;
		};

		struct SegmentResult
		{
			SegmentResult();

			point_seq_type points;
			unsigned int inserted_point_count;
			double maximum_segment_length_km;
		};

		/**
		 * Subdivide and perturb a great-circle segment. The result is independent
		 * of input direction: reversing the endpoints reverses the same points.
		 */
		SegmentResult
		naturalize_segment(
				const GPlatesMaths::PointOnSphere &start,
				const GPlatesMaths::PointOnSphere &end,
				const Parameters &parameters);

		double
		maximum_segment_length_km(
				const point_seq_type &points,
				bool closed,
				double planet_radius_km);

		bool
		points_are_close(
				const GPlatesMaths::PointOnSphere &point1,
				const GPlatesMaths::PointOnSphere &point2,
				double tolerance_km,
				double planet_radius_km);

		bool
		segments_match(
				const GPlatesMaths::PointOnSphere &start1,
				const GPlatesMaths::PointOnSphere &end1,
				const GPlatesMaths::PointOnSphere &start2,
				const GPlatesMaths::PointOnSphere &end2,
				double tolerance_km,
				double planet_radius_km,
				bool &reversed);

		/**
		 * True when two collinear arcs overlap by more than the tolerance, but
		 * their endpoint pairs do not identify the same complete segment.
		 */
		bool
		segments_partially_overlap(
				const GPlatesMaths::PointOnSphere &start1,
				const GPlatesMaths::PointOnSphere &end1,
				const GPlatesMaths::PointOnSphere &start2,
				const GPlatesMaths::PointOnSphere &end2,
				double tolerance_km,
				double planet_radius_km);
	}
}

#endif // GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEGEOMETRY_H
