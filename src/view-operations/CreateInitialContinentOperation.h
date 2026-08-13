/* $Id$ */

/**
 * \file
 * Worldbuilding Pasta "Create Initial Continent" operation.
 */

#ifndef GPLATES_VIEWOPERATIONS_CREATEINITIALCONTINENTOPERATION_H
#define GPLATES_VIEWOPERATIONS_CREATEINITIALCONTINENTOPERATION_H

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
	class CreateInitialContinentOperation :
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

		explicit
		CreateInitialContinentOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent_widget);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CREATEINITIALCONTINENTOPERATION_H
