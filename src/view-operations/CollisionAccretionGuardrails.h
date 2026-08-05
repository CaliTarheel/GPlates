/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_COLLISIONACCRETIONGUARDRAILS_H
#define GPLATES_VIEWOPERATIONS_COLLISIONACCRETIONGUARDRAILS_H

#include <QString>
#include <QStringList>
#include <vector>
#include "model/types.h"

namespace GPlatesViewOperations
{
	/** Shared preflight for collision, accretion and inherited-suture rerifting. */
	class CollisionAccretionGuardrails
	{
	public:
		enum Mode { COLLISION, ACCRETION, RERIFT };
		enum Survivor { KEEP_BOTH, INCOMING_SURVIVES, RECEIVING_SURVIVES, EXPLICIT_CHILD_IDS };
		struct Request
		{
			Request();
			Mode mode;
			double event_time;
			bool project_schedule_available;
			bool event_is_project_timestamp;
			GPlatesModel::integer_plate_id_type incoming_plate;
			GPlatesModel::integer_plate_id_type receiving_plate;
			Survivor survivor;
			bool boolean_preview_reviewed;
			unsigned int boolean_output_count;
			bool craton_geometry_protected;
			bool lineage_will_be_recorded;
			bool no_rotation_jump;
		};
		struct Report
		{
			bool valid;
			QStringList errors;
			QStringList confirmations;
		};
		static Report validate(const Request &request);
		static bool is_project_timestamp(double time, const std::vector<double> &timestamps);
		static QString mode_name(Mode mode);
	};
}
#endif
