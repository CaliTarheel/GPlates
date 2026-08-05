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
		struct Sample
		{
			double older_time;
			double younger_time;
			bool moving_circuit_valid;
			bool fixed_circuit_valid;
			double absolute_stage_degrees;
			double relative_stage_degrees;
			double relative_rate_degrees_per_ma;
			bool pole_carry_forward;
			QString diagnostic;
		};

		static Sample analyse(
				const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
				GPlatesModel::integer_plate_id_type moving_plate,
				GPlatesModel::integer_plate_id_type fixed_plate,
				double older_time,
				double younger_time);

		static double angle_degrees(const GPlatesMaths::FiniteRotation &rotation);
	};
}
#endif
