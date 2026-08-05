/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include "ProjectTimestampSchedule.h"

#include <cmath>
#include <stdexcept>


std::vector<double>
GPlatesAppLogic::ProjectTimestampSchedule::build(
		double youngest_time,
		double oldest_time,
		double step,
		std::size_t maximum_timestamps)
{
	if (!std::isfinite(youngest_time) || !std::isfinite(oldest_time) ||
			!std::isfinite(step))
	{
		throw std::invalid_argument("Timestamp bounds and step must be finite.");
	}
	if (youngest_time < 0.0 || oldest_time < youngest_time || step <= 0.0)
	{
		throw std::invalid_argument(
				"Timestamp schedule requires 0 <= youngest <= oldest and step > 0.");
	}
	if (maximum_timestamps == 0)
	{
		throw std::invalid_argument("Timestamp schedule capacity must be positive.");
	}

	const double span = oldest_time - youngest_time;
	const double regular_intervals_value = std::floor(span / step + 1e-12);
	if (regular_intervals_value + 1.0 > static_cast<double>(maximum_timestamps))
	{
		throw std::length_error("Timestamp schedule exceeds its safety limit.");
	}
	const std::size_t regular_intervals =
			static_cast<std::size_t>(regular_intervals_value);
	const bool needs_oldest_endpoint =
			std::fabs(youngest_time + regular_intervals * step - oldest_time) > 1e-9;
	const std::size_t timestamp_count = regular_intervals + 1 +
			(needs_oldest_endpoint ? 1 : 0);
	if (timestamp_count > maximum_timestamps)
	{
		throw std::length_error("Timestamp schedule exceeds its safety limit.");
	}

	std::vector<double> timestamps;
	timestamps.reserve(timestamp_count);
	for (std::size_t index = 0; index <= regular_intervals; ++index)
	{
		timestamps.push_back(youngest_time + index * step);
	}
	if (needs_oldest_endpoint)
	{
		timestamps.push_back(oldest_time);
	}
	else
	{
		// Eliminate accumulated floating-point drift at the inclusive endpoint.
		timestamps.back() = oldest_time;
	}
	return timestamps;
}
