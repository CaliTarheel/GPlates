/* $Id$ */

/**
 * \file
 * Persistent window driving the Ocean Crust Creation tool family (MOR fill,
 * RRR triple-junction, Pacific-style plate birth).
 *
 * Copyright (C) 2026 The GPlates development team.
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

#include <QFrame>
#include <QHBoxLayout>
#include <QStringList>
#include <QVBoxLayout>

#include "OceanCrustCreationDialog.h"

#include "app-logic/ApplicationState.h"

#include "gui/CanvasToolWorkflows.h"

#include "model/FeatureHandle.h"

#include "view-operations/CreateOceanCrustOperation.h"
#include "view-operations/CreatePacificPlateOperation.h"
#include "view-operations/CreateTripleJunctionCrustOperation.h"


GPlatesQtWidgets::OceanCrustCreationDialog::OceanCrustCreationDialog(
		GPlatesViewOperations::CreateOceanCrustOperation &mor_operation,
		GPlatesViewOperations::CreateTripleJunctionCrustOperation &triple_junction_operation,
		GPlatesViewOperations::CreatePacificPlateOperation &pacific_plate_operation,
		GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
		GPlatesAppLogic::ApplicationState &application_state,
		QWidget *parent_) :
	GPlatesDialog(parent_, Qt::Window),
	d_mor_operation(mor_operation),
	d_triple_junction_operation(triple_junction_operation),
	d_pacific_plate_operation(pacific_plate_operation),
	d_canvas_tool_workflows(canvas_tool_workflows)
{
	setWindowTitle(tr("Ocean Crust Creation"));

	QVBoxLayout *main_layout = new QVBoxLayout(this);

	// At the top and set once, rather than buried in Pacific-Style Plate's own commit dialog -
	// applies to that tool's three newly-created MidOceanRidge features specifically (crust
	// still has its own destination picker per tool, since that already worked fine).
	d_mor_destination_widget = new ChooseFeatureCollectionWidget(
			application_state.get_reconstruct_method_registry(),
			application_state.get_feature_collection_file_state(),
			application_state.get_feature_collection_file_io(),
			this);
	d_mor_destination_widget->setTitle(tr("New MOR Destination (Pacific-Style Plate)"));
	d_mor_destination_widget->set_help_text(
			tr("Where Create Pacific-Style Plate writes its three new MidOceanRidge features -"
				" set once here rather than asked again in that tool's own dialog. Its crust still"
				" has its own destination picker there."));
	d_mor_destination_widget->initialise();
	main_layout->addWidget(d_mor_destination_widget);

	QFrame *mor_separator = new QFrame(this);
	mor_separator->setFrameShape(QFrame::HLine);
	mor_separator->setFrameShadow(QFrame::Sunken);
	main_layout->addWidget(mor_separator);

	// The instruction is the most important thing in the window, so it leads and it is the
	// only thing in bold. It always says what to do *now*.
	d_instruction_label = new QLabel(this);
	d_instruction_label->setWordWrap(true);
	d_instruction_label->setStyleSheet("QLabel { font-weight: bold; }");
	main_layout->addWidget(d_instruction_label);

	QLabel *explanation = new QLabel(
			tr("Shift-click half-stage MidOceanRidge features to select up to 3 (Shift-click a"
				" selected one again to remove it). This window stays open while you work, so the"
				" selection is always visible here rather than a message that has already vanished."),
			this);
	explanation->setWordWrap(true);
	main_layout->addWidget(explanation);

	QFrame *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	main_layout->addWidget(separator);

	// Live state. Without this the user is trusting that their Shift-click registered.
	d_selection_label = new QLabel(this);
	d_selection_label->setWordWrap(true);
	main_layout->addWidget(d_selection_label);

	d_generate_mor_crust_button = new QPushButton(tr("Generate Oceanic Crust from &MOR..."), this);
	d_generate_mor_crust_button->setToolTip(
			tr("Fill open ocean space on each side of the one selected half-stage MOR."));
	main_layout->addWidget(d_generate_mor_crust_button);

	d_generate_triple_junction_button = new QPushButton(
			tr("Generate RRR &Triple-Junction Crust..."), this);
	d_generate_triple_junction_button->setToolTip(
			tr("Extend the 3 selected connected half-stage MORs to one junction and fill all three sectors."));
	main_layout->addWidget(d_generate_triple_junction_button);

	d_create_pacific_plate_button = new QPushButton(tr("Create &Pacific-Style Plate..."), this);
	d_create_pacific_plate_button->setToolTip(
			tr("With 3 MORs selected, click the open void they bound, then use this to create the new plate."));
	main_layout->addWidget(d_create_pacific_plate_button);

	QHBoxLayout *clear_layout = new QHBoxLayout;
	d_clear_selection_button = new QPushButton(tr("&Clear Selection"), this);
	d_clear_selection_button->setToolTip(tr("Forget the selected MORs and any captured void seed."));
	clear_layout->addWidget(d_clear_selection_button);
	clear_layout->addStretch();
	main_layout->addLayout(clear_layout);

	d_message_label = new QLabel(this);
	d_message_label->setWordWrap(true);
	main_layout->addWidget(d_message_label);

	QHBoxLayout *action_buttons = new QHBoxLayout;
	action_buttons->addStretch();
	QPushButton *close_button = new QPushButton(tr("Close"), this);
	action_buttons->addWidget(close_button);
	main_layout->addLayout(action_buttons);

	QObject::connect(d_generate_mor_crust_button, SIGNAL(clicked()), this, SLOT(handle_generate_mor_crust()));
	QObject::connect(d_generate_triple_junction_button, SIGNAL(clicked()), this, SLOT(handle_generate_triple_junction_crust()));
	QObject::connect(d_create_pacific_plate_button, SIGNAL(clicked()), this, SLOT(handle_create_pacific_plate()));
	QObject::connect(d_clear_selection_button, SIGNAL(clicked()), this, SLOT(handle_clear_selection()));
	QObject::connect(close_button, SIGNAL(clicked()), this, SLOT(close()));

	refresh_display();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::pop_up()
{
	// A Shift-click is about to be needed on the globe. Whatever tool was active before this
	// window opened, make sure it is one that reports feature focus - otherwise the Shift-click
	// this window is instructing the user to make would silently do nothing.
	d_canvas_tool_workflows.choose_canvas_tool(
			GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION,
			GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);

	refresh_display();

	show();
	activateWindow();
	raise();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::handle_generate_mor_crust()
{
	const GPlatesViewOperations::CreateOceanCrustOperation::Result result =
			d_mor_operation.trigger(this);
	d_message_label->setText(result.message);
	refresh_display();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::handle_generate_triple_junction_crust()
{
	const GPlatesViewOperations::CreateTripleJunctionCrustOperation::Result result =
			d_triple_junction_operation.trigger(this);
	d_message_label->setText(result.message);
	refresh_display();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::handle_create_pacific_plate()
{
	apply_mor_destination_to_operation();
	const GPlatesViewOperations::CreatePacificPlateOperation::Result result =
			d_pacific_plate_operation.trigger(this);
	d_message_label->setText(result.message);
	refresh_display();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::apply_mor_destination_to_operation()
{
	try
	{
		const std::pair<GPlatesAppLogic::FeatureCollectionFileState::file_reference, bool> selection =
				d_mor_destination_widget->get_file_reference();
		d_pacific_plate_operation.set_preset_mor_destination(
				selection.first.get_file().get_feature_collection());
	}
	catch (const ChooseFeatureCollectionWidget::NoFeatureCollectionSelectedException &)
	{
		// Nothing loaded and nothing to create from yet - Pacific-Style Plate falls back to
		// its own crust destination for the new MORs too, same as before this widget existed.
		d_pacific_plate_operation.clear_preset_mor_destination();
	}
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::handle_clear_selection()
{
	d_mor_operation.clear_selected_mors();
	d_pacific_plate_operation.clear_seed();
	d_message_label->setText(tr("Cleared the MOR selection."));
	refresh_display();
}


void
GPlatesQtWidgets::OceanCrustCreationDialog::refresh_display()
{
	const std::vector<GPlatesModel::FeatureHandle::weak_ref> &selected_mors =
			d_mor_operation.selected_mors();
	const unsigned int mor_count = static_cast<unsigned int>(selected_mors.size());

	QString selection_text = tr("MORs selected: %1 of 3").arg(mor_count);
	if (mor_count > 0)
	{
		QStringList ids;
		for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator mor_iter =
					selected_mors.begin(); mor_iter != selected_mors.end(); ++mor_iter)
		{
			if (mor_iter->is_valid())
			{
				ids << (*mor_iter)->feature_id().get().qstring();
			}
		}
		selection_text += tr(" (%1)").arg(ids.join(", "));
	}
	if (d_pacific_plate_operation.seed_capture_is_armed())
	{
		selection_text += tr(" - click the open void to set the Pacific-Style Plate seed.");
	}
	else if (d_pacific_plate_operation.seed_is_captured())
	{
		selection_text += tr(" - void seed captured.");
	}
	d_selection_label->setText(selection_text);

	if (mor_count == 0)
	{
		d_instruction_label->setText(
				tr("Shift-click a half-stage MidOceanRidge feature to begin."));
	}
	else if (mor_count < 3)
	{
		d_instruction_label->setText(
				tr("Shift-click more half-stage MORs, or use Generate Oceanic Crust from MOR with"
					" just the one you have."));
	}
	else if (d_pacific_plate_operation.seed_capture_is_armed())
	{
		d_instruction_label->setText(
				tr("Click the open void the 3 selected MORs bound to set the Pacific-Style Plate seed."));
	}
	else
	{
		d_instruction_label->setText(
				tr("3 MORs selected - use Generate RRR Triple-Junction Crust, or Create"
					" Pacific-Style Plate (which will first ask you to click the open void)."));
	}

	d_generate_mor_crust_button->setEnabled(mor_count == 1);
	d_generate_triple_junction_button->setEnabled(mor_count == 3);
	d_create_pacific_plate_button->setEnabled(mor_count == 3);
	d_clear_selection_button->setEnabled(
			mor_count > 0 || d_pacific_plate_operation.seed_is_captured() ||
			d_pacific_plate_operation.seed_capture_is_armed());
}
