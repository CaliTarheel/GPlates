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
	QObject::connect(
			button_average_vertex_positions,
			SIGNAL(clicked()),
			this,
			SLOT(handle_average_selected_vertex_positions()));
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
GPlatesQtWidgets::ModifyGeometryWidget::handle_vertex_selection_state_changed(
		unsigned int selected_vertex_count,
		bool can_delete_selection)
{
	if (selected_vertex_count == 0)
	{
		label_vertex_selection_count->setText(tr("No vertices selected"));
	}
	else
	{
		label_vertex_selection_count->setText(
				tr("%1 vertices selected").arg(selected_vertex_count));
	}

	button_average_vertex_positions->setEnabled(selected_vertex_count > 1);
	button_delete_selected_vertices->setEnabled(can_delete_selection);
}
