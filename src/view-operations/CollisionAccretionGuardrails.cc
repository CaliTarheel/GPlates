/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "CollisionAccretionGuardrails.h"
#include <cmath>

GPlatesViewOperations::CollisionAccretionGuardrails::Request::Request() :
	mode(COLLISION), event_time(0), project_schedule_available(false),
	event_is_project_timestamp(false), incoming_plate(0), receiving_plate(0),
	survivor(KEEP_BOTH), boolean_preview_reviewed(false), boolean_output_count(0),
	craton_geometry_protected(false), lineage_will_be_recorded(false), no_rotation_jump(false)
{  }

QString GPlatesViewOperations::CollisionAccretionGuardrails::mode_name(Mode mode)
{
	switch (mode) { case COLLISION: return "collision"; case ACCRETION: return "accretion"; case RERIFT: return "rerift"; }
	return "unknown";
}

bool GPlatesViewOperations::CollisionAccretionGuardrails::is_project_timestamp(
		double time, const std::vector<double> &timestamps)
{
	for (std::vector<double>::const_iterator timestamp = timestamps.begin(); timestamp != timestamps.end(); ++timestamp)
		if (std::fabs(time - *timestamp) <= 1e-9)
			return true;
	return false;
}

GPlatesViewOperations::CollisionAccretionGuardrails::Report
GPlatesViewOperations::CollisionAccretionGuardrails::validate(const Request &request)
{
	Report report;
	if (request.event_time < 0)
		report.errors.append("Event time cannot be younger than 0 Ma.");
	if (request.project_schedule_available && !request.event_is_project_timestamp)
		report.warnings.append(
				"Event time is not a required Project Timestamp. It is permitted only as a local tectonic keyframe and does not alter the authoritative schedule.");
	if (request.incoming_plate == 0 || request.receiving_plate == 0)
		report.errors.append("Both source Plate IDs must be explicit.");
	if (request.incoming_plate == request.receiving_plate && request.survivor != KEEP_BOTH)
		report.errors.append("A single Plate ID cannot be retired into itself.");
	if (!request.boolean_preview_reviewed || request.boolean_output_count == 0)
		report.errors.append("A reviewed Boolean/spherical split result is required before commit.");
	if (request.mode == RERIFT && request.boolean_output_count != 2)
		report.errors.append("Rerifting must produce exactly two reviewed child crust polygons.");
	if (!request.craton_geometry_protected)
		report.errors.append("Craton geometry protection has not been confirmed.");
	if (!request.lineage_will_be_recorded)
		report.errors.append("Source/successor Feature ID lineage must be recorded.");
	if (!request.no_rotation_jump)
		report.errors.append("The successor rotation must equal the source absolute rotation at event time.");
	if (request.survivor == KEEP_BOTH)
		report.confirmations.append("Both source Plate IDs remain active; no survivor is inferred.");
	else if (request.survivor == RECEIVING_SURVIVES)
		report.confirmations.append(QString("Receiving Plate %1 is the explicitly selected survivor.").arg(request.receiving_plate));
	else if (request.survivor == INCOMING_SURVIVES)
		report.confirmations.append(QString("Incoming Plate %1 is the explicitly selected survivor.").arg(request.incoming_plate));
	else
		report.confirmations.append("Child Plate IDs were explicitly supplied; the source-ID child is not inferred.");
	report.confirmations.append("Craton polygons remain geometrically unchanged across the event slice.");
	report.confirmations.append("The reviewed Boolean result, lineage updates and rotation samples commit atomically.");
	report.valid = report.errors.isEmpty();
	return report;
}
