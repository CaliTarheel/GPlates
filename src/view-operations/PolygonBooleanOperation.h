/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_POLYGONBOOLEANOPERATION_H
#define GPLATES_VIEWOPERATIONS_POLYGONBOOLEANOPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "model/ModelInterface.h"


class QWidget;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesGui
{
	class FeatureFocus;
}

namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	class PolygonBooleanOperation :
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
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_),
				message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		PolygonBooleanOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(
				QWidget *parent);

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
	};
}

#endif // GPLATES_VIEWOPERATIONS_POLYGONBOOLEANOPERATION_H
