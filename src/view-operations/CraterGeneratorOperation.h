/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#ifndef GPLATES_VIEW_OPERATIONS_CRATERGENERATOROPERATION_H
#define GPLATES_VIEW_OPERATIONS_CRATERGENERATOROPERATION_H

#include <QObject>


namespace GPlatesAppLogic
{
	class ApplicationState;
}

class QWidget;


namespace GPlatesViewOperations
{
	class CraterGeneratorOperation :
			public QObject
	{
		Q_OBJECT

	public:
		CraterGeneratorOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				QWidget *parent_widget);

	public Q_SLOTS:
		void
		trigger();

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		QWidget *d_parent_widget;
	};
}

#endif
