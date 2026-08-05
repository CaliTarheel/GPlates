/* $Id$ */

/**
 * \file
 * Review-and-commit workflow for a selected continent's Voronoi rift network.
 */

#ifndef GPLATES_VIEWOPERATIONS_PROPOSEINITIALRIFTSOPERATION_H
#define GPLATES_VIEWOPERATIONS_PROPOSEINITIALRIFTSOPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "model/ModelInterface.h"


class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	class RenderedGeometryCollection;

	class ProposeInitialRiftsOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			OPERATION_COMPLETED,
			OPERATION_CANCELLED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_),
				message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		ProposeInitialRiftsOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent_widget);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		RenderedGeometryCollection &d_rendered_geometry_collection;
		GPlatesModel::ModelInterface d_model_interface;
		bool d_review_in_progress;
	};
}

#endif // GPLATES_VIEWOPERATIONS_PROPOSEINITIALRIFTSOPERATION_H
