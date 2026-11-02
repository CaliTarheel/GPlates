/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "ReconstructionTimeSpinBox.h"

#include <algorithm>
#include <cmath>


namespace
{
	const double TIME_STEP_MA = 10.0;
}


GPlatesQtWidgets::ReconstructionTimeSpinBox::ReconstructionTimeSpinBox(
		QWidget *parent) :
	QDoubleSpinBox(parent)
{
	setRange(0.0, 9999.0);
	setSingleStep(TIME_STEP_MA);
}


void
GPlatesQtWidgets::ReconstructionTimeSpinBox::stepBy(
		int steps)
{
	if (steps == 0 || isReadOnly())
	{
		return;
	}

	// Interpret pending text first, including when keyboard tracking has been disabled.
	interpretText();
	const double current_time = value();
	const double first_grid_time = TIME_STEP_MA * std::ceil(minimum() / TIME_STEP_MA);
	const double last_grid_time = TIME_STEP_MA * std::floor(maximum() / TIME_STEP_MA);
	if (first_grid_time > last_grid_time)
	{
		return;
	}

	const double grid_index = steps > 0
			? std::floor(current_time / TIME_STEP_MA)
			: std::ceil(current_time / TIME_STEP_MA);
	const double next_time = std::max(first_grid_time,
			std::min(TIME_STEP_MA * (grid_index + steps), last_grid_time));
	// A non-grid boundary must neither become a step destination nor reverse the direction.
	if ((steps > 0 && next_time <= current_time) || (steps < 0 && next_time >= current_time))
	{
		return;
	}

	setValue(next_time);
	Q_EMIT user_stepped(value());
}


QAbstractSpinBox::StepEnabled
GPlatesQtWidgets::ReconstructionTimeSpinBox::stepEnabled() const
{
	if (isReadOnly())
	{
		return StepNone;
	}
	// Keyboard handling checks this before calling stepBy. Pending valid text can have
	// moved away from a boundary even when keyboard tracking has not updated value().
	const double current_time = hasAcceptableInput() ? valueFromText(text()) : value();
	StepEnabled enabled = StepNone;
	if (TIME_STEP_MA * (std::floor(current_time / TIME_STEP_MA) + 1) <= maximum())
	{
		enabled |= StepUpEnabled;
	}
	if (TIME_STEP_MA * (std::ceil(current_time / TIME_STEP_MA) - 1) >= minimum())
	{
		enabled |= StepDownEnabled;
	}
	return enabled;
}
