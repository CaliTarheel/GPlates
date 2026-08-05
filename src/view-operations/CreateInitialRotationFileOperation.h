/* $Id$ */

/**
 * \file
 * Worldbuilding Pasta initial rotation-file creation and loading operation.
 */

#ifndef GPLATES_VIEWOPERATIONS_CREATEINITIALROTATIONFILEOPERATION_H
#define GPLATES_VIEWOPERATIONS_CREATEINITIALROTATIONFILEOPERATION_H

#include <boost/noncopyable.hpp>

#include <QString>


class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesViewOperations
{
	class CreateInitialRotationFileOperation :
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
			{ }

			Outcome outcome;
			QString message;
		};

		explicit
		CreateInitialRotationFileOperation(
				GPlatesAppLogic::ApplicationState &application_state);

		Result
		trigger(
				QWidget *parent_widget);

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CREATEINITIALROTATIONFILEOPERATION_H
