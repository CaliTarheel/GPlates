/* $Id$ */

/**
 * \file
 * World Building "Naturalize Coastline" operation.
 */

#ifndef GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEOPERATION_H
#define GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEOPERATION_H

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

namespace GPlatesViewOperations
{
	class RenderedGeometryCollection;

	class NaturalizeCoastlineOperation :
			private boost::noncopyable
	{
	public:
		enum Outcome
		{
			NATURALIZE_COMPLETED,
			NATURALIZE_CANCELLED,
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

		NaturalizeCoastlineOperation(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesAppLogic::ApplicationState &application_state,
				RenderedGeometryCollection &rendered_geometry_collection);

		Result
		trigger(QWidget *parent_widget);

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesAppLogic::ApplicationState &d_application_state;
		RenderedGeometryCollection &d_rendered_geometry_collection;
		GPlatesModel::ModelInterface d_model_interface;
	};
}

#endif // GPLATES_VIEWOPERATIONS_NATURALIZECOASTLINEOPERATION_H
