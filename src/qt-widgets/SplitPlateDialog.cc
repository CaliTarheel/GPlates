/* $Id$ */

/**
 * \file
 * Persistent window driving the Split Plate workflow.
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

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "SplitPlateDialog.h"

#include "app-logic/ApplicationState.h"

#include "gui/CanvasToolWorkflows.h"
#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "view-operations/RenderedGeometryFactory.h"


namespace
{
	// Same amber/blue convention as Boolean Polygons: amber for the thing being kept and
	// modified, blue for the thing that acts on it.
	const GPlatesGui::Colour POLYGON_HIGHLIGHT_COLOUR(1.0f, 0.75f, 0.0f, 0.6f);
	const GPlatesGui::Colour POLYLINE_HIGHLIGHT_COLOUR(0.2f, 0.5f, 1.0f, 0.6f);
}


GPlatesQtWidgets::SplitPlateDialog::SplitPlateDialog(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
		GPlatesViewOperations::RenderedGeometryCollection &rendered_geometry_collection,
		QWidget *parent_) :
	GPlatesDialog(parent_, Qt::Window),
	d_feature_focus(feature_focus),
	d_canvas_tool_workflows(canvas_tool_workflows),
	d_operation(new GPlatesViewOperations::SplitPlateOperation(feature_focus, application_state)),
	d_highlight_layer_ptr(
			rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
					GPlatesViewOperations::RenderedGeometryCollection::FEATURE_INSPECTION_CANVAS_TOOL_WORKFLOW_LAYER))
{
	setWindowTitle(tr("Split Plate"));

	QVBoxLayout *main_layout = new QVBoxLayout(this);

	d_instruction_label = new QLabel(this);
	d_instruction_label->setWordWrap(true);
	d_instruction_label->setStyleSheet("QLabel { font-weight: bold; }");
	main_layout->addWidget(d_instruction_label);

	QLabel *explanation = new QLabel(
			tr("Cut a polygon in two. Choose the polygon, then the cutting polyline, then press"
				" Commit. The cutter must cross the polygon's boundary exactly twice; everything"
				" is chosen by clicking features on the globe, so this window stays open while"
				" you work."),
			this);
	explanation->setWordWrap(true);
	main_layout->addWidget(explanation);

	QFrame *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	main_layout->addWidget(separator);

	d_polygon_label = new QLabel(this);
	d_polygon_label->setWordWrap(true);
	main_layout->addWidget(d_polygon_label);

	d_polyline_label = new QLabel(this);
	d_polyline_label->setWordWrap(true);
	main_layout->addWidget(d_polyline_label);

	QHBoxLayout *selection_buttons = new QHBoxLayout;
	d_select_polygon_button = new QPushButton(tr("&Add Polygon"), this);
	d_select_polygon_button->setToolTip(
			tr("Arm selection, then click a polygon on the globe to split. Repeat to split more"
				" than one polygon with the same cutter."));
	d_clear_polygons_button = new QPushButton(tr("Clear &Polygons"), this);
	d_clear_polygons_button->setToolTip(tr("Forget every selected polygon and any captured cutting polyline."));
	d_select_polyline_button = new QPushButton(tr("Choose Cutting Poly&line"), this);
	d_select_polyline_button->setToolTip(
			tr("Arm selection, then click the polyline that cuts every selected polygon."));
	selection_buttons->addWidget(d_select_polygon_button);
	selection_buttons->addWidget(d_clear_polygons_button);
	selection_buttons->addWidget(d_select_polyline_button);
	main_layout->addLayout(selection_buttons);

	d_message_label = new QLabel(this);
	d_message_label->setWordWrap(true);
	main_layout->addWidget(d_message_label);

	QHBoxLayout *action_buttons = new QHBoxLayout;
	action_buttons->addStretch();
	d_commit_button = new QPushButton(tr("&Commit"), this);
	d_commit_button->setDefault(true);
	QPushButton *close_button = new QPushButton(tr("Close"), this);
	action_buttons->addWidget(d_commit_button);
	action_buttons->addWidget(close_button);
	main_layout->addLayout(action_buttons);

	QObject::connect(d_select_polygon_button, SIGNAL(clicked()), this, SLOT(handle_select_polygon()));
	QObject::connect(d_clear_polygons_button, SIGNAL(clicked()), this, SLOT(handle_clear_polygons()));
	QObject::connect(d_select_polyline_button, SIGNAL(clicked()), this, SLOT(handle_select_polyline()));
	QObject::connect(d_commit_button, SIGNAL(clicked()), this, SLOT(handle_commit()));
	QObject::connect(close_button, SIGNAL(clicked()), this, SLOT(close()));

	QObject::connect(
			&d_feature_focus,
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_focus_changed()));

	update_display();
}


void
GPlatesQtWidgets::SplitPlateDialog::pop_up()
{
	// Start clean each time it is opened, rather than resuming a selection the user has
	// probably forgotten about.
	d_operation->reset();
	d_operation->arm_polygon_selection();
	// A click is about to be needed on the globe. Make sure the tool that reports feature
	// focus changes is active - otherwise the click this dialog is instructing the user to
	// make would silently do nothing.
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display();

	show();
	activateWindow();
	raise();
}


void
GPlatesQtWidgets::SplitPlateDialog::closeEvent(
		QCloseEvent *event_)
{
	// Disarm on close, so a later click on the globe is not silently captured by an
	// operation the user believes they have finished with.
	d_operation->reset();
	update_highlight();
	GPlatesDialog::closeEvent(event_);
}


void
GPlatesQtWidgets::SplitPlateDialog::handle_select_polygon()
{
	const GPlatesViewOperations::SplitPlateOperation::Result result =
			d_operation->arm_polygon_selection();
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display(result.message);
}


void
GPlatesQtWidgets::SplitPlateDialog::handle_clear_polygons()
{
	d_operation->clear_polygons();
	update_display(tr("Cleared the selected polygons."));
}


void
GPlatesQtWidgets::SplitPlateDialog::handle_select_polyline()
{
	const GPlatesViewOperations::SplitPlateOperation::Result result =
			d_operation->arm_polyline_selection();
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display(result.message);
}


void
GPlatesQtWidgets::SplitPlateDialog::handle_commit()
{
	const GPlatesViewOperations::SplitPlateOperation::Result result = d_operation->commit();

	if (result.outcome == GPlatesViewOperations::SplitPlateOperation::SPLIT_COMPLETED)
	{
		// Leave the window open and armed for the next split - splitting several plates in a
		// row is the normal way this gets used.
		d_operation->arm_polygon_selection();
	}

	update_display(result.message);
}


void
GPlatesQtWidgets::SplitPlateDialog::handle_focus_changed()
{
	if (!isVisible())
	{
		return;
	}

	if (d_operation->selection_mode() == GPlatesViewOperations::SplitPlateOperation::NOT_SELECTING)
	{
		// Nothing armed, so this focus change is the user doing something else entirely.
		return;
	}

	const GPlatesViewOperations::SplitPlateOperation::Result result =
			d_operation->capture_armed_selection();

	// After capturing a polygon, stay armed for more - splitting several plates with the same
	// cutter in one pass is the point of supporting more than one. The user presses Choose
	// Cutting Polyline explicitly once they are done adding polygons.
	if (result.outcome == GPlatesViewOperations::SplitPlateOperation::POLYGON_CAPTURED)
	{
		d_operation->arm_polygon_selection();
	}

	update_display(result.message);
}


void
GPlatesQtWidgets::SplitPlateDialog::update_display(
		const QString &message)
{
	const bool has_polygon = d_operation->has_polygon();
	const bool has_polyline = d_operation->has_polyline();

	// The instruction always describes the next physical action, in the order it happens.
	if (!has_polygon)
	{
		d_instruction_label->setText(
				tr("Click the polygon on the globe that you want to split."));
	}
	else if (!has_polyline)
	{
		d_instruction_label->setText(
				tr("Add another polygon, or press Choose Cutting Polyline and click the polyline that cuts them."));
	}
	else
	{
		d_instruction_label->setText(
				tr("Press Commit to split the polygon(s), or choose a different polygon/polyline first."));
	}

	d_polygon_label->setText(tr("Polygon: %1").arg(d_operation->polygon_status()));
	d_polyline_label->setText(tr("Cutter: %1").arg(d_operation->polyline_status()));

	d_message_label->setText(message);

	d_clear_polygons_button->setEnabled(has_polygon);
	d_select_polyline_button->setEnabled(has_polygon);
	// Committing needs at least one polygon and a polyline captured.
	d_commit_button->setEnabled(has_polygon && has_polyline);

	update_highlight();
}


void
GPlatesQtWidgets::SplitPlateDialog::update_highlight()
{
	GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;

	d_highlight_layer_ptr->clear_rendered_geometries();

	const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> polygons =
			d_operation->polygons();
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
			polygon_iter = polygons.begin(); polygon_iter != polygons.end(); ++polygon_iter)
	{
		d_highlight_layer_ptr->add_rendered_geometry(
				GPlatesViewOperations::RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						*polygon_iter,
						POLYGON_HIGHLIGHT_COLOUR,
						GPlatesViewOperations::RenderedGeometryFactory::DEFAULT_LINE_WIDTH_HINT,
						/*filled=*/true));
	}

	const boost::optional<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> polyline =
			d_operation->polyline();
	if (polyline)
	{
		d_highlight_layer_ptr->add_rendered_geometry(
				GPlatesViewOperations::RenderedGeometryFactory::create_rendered_polyline_on_sphere(
						polyline.get(),
						POLYLINE_HIGHLIGHT_COLOUR,
						3.0f));
	}

	d_highlight_layer_ptr->set_active(!polygons.empty() || polyline);
}
