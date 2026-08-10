/* $Id$ */

/**
 * \file
 * Persistent window driving the Split Plate workflow.
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

#ifndef GPLATES_QTWIDGETS_SPLITPLATEDIALOG_H
#define GPLATES_QTWIDGETS_SPLITPLATEDIALOG_H

#include <boost/scoped_ptr.hpp>
#include <QLabel>
#include <QPushButton>

#include "GPlatesDialog.h"

#include "view-operations/RenderedGeometryCollection.h"
#include "view-operations/SplitPlateOperation.h"


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
	 * Same picker-dialog pattern as BooleanPolygonsDialog: pick the polygon, pick the
	 * cutting polyline, then commit as an explicit third step - rather than the old
	 * focus-then-invoke-a-menu-item-twice flow, which relied on the user remembering
	 * what state a stateless-looking menu item was in.
	 */
	class SplitPlateDialog :
			public GPlatesDialog
	{
		Q_OBJECT

	public:

		SplitPlateDialog(
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
		handle_select_polygon();

		void
		handle_clear_polygons();

		void
		handle_select_polyline();

		void
		handle_commit();

		/**
		 * The globe told us the focused feature changed. If a selection is armed, this is
		 * the user's click arriving, so capture it.
		 */
		void
		handle_focus_changed();

	private:

		void
		update_display(
				const QString &message = QString());

		/**
		 * Redraws the captured polygon(s) (amber) and cutting polyline (blue) so the current
		 * selection is always visible on the globe, independent of feature focus.
		 */
		void
		update_highlight();

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesGui::CanvasToolWorkflows &d_canvas_tool_workflows;
		boost::scoped_ptr<GPlatesViewOperations::SplitPlateOperation> d_operation;
		GPlatesViewOperations::RenderedGeometryCollection::child_layer_owner_ptr_type d_highlight_layer_ptr;

		QLabel *d_instruction_label;
		QLabel *d_polygon_label;
		QLabel *d_polyline_label;
		QLabel *d_message_label;
		QPushButton *d_select_polygon_button;
		QPushButton *d_clear_polygons_button;
		QPushButton *d_select_polyline_button;
		QPushButton *d_commit_button;
	};
}

#endif  // GPLATES_QTWIDGETS_SPLITPLATEDIALOG_H
