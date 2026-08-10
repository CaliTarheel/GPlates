/* $Id$ */

/**
 * \file Displays lat/lon points of geometry being modified by a canvas tool.
 * 
 * $Revision$
 * $Date$
 * 
 * Copyright (C) 2008, 2009, 2011 The University of Sydney, Australia
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

#ifndef GPLATES_QTWIDGETS_MODIFYGEOMETRYWIDGET_H
#define GPLATES_QTWIDGETS_MODIFYGEOMETRYWIDGET_H

#include <QDebug>
#include <QShortcut>
#include <QWidget>
#include <QTreeWidget>
#include <boost/scoped_ptr.hpp>

#include "ui_ModifyGeometryWidgetUi.h"
#include "LatLonCoordinatesTable.h"
#include "TaskPanelWidget.h"

#include "maths/GeometryOnSphere.h"


namespace GPlatesCanvasTools
{
	class GeometryOperationState;
	class ModifyGeometryState;
}

namespace GPlatesViewOperations
{
	class GeometryBuilder;
}

namespace GPlatesQtWidgets
{
	class LatLonCoordinatesTable;

	class ModifyGeometryWidget :
			public TaskPanelWidget, 
			protected Ui_ModifyGeometryWidget
	{
		Q_OBJECT

	public:

		explicit
		ModifyGeometryWidget(
				GPlatesCanvasTools::GeometryOperationState &geometry_operation_state,
				GPlatesCanvasTools::ModifyGeometryState &modify_geometry_state,
				QWidget *parent_ = NULL);

		~ModifyGeometryWidget();

		void
		reload_coordinates_table_if_necessary()
		{
			d_lat_lon_coordinates_table->reload_if_necessary();
		}

		virtual
		void
		handle_activation();

	private Q_SLOTS:
		void
		handle_delete_selected_vertices();

		void
		handle_average_selected_vertex_positions();

		void
		handle_cluster_selected_vertices();

		void
		handle_snap_selected_vertices_to_plate();

		void
		handle_vertex_selection_state_changed(
				unsigned int selected_vertex_count,
				bool can_delete_selection);

	private:

		QTreeWidget *
		coordinates_table()
		{
			return treewidget_coordinates;
		}

		/**
		 * A wrapper around coordinates table that listens to a GeometryBuilder
		 * and fills in the table accordingly.
		 */
		boost::scoped_ptr<LatLonCoordinatesTable> d_lat_lon_coordinates_table;

		GPlatesCanvasTools::ModifyGeometryState &d_modify_geometry_state;

		/**
		 * Lets Backspace trigger "Delete Selected" without having to click into this panel -
		 * lassoing on the globe leaves the globe canvas focused, not this dock, and X/Del are
		 * already claimed by the separate Delete Vertex tool and Delete Feature respectively.
		 * Safe to fire regardless of which tool is active: the request is a no-op unless Move
		 * Vertex is the active geometry operation (see
		 * MoveVertexGeometryOperation::handle_delete_selected_vertices_requested).
		 */
		QShortcut *d_delete_selected_vertices_shortcut;

	};
}

#endif // GPLATES_QTWIDGETS_MODIFYGEOMETRYWIDGET_H
