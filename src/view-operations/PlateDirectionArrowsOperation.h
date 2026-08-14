/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_PLATEDIRECTIONARROWSOPERATION_H
#define GPLATES_VIEWOPERATIONS_PLATEDIRECTIONARROWSOPERATION_H

#include <QObject>

#include "model/types.h"
#include "RenderedGeometryCollection.h"

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
	class PlateDirectionArrowsOperation :
			public QObject
	{
		Q_OBJECT

	public:
		PlateDirectionArrowsOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state,
				RenderedGeometryCollection &rendered_geometry_collection,
				QWidget *parent_widget);

	public Q_SLOTS:
		void
		show_dialog();

		void
		clear();

		void
		handle_reconstructed(
				GPlatesAppLogic::ApplicationState &application_state);

	private:
		void
		render(
				bool notify_if_empty = false);

		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;
		RenderedGeometryCollection &d_rendered_geometry_collection;
		QWidget *d_parent_widget;
		RenderedGeometryCollection::child_layer_owner_ptr_type d_arrow_layer;
		bool d_enabled;
		GPlatesModel::integer_plate_id_type d_plate_id;
		double d_delta_time;
		double d_arrow_scale;
		unsigned int d_max_arrows_per_feature;
	};
}

#endif // GPLATES_VIEWOPERATIONS_PLATEDIRECTIONARROWSOPERATION_H
