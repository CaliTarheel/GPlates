/* $Id$ */

/**
 * \file
 * Persistent window driving the polygon Boolean workflow.
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

#ifndef GPLATES_QTWIDGETS_BOOLEANPOLYGONSDIALOG_H
#define GPLATES_QTWIDGETS_BOOLEANPOLYGONSDIALOG_H

#include <boost/scoped_ptr.hpp>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include "GPlatesDialog.h"

#include "view-operations/BooleanPolygonOperation.h"
#include "view-operations/RenderedGeometryCollection.h"


namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesGui
{
	class CanvasToolWorkflows;
	class FeatureFocus;
}

namespace GPlatesQtWidgets
{
	/**
	 * Combines polygons with a Boolean operation - union, difference, intersection or symmetric
	 * difference.
	 *
	 * This window stays open for the whole operation, deliberately. The work involves clicking
	 * features on the globe, which a modal dialog would prevent, and it is multi-step, so the
	 * user needs somewhere to look that says what has been captured and what to do next. An
	 * instruction delivered by a prompt that then disappears has to be held in the user's head
	 * while they do the fiddly part, with nothing confirming whether a click registered.
	 */
	class BooleanPolygonsDialog :
			public GPlatesDialog
	{
		Q_OBJECT

	public:

		BooleanPolygonsDialog(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
				GPlatesViewOperations::RenderedGeometryCollection &rendered_geometry_collection,
				QWidget *parent_ = NULL);

		/**
		 * Re-arm from the beginning and show the window.
		 */
		void
		pop_up();

	protected:

		void
		closeEvent(
				QCloseEvent *event_) override;

	private Q_SLOTS:

		void
		handle_select_first();

		void
		handle_add_polygon();

		void
		handle_clear();

		void
		handle_apply();

		void
		handle_unify();

		/**
		 * The globe told us the focused feature changed. If a selection is armed, this is the
		 * user's click arriving, so capture it.
		 */
		void
		handle_focus_changed();

	private:

		void
		update_display(
				const QString &message = QString());

		/**
		 * True if the first polygon or any captured operand has an interior ring (hole).
		 * The Boolean clipping engine's tessellate-then-2D-clip pipeline is not guaranteed to
		 * preserve holes correctly, so this drives a one-click warning before Apply/Unify
		 * actually runs - see @a d_hole_warning_acknowledged.
		 */
		bool
		selection_has_holes() const;

		/**
		 * Redraws the first polygon (amber) and operand polygons (blue) so the current
		 * selection is always visible on the globe, independent of feature focus - which
		 * moves on with every subsequent click and would otherwise strip the highlight from
		 * whatever was captured before.
		 */
		void
		update_highlight();

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesGui::CanvasToolWorkflows &d_canvas_tool_workflows;
		boost::scoped_ptr<GPlatesViewOperations::BooleanPolygonOperation> d_operation;
		GPlatesViewOperations::RenderedGeometryCollection::child_layer_owner_ptr_type d_highlight_layer_ptr;

		QLabel *d_instruction_label;
		QLabel *d_first_label;
		QLabel *d_operands_label;
		QLabel *d_message_label;
		QComboBox *d_operation_combo;
		QPushButton *d_select_first_button;
		QPushButton *d_add_polygon_button;
		QPushButton *d_clear_button;
		QPushButton *d_unify_button;
		QPushButton *d_apply_button;

		/**
		 * Set when a hole warning has just been shown instead of applying, so the next click
		 * of the same button proceeds anyway rather than warning forever. Cleared whenever the
		 * selection changes, so a stale acknowledgement never silently waves through a
		 * different, unreviewed selection.
		 */
		bool d_hole_warning_acknowledged;
	};
}

#endif  // GPLATES_QTWIDGETS_BOOLEANPOLYGONSDIALOG_H
