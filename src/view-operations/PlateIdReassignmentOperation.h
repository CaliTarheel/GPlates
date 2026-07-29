/* $Id$ */

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

		void
		trigger_bulk();

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		QWidget *d_parent_widget;
	};
}

#endif // GPLATES_VIEWOPERATIONS_PLATEIDREASSIGNMENTOPERATION_H
