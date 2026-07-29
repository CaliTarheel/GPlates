/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H
#define GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "model/ModelInterface.h"


class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	class RotationFileEditorOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			OPERATION_CANCELLED,
			OPERATION_COMPLETED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) : outcome(outcome_), message(message_) {  }
			Outcome outcome;
			QString message;
		};

		explicit
		RotationFileEditorOperation(
				GPlatesAppLogic::ApplicationState &application_state);

		Result
		trigger(
				QWidget *parent);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesModel::ModelInterface d_model_interface;
	};
}

#endif // GPLATES_VIEWOPERATIONS_ROTATIONFILEEDITOROPERATION_H
