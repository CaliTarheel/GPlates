/* $Id$ */

/**
 * \file
 * Deterministic spherical geometry generation for the Worldbuilding Pasta
 * "Create Initial Continent" operation.
 */

#ifndef GPLATES_VIEWOPERATIONS_INITIALCONTINENTGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_INITIALCONTINENTGEOMETRY_H

#include <vector>

#include <boost/cstdint.hpp>

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"


namespace GPlatesViewOperations
{
	namespace InitialContinentGeometry
	{
		struct Parameters
		{
			Parameters();

			unsigned int craton_count;
			// In the range [0,1]. Higher values create a larger craton-area
			// fraction and reduce the space between neighbouring cratons.
			double packing;
			// Fraction of the complete spherical surface, in the range [0.05,0.45].
			double continent_area_fraction;
			// In the range [0,1]. Controls the amplitude of medium- and
			// high-frequency coastline variation.
			double coastline_swiggle;
			boost::uint32_t random_seed;
			double maximum_segment_length_km;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double continent_area_fraction;
			double total_craton_area_fraction;
			double craton_fill_fraction;
			double continent_perimeter_km;
			double average_continent_segment_length_km;
			double maximum_continent_segment_length_km;
			double average_craton_segment_length_km;
			double maximum_segment_length_km;
			unsigned int continent_vertex_count;
			unsigned int craton_vertex_count;
		};

		struct Result
		{
			Result(
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent_,
					const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons_,
					const GPlatesMaths::PointOnSphere &centre_,
					const Metrics &metrics_);

			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent;
			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> cratons;
			GPlatesMaths::PointOnSphere centre;
			Metrics metrics;
		};

		/**
		 * Generate one continent and a Worldbuilding Pasta-sized set of cratons.
		 * The same parameters and seed always produce the same geometry.
		 *
		 * Throws std::invalid_argument for invalid parameters and std::runtime_error
		 * if a valid packing cannot be produced.
		 */
		Result
		generate(
				const Parameters &parameters);
	}
}

#endif // GPLATES_VIEWOPERATIONS_INITIALCONTINENTGEOMETRY_H
