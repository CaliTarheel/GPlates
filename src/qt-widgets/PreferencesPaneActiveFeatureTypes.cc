/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include <algorithm>
#include <boost/foreach.hpp>

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTextStream>
#include <QVBoxLayout>

#include "PreferencesPaneActiveFeatureTypes.h"

#include "FeatureTypeDisplayPreferences.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/UserPreferences.h"

#include "file-io/File.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/Gpgim.h"
#include "model/QualifiedXmlName.h"


GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::PreferencesPaneActiveFeatureTypes(
		GPlatesAppLogic::ApplicationState &app_state,
		QWidget *parent_) :
	QWidget(parent_),
	d_application_state(app_state),
	d_preferences(app_state.get_user_preferences()),
	d_filter_line_edit(new QLineEdit(this)),
	d_feature_type_list(new QListWidget(this)),
	d_summary_label(new QLabel(this)),
	d_populating(false)
{
	QVBoxLayout *main_layout = new QVBoxLayout(this);

	QLabel *heading_label = new QLabel(tr("<b>Active Feature Types</b>"), this);
	main_layout->addWidget(heading_label);

	QLabel *description_label = new QLabel(
			tr("Choose which feature types are offered when creating or changing a feature type. "
				"This changes only the displayed choices; it does not modify existing feature data."),
			this);
	description_label->setWordWrap(true);
	main_layout->addWidget(description_label);

	d_filter_line_edit->setPlaceholderText(tr("Filter feature types..."));
	d_filter_line_edit->setClearButtonEnabled(true);
	main_layout->addWidget(d_filter_line_edit);

	d_feature_type_list->setAlternatingRowColors(true);
	main_layout->addWidget(d_feature_type_list, 1);

	QHBoxLayout *controls_layout = new QHBoxLayout;
	QPushButton *show_all_button = new QPushButton(tr("Show All"), this);
	QPushButton *hide_all_button = new QPushButton(tr("Hide All"), this);
	controls_layout->addWidget(show_all_button);
	controls_layout->addWidget(hide_all_button);
	controls_layout->addStretch();
	controls_layout->addWidget(d_summary_label);
	main_layout->addLayout(controls_layout);

	QHBoxLayout *presets_layout = new QHBoxLayout;
	QLabel *presets_label = new QLabel(tr("Presets:"), this);
	QPushButton *loaded_project_button = new QPushButton(tr("Loaded Project Types"), this);
	loaded_project_button->setToolTip(
			tr("Show only feature types currently present in loaded feature collections."));
	presets_layout->addWidget(presets_label);
	presets_layout->addWidget(loaded_project_button);
	presets_layout->addStretch();
	main_layout->addLayout(presets_layout);

	// The presets above are fixed at compile time. Saving and loading a list makes the set of
	// active feature types an ordinary project artifact instead: one that can be produced by
	// curating this list and saving it, kept alongside a project, handed to another user, and
	// reviewed in version control - none of which needs a new build.
	QHBoxLayout *list_file_layout = new QHBoxLayout;
	QLabel *list_file_label = new QLabel(tr("Feature type list:"), this);
	QPushButton *save_list_button = new QPushButton(tr("Save..."), this);
	save_list_button->setToolTip(
			tr("Write the currently checked feature types to a file."));
	QPushButton *load_list_button = new QPushButton(tr("Load..."), this);
	load_list_button->setToolTip(
			tr("Replace the checked feature types with a list read from a file."));
	list_file_layout->addWidget(list_file_label);
	list_file_layout->addWidget(save_list_button);
	list_file_layout->addWidget(load_list_button);
	list_file_layout->addStretch();
	main_layout->addLayout(list_file_layout);

	populate_feature_types();

	QObject::connect(
			d_feature_type_list,
			SIGNAL(itemChanged(QListWidgetItem *)),
			this,
			SLOT(handle_item_changed(QListWidgetItem *)));
	QObject::connect(
			d_filter_line_edit,
			SIGNAL(textChanged(QString)),
			this,
			SLOT(handle_filter_text_changed(QString)));
	QObject::connect(show_all_button, SIGNAL(clicked()), this, SLOT(show_all_feature_types()));
	QObject::connect(hide_all_button, SIGNAL(clicked()), this, SLOT(hide_all_feature_types()));
	QObject::connect(
			loaded_project_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_loaded_project_feature_types()));
	QObject::connect(save_list_button, SIGNAL(clicked()), this, SLOT(save_feature_type_list()));
	QObject::connect(load_list_button, SIGNAL(clicked()), this, SLOT(load_feature_type_list()));
}


QStringList
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::get_checked_feature_types() const
{
	QStringList checked_feature_types;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		QListWidgetItem *item = d_feature_type_list->item(row);
		if (item->checkState() == Qt::Checked)
		{
			checked_feature_types.append(item->data(Qt::UserRole).toString());
		}
	}

	return checked_feature_types;
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::save_feature_type_list()
{
	const QString filename = QFileDialog::getSaveFileName(
			this,
			tr("Save Feature Type List"),
			QString(),
			tr("Feature type lists (*.txt);;All files (*)"));
	if (filename.isEmpty())
	{
		return;
	}

	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
	{
		QMessageBox::critical(
				this,
				tr("Save Feature Type List"),
				tr("Could not open '%1' for writing.").arg(QDir::toNativeSeparators(filename)));
		return;
	}

	QTextStream stream(&file);
	// Written as UTF-8 so the file reads back identically regardless of the machine's locale.
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
	stream.setEncoding(QStringConverter::Utf8);
#else
	stream.setCodec("UTF-8");
#endif

	// A short header so the file explains itself when opened in a text editor.
	stream << "# GPlates active feature type list.\n";
	stream << "# One qualified feature type per line. Blank lines and lines beginning with '#'\n";
	stream << "# are ignored. Load this file from Preferences > Active Feature Types.\n";

	const QStringList checked_feature_types = get_checked_feature_types();
	BOOST_FOREACH(const QString &feature_type, checked_feature_types)
	{
		stream << feature_type << "\n";
	}

	stream.flush();
	file.close();

	d_summary_label->setText(
			tr("Saved %n feature type(s).", "", checked_feature_types.count()));
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::load_feature_type_list()
{
	const QString filename = QFileDialog::getOpenFileName(
			this,
			tr("Load Feature Type List"),
			QString(),
			tr("Feature type lists (*.txt);;All files (*)"));
	if (filename.isEmpty())
	{
		return;
	}

	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QMessageBox::critical(
				this,
				tr("Load Feature Type List"),
				tr("Could not open '%1' for reading.").arg(QDir::toNativeSeparators(filename)));
		return;
	}

	QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
	stream.setEncoding(QStringConverter::Utf8);
#else
	stream.setCodec("UTF-8");
#endif

	// Collect the names this build knows about, so we can tell the user which entries we could
	// not apply rather than discarding them silently. A list saved from a build with a different
	// set of feature types available would otherwise look like it loaded correctly.
	QSet<QString> known_feature_types;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		known_feature_types.insert(d_feature_type_list->item(row)->data(Qt::UserRole).toString());
	}

	QStringList feature_types_to_check;
	QStringList unrecognised_feature_types;
	while (!stream.atEnd())
	{
		const QString line = stream.readLine().trimmed();
		if (line.isEmpty() ||
			line.startsWith('#'))
		{
			continue;
		}

		if (known_feature_types.contains(line))
		{
			feature_types_to_check.append(line);
		}
		else
		{
			unrecognised_feature_types.append(line);
		}
	}

	file.close();

	if (feature_types_to_check.isEmpty() &&
		unrecognised_feature_types.isEmpty())
	{
		QMessageBox::warning(
				this,
				tr("Load Feature Type List"),
				tr("'%1' contains no feature types. The current selection has not been changed.")
						.arg(QDir::toNativeSeparators(filename)));
		return;
	}

	set_checked_feature_types(feature_types_to_check);

	if (!unrecognised_feature_types.isEmpty())
	{
		QMessageBox::warning(
				this,
				tr("Load Feature Type List"),
				tr("%n entry/entries in the file were not recognised as feature types in this build"
					" of GPlates and were not applied:", "", unrecognised_feature_types.count())
						+ QString("\n\n") + unrecognised_feature_types.join(QString("\n")));
	}
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::populate_feature_types()
{
	d_populating = true;
	d_feature_type_list->clear();

	const QStringList hidden_feature_types = d_preferences.get_value(
			FeatureTypeDisplayPreferences::hidden_feature_types_key()).toStringList();

	GPlatesModel::Gpgim::feature_type_seq_type feature_types =
			GPlatesModel::Gpgim::instance().get_concrete_feature_types();
	std::stable_sort(feature_types.begin(), feature_types.end());

	BOOST_FOREACH(const GPlatesModel::FeatureType &feature_type, feature_types)
	{
		const QString qualified_name =
				GPlatesModel::convert_qualified_xml_name_to_qstring(feature_type);

		QListWidgetItem *item = new QListWidgetItem(feature_type.get_name().qstring());
		item->setData(Qt::UserRole, qualified_name);
		item->setToolTip(qualified_name);
		item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
		item->setCheckState(hidden_feature_types.contains(qualified_name)
				? Qt::Unchecked
				: Qt::Checked);
		d_feature_type_list->addItem(item);
	}

	d_populating = false;
	update_summary();
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::handle_item_changed(
		QListWidgetItem *)
{
	if (d_populating)
	{
		return;
	}

	save_hidden_feature_types();
	update_summary();
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::handle_filter_text_changed(
		const QString &text)
{
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		QListWidgetItem *item = d_feature_type_list->item(row);
		item->setHidden(!item->text().contains(text, Qt::CaseInsensitive) &&
				!item->data(Qt::UserRole).toString().contains(text, Qt::CaseInsensitive));
	}
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::show_all_feature_types()
{
	set_all_feature_types_checked(true);
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::hide_all_feature_types()
{
	set_all_feature_types_checked(false);
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::show_loaded_project_feature_types()
{
	QSet<QString> loaded_feature_types;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded_files =
			d_application_state.get_feature_collection_file_state().get_loaded_files();

	BOOST_FOREACH(
			const GPlatesAppLogic::FeatureCollectionFileState::file_reference &loaded_file,
			loaded_files)
	{
		GPlatesModel::FeatureCollectionHandle::weak_ref feature_collection =
				loaded_file.get_file().get_feature_collection();
		if (!feature_collection.is_valid())
		{
			continue;
		}

		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = feature_collection->begin();
			feature_iter != feature_collection->end();
			++feature_iter)
		{
			const GPlatesModel::FeatureHandle::non_null_ptr_type feature = *feature_iter;
			loaded_feature_types.insert(
					GPlatesModel::convert_qualified_xml_name_to_qstring(feature->feature_type()));
		}
	}

	if (loaded_feature_types.isEmpty())
	{
		d_summary_label->setText(tr("No feature types found in loaded collections"));
		return;
	}

	set_checked_feature_types(loaded_feature_types.values());
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::set_all_feature_types_checked(
		bool checked)
{
	d_populating = true;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		d_feature_type_list->item(row)->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
	}
	d_populating = false;

	save_hidden_feature_types();
	update_summary();
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::set_checked_feature_types(
		const QStringList &checked_feature_types)
{
	d_populating = true;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		QListWidgetItem *item = d_feature_type_list->item(row);
		item->setCheckState(checked_feature_types.contains(item->data(Qt::UserRole).toString())
				? Qt::Checked
				: Qt::Unchecked);
	}
	d_populating = false;

	save_hidden_feature_types();
	update_summary();
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::save_hidden_feature_types()
{
	QStringList hidden_feature_types;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		QListWidgetItem *item = d_feature_type_list->item(row);
		if (item->checkState() != Qt::Checked)
		{
			hidden_feature_types.append(item->data(Qt::UserRole).toString());
		}
	}

	d_preferences.set_value(
			FeatureTypeDisplayPreferences::hidden_feature_types_key(),
			hidden_feature_types);
}


void
GPlatesQtWidgets::PreferencesPaneActiveFeatureTypes::update_summary()
{
	int visible_feature_type_count = 0;
	for (int row = 0; row < d_feature_type_list->count(); ++row)
	{
		if (d_feature_type_list->item(row)->checkState() == Qt::Checked)
		{
			++visible_feature_type_count;
		}
	}

	d_summary_label->setText(
			tr("%1 of %2 shown")
					.arg(visible_feature_type_count)
					.arg(d_feature_type_list->count()));
}
