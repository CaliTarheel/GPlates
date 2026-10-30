/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#ifndef GPLATES_QTWIDGETS_PREFERENCESPANEACTIVEFEATURETYPES_H
#define GPLATES_QTWIDGETS_PREFERENCESPANEACTIVEFEATURETYPES_H

#include <QStringList>
#include <QWidget>


class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace GPlatesAppLogic
{
	class ApplicationState;
	class UserPreferences;
}

namespace GPlatesQtWidgets
{
	/**
	 * User-facing controls for choosing the active feature types.
	 *
	 * This pane currently controls which feature types are offered by the
	 * Create and Change Feature Type dialogs. It does not modify existing
	 * features or the GPGIM.
	 */
	class PreferencesPaneActiveFeatureTypes :
			public QWidget
	{
		Q_OBJECT

	public:

		explicit
		PreferencesPaneActiveFeatureTypes(
				GPlatesAppLogic::ApplicationState &app_state,
				QWidget *parent_ = NULL);

	private Q_SLOTS:

		void
		handle_item_changed(
				QListWidgetItem *item);

		void
		handle_filter_text_changed(
				const QString &text);

		void
		show_all_feature_types();

		void
		hide_all_feature_types();

		void
		show_loaded_project_feature_types();

		/**
		 * Write the currently checked feature types to a file chosen by the user.
		 */
		void
		save_feature_type_list();

		/**
		 * Replace the checked feature types with a list read from a file chosen by the user.
		 */
		void
		load_feature_type_list();

	private:

		/**
		 * The qualified names of the feature types currently checked, in list order.
		 */
		QStringList
		get_checked_feature_types() const;

		void
		populate_feature_types();

		void
		set_all_feature_types_checked(
				bool checked);

		void
		set_checked_feature_types(
				const QStringList &checked_feature_types);

		void
		save_hidden_feature_types();

		void
		update_summary();

		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesAppLogic::UserPreferences &d_preferences;
		QLineEdit *d_filter_line_edit;
		QListWidget *d_feature_type_list;
		QLabel *d_summary_label;
		bool d_populating;
	};
}

#endif  // GPLATES_QTWIDGETS_PREFERENCESPANEACTIVEFEATURETYPES_H
