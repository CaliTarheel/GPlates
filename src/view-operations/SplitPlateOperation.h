/* $Id$ */

/**
 * \file
 * Implements the Polygon Operations "Split Plate" tool.
 */

#ifndef GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H
#define GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"

#include "maths/PolygonOnSphere.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"


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

class QWidget;

namespace GPlatesViewOperations
{
	/**
	 * Splits a visible reconstructed polygon using a visible reconstructed
	 * polyline selected in a transient tool window.
	 */
	class SplitPlateOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			OPERATION_CANCELLED,
			SPLIT_COMPLETED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(
					Outcome outcome_,
					const QString &message_) :
				outcome(outcome_),
				message(message_)
			{  }

			Outcome outcome;
			QString message;
		};

		SplitPlateOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		/**
		 * Opens the selection window and performs the requested split.
		 */
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

#endif // GPLATES_VIEWOPERATIONS_SPLITPLATEOPERATION_H
