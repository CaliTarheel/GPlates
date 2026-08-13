/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_ADVANCEPLATEMOTIONGEOMETRY_H
#define GPLATES_VIEWOPERATIONS_ADVANCEPLATEMOTIONGEOMETRY_H

#include <vector>

#include <boost/optional.hpp>

#include "maths/FiniteRotation.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"


namespace GPlatesViewOperations
{
	namespace AdvancePlateMotionGeometry
	{
		struct Parameters
		{
			Parameters();

			double history_direction_weight;
			double ridge_push_weight;
			double slab_pull_weight;
			double default_speed_cm_per_year;
			double minimum_speed_cm_per_year;
			double maximum_speed_cm_per_year;
			unsigned int motion_path_segments;
			double planet_radius_km;
		};

		struct Proposal
		{
			Proposal(
					const GPlatesMaths::FiniteRotation &motion_rotation_,
					const GPlatesMaths::PointOnSphere &destination_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &motion_path_,
					double speed_cm_per_year_,
					bool used_history_,
					unsigned int ridge_count_,
					unsigned int subduction_count_);

			GPlatesMaths::FiniteRotation motion_rotation;
			GPlatesMaths::PointOnSphere destination;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type motion_path;
			double speed_cm_per_year;
			bool used_history;
			unsigned int ridge_count;
			unsigned int subduction_count;
		};

		Proposal
		generate_motion(
				const GPlatesMaths::PointOnSphere &continent_centre,
				const boost::optional<GPlatesMaths::PointOnSphere> &previous_continent_centre,
				double previous_interval_ma,
				const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &ridges,
				const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> &subduction_zones,
				double interval_ma,
				const Parameters &parameters = Parameters());
	}
}

#endif // GPLATES_VIEWOPERATIONS_ADVANCEPLATEMOTIONGEOMETRY_H
