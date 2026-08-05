/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#ifndef GPLATES_APP_LOGIC_PROJECTTIMESTAMPSCHEDULE_H
#define GPLATES_APP_LOGIC_PROJECTTIMESTAMPSCHEDULE_H

#include <cstddef>
#include <vector>


namespace GPlatesAppLogic
{
	/**
	 * Builds a bounded, inclusive geological-time schedule for non-UI
	 * world-building operations.
	 */
	class ProjectTimestampSchedule
	{
	public:
		static
		std::vector<double>
		build(
				double youngest_time,
				double oldest_time,
				double step,
				std::size_t maximum_timestamps = 100000);
	};
}

#endif
