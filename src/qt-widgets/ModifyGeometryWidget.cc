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

#include <QHeaderView>
#include <QWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ModifyGeometryWidget.h"
#include "LatLonCoordinatesTable.h"

#include <QInputDialog>

#include "canvas-tools/ModifyGeometryState.h"


GPlatesQtWidgets::ModifyGeometryWidget::ModifyGeometryWidget(
		GPlatesCanvasTools::GeometryOperationState &geometry_operation_state,
		GPlatesCanvasTools::ModifyGeometryState &modify_geometry_state,
		QWidget *parent_):
	TaskPanelWidget(parent_),
	d_modify_geometry_state(modify_geometry_state)
{
	setupUi(this);
	
	// Set up the header of the coordinates widget.
	coordinates_table()->header()->setSectionResizeMode(QHeaderView::Stretch);

	// Get a wrapper around coordinates table that listens to a GeometryBuilder
	// and fills in the table accordingly.
	d_lat_lon_coordinates_table.reset(
			new LatLonCoordinatesTable(coordinates_table(), geometry_operation_state));

	QObject::connect(
			button_delete_selected_vertices,
			SIGNAL(clicked()),
			this,
			SLOT(handle_delete_selected_vertices()));

	// Window-scoped (not limited to this panel having focus) since lassoing on the globe
	// leaves the globe canvas focused, not this dock - the shortcut needs to fire from there.
	d_delete_selected_vertices_shortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
	d_delete_selected_vertices_shortcut->setContext(Qt::WindowShortcut);
	QObject::connect(
			d_delete_selected_vertices_shortcut,
			SIGNAL(activated()),
			this,
			SLOT(handle_delete_selected_vertices()));
	QObject::connect(
			button_average_vertex_positions,
			SIGNAL(clicked()),
			this,
			SLOT(handle_average_selected_vertex_positions()));
	QObject::connect(
			button_cluster_selected_vertices,
			SIGNAL(clicked()),
			this,
			SLOT(handle_cluster_selected_vertices()));
	QObject::connect(
			button_snap_selected_vertices_to_plate,
			SIGNAL(clicked()),
			this,
			SLOT(handle_snap_selected_vertices_to_plate()));
	QObject::connect(
			&d_modify_geometry_state,
			SIGNAL(vertex_selection_state_changed(unsigned int,bool)),
			this,
			SLOT(handle_vertex_selection_state_changed(unsigned int,bool)));

	handle_vertex_selection_state_changed(0, false);

}


GPlatesQtWidgets::ModifyGeometryWidget::~ModifyGeometryWidget()
{
	// boost::scoped_ptr destructor needs complete type.
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_activation()
{
	reload_coordinates_table_if_necessary();
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_delete_selected_vertices()
{
	d_modify_geometry_state.request_delete_selected_vertices();
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_average_selected_vertex_positions()
{
	d_modify_geometry_state.request_average_selected_vertex_positions();
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_cluster_selected_vertices()
{
	bool accepted = false;
	const double threshold_degrees = QInputDialog::getDouble(
			this,
			tr("Cluster Selected Vertices"),
			tr("Maximum separation (degrees):"),
			0.1,
			0.000001,
			180.0,
			6,
			&accepted);
	if (accepted)
	{
		d_modify_geometry_state.request_cluster_selected_vertices(threshold_degrees);
	}
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_snap_selected_vertices_to_plate()
{
	bool accepted = false;
	const int plate_id = QInputDialog::getInt(
			this,
			tr("Snap Selected Vertices to Plate"),
			tr("Guide Plate ID:"),
			0,
			0,
			99999999,
			1,
			&accepted);
	if (!accepted)
	{
		return;
	}
	const double threshold_degrees = QInputDialog::getDouble(
			this,
			tr("Snap Selected Vertices to Plate"),
			tr("Maximum snap distance (degrees):"),
			1.0,
			0.000001,
			180.0,
			6,
			&accepted);
	if (accepted)
	{
		d_modify_geometry_state.request_snap_selected_vertices_to_plate(
				plate_id,
				threshold_degrees);
	}
}


void
GPlatesQtWidgets::ModifyGeometryWidget::handle_vertex_selection_state_changed(
		unsigned int selected_vertex_count,
		bool can_delete_selection)
{
	if (selected_vertex_count == 0)
	{
		label_vertex_selection_count->setText(tr("No vertices selected"));
	}
	else if (selected_vertex_count == 1)
	{
		label_vertex_selection_count->setText(tr("1 vertex selected"));
	}
	else
	{
		label_vertex_selection_count->setText(
				tr("%1 vertices selected").arg(selected_vertex_count));
	}

	button_average_vertex_positions->setEnabled(selected_vertex_count > 1);
	button_delete_selected_vertices->setEnabled(can_delete_selection);
	button_cluster_selected_vertices->setEnabled(selected_vertex_count > 1);
	button_snap_selected_vertices_to_plate->setEnabled(selected_vertex_count > 0);
}
