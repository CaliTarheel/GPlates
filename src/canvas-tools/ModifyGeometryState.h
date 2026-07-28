/* $Id$ */

/**
 * \file 
 * $Revision$
 * $Date$
 * 
 * Copyright (C) 2012 The University of Sydney, Australia
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

#ifndef GPLATES_CANVASTOOLS_MODIFYGEOMETRYSTATE_H
#define GPLATES_CANVASTOOLS_MODIFYGEOMETRYSTATE_H

#include <QObject>

#include "model/types.h"


namespace GPlatesCanvasTools
{
	/**
	 * This is used to communicate between @a ModifyGeometryWidget (and associated sub-widgets) and
	 * canvas tools that can modify either digitised or focused feature geometry.
	 *
	 * Currently it's used to communicate snap-to-vertices information to the Move Vertex tool.
	 */
	class ModifyGeometryState :
			public QObject
	{
		Q_OBJECT

	public:

		/**
		 * Sets user-provided move-nearby-vertex information (from the task panel tab).
		 */
		void
		set_snap_vertices_setup(
				bool should_check_nearby_vertices,
				double threshold,
				bool should_use_plate_id,
				GPlatesModel::integer_plate_id_type plate_id)
		{
			Q_EMIT snap_vertices_setup_changed(should_check_nearby_vertices, threshold, should_use_plate_id, plate_id);
		}

		void
		request_delete_selected_vertices()
		{
			Q_EMIT delete_selected_vertices_requested();
		}

		void
		request_average_selected_vertex_positions()
		{
			Q_EMIT average_selected_vertex_positions_requested();
		}

		void
		set_vertex_selection_state(
				unsigned int selected_vertex_count,
				bool can_delete_selection)
		{
			Q_EMIT vertex_selection_state_changed(selected_vertex_count, can_delete_selection);
		}

	Q_SIGNALS:
		// NOTE: all signals/slots should use namespace scope for all arguments
		//       otherwise differences between signals and slots will cause Qt
		//       to not be able to connect them at runtime.

		void
		snap_vertices_setup_changed(
				bool should_check_nearby_vertices,
				double threshold,
				bool should_use_plate_id,
				GPlatesModel::integer_plate_id_type plate_id);

		void
		delete_selected_vertices_requested();

		void
		average_selected_vertex_positions_requested();

		void
		vertex_selection_state_changed(
				unsigned int selected_vertex_count,
				bool can_delete_selection);

	};
}

#endif // GPLATES_CANVASTOOLS_MODIFYGEOMETRYSTATE_H
