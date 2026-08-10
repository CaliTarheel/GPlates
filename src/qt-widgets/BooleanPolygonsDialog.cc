/* $Id$ */

/**
 * \file
 * Persistent window driving the polygon Boolean workflow.
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

#include "BooleanPolygonsDialog.h"

#include "app-logic/ApplicationState.h"

#include "gui/CanvasToolWorkflows.h"
#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "view-operations/RenderedGeometryFactory.h"
#include "view-operations/SubductionCutterGeometry.h"


namespace
{
	// Amber for the first polygon - the one whose properties survive into the result, so it
	// reads as "the important one". Blue for everything added to it, so the two roles are never
	// ambiguous at a glance.
	const GPlatesGui::Colour FIRST_POLYGON_HIGHLIGHT_COLOUR(1.0f, 0.75f, 0.0f, 0.6f);
	const GPlatesGui::Colour OPERAND_POLYGON_HIGHLIGHT_COLOUR(0.2f, 0.5f, 1.0f, 0.6f);
}


GPlatesQtWidgets::BooleanPolygonsDialog::BooleanPolygonsDialog(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows,
		GPlatesViewOperations::RenderedGeometryCollection &rendered_geometry_collection,
		QWidget *parent_) :
	GPlatesDialog(parent_, Qt::Window),
	d_feature_focus(feature_focus),
	d_canvas_tool_workflows(canvas_tool_workflows),
	d_operation(new GPlatesViewOperations::BooleanPolygonOperation(feature_focus, application_state)),
	d_highlight_layer_ptr(
			rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
					GPlatesViewOperations::RenderedGeometryCollection::FEATURE_INSPECTION_CANVAS_TOOL_WORKFLOW_LAYER)),
	d_hole_warning_acknowledged(false)
{
	setWindowTitle(tr("Boolean Polygons"));

	QVBoxLayout *main_layout = new QVBoxLayout(this);

	// The instruction is the most important thing in the window, so it leads and it is the only
	// thing in bold. It always says what to do *now*, rather than describing the tool.
	d_instruction_label = new QLabel(this);
	d_instruction_label->setWordWrap(true);
	d_instruction_label->setStyleSheet("QLabel { font-weight: bold; }");
	main_layout->addWidget(d_instruction_label);

	QLabel *explanation = new QLabel(
			tr("Combine polygons into one shape. Pick the polygon to keep, then add the others to"
				" combine with it. Everything is chosen by clicking features on the globe, so this"
				" window stays open while you work."),
			this);
	explanation->setWordWrap(true);
	main_layout->addWidget(explanation);

	QFrame *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	main_layout->addWidget(separator);

	// Live state. Without this the user is trusting that their click registered.
	d_first_label = new QLabel(this);
	d_first_label->setWordWrap(true);
	main_layout->addWidget(d_first_label);

	d_operands_label = new QLabel(this);
	d_operands_label->setWordWrap(true);
	main_layout->addWidget(d_operands_label);

	QHBoxLayout *selection_buttons = new QHBoxLayout;
	d_select_first_button = new QPushButton(tr("Choose &First Polygon"), this);
	d_select_first_button->setToolTip(
			tr("Arm selection, then click the polygon on the globe that the others will be combined into."));
	d_add_polygon_button = new QPushButton(tr("&Add Polygon"), this);
	d_add_polygon_button->setToolTip(
			tr("Arm selection, then click another polygon on the globe. Repeat for as many as you want."));
	d_clear_button = new QPushButton(tr("&Clear Added"), this);
	d_clear_button->setToolTip(tr("Forget the added polygons, keeping the first."));
	selection_buttons->addWidget(d_select_first_button);
	selection_buttons->addWidget(d_add_polygon_button);
	selection_buttons->addWidget(d_clear_button);
	main_layout->addLayout(selection_buttons);

	QHBoxLayout *unify_buttons = new QHBoxLayout;
	d_unify_button = new QPushButton(tr("&Unify Matching Polygons"), this);
	d_unify_button->setToolTip(
			tr("Find every other polygon in this layer that shares the first polygon's Plate ID and"
				" age range, and union them all into it in one step - no need to click each one."));
	unify_buttons->addWidget(d_unify_button);
	unify_buttons->addStretch();
	main_layout->addLayout(unify_buttons);

	QHBoxLayout *operation_layout = new QHBoxLayout;
	operation_layout->addWidget(new QLabel(tr("Operation:"), this));
	d_operation_combo = new QComboBox(this);
	// Union leads because merging pieces into one shape is the common case.
	d_operation_combo->addItem(
			tr("Union - merge into one shape"),
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_UNION);
	d_operation_combo->addItem(
			tr("Subtract - cut the others out of the first"),
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_DIFFERENCE);
	d_operation_combo->addItem(
			tr("Intersect - keep only the overlap"),
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_INTERSECTION);
	d_operation_combo->addItem(
			tr("Symmetric difference - keep everything except the overlap"),
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_SYMMETRIC_DIFFERENCE);
	operation_layout->addWidget(d_operation_combo, 1);
	main_layout->addLayout(operation_layout);

	d_message_label = new QLabel(this);
	d_message_label->setWordWrap(true);
	main_layout->addWidget(d_message_label);

	QHBoxLayout *action_buttons = new QHBoxLayout;
	action_buttons->addStretch();
	d_apply_button = new QPushButton(tr("A&pply"), this);
	d_apply_button->setDefault(true);
	QPushButton *close_button = new QPushButton(tr("Close"), this);
	action_buttons->addWidget(d_apply_button);
	action_buttons->addWidget(close_button);
	main_layout->addLayout(action_buttons);

	QObject::connect(d_select_first_button, SIGNAL(clicked()), this, SLOT(handle_select_first()));
	QObject::connect(d_add_polygon_button, SIGNAL(clicked()), this, SLOT(handle_add_polygon()));
	QObject::connect(d_clear_button, SIGNAL(clicked()), this, SLOT(handle_clear()));
	QObject::connect(d_unify_button, SIGNAL(clicked()), this, SLOT(handle_unify()));
	QObject::connect(d_apply_button, SIGNAL(clicked()), this, SLOT(handle_apply()));
	QObject::connect(close_button, SIGNAL(clicked()), this, SLOT(close()));

	QObject::connect(
			&d_feature_focus,
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_focus_changed()));

	update_display();
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::pop_up()
{
	// Start clean each time it is opened, rather than resuming a selection the user has probably
	// forgotten about.
	d_operation->reset();
	d_operation->arm_first_selection();
	// A click is about to be needed on the globe. Whatever tool was active before this dialog
	// opened, make sure it is the one that reports feature focus changes - otherwise the click
	// this dialog is instructing the user to make would silently do nothing.
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display();

	show();
	activateWindow();
	raise();
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::closeEvent(
		QCloseEvent *event_)
{
	// Disarm on close, so a later click on the globe is not silently captured by an operation the
	// user believes they have finished with.
	d_operation->reset();
	update_highlight();
	GPlatesDialog::closeEvent(event_);
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_select_first()
{
	d_hole_warning_acknowledged = false;
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_operation->arm_first_selection();
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display(result.message);
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_add_polygon()
{
	d_hole_warning_acknowledged = false;
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_operation->arm_operand_selection();
	d_canvas_tool_workflows.choose_canvas_tool(GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION, GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
	update_display(result.message);
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_clear()
{
	d_hole_warning_acknowledged = false;
	d_operation->clear_operands();
	update_display(tr("Cleared the added polygons."));
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_unify()
{
	// Only the first polygon is checkable ahead of time - matching operands aren't known until
	// apply_unify() itself does the matching.
	if (!d_hole_warning_acknowledged && selection_has_holes())
	{
		d_hole_warning_acknowledged = true;
		update_display(tr(
				"The first polygon has an interior ring (hole). The Boolean clipping engine is not"
				" guaranteed to preserve holes correctly - click Unify Matching Polygons again to"
				" proceed anyway, or Choose First Polygon again to pick a different one."));
		return;
	}
	d_hole_warning_acknowledged = false;

	const GPlatesViewOperations::BooleanPolygonOperation::Result result = d_operation->apply_unify();

	if (result.outcome == GPlatesViewOperations::BooleanPolygonOperation::BOOLEAN_COMPLETED)
	{
		// Same re-arm as a normal Apply - combining several groups in a row is the normal way
		// this gets used.
		d_operation->arm_first_selection();
	}

	update_display(result.message);
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_apply()
{
	if (!d_hole_warning_acknowledged && selection_has_holes())
	{
		d_hole_warning_acknowledged = true;
		update_display(tr(
				"The first polygon or an added operand has an interior ring (hole). The Boolean"
				" clipping engine is not guaranteed to preserve holes correctly - click Apply again"
				" to proceed anyway, or Clear Added / re-choose to change the selection."));
		return;
	}
	d_hole_warning_acknowledged = false;

	const GPlatesViewOperations::SubductionCutterGeometry::BooleanOperation operation =
			static_cast<GPlatesViewOperations::SubductionCutterGeometry::BooleanOperation>(
					d_operation_combo->currentData().toInt());

	const GPlatesViewOperations::BooleanPolygonOperation::Result result = d_operation->apply(operation);

	if (result.outcome == GPlatesViewOperations::BooleanPolygonOperation::BOOLEAN_COMPLETED)
	{
		// Leave the window open and armed for the next one - combining several groups in a row is
		// the normal way this gets used, and closing would make that tedious.
		d_operation->arm_first_selection();
	}

	update_display(result.message);
}


bool
GPlatesQtWidgets::BooleanPolygonsDialog::selection_has_holes() const
{
	const boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> first =
			d_operation->first_polygon();
	if (first && (*first)->number_of_interior_rings() > 0)
	{
		return true;
	}
	const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> operands =
			d_operation->operand_polygons();
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
			operand_iter = operands.begin(); operand_iter != operands.end(); ++operand_iter)
	{
		if ((*operand_iter)->number_of_interior_rings() > 0)
		{
			return true;
		}
	}
	return false;
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::handle_focus_changed()
{
	if (!isVisible())
	{
		return;
	}

	if (d_operation->selection_mode() == GPlatesViewOperations::BooleanPolygonOperation::NOT_SELECTING)
	{
		// Nothing armed, so this focus change is the user doing something else entirely.
		return;
	}

	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_operation->capture_armed_selection();

	// After capturing the first polygon, arm for operands automatically. The user's next click is
	// almost certainly the next polygon, and making them press a button between every click would
	// be needless.
	if (result.outcome == GPlatesViewOperations::BooleanPolygonOperation::FIRST_CAPTURED ||
		result.outcome == GPlatesViewOperations::BooleanPolygonOperation::OPERAND_CAPTURED)
	{
		d_operation->arm_operand_selection();
	}

	update_display(result.message);
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::update_display(
		const QString &message)
{
	const bool has_first = d_operation->has_first();
	const unsigned int operand_count = d_operation->operand_count();

	// The instruction always describes the next physical action, in the order it happens.
	if (!has_first)
	{
		d_instruction_label->setText(
				tr("Click the polygon on the globe that the others will be combined into."));
	}
	else if (operand_count == 0)
	{
		d_instruction_label->setText(
				tr("Now click another polygon to combine with it. Click as many as you like."));
	}
	else
	{
		d_instruction_label->setText(
				tr("Click more polygons to add them, or press Apply to combine what you have."));
	}

	d_first_label->setText(tr("First polygon: %1").arg(d_operation->first_status()));
	d_operands_label->setText(tr("Added: %1").arg(d_operation->operands_status()));

	d_message_label->setText(message);

	d_add_polygon_button->setEnabled(has_first);
	d_clear_button->setEnabled(operand_count > 0);
	d_unify_button->setEnabled(has_first);
	// Every operation needs a first polygon and at least one other to act on.
	d_apply_button->setEnabled(has_first && operand_count > 0);

	update_highlight();
}


void
GPlatesQtWidgets::BooleanPolygonsDialog::update_highlight()
{
	GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;

	d_highlight_layer_ptr->clear_rendered_geometries();

	const boost::optional<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> first =
			d_operation->first_polygon();
	if (first)
	{
		d_highlight_layer_ptr->add_rendered_geometry(
				GPlatesViewOperations::RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						first.get(),
						FIRST_POLYGON_HIGHLIGHT_COLOUR,
						GPlatesViewOperations::RenderedGeometryFactory::DEFAULT_LINE_WIDTH_HINT,
						/*filled=*/true));
	}

	const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> operands =
			d_operation->operand_polygons();
	for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
			operand_iter = operands.begin(); operand_iter != operands.end(); ++operand_iter)
	{
		d_highlight_layer_ptr->add_rendered_geometry(
				GPlatesViewOperations::RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						*operand_iter,
						OPERAND_POLYGON_HIGHLIGHT_COLOUR,
						GPlatesViewOperations::RenderedGeometryFactory::DEFAULT_LINE_WIDTH_HINT,
						/*filled=*/true));
	}

	d_highlight_layer_ptr->set_active(first || !operands.empty());
}
