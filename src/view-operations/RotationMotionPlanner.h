/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_ROTATIONMOTIONPLANNER_H
#define GPLATES_VIEWOPERATIONS_ROTATIONMOTIONPLANNER_H

#include <QString>
#include "app-logic/ReconstructionTreeCreator.h"
#include "maths/FiniteRotation.h"
#include "model/types.h"

namespace GPlatesViewOperations
{
	class RotationMotionPlanner
	{
	public:
		enum BoundaryKind
		{
			NO_BOUNDARY,
			MID_OCEAN_RIDGE,
			TRANSFORM_FAULT,
			SUBDUCTION_TRENCH
		};

		struct Sample
		{
			double older_time;
			double younger_time;
			bool moving_circuit_valid;
			bool fixed_circuit_valid;
			double absolute_stage_degrees;
			double relative_stage_degrees;
			double relative_rate_degrees_per_ma;
			double local_speed_cm_per_year;
			double boundary_normal_cm_per_year;
			double boundary_parallel_cm_per_year;
			double motion_azimuth_degrees;
			bool boundary_alignment_valid;
			bool pole_carry_forward;
			QString diagnostic;
		};

		static Sample analyse(
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				GPlatesModel::integer_plate_id_type moving_plate,
				GPlatesModel::integer_plate_id_type fixed_plate,
				double older_time,
				double younger_time,
				double sample_latitude,
				double sample_longitude,
				double boundary_strike_degrees,
				BoundaryKind boundary_kind,
				double planet_radius_km);

		static double angle_degrees(const GPlatesMaths::FiniteRotation &rotation);

		static void decompose_boundary_velocity(
				double north_cm_per_year,
				double east_cm_per_year,
				double boundary_strike_degrees,
				double &normal_cm_per_year,
				double &parallel_cm_per_year);
	};
}
#endif
