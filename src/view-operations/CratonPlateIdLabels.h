/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_CRATONPLATEIDLABELS_H
#define GPLATES_VIEWOPERATIONS_CRATONPLATEIDLABELS_H

#include <QObject>

#include "RenderedGeometryCollection.h"


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
	/**
	 * Keeps reconstruction plate IDs visibly anchored inside reconstructed
	 * gpml:Craton polygons.
	 */
	class CratonPlateIdLabels :
			public QObject
	{
		Q_OBJECT

	public:
		CratonPlateIdLabels(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

	private Q_SLOTS:
		void handle_reconstructed(GPlatesAppLogic::ApplicationState &application_state);

		void refresh();

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_label_layer;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CRATONPLATEIDLABELS_H
