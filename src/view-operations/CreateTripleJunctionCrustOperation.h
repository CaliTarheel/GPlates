/* $Id$ */

/**
 * \file
 * Extends three selected half-stage MORs and creates their RRR crust bands.
 */

#ifndef GPLATES_VIEWOPERATIONS_CREATETRIPLEJUNCTIONCRUSTOPERATION_H
#define GPLATES_VIEWOPERATIONS_CREATETRIPLEJUNCTIONCRUSTOPERATION_H

#include <boost/noncopyable.hpp>
#include <QString>

#include "model/FeatureCollectionHandle.h"
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
	class CreateOceanCrustOperation;

	class CreateTripleJunctionCrustOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			SELECTION_REQUIRED,
			OPERATION_CANCELLED,
			OPERATION_COMPLETED,
			OPERATION_ERROR
		};

		struct Result
		{
			Result(Outcome outcome_, const QString &message_) :
				outcome(outcome_), message(message_) { }

			Outcome outcome;
			QString message;
		};

		CreateTripleJunctionCrustOperation(
				CreateOceanCrustOperation &mor_selection,
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		Result
		trigger(QWidget *parent_widget);

	private:
		CreateOceanCrustOperation &d_mor_selection;
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_last_output_collection;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CREATETRIPLEJUNCTIONCRUSTOPERATION_H
