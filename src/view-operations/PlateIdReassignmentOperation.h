/* $Id$ */

/**
 * \file
 *
 * Copyright (C) 2026 CaliTarheel
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

#ifndef GPLATES_VIEWOPERATIONS_PLATEIDREASSIGNMENTOPERATION_H
#define GPLATES_VIEWOPERATIONS_PLATEIDREASSIGNMENTOPERATION_H

#include <QObject>

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesPresentation
{
	class ViewState;
}

class QWidget;

namespace GPlatesViewOperations
{
	class PlateIdReassignmentOperation :
			public QObject
	{
		Q_OBJECT

	public:
		PlateIdReassignmentOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state,
				QWidget *parent_widget);

	public Q_SLOTS:
		void
		trigger();

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		QWidget *d_parent_widget;
	};
}

#endif // GPLATES_VIEWOPERATIONS_PLATEIDREASSIGNMENTOPERATION_H
