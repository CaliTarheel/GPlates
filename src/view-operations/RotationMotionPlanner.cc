/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "RotationMotionPlanner.h"
#include <algorithm>
#include <cmath>
#include "app-logic/ReconstructionTree.h"
#include "maths/CalculateVelocity.h"
#include "maths/LatLonPoint.h"

double GPlatesViewOperations::RotationMotionPlanner::angle_degrees(
		const GPlatesMaths::FiniteRotation &rotation)
{
	const double scalar = std::max(-1.0, std::min(1.0, rotation.unit_quat().w().dval()));
	double angle = 2.0 * std::acos(std::fabs(scalar));
	return angle * 180.0 / 3.14159265358979323846;
}

void GPlatesViewOperations::RotationMotionPlanner::decompose_boundary_velocity(
		double north_cm_per_year,
		double east_cm_per_year,
		double boundary_strike_degrees,
		double &normal_cm_per_year,
		double &parallel_cm_per_year)
{
	const double strike_radians = boundary_strike_degrees * 3.14159265358979323846 / 180.0;
	parallel_cm_per_year = north_cm_per_year * std::cos(strike_radians) +
			east_cm_per_year * std::sin(strike_radians);
	normal_cm_per_year = -north_cm_per_year * std::sin(strike_radians) +
			east_cm_per_year * std::cos(strike_radians);
}

GPlatesViewOperations::RotationMotionPlanner::Sample
GPlatesViewOperations::RotationMotionPlanner::analyse(
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		GPlatesModel::integer_plate_id_type moving_plate,
		GPlatesModel::integer_plate_id_type fixed_plate,
		double older_time,
		double younger_time,
		double sample_latitude,
		double sample_longitude,
		double boundary_strike_degrees,
		BoundaryKind boundary_kind,
		double planet_radius_km)
{
	Sample result = { older_time, younger_time, false, false, 0.0, 0.0, 0.0,
			0.0, 0.0, 0.0, 0.0, boundary_kind == NO_BOUNDARY, false, QString() };
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
	const GPlatesMaths::PointOnSphere sample_point = GPlatesMaths::make_point_on_sphere(
			GPlatesMaths::LatLonPoint(sample_latitude, sample_longitude));
	const GPlatesMaths::Vector3D local_velocity = GPlatesMaths::calculate_velocity_vector(
			sample_point, relative_stage, older_time - younger_time, planet_radius_km);
	const GPlatesMaths::VectorColatitudeLongitude local_components =
			GPlatesMaths::convert_vector_from_xyz_to_colat_lon(sample_point, local_velocity);
	const double north = -local_components.get_vector_colatitude().dval();
	const double east = local_components.get_vector_longitude().dval();
	result.local_speed_cm_per_year = local_velocity.magnitude().dval();
	result.motion_azimuth_degrees = result.local_speed_cm_per_year < 1e-12
			? 0.0
			: std::fmod(std::atan2(east, north) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
	decompose_boundary_velocity(
			north, east, boundary_strike_degrees,
			result.boundary_normal_cm_per_year, result.boundary_parallel_cm_per_year);
	result.pole_carry_forward = result.relative_stage_degrees < 1e-8;
	if (result.pole_carry_forward)
	{
		result.diagnostic = QString::fromLatin1(
				"No relative motion: a confirmed pole carry-forward will not move geometry.");
		return result;
	}
	const double normal_fraction = std::fabs(result.boundary_normal_cm_per_year) /
			std::max(1e-12, result.local_speed_cm_per_year);
	const double parallel_fraction = std::fabs(result.boundary_parallel_cm_per_year) /
			std::max(1e-12, result.local_speed_cm_per_year);
	if (boundary_kind == MID_OCEAN_RIDGE)
	{
		result.boundary_alignment_valid = normal_fraction >= 0.8660254038;
		result.diagnostic = result.boundary_alignment_valid
				? QString::fromLatin1("MOR check passed: motion is within 30 degrees of the boundary normal.")
				: QString::fromLatin1("MOR warning: relative motion is not sufficiently normal to the entered ridge strike.");
	}
	else if (boundary_kind == TRANSFORM_FAULT)
	{
		result.boundary_alignment_valid = parallel_fraction >= 0.8660254038;
		result.diagnostic = result.boundary_alignment_valid
				? QString::fromLatin1("Transform check passed: motion is within 30 degrees of the boundary tangent.")
				: QString::fromLatin1("Transform warning: relative motion is not sufficiently parallel to the entered fault strike.");
	}
	else if (boundary_kind == SUBDUCTION_TRENCH)
	{
		result.boundary_alignment_valid = normal_fraction >= 0.5;
		result.diagnostic = result.boundary_alignment_valid
				? QString::fromLatin1("Trench check is normal-dominated; confirm the signed normal component matches the declared polarity and first-contact direction.")
				: QString::fromLatin1("Trench warning: relative motion is mostly trench-parallel, not convergent across the entered strike.");
	}
	else
	{
		result.diagnostic = QString::fromLatin1(
				"Motion sampled; choose a boundary type to apply an alignment gate.");
	}
	return result;
}
