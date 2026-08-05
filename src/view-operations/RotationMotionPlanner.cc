/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "RotationMotionPlanner.h"
#include <algorithm>
#include <cmath>
#include "app-logic/ReconstructionTree.h"

double GPlatesViewOperations::RotationMotionPlanner::angle_degrees(
		const GPlatesMaths::FiniteRotation &rotation)
{
	const double scalar = std::max(-1.0, std::min(1.0, rotation.unit_quat().w().dval()));
	double angle = 2.0 * std::acos(std::fabs(scalar));
	return angle * 180.0 / 3.14159265358979323846;
}

GPlatesViewOperations::RotationMotionPlanner::Sample
GPlatesViewOperations::RotationMotionPlanner::analyse(
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		GPlatesModel::integer_plate_id_type moving_plate,
		GPlatesModel::integer_plate_id_type fixed_plate,
		double older_time,
		double younger_time)
{
	Sample result = { older_time, younger_time, false, false, 0.0, 0.0, 0.0, false, QString() };
	if (older_time <= younger_time)
	{
		result.diagnostic = QString::fromLatin1("Older time must be greater than younger time.");
		return result;
	}
	const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
			tree_creator.get_reconstruction_tree(older_time);
	const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type younger_tree =
			tree_creator.get_reconstruction_tree(younger_time);
	const boost::optional<GPlatesMaths::FiniteRotation> moving_older =
			older_tree->get_composed_absolute_rotation_or_none(moving_plate);
	const boost::optional<GPlatesMaths::FiniteRotation> moving_younger =
			younger_tree->get_composed_absolute_rotation_or_none(moving_plate);
	const boost::optional<GPlatesMaths::FiniteRotation> fixed_older =
			older_tree->get_composed_absolute_rotation_or_none(fixed_plate);
	const boost::optional<GPlatesMaths::FiniteRotation> fixed_younger =
			younger_tree->get_composed_absolute_rotation_or_none(fixed_plate);
	result.moving_circuit_valid = moving_older && moving_younger;
	result.fixed_circuit_valid = fixed_older && fixed_younger;
	if (!result.moving_circuit_valid || !result.fixed_circuit_valid)
	{
		result.diagnostic = QString::fromLatin1("Incomplete plate circuit at one or both interval endpoints.");
		return result;
	}

	const GPlatesMaths::FiniteRotation absolute_stage = GPlatesMaths::compose(
			*moving_younger, GPlatesMaths::get_reverse(*moving_older));
	const GPlatesMaths::FiniteRotation relative_older = GPlatesMaths::compose(
			GPlatesMaths::get_reverse(*fixed_older), *moving_older);
	const GPlatesMaths::FiniteRotation relative_younger = GPlatesMaths::compose(
			GPlatesMaths::get_reverse(*fixed_younger), *moving_younger);
	const GPlatesMaths::FiniteRotation relative_stage = GPlatesMaths::compose(
			relative_younger, GPlatesMaths::get_reverse(relative_older));
	result.absolute_stage_degrees = angle_degrees(absolute_stage);
	result.relative_stage_degrees = angle_degrees(relative_stage);
	result.relative_rate_degrees_per_ma = result.relative_stage_degrees / (older_time - younger_time);
	result.pole_carry_forward = result.relative_stage_degrees < 1e-8;
	result.diagnostic = result.pole_carry_forward
			? QString::fromLatin1("No relative motion: a confirmed pole carry-forward will not move geometry.")
			: QString::fromLatin1("Motion present: review MOR normal, transform tangency, trench convergence and first contact before editing.");
	return result;
}
