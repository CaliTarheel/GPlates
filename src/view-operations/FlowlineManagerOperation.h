/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#ifndef GPLATES_VIEW_OPERATIONS_FLOWLINEMANAGEROPERATION_H
#define GPLATES_VIEW_OPERATIONS_FLOWLINEMANAGEROPERATION_H

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
	class FlowlineManagerOperation :
			public QObject
	{
		Q_OBJECT

	public:
		FlowlineManagerOperation(
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

#endif
