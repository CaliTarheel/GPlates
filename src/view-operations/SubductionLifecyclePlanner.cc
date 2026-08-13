/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "SubductionLifecyclePlanner.h"

GPlatesViewOperations::SubductionLifecyclePlanner::Request::Request() :
	event_type(CONTINUE_SUBDUCTION), event_time(0), trench_start_time(0),
	overriding_plate(0), subducting_plate(0), polarity_left(true),
	successor_polarity_left(true), migration_offset_km(0), duration_ma(0), isolates_plate_fragment(false)
{  }

QString GPlatesViewOperations::SubductionLifecyclePlanner::event_name(EventType type)
{
	switch (type)
	{
	case CONTINUE_SUBDUCTION: return "continued subduction";
	case TRENCH_EXTENSION: return "trench extension";
	case ROLLBACK: return "rollback";
	case TRENCH_JUMP: return "trench jump";
	case POLARITY_REVERSAL: return "polarity reversal";
	case PLATE_INVASION: return "plate invasion";
	case FLAT_SLAB: return "flat-slab interval";
	case FINAL_RETIREMENT: return "final retirement";
	}
	return "unknown subduction event";
}

GPlatesViewOperations::SubductionLifecyclePlanner::Plan
GPlatesViewOperations::SubductionLifecyclePlanner::plan(const Request &request)
{
	Plan result;
	result.valid = true;
	result.event_name = event_name(request.event_type);
	result.creates_successor_trench = request.event_type != FINAL_RETIREMENT;
	result.retires_ocean_crust = request.event_type == TRENCH_JUMP ||
			request.event_type == PLATE_INVASION || request.event_type == FINAL_RETIREMENT;
	result.delegate_plate_birth = request.isolates_plate_fragment;

	if (request.event_time < 0 || request.event_time > request.trench_start_time + 1e-9)
		result.errors.append("Event time must be between the trench start and 0 Ma.");
	if (request.overriding_plate == 0 || request.subducting_plate == 0)
		result.errors.append("Both overriding and subducting Plate IDs are required.");
	if (request.overriding_plate == request.subducting_plate)
		result.errors.append("Overriding and subducting Plate IDs must differ.");
	if (request.event_type == POLARITY_REVERSAL &&
			request.polarity_left == request.successor_polarity_left)
		result.errors.append("A polarity reversal must explicitly change Left to Right or Right to Left.");
	if ((request.event_type == TRENCH_EXTENSION || request.event_type == ROLLBACK || request.event_type == TRENCH_JUMP ||
			request.event_type == PLATE_INVASION) && request.migration_offset_km <= 0)
		result.errors.append("Extension, rollback, jump, and invasion require a positive reviewed distance.");
	if (request.event_type == FLAT_SLAB && request.duration_ma <= 0)
		result.errors.append("A flat-slab interval requires a positive reviewed duration.");
	if (request.event_type == CONTINUE_SUBDUCTION && request.migration_offset_km > 0)
		result.warnings.append("The offset is ignored for continued subduction.");

	result.atomic_steps.append(QString("End the selected trench version at %1 Ma without rewriting its older history.")
			.arg(request.event_time, 0, 'g', 12));
	if (result.creates_successor_trench)
	{
		result.atomic_steps.append(QString("Create a successor trench carrying overriding Plate %1, subducting Plate %2, and %3 polarity.")
				.arg(request.overriding_plate).arg(request.subducting_plate)
				.arg(request.successor_polarity_left ? "Left" : "Right"));
		result.atomic_steps.append("Record source/successor Feature IDs and the lifecycle event in the shared event ledger.");
	}
	else
		result.atomic_steps.append("Create no successor trench; mark the selected subduction system retired.");
	if (result.retires_ocean_crust)
		result.atomic_steps.append("Delegate the consumed ocean-crust split/retirement to the existing crust-retirement operation.");
	if (request.event_type == FLAT_SLAB)
		result.atomic_steps.append(QString("Preserve trench geometry and plate identities; version the flat-slab interpretation for %1 Ma and review the effects offset.")
				.arg(request.duration_ma, 0, 'f', 1));
	if (request.migration_offset_km > 0)
		result.atomic_steps.append(QString("Preview the successor extension or displacement of %1 km before any geometry is committed.")
				.arg(request.migration_offset_km, 0, 'f', 1));
	if (result.delegate_plate_birth)
		result.atomic_steps.append("The event isolates a plate fragment: delegate its ID and .rot insertion to the existing Pacific/plate-birth workflow.");
	result.atomic_steps.append("Commit every feature/property/geometry change as one composite undo transaction.");
	result.valid = result.errors.isEmpty();
	return result;
}
