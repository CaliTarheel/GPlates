/* $Id$ */

/**
 * \file 
 * $Revision$
 * $Date$ 
 * 
 * Copyright (C) 2006, 2007, 2008, 2009, 2010, 2011 The University of Sydney, Australia
 * Copyright (C) 2007, 2008, 2009, 2010, 2011 Geological Survey of Norway
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
 
#ifndef GPLATES_QTWIDGETS_VIEWPORTWINDOW_H
#define GPLATES_QTWIDGETS_VIEWPORTWINDOW_H

#include <list>
#include <memory>
#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <QTimer>
#include <QCloseEvent>
#include <QMainWindow>
#include <QPoint>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUndoGroup>

#include "ui_ViewportWindowUi.h"

#include "canvas-tools/CanvasTool.h"

#include "gui/CanvasToolWorkflows.h"

#include "model/FeatureHandle.h"


class QLabel;
class QPushButton;
class QDialog;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;


namespace GPlatesAppLogic
{
	class ApplicationState;
	class FeatureCollectionFileIO;
}

namespace GPlatesCanvasTools
{
	class GeometryOperationState;
	class MeasureDistanceState;
	class ModifyGeometryState;
}

namespace GPlatesFileIO
{
	struct ReadErrorAccumulation;
}

namespace GPlatesGui
{
	class Dialogs;
	class DockState;
	class EnableCanvasTool;
	class FeatureFocus;
	class FileIOFeedback;
	class FullScreenMode;
	class ImportMenu;
	class SessionMenu;
	class TrinketArea;
	class UnsavedChangesTracker;
	class UtilitiesMenu;
}

namespace GPlatesMaths
{
	class PointOnSphere;
}

namespace GPlatesPresentation
{
	class ViewState;
	class VisualLayer;
}

namespace GPlatesViewOperations
{
	class AdvancePlateMotionOperation;
	class BooleanPolygonOperation;
	class CloneOperation;
	class CollisionOrogenyOperation;
	class CreateInitialContinentOperation;
	class CreateInitialRotationFileOperation;
	class CreateOceanCrustOperation;
	class CreatePacificPlateOperation;
	class CreateTripleJunctionCrustOperation;
	class CratonPlateIdLabels;
	class DeleteFeatureOperation;
	class GenerateInitialSubductionOperation;
	class GenerateMantleEventsOperation;
	class GenerateSubductionEffectsOperation;
	class MakeRiftOperation;
	class NaturalizeCoastlineOperation;
	class PlateDirectionArrowsOperation;
	class PlateIdReassignmentOperation;
	class ProposeInitialRiftsOperation;
	class PostCollisionRiftOperation;
	class RotationFileEditorOperation;
	class SplitPlateOperation;
	class SubductionCutterOperation;
}

namespace GPlatesQtWidgets
{
	class CanvasToolBarDockWidget;
	class DockWidget;
	class GlobeCanvas;
	class MapView;
	class PythonConsoleDialog;
	class ReconstructionViewWidget;
	class SearchResultsDockWidget;
	class ProjectDocumentsDockWidget;
	class SmallCircleManager;
	class TaskPanel;

	class ViewportWindow :
			public QMainWindow, 
			protected Ui_ViewportWindow
	{
		Q_OBJECT
		
	public:

		explicit
		ViewportWindow(
				GPlatesAppLogic::ApplicationState &application_state,
				GPlatesPresentation::ViewState &view_state);

		virtual
		~ViewportWindow();


		/**
		 * Loads the specified project file as a convenient alternative to having to
		 * explicitly load it by accessing the GUI.
		 */
		void
		load_project(
				const QString &project_filename);


		/**
		 * Loads the specified feature collection files as a convenient alternative to having to
		 * explicitly load them by accessing the GUI.
		 */
		void
		load_feature_collections(
				const QStringList &filenames);


		/**
		 * Shows the main window.
		 *
		 * Internally this calls QMainWindow::show() and then calls functions that rely
		 * on the main window being visible (such as activating the default canvas tool which
		 * checks for visibility of the main view canvas).
		 */
		void
		display();


		//! Returns the application state.
		GPlatesAppLogic::ApplicationState &
		get_application_state();

		//! Returns the view state.
		GPlatesPresentation::ViewState &
		get_view_state();

		/**
		 * Gives the Worldbuilding Pasta MOR selection first refusal on a
		 * Shift-click. Returns false so other Shift-click behaviour can continue
		 * when the focused feature is not a supported half-stage MOR.
		 */
		bool
		try_select_worldbuilding_mor();

		/** Gives an armed Pacific-plate seed capture first refusal on an ordinary click. */
		bool
		try_capture_pacific_void_seed(
				const GPlatesMaths::PointOnSphere &point_on_sphere,
				bool is_on_earth);

		ReconstructionViewWidget &
		reconstruction_view_widget();

		const ReconstructionViewWidget &
		reconstruction_view_widget() const;

		CanvasToolBarDockWidget &
		canvas_tool_bar_dock_widget();

		SearchResultsDockWidget &
		search_results_dock_widget();

		ProjectDocumentsDockWidget &
		project_documents_dock_widget();

		GlobeCanvas &
		globe_canvas();

		const GlobeCanvas &
		globe_canvas() const;

		MapView &
		map_view();

		const MapView &
		map_view() const;
		
		
		/**
		 * Accessor for the Dialogs class which manages all the instances of major dialogs/windows
		 * that would ordinarily hang off of ViewportWindow and clutter things up.
		 */
		GPlatesGui::Dialogs &
		dialogs() const;

		GPlatesGui::FileIOFeedback &
		file_io_feedback();

		GPlatesGui::CanvasToolWorkflows &
		canvas_tool_workflows();


		GPlatesGui::TrinketArea &
		trinket_area();

		/** Get a pointer to the TaskPanel */
		TaskPanel *
		task_panel_ptr() const;

		GPlatesGui::ImportMenu &
		import_menu();

		GPlatesGui::UtilitiesMenu &
		utilities_menu();

		/** Shows statistics for the currently focused feature at a global screen position. */
		void
		show_focused_feature_context_menu(
				const QPoint &global_position);

	public Q_SLOTS:
		
		void
		status_message(
				const QString &message,
				int timeout = 20000);

		void
		enable_or_disable_feature_actions(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		remember_created_feature(
				GPlatesModel::FeatureHandle::weak_ref feature);

		void
		select_last_created_feature();

		void
		handle_load_symbol_file();

		void 
		handle_unload_symbol_file();

		void
		update_tools_and_status_message();

		void
		handle_read_errors(
				const GPlatesFileIO::ReadErrorAccumulation &new_read_errors);

		/**
		 * Add secret menu filled with actions aid GUI-related debugging.
		 * Triggered from gplates_main and commandline switch --debug-gui.
		 */
		void
		install_gui_debug_menu();

		void
		hide_symbol_menu()
		{
			action_Load_Symbol->setVisible(false);
			action_Unload_Symbol->setVisible(false);
		}

		void
		hide_python_menu()
		{
			action_Open_Python_Console->setVisible(false);
		}

	protected:
	
		/**
		 * A reimplementation of QWidget::closeEvent() to allow closure to be postponed.
		 * To request program termination in the same manner as using the window manager's
		 * 'close' button, you should call ViewportWindow::close().
		 */
		virtual
		void
		closeEvent(QCloseEvent *close_event);

		/**
		 * Reimplementation of drag/drop events so we can handle users dragging files onto
		 * GPlates main window.
		 */
		virtual
		void
		dragEnterEvent(
				QDragEnterEvent *ev);

		/**
		 * Reimplementation of drag/drop events so we can handle users dragging files onto
		 * GPlates main window.
		 */
		virtual
		void
		dropEvent(
				QDropEvent *ev);


		virtual
		void
		showEvent(
				QShowEvent *ev);

	private:

		/**
		 * Connects all the Signal/Slot relationships for ViewportWindow toolbar
		 * buttons and menu items.
		 */
		void
		connect_menu_actions();

		void
		connect_file_menu_actions();

		void
		connect_edit_menu_actions();

		void
		connect_view_menu_actions();

		void
		connect_features_menu_actions();

		void
		connect_reconstruction_menu_actions();

		void
		connect_utilities_menu_actions();

		void
		connect_tools_menu_actions();

		void
		connect_world_building_menu_actions();

		void
		connect_window_menu_actions();

		void
		connect_help_menu_actions();

		/**
		 * Copies the menu structure found in ViewportWindow's menu bar into the
		 * special full-screen-mode 'GMenu' button.
		 */
		void
		populate_gmenu_from_menubar();

		/**
		 * Configures the ActionButtonBox inside the Feature tab of the Task Panel
		 * with some of the QActions that ViewportWindow has on the menu bar.
		 */
		void
		set_up_task_panel_actions();

		void
		set_window_title(
				boost::optional<QString> project_filename = boost::none);

		void
		show_feature_context_menu_at_point(
				const GPlatesMaths::PointOnSphere &point_on_sphere,
				double proximity_inclusion_threshold);

	private Q_SLOTS:

		void
		set_visual_layers_dialog_visibility(
				bool visible);

		void
		handle_window_menu_about_to_show();

		void
		enable_static_point_display();

		void
		enable_static_line_display();

		void
		enable_static_polygon_display();

		void
		enable_static_multipoint_display();

		void
		enable_velocity_arrow_display();

		void
		enable_topological_section_display();

		void
		enable_topological_line_display();

		void
		enable_topological_polygon_display();

		void
		enable_topological_network_display();

		void
		enable_raster_display();

		void
		enable_3d_scalar_field_display();

		void
		enable_scalar_coverage_display();

		void
		enable_all_geometries_display();

		void
		handle_render_settings_changed();

		void
		enable_stars_display();

		void
		handle_move_camera_up();

		void
		handle_move_camera_down();

		void
		handle_move_camera_left();

		void
		handle_move_camera_right();

		void
		handle_rotate_camera_clockwise();

		void
		handle_rotate_camera_anticlockwise();

		void
		handle_reset_camera_orientation();

		void
		handle_canvas_tool_activated(
				GPlatesGui::CanvasToolWorkflows::WorkflowType workflow,
				GPlatesGui::CanvasToolWorkflows::ToolType tool);

		void
		handle_globe_feature_context_menu(
				const GPlatesMaths::PointOnSphere &click_position,
				const GPlatesMaths::PointOnSphere &oriented_click_position,
				bool is_on_globe,
				Qt::MouseButton button,
				Qt::KeyboardModifiers modifiers);

		void
		handle_map_feature_context_menu(
				const QPointF &click_position,
				bool is_on_surface,
				Qt::MouseButton button,
				Qt::KeyboardModifiers modifiers);

		void
		handle_changed_project_filename(
				boost::optional<QString> project_filename);

		void
		show_menu_item_status_tip_in_status_bar();

		void
		pop_up_import_raster_dialog();

		void
		pop_up_import_raster_dialog(
				bool time_dependent_raster);

		void
		pop_up_import_time_dependent_raster_dialog();

		void
		pop_up_import_scalar_field_3d_dialog();

		void
		handle_colour_scheme_delegator_changed();

		void
		handle_visual_layer_added(
				size_t index);

		void
		open_new_window();

		void
		pop_up_background_colour_picker();

		void
		clone_feature_with_dialog();

		void
		update_undo_action_tooltip();

		void
		update_redo_action_tooltip();

		void
		open_online_documentation();

		void
		pop_up_python_console();

		void
		handle_create_initial_continent();

		void
		show_boolean_polygons_window();

		void
		handle_boolean_select_first();

		void
		handle_boolean_select_operand();

		void
		handle_boolean_remove_operand();

		void
		handle_boolean_clear_operands();

		void
		handle_boolean_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_boolean_preview();

		void
		handle_boolean_apply();

		void
		handle_boolean_cancel();

		void
		update_boolean_palette(
				const QString &message = QString());

		void
		handle_propose_initial_rifts();

		void
		show_make_rift_window();

		void
		handle_make_rift_select_continent();

		void
		handle_make_rift_select_rift();

		void
		handle_make_rift_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_make_rift();

		void
		show_initial_subduction_window();

		void
		handle_initial_subduction_select_continent();

		void
		handle_initial_subduction_select_mor();

		void
		handle_initial_subduction_cancel_selection();

		void
		handle_initial_subduction_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_generate_initial_subduction();

		void
		show_subduction_effects_window();

		void
		handle_subduction_effects_select_subduction();

		void
		handle_subduction_effects_select_continent();

		void
		handle_subduction_effects_clear_continent();

		void
		handle_subduction_effects_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_subduction_effects_preview();

		void
		handle_subduction_effects_commit();

		void
		handle_subduction_effects_controls_changed();

		void
		handle_subduction_effect_type_changed(
				int index);

		void
		show_collision_orogeny_window();

		void
		handle_collision_select_incoming();

		void
		handle_collision_select_receiving();

		void
		handle_collision_select_trench();

		void
		handle_collision_clear_trench();

		void
		handle_collision_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_collision_preview();

		void
		handle_collision_commit();

		void
		handle_collision_controls_changed();

		void
		handle_collision_type_changed(
				int index);

		void
		show_post_collision_rift_window();

		void
		handle_post_collision_rift_select_host();

		void
		handle_post_collision_rift_select_suture();

		void
		handle_post_collision_rift_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_post_collision_rift_preview();

		void
		handle_post_collision_rift_commit();

		void
		handle_post_collision_rift_controls_changed();

		void
		show_mantle_events_window();

		void
		handle_mantle_events_select_continent();

		void
		handle_mantle_events_select_rift();

		void
		handle_mantle_events_clear_rift();

		void
		handle_mantle_events_focus_changed(
				GPlatesGui::FeatureFocus &feature_focus);

		void
		handle_mantle_events_preview();

		void
		handle_mantle_events_commit();

		void
		handle_mantle_events_controls_changed();

		void
		handle_advance_plate_motion();

		void
		handle_create_ocean_crust();

		void
		handle_create_triple_junction_crust();

		void
		handle_create_pacific_plate();

		void
		handle_create_initial_rotation_file();

		void
		apply_artifexia_preset();

		void
		handle_split_plate();

		void
		handle_naturalize_coastline();

		void
		handle_subduction_cutter();

		void
		handle_rotation_file_editor();

		void
		open_dataset_webpage();

		void
		update_make_rift_palette(
				const QString &message = QString());

		void
		update_post_collision_rift_palette(
				const QString &message = QString());

		void
		update_mantle_events_palette(
				const QString &message = QString());

		void
		update_initial_subduction_palette(
				const QString &message = QString());

		void
		update_subduction_effects_palette(
				const QString &message = QString());

		void
		update_collision_palette(
				const QString &message = QString());
		
	private:

		//
		// Some pointers below are QPointer and some are boost::scoped_ptr.
		//
		// QPointer is used when deletion of the object is taken care of because it has a parent
		// (and the object inherits from QObject since QPointer only works with QObject derivations).
		//
		// boost::scoped_ptr is used when the object must be deleted because no one owns it.
		// The object could be a regular object or one derived from QObject.
		// In the case of QObject it must be explicitly deleted because it has no parent.
		//
		// Note: Apparently the Qt memory management system can detect if an object
		// (derived from QObject) has already been deleted (prematurely) and avoid a double-delete.
		// It's a nice safeguard in case of a programming error, but it shouldn't be relied upon.
		//

		GPlatesAppLogic::ApplicationState &d_application_state;
		GPlatesPresentation::ViewState &d_view_state;

		//! The state targeted by geometry operation canvas tools and displayed in task panel.
		boost::scoped_ptr<GPlatesCanvasTools::GeometryOperationState> d_geometry_operation_state_ptr;

		//! The state targeted by canvas tools that modify geometry and displayed in task panel.
		boost::scoped_ptr<GPlatesCanvasTools::ModifyGeometryState> d_modify_geometry_state;

		//! The state targeted by measure distance canvas tool and displayed in task panel.
		boost::scoped_ptr<GPlatesCanvasTools::MeasureDistanceState> d_measure_distance_state_ptr;

		//! The selected canvas tool state.
		boost::scoped_ptr<GPlatesGui::CanvasToolWorkflows> d_canvas_tool_workflows;

		//! For cloning a feature.
		boost::scoped_ptr<GPlatesViewOperations::CloneOperation> d_clone_operation_ptr;

		//! For persistent union, subtraction, intersection and symmetric difference of polygons.
		boost::scoped_ptr<GPlatesViewOperations::BooleanPolygonOperation> d_boolean_polygon_operation_ptr;

		//! For reviewing continent collisions, sutures and collisional orogenies.
		boost::scoped_ptr<GPlatesViewOperations::CollisionOrogenyOperation> d_collision_orogeny_operation_ptr;

		//! For rerifting a welded assemblage near an inherited suture.
		boost::scoped_ptr<GPlatesViewOperations::PostCollisionRiftOperation> d_post_collision_rift_operation_ptr;

		//! For reviewable LIP, hotspot and native MotionPath generation.
		boost::scoped_ptr<GPlatesViewOperations::GenerateMantleEventsOperation> d_generate_mantle_events_operation_ptr;

		//! For deleting a feature.
		boost::scoped_ptr<GPlatesViewOperations::DeleteFeatureOperation> d_delete_feature_operation_ptr;

		//! For generating a Worldbuilding Pasta initial continent and its cratons.
		boost::scoped_ptr<GPlatesViewOperations::CreateInitialContinentOperation> d_create_initial_continent_operation_ptr;

		//! For creating, validating, saving and loading the initial plate circuit.
		boost::scoped_ptr<GPlatesViewOperations::CreateInitialRotationFileOperation> d_create_initial_rotation_file_operation_ptr;

		//! For proposing the next younger rotation poles and turn-specific flowlines.
		boost::scoped_ptr<GPlatesViewOperations::AdvancePlateMotionOperation> d_advance_plate_motion_operation_ptr;

		//! For creating a separate editable ocean-crust age band from recorded plate motion.
		boost::scoped_ptr<GPlatesViewOperations::CreateOceanCrustOperation> d_create_ocean_crust_operation_ptr;

		//! RRR mode sharing the MOR selection and ocean-crust builder with the basic workflow.
		boost::scoped_ptr<GPlatesViewOperations::CreateTripleJunctionCrustOperation> d_create_triple_junction_crust_operation_ptr;

		//! Local seeded plate-birth mode sharing the MOR selection and geometry services.
		boost::scoped_ptr<GPlatesViewOperations::CreatePacificPlateOperation> d_create_pacific_plate_operation_ptr;

		//! For proposing, reviewing and committing an initial rift system.
		boost::scoped_ptr<GPlatesViewOperations::ProposeInitialRiftsOperation> d_propose_initial_rifts_operation_ptr;

		//! For persistently selecting a continent and arbitrary rift polyline, then cutting it.
		boost::scoped_ptr<GPlatesViewOperations::MakeRiftOperation> d_make_rift_operation_ptr;

		//! For deriving a broad opposite-margin trench from a rifted continent and half-stage MOR.
		boost::scoped_ptr<GPlatesViewOperations::GenerateInitialSubductionOperation> d_generate_initial_subduction_operation_ptr;

		//! For reviewing and committing island arcs and active-margin mountain belts.
		boost::scoped_ptr<GPlatesViewOperations::GenerateSubductionEffectsOperation> d_generate_subduction_effects_operation_ptr;

		//! Automatically displays each visible Craton feature's reconstruction plate ID.
		boost::scoped_ptr<GPlatesViewOperations::CratonPlateIdLabels> d_craton_plate_id_labels_ptr;

		//! For splitting a polygon feature with a selected polyline.
		boost::scoped_ptr<GPlatesViewOperations::SplitPlateOperation> d_split_plate_operation_ptr;

		//! For roughening long coastline sections while preserving shared geometry.
		boost::scoped_ptr<GPlatesViewOperations::NaturalizeCoastlineOperation> d_naturalize_coastline_operation_ptr;

		//! For chronologically cutting subducting plate polygons beneath an overriding plate.
		boost::scoped_ptr<GPlatesViewOperations::SubductionCutterOperation> d_subduction_cutter_operation_ptr;

		//! For motion-preserving plate circuit edits in loaded rotation collections.
		boost::scoped_ptr<GPlatesViewOperations::RotationFileEditorOperation> d_rotation_file_editor_operation_ptr;

		boost::scoped_ptr<GPlatesViewOperations::PlateIdReassignmentOperation> d_plate_id_reassignment_operation_ptr;

		//! For drawing direct plate-motion arrows on visible features.
		boost::scoped_ptr<GPlatesViewOperations::PlateDirectionArrowsOperation> d_plate_direction_arrows_operation_ptr;


		/**
		 * Manages all the major dialogs that would otherwise clutter up ViewportWindow.
		 *
		 * NOTE: This is one of the first data members other data members rely on it and
		 * its constructor doesn't initialise any dialog so its creation has few dependencies.
		 */
		QPointer<GPlatesGui::Dialogs> d_dialogs_ptr;

		//! Handles transitions to/from fullscreen mode.
		QPointer<GPlatesGui::FullScreenMode> d_full_screen_mode;

		//! Manages the icons in the status bar
		QPointer<GPlatesGui::TrinketArea> d_trinket_area_ptr;

		/**
		 * Tracks changes to saved/unsaved status of files and manages user notification of same.
		 *
		 * QPointer is a guarded pointer which will be set to null when the QObject it points to
		 * gets deleted; The UnsavedChangesTracker is parented to ViewportWindow, so the Qt
		 * object system handles cleanup, and so that I have easier access to it via GuiDebug.
		 */
		QPointer<GPlatesGui::UnsavedChangesTracker> d_unsaved_changes_tracker_ptr;

		/**
		 * Wraps file loading and saving, opening dialogs appropriately for filenames and error feedback.
		 * Can later provide save/load progress reports to progress bars in GUI.
		 *
		 * QPointer is a guarded pointer which will be set to null when the QObject it points to
		 * gets deleted; The FileIOFeedback is parented to ViewportWindow, so the Qt
		 * object system handles cleanup, and so that I have easier access to it via GuiDebug.
		 */
		QPointer<GPlatesGui::FileIOFeedback> d_file_io_feedback_ptr;

		/**
		 * Manages the Open Recent Session menu.
		 */
		QPointer<GPlatesGui::SessionMenu> d_session_menu_ptr;

		/**
		 * Encapsulates logic regarding the Import submenu of the File menu.
		 */
		QPointer<GPlatesGui::ImportMenu> d_import_menu_ptr;

		/**
		 * Allows Python scripts to be run from the Utilities menu.
		 */
		QPointer<GPlatesGui::UtilitiesMenu> d_utilities_menu_ptr;

		/**
		 * Deals with all the micro-management of the ViewportWindow's docks.
		 */
		QPointer<GPlatesGui::DockState> d_dock_state_ptr;

		/**
		 * A tabbed search results dock widget.
		 */
		QPointer<SearchResultsDockWidget> d_search_results_dock_ptr;

		/**
		 * A tabbed toolbar for the canvas tools.
		 */
		QPointer<CanvasToolBarDockWidget> d_canvas_tools_dock_ptr;

		//! Associated Markdown documents, editor/preview and project metadata status.
		QPointer<ProjectDocumentsDockWidget> d_project_documents_dock_ptr;

		//! Floating palette for procedural worldbuilding operations.
		QPointer<QDockWidget> d_worldbuilding_pasta_dock_ptr;

		//! Persistent modeless polygon Boolean workflow.
		QPointer<QDialog> d_boolean_polygon_dialog_ptr;
		QPointer<QPushButton> d_boolean_select_first_button_ptr;
		QPointer<QPushButton> d_boolean_select_operand_button_ptr;
		QPointer<QPushButton> d_boolean_remove_operand_button_ptr;
		QPointer<QPushButton> d_boolean_clear_operands_button_ptr;
		QPointer<QPushButton> d_boolean_preview_button_ptr;
		QPointer<QPushButton> d_boolean_apply_button_ptr;
		QPointer<QPushButton> d_boolean_cancel_button_ptr;
		QPointer<QLabel> d_boolean_first_status_label_ptr;
		QPointer<QLabel> d_boolean_operands_status_label_ptr;
		QPointer<QLabel> d_boolean_instruction_label_ptr;
		QPointer<QComboBox> d_boolean_operation_combo_ptr;

		//! Persistent modeless workflow window launched by the palette's Make Rift button.
		QPointer<QDialog> d_make_rift_dialog_ptr;
		QPointer<QPushButton> d_make_rift_select_continent_button_ptr;
		QPointer<QPushButton> d_make_rift_select_rift_button_ptr;
		QPointer<QPushButton> d_make_rift_cut_button_ptr;
		QPointer<QLabel> d_make_rift_continent_status_label_ptr;
		QPointer<QLabel> d_make_rift_rift_status_label_ptr;
		QPointer<QLabel> d_make_rift_instruction_label_ptr;

		//! Persistent modeless workflow window for the first active-margin step.
		QPointer<QDialog> d_initial_subduction_dialog_ptr;
		QPointer<QPushButton> d_initial_subduction_select_continent_button_ptr;
		QPointer<QPushButton> d_initial_subduction_select_mor_button_ptr;
		QPointer<QPushButton> d_initial_subduction_generate_button_ptr;
		QPointer<QLabel> d_initial_subduction_continent_status_label_ptr;
		QPointer<QLabel> d_initial_subduction_mor_status_label_ptr;
		QPointer<QLabel> d_initial_subduction_instruction_label_ptr;

		//! Persistent review window for subduction-driven island arcs and mountain belts.
		QPointer<QDialog> d_subduction_effects_dialog_ptr;
		QPointer<QPushButton> d_subduction_effects_select_subduction_button_ptr;
		QPointer<QPushButton> d_subduction_effects_select_continent_button_ptr;
		QPointer<QPushButton> d_subduction_effects_clear_continent_button_ptr;
		QPointer<QPushButton> d_subduction_effects_preview_button_ptr;
		QPointer<QPushButton> d_subduction_effects_commit_button_ptr;
		QPointer<QLabel> d_subduction_effects_subduction_status_label_ptr;
		QPointer<QLabel> d_subduction_effects_continent_status_label_ptr;
		QPointer<QLabel> d_subduction_effects_instruction_label_ptr;
		QPointer<QComboBox> d_subduction_effect_type_combo_ptr;
		QPointer<QCheckBox> d_subduction_effects_flip_polarity_check_ptr;
		QPointer<QCheckBox> d_subduction_effects_early_arc_check_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_arc_delay_spin_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_trim_start_spin_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_trim_end_spin_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_offset_spin_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_irregularity_spin_ptr;
		QPointer<QDoubleSpinBox> d_subduction_effects_belt_width_spin_ptr;

		//! Persistent review window for continental collision and collisional orogeny.
		QPointer<QDialog> d_collision_dialog_ptr;
		QPointer<QPushButton> d_collision_select_incoming_button_ptr;
		QPointer<QPushButton> d_collision_select_receiving_button_ptr;
		QPointer<QPushButton> d_collision_select_trench_button_ptr;
		QPointer<QPushButton> d_collision_clear_trench_button_ptr;
		QPointer<QPushButton> d_collision_preview_button_ptr;
		QPointer<QPushButton> d_collision_commit_button_ptr;
		QPointer<QLabel> d_collision_incoming_status_label_ptr;
		QPointer<QLabel> d_collision_receiving_status_label_ptr;
		QPointer<QLabel> d_collision_trench_status_label_ptr;
		QPointer<QLabel> d_collision_instruction_label_ptr;
		QPointer<QComboBox> d_collision_type_combo_ptr;
		QPointer<QSpinBox> d_collision_precursor_spin_ptr;
		QPointer<QDoubleSpinBox> d_collision_contact_threshold_spin_ptr;
		QPointer<QCheckBox> d_collision_auto_width_check_ptr;
		QPointer<QDoubleSpinBox> d_collision_belt_width_spin_ptr;
		QPointer<QSpinBox> d_collision_smoothing_spin_ptr;
		QPointer<QDoubleSpinBox> d_collision_irregularity_spin_ptr;
		QPointer<QDoubleSpinBox> d_collision_active_duration_spin_ptr;
		QPointer<QDoubleSpinBox> d_collision_old_age_spin_ptr;
		QPointer<QCheckBox> d_collision_terminate_trench_check_ptr;
		QPointer<QCheckBox> d_collision_allow_nonconvergent_check_ptr;
		QPointer<QCheckBox> d_collision_deform_margins_check_ptr;
		QPointer<QDoubleSpinBox> d_collision_deformation_reach_spin_ptr;
		QPointer<QCheckBox> d_collision_retire_incoming_check_ptr;

		//! Persistent post-collision suture-reactivation and rerifting review.
		QPointer<QDialog> d_post_collision_rift_dialog_ptr;
		QPointer<QPushButton> d_post_collision_rift_select_host_button_ptr;
		QPointer<QPushButton> d_post_collision_rift_select_suture_button_ptr;
		QPointer<QPushButton> d_post_collision_rift_preview_button_ptr;
		QPointer<QPushButton> d_post_collision_rift_commit_button_ptr;
		QPointer<QLabel> d_post_collision_rift_host_status_label_ptr;
		QPointer<QLabel> d_post_collision_rift_suture_status_label_ptr;
		QPointer<QLabel> d_post_collision_rift_instruction_label_ptr;
		QPointer<QDoubleSpinBox> d_post_collision_rift_offset_spin_ptr;
		QPointer<QDoubleSpinBox> d_post_collision_rift_wiggle_spin_ptr;
		QPointer<QDoubleSpinBox> d_post_collision_rift_segment_spin_ptr;
		QPointer<QDoubleSpinBox> d_post_collision_rift_extension_spin_ptr;
		QPointer<QComboBox> d_post_collision_rift_side_combo_ptr;
		QPointer<QSpinBox> d_post_collision_rift_seed_spin_ptr;
		QPointer<QLineEdit> d_post_collision_rift_left_name_ptr;
		QPointer<QLineEdit> d_post_collision_rift_right_name_ptr;
		QPointer<QSpinBox> d_post_collision_rift_left_plate_spin_ptr;
		QPointer<QSpinBox> d_post_collision_rift_right_plate_spin_ptr;

		//! Persistent LIP, hotspot and native hotspot-trail review.
		QPointer<QDialog> d_mantle_events_dialog_ptr;
		QPointer<QPushButton> d_mantle_events_select_continent_button_ptr;
		QPointer<QPushButton> d_mantle_events_select_rift_button_ptr;
		QPointer<QPushButton> d_mantle_events_clear_rift_button_ptr;
		QPointer<QPushButton> d_mantle_events_preview_button_ptr;
		QPointer<QPushButton> d_mantle_events_commit_button_ptr;
		QPointer<QLabel> d_mantle_events_continent_status_label_ptr;
		QPointer<QLabel> d_mantle_events_rift_status_label_ptr;
		QPointer<QLabel> d_mantle_events_instruction_label_ptr;
		QPointer<QComboBox> d_mantle_events_placement_combo_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_diameter_spin_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_irregularity_spin_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_segment_spin_ptr;
		QPointer<QSpinBox> d_mantle_events_seed_spin_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_active_duration_spin_ptr;
		QPointer<QCheckBox> d_mantle_events_create_hotspot_check_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_hotspot_lifetime_spin_ptr;
		QPointer<QDoubleSpinBox> d_mantle_events_trail_step_spin_ptr;
		QPointer<QSpinBox> d_mantle_events_mantle_plate_spin_ptr;

		/**
		 * The central widget in the main window containing everything except the menubar,
		 * search results dock and canvas tools dock.
		 */
		QPointer<ReconstructionViewWidget> d_reconstruction_view_widget_ptr;

		/**
		 * Depends on FeatureFocus, Model, topology sections container.
		 * Is parented by 'this' - Qt will clean up when 'this' is destroyed.
		 */
		QPointer<TaskPanel> d_task_panel_ptr;

		QPointer<QAction> d_undo_action_ptr;

		QPointer<QAction> d_redo_action_ptr;

		QPointer<QAction> d_select_last_created_feature_action;
		GPlatesModel::FeatureHandle::weak_ref d_last_created_feature;

		// To prevent infinite loops.
		bool d_inside_update_undo_action_tooltip;
		bool d_inside_update_redo_action_tooltip;
	};
}

#endif  // GPLATES_QTWIDGETS_VIEWPORTWINDOW_H
