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

#ifndef GPLATES_QTWIDGETS_RECONSTRUCTIONTIMESPINBOX_H
#define GPLATES_QTWIDGETS_RECONSTRUCTIONTIMESPINBOX_H

#include <QDoubleSpinBox>


namespace GPlatesQtWidgets
{
	/** Arrow, keyboard and wheel steps land on the next 10 Ma multiple in their direction.
	 * Typed and programmatically supplied fractional ages remain valid.
	 */
	class ReconstructionTimeSpinBox :
			public QDoubleSpinBox
	{
		Q_OBJECT

	public:
		explicit
		ReconstructionTimeSpinBox(
				QWidget *parent = NULL);

		void
		stepBy(
				int steps) override;

	Q_SIGNALS:
		/** Commit a user step without committing intermediate text edits or moving focus. */
		void
		user_stepped(
				double time);

	protected:
		StepEnabled
		stepEnabled() const override;
	};
}

#endif // GPLATES_QTWIDGETS_RECONSTRUCTIONTIMESPINBOX_H
