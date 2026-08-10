/* $Id$ */

/**
 * \file
 * Persistent window driving the Ocean Crust Creation tool family (MOR fill,
 * RRR triple-junction, Pacific-style plate birth).
 *
 * Copyright (C) 2026 The GPlates development team.
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

#ifndef GPLATES_QTWIDGETS_OCEANCRUSTCREATIONDIALOG_H
#define GPLATES_QTWIDGETS_OCEANCRUSTCREATIONDIALOG_H

#include <QLabel>
#include <QPushButton>

#include "ChooseFeatureCollectionWidget.h"
#include "GPlatesDialog.h"


namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	class CreateOceanCrustOperation;
	class CreatePacificPlateOperation;
	class CreateTripleJunctionCrustOperation;
}

namespace GPlatesGui
{
	class CanvasToolWorkflows;
}

namespace GPlatesQtWidgets
{
	/**
	 * All three Ocean Crust Creation tools share one Shift-click MOR selection
	 * (@a CreateOceanCrustOperation::selected_mors), so this window shows that shared state
	 * live and offers all three actions from one place, rather than each tool popping its own
	 * "Shift-click a MOR" message box and vanishing - which leaves the user re-doing the
	 * selection with no on-screen record of what they had already picked.
	 *
	 * The window itself never blocks: selection happens by Shift-clicking (MOR) or an ordinary
	 * click (Pacific-Style Plate's void seed) directly on the globe/map, which works whether or
	 * not this window has focus. The window's job is only to keep the current state visible and
	 * offer buttons for whichever action the current selection supports.
	 */
	class OceanCrustCreationDialog :
			public GPlatesDialog
	{
		Q_OBJECT

	public:

		OceanCrustCreationDialog(
				GPlatesViewOperations::CreateOceanCrustOperation &mor_operation,
				GPlatesViewOperations::CreateTripleJunctionCrustOperation &triple_junction_operation,
				GPlatesViewOperations::CreatePacificPlateOperation &pacific_plate_operation,
				GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
				GPlatesAppLogic::ApplicationState &application_state,
				QWidget *parent_ = NULL);

		/**
		 * Show the window (creating it, if this is the first call, is the caller's job).
		 * Safe to call repeatedly - just raises and refreshes an already-open window.
		 */
		void
		pop_up();

		/**
		 * Refreshes the live status display. Called by @a ViewportWindow after every
		 * Shift-click MOR selection change and every Pacific-Style Plate seed capture, since
		 * those happen via direct calls from the canvas tools rather than a Qt signal this
		 * window could listen for on its own.
		 */
		void
		refresh_display();

	private Q_SLOTS:

		void
		handle_generate_mor_crust();

		void
		handle_generate_triple_junction_crust();

		void
		handle_create_pacific_plate();

		void
		handle_clear_selection();

	private:

		/**
		 * Reads the MOR-destination widget (if the user made a selection there) into the
		 * Pacific-Style Plate operation, so it does not have to ask again in its own dialog.
		 * Called right before every Pacific-Style Plate trigger, since there is no cheap way to
		 * be notified the moment the widget's selection changes.
		 */
		void
		apply_mor_destination_to_operation();

		GPlatesViewOperations::CreateOceanCrustOperation &d_mor_operation;
		GPlatesViewOperations::CreateTripleJunctionCrustOperation &d_triple_junction_operation;
		GPlatesViewOperations::CreatePacificPlateOperation &d_pacific_plate_operation;
		GPlatesGui::CanvasToolWorkflows &d_canvas_tool_workflows;

		ChooseFeatureCollectionWidget *d_mor_destination_widget;
		QLabel *d_instruction_label;
		QLabel *d_selection_label;
		QLabel *d_message_label;
		QPushButton *d_generate_mor_crust_button;
		QPushButton *d_generate_triple_junction_button;
		QPushButton *d_create_pacific_plate_button;
		QPushButton *d_clear_selection_button;
	};
}

#endif  // GPLATES_QTWIDGETS_OCEANCRUSTCREATIONDIALOG_H
