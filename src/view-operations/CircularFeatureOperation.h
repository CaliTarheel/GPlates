/*
 * Copyright (C) 2026 The GPlates development team.
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_VIEWOPERATIONS_CIRCULARFEATUREOPERATION_H
#define GPLATES_VIEWOPERATIONS_CIRCULARFEATUREOPERATION_H

#include <vector>
#include <QObject>
#include <QPointer>

#include "model/FeatureCollectionHandle.h"


class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;

namespace GPlatesAppLogic
{
	class ApplicationState;
}

namespace GPlatesMaths
{
	class SmallCircle;
}

namespace GPlatesQtWidgets
{
	class SmallCircleWidget;
	class ViewportWindow;
}

namespace GPlatesViewOperations
{
	/**
	 * Modeless World Building workflow for drawing size-limited circular
	 * polygon or polyline features with a user-selected appearance time.
	 */
	class CircularFeatureOperation :
			public QObject
	{
		Q_OBJECT

	public:
		CircularFeatureOperation(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesQtWidgets::ViewportWindow &viewport_window);

	public Q_SLOTS:
		void
		trigger();

	private Q_SLOTS:
		void
		activate_placement();

		void
		handle_circle_completed();

		void
		handle_maximum_radius_changed(
				double maximum_radius_km);

		void
		handle_dialog_finished(
				int result);

		void
		refresh_output_collections();

	private:
		void
		create_dialog();

		GPlatesModel::FeatureCollectionHandle::weak_ref
		ensure_output_collection();

		void
		create_circular_feature(
				const GPlatesMaths::SmallCircle &circle);

		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesQtWidgets::ViewportWindow &d_viewport_window;
		GPlatesQtWidgets::SmallCircleWidget &d_small_circle_widget;

		QPointer<QDialog> d_dialog;
		QPointer<QDoubleSpinBox> d_appearance_time_spin;
		QPointer<QComboBox> d_geometry_type_combo;
		QPointer<QDoubleSpinBox> d_maximum_radius_spin;
		QPointer<QComboBox> d_output_collection_combo;
		QPointer<QLabel> d_status_label;

		std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> d_output_collections;
		bool d_placement_active;
		unsigned int d_feature_count;
	};
}

#endif // GPLATES_VIEWOPERATIONS_CIRCULARFEATUREOPERATION_H
