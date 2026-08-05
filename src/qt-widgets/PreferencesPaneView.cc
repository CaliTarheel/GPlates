/* $Id$ */

/**
 * \file 
 * $Revision$
 * $Date$ 
 * 
 * Copyright (C) 2011 The University of Sydney, Australia
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 *
 * GPlates is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "PreferencesPaneView.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QVBoxLayout>

#include "QtWidgetUtils.h"
#include "app-logic/ApplicationState.h"
#include "app-logic/UserPreferences.h"
#include "gui/ConfigGuiUtils.h"


GPlatesQtWidgets::PreferencesPaneView::PreferencesPaneView(
		GPlatesAppLogic::ApplicationState &app_state,
		QWidget *parent_):
	QWidget(parent_)
{
	setupUi(this);
	GPlatesAppLogic::UserPreferences &prefs = app_state.get_user_preferences();

	QGroupBox *feature_creation_group = new QGroupBox(tr("New Feature Time Defaults"), this);
	QVBoxLayout *feature_creation_layout = new QVBoxLayout(feature_creation_group);
	QCheckBox *default_begin_to_current = new QCheckBox(
			tr("Default start time to the current displayed time"), feature_creation_group);
	QCheckBox *default_end_to_current = new QCheckBox(
			tr("Default end time to the current displayed time"), feature_creation_group);
	default_begin_to_current->setToolTip(tr(
			"When creating a feature, initialise its Begin time from the current reconstruction time."));
	default_end_to_current->setToolTip(tr(
			"When creating a feature, initialise its End time from the current reconstruction time."));
	feature_creation_layout->addWidget(default_begin_to_current);
	feature_creation_layout->addWidget(default_end_to_current);
	verticalLayout_2->insertWidget(1, feature_creation_group);

	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(
			default_begin_to_current, prefs,
			"feature_creation/default_begin_time_to_current", NULL);
	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(
			default_end_to_current, prefs,
			"feature_creation/default_end_time_to_current", NULL);
	
	// View Time UserPreferences link:-
	
	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(spinbox_time_range_start, prefs,
			"view/animation/default_time_range_start", toolbutton_reset_time_range);
	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(spinbox_time_range_end, prefs,
			"view/animation/default_time_range_end", toolbutton_reset_time_range);
	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(spinbox_time_range_increment, prefs,
			"view/animation/default_time_increment", toolbutton_reset_time_range);

	// Misc view options link:-
	
	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(checkbox_show_stars, prefs,
			"view/show_stars", NULL);		// Not much point to a 'reset' button here.

	GPlatesGui::ConfigGuiUtils::link_widget_to_preference(
			checkbox_show_topological_sections, prefs,
			"view/geometry_visibility/show_topological_sections", NULL/*no reset button*/);
}


