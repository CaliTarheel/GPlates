/* $Id$ */

/**
 * \file
 * Deterministic proposal geometry for Worldbuilding Pasta island arcs and
 * active-margin orogenic belts.
 */

#ifndef GPLATES_VIEWOPERATIONS_SUBDUCTIONEFFECTSGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_SUBDUCTIONEFFECTSGEOMETRY_H

#include <vector>

#include <boost/optional.hpp>

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace SubductionEffectsGeometry
	{
		enum EffectType
		{
			ISLAND_ARC,
			ANDEAN_OROGENY,
			LARAMIDE_OROGENY
		};

		struct Parameters
		{
			Parameters();

			EffectType effect_type;
			bool overriding_side_is_left;
			double trim_start_percent;
			double trim_end_percent;
			double trench_to_effect_offset_km;
			double maximum_segment_length_km;
			double island_irregularity;
			double belt_width_km;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double source_length_km;
			double selected_length_km;
			double overriding_containment_fraction;
			double maximum_segment_length_km;
			unsigned int island_count;
			unsigned int proposal_vertex_count;
		};

		struct Result
		{
			Result();

			std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> polygons;
			boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> guide;
			Metrics metrics;
		};

		/**
		 * Returns the fraction of sampled offset points that lie in @a overriding_crust.
		 * This is used to validate (and, in the UI, recommend) subduction polarity.
		 */
		double
		calculate_overriding_containment_fraction(
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &subduction_zone,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &overriding_crust,
				bool overriding_side_is_left,
				double probe_offset_km = 150.0,
				double planet_radius_km = 6371.0088);

		/**
		 * Generates either a notational island-arc line or a continent-contained
		 * orogenic corridor on the overriding side of a directed subduction polyline.
		 */
		Result
		generate(
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &subduction_zone,
				const Parameters &parameters,
				const boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &
						overriding_crust = boost::none);

		/**
		 * Builds narrow mountain corridors only where an arc line crosses a land
		 * polygon. Multiple crossings produce separate editable belts.
		 */
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
		generate_land_intersection_belts(
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &arc_line,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &land,
				double belt_width_km,
				double maximum_segment_length_km = 60.0,
				double planet_radius_km = 6371.0088);
	}
}

#endif // GPLATES_VIEWOPERATIONS_SUBDUCTIONEFFECTSGEOMETRY_H
