/* $Id$ */

/**
 * \file
 * Deterministic spherical contact, suture and collisional-orogen proposals.
 */

#ifndef GPLATES_VIEWOPERATIONS_COLLISIONGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_COLLISIONGEOMETRY_H

#include <boost/optional.hpp>

#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace CollisionGeometry
	{
		enum CollisionType
		{
			ARC_OR_TERRANE_ACCRETION,
			URAL_OROGENY,
			HIMALAYAN_OROGENY
		};

		struct Parameters
		{
			Parameters();

			CollisionType collision_type;
			double contact_threshold_km;
			double belt_width_km;
			double maximum_segment_length_km;
			unsigned int smoothing_iterations;
			double belt_irregularity;
			double deformation_reach_km;
			double planet_radius_km;
		};

		struct Metrics
		{
			Metrics();

			double minimum_gap_km;
			double contact_length_km;
			double incoming_to_receiving_area_ratio;
			double incoming_overlap_fraction;
			double receiving_overlap_fraction;
			double maximum_suture_segment_km;
			double maximum_belt_segment_km;
			double post_deformation_gap_km;
			unsigned int contact_sample_count;
		};

		struct Result
		{
			Result(
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &suture_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &orogenic_belt_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &deformed_incoming_,
					const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &deformed_receiving_,
					const GPlatesMaths::PointOnSphere &incoming_contact_,
					const GPlatesMaths::PointOnSphere &receiving_contact_);

			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type suture;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type orogenic_belt;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type deformed_incoming;
			GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type deformed_receiving;
			GPlatesMaths::PointOnSphere incoming_contact;
			GPlatesMaths::PointOnSphere receiving_contact;
			Metrics metrics;
		};

		CollisionType
		recommend_collision_type(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &incoming,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &receiving,
				const boost::optional<double> &closing_speed_cm_per_year,
				unsigned int precursor_collision_count);

		double
		default_belt_width_km(
				CollisionType collision_type);

		Result
		generate(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &incoming,
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &receiving,
				const Parameters &parameters);
	}
}

#endif // GPLATES_VIEWOPERATIONS_COLLISIONGEOMETRY_H
