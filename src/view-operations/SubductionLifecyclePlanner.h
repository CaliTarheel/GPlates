/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_SUBDUCTIONLIFECYCLEPLANNER_H
#define GPLATES_VIEWOPERATIONS_SUBDUCTIONLIFECYCLEPLANNER_H

#include <vector>
#include <QString>
#include <QStringList>
#include "model/types.h"

namespace GPlatesViewOperations
{
	/** Builds a reviewable, non-mutating trench lifecycle transaction plan. */
	class SubductionLifecyclePlanner
	{
	public:
		enum EventType
		{
			CONTINUE_SUBDUCTION, TRENCH_EXTENSION, ROLLBACK, TRENCH_JUMP,
			POLARITY_REVERSAL, PLATE_INVASION, FLAT_SLAB, FINAL_RETIREMENT
		};
		struct Request
		{
			Request();
			EventType event_type;
			double event_time;
			double trench_start_time;
			GPlatesModel::integer_plate_id_type overriding_plate;
			GPlatesModel::integer_plate_id_type subducting_plate;
			bool polarity_left;
			bool successor_polarity_left;
			double migration_offset_km;
			double duration_ma;
			bool isolates_plate_fragment;
		};
		struct Plan
		{
			bool valid;
			QString event_name;
			QStringList errors;
			QStringList warnings;
			QStringList atomic_steps;
			bool creates_successor_trench;
			bool retires_ocean_crust;
			bool delegate_plate_birth;
		};
		static Plan plan(const Request &request);
		static QString event_name(EventType event_type);
	};
}
#endif
