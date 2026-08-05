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

#if defined(_MSC_VER) && _MSC_VER <= 1400
////Visual C++ 2005
#pragma warning( disable : 4005 )
#endif 

#include <algorithm>
#include <iostream>
#include <iterator>
#include <memory>
#include <boost/format.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/bind/bind.hpp>

#include <QActionGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QGroupBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QMenu>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "ViewportWindow.h"

#include "ActionButtonBox.h"
#include "CanvasToolBarDockWidget.h"
#include "ChooseFeatureCollectionDialog.h"
#include "CreateFeatureDialog.h"
#include "DigitisationWidget.h"
#include "DockWidget.h"
#include "DrawStyleDialog.h"
#include "FeaturePropertiesDialog.h"
#include "FeatureTypeDisplayPreferences.h"
#include "GlobeCanvas.h"
#include "GlobeAndMapWidget.h"
#include "ImportRasterDialog.h"
#include "ImportScalarField3DDialog.h"
#include "MapCanvas.h"
#include "MapView.h"
#include "PythonConsoleDialog.h"
#include "QtWidgetUtils.h"
#include "ReadErrorAccumulationDialog.h"
#include "ReconstructionViewWidget.h"
#include "SaveFileDialog.h"
#include "SearchResultsDockWidget.h"
#include "ProjectDocumentsDockWidget.h"
#include "TaskPanel.h"
#include "VisualLayersDialog.h"

#include "api/DeferredApiCall.h"
#include "api/PythonInterpreterLocker.h"
#include "api/PythonUtils.h"
#include "api/Sleeper.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/PlanetaryParameters.h"
#include "app-logic/AppLogicUtils.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/PlateVelocityUtils.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/UserPreferences.h"
#include "app-logic/VelocityDeltaTime.h"
#include "app-logic/WorldbuildingProjectManifest.h"

#include "canvas-tools/GeometryOperationState.h"
#include "canvas-tools/MeasureDistanceState.h"
#include "canvas-tools/ModifyGeometryState.h"

#include "file-io/ReadErrorAccumulation.h"
#include "file-io/SymbolFileReader.h"

#include "global/GPlatesAssert.h"
#include "global/GPlatesException.h"
#include "global/Version.h"
#include "global/config.h"
#include "global/python.h"

#include "gui/AnimationController.h"
#include "gui/AddClickedGeometriesToFeatureTable.h"
#include "gui/CanvasToolWorkflows.h"
#include "gui/ColourSchemeDelegator.h"
#include "gui/DockState.h"
#include "gui/Dialogs.h"
#include "gui/DrawStyleManager.h"
#include "gui/FeatureFocus.h"
#include "gui/FileIOFeedback.h"
#include "gui/FullScreenMode.h"
#include "gui/GuiDebug.h"
#include "gui/ImportMenu.h"
#include "gui/MapProjection.h"
#include "gui/PythonManager.h"
#include "gui/RenderSettings.h"
#include "gui/SessionMenu.h"
#include "gui/TrinketArea.h"
#include "gui/UnsavedChangesTracker.h"
#include "gui/UtilitiesMenu.h"

#include "model/Model.h"
#include "model/Gpgim.h"
#include "model/QualifiedXmlName.h"
#include "model/types.h"

#include "maths/Centroid.h"
#include "maths/LatLonPoint.h"
#include "maths/MultiPointOnSphere.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "maths/UnitVector3D.h"
#include "maths/Vector3D.h"

#include "presentation/SessionManagement.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"
#include "presentation/ViewState.h"

#include "utils/ComponentManager.h"
#include "utils/DeferredCallEvent.h"
#include "utils/Earth.h"
#include "utils/Profile.h"

#include "view-operations/CloneOperation.h"
#include "view-operations/BooleanPolygonOperation.h"
#include "view-operations/CollisionOrogenyOperation.h"
#include "view-operations/AdvancePlateMotionOperation.h"
#include "view-operations/CreateInitialContinentOperation.h"
#include "view-operations/CreateInitialRotationFileOperation.h"
#include "view-operations/CreateOceanCrustOperation.h"
#include "view-operations/CreatePacificPlateOperation.h"
#include "view-operations/CreateTripleJunctionCrustOperation.h"
#include "view-operations/CratonPlateIdLabels.h"
#include "view-operations/DeleteFeatureOperation.h"
#include "view-operations/GenerateInitialSubductionOperation.h"
#include "view-operations/GenerateMantleEventsOperation.h"
#include "view-operations/GenerateSubductionEffectsOperation.h"
#include "view-operations/MakeRiftOperation.h"
#include "view-operations/NaturalizeCoastlineOperation.h"
#include "view-operations/PlateDirectionArrowsOperation.h"
#include "view-operations/PlateIdReassignmentOperation.h"
#include "view-operations/PostCollisionRiftOperation.h"
#include "view-operations/ProposeInitialRiftsOperation.h"
#include "view-operations/CraterGeneratorOperation.h"
#include "view-operations/FlowlineManagerOperation.h"
#include "view-operations/RenderedGeometryCollection.h"
#include "view-operations/RenderedGeometryParameters.h"
#include "view-operations/RotationFileEditorOperation.h"
#include "view-operations/SplitPlateOperation.h"
#include "view-operations/SubductionCutterOperation.h"
#include "view-operations/UndoRedo.h"

namespace GPlatesQtWidgets
{
	namespace
	{
		const char *STATUS_MESSAGE_SUFFIX_FOR_GLOBE = QT_TR_NOOP("Ctrl+drag to re-orient the globe.");
		const char *STATUS_MESSAGE_SUFFIX_FOR_MAP = QT_TR_NOOP("Ctrl+drag to pan the map.");

		struct ArtifexiaDrawStyle
		{
			const char *category_name;
			const char *style_name;
			const char *configuration_name;
			const char *configuration_value;
		};

		struct ArtifexiaLayerStyle
		{
			const char *layer_name;
			unsigned int draw_style_index;
		};

		enum ArtifexiaDrawStyleIndex
		{
			ARTIFEXIA_DEFAULT_STYLE,
			ARTIFEXIA_SILVER_STYLE,
			ARTIFEXIA_PURPLE_STYLE,
			ARTIFEXIA_ORANGE_STYLE,
			ARTIFEXIA_BLACK_STYLE,
			ARTIFEXIA_RED_STYLE,
			ARTIFEXIA_BLUE_STYLE,
			ARTIFEXIA_PINK_STYLE,
			ARTIFEXIA_GREEN_STYLE,
			ARTIFEXIA_FEATURE_AGE_DEFAULT_STYLE,
			ARTIFEXIA_FEATURE_AGE_MONOCHROME_STYLE,
			ARTIFEXIA_WHITE_STYLE,
			ARTIFEXIA_GOLD_STYLE
		};

		// Draw-style records transcribed from Artifexia/arty.gproj. An empty category denotes
		// the project's intentional use of GPlates' default plate-ID style.
		const ArtifexiaDrawStyle ARTIFEXIA_DRAW_STYLES[] =
		{
			{ "", "", "", "" },
			{ "SingleColour", "silver", "Color", "silver" },
			{ "SingleColour", "Purple", "Color", "#55007f" },
			{ "SingleColour", "orange", "Color", "orange" },
			{ "SingleColour", "black", "Color", "black" },
			{ "SingleColour", "red", "Color", "#ff0000" },
			{ "SingleColour", "blue", "Color", "blue" },
			{ "SingleColour", "pink", "Color", "pink" },
			{ "SingleColour", "green", "Color", "green" },
			{ "FeatureAge", "Default", "Palette", "FeatureAgeDefault" },
			{ "FeatureAge", "Monochrome", "Palette", "FeatureAgeMono" },
			{ "SingleColour", "white", "Color", "white" },
			{ "SingleColour", "gold", "Color", "gold" }
		};

		// Names from the demo's saved visual layers, plus exact aliases created by the
		// Worldbuilding Pasta operations in this build.
		const ArtifexiaLayerStyle ARTIFEXIA_LAYER_STYLES[] =
		{
			{ "continents", ARTIFEXIA_DEFAULT_STYLE },
			{ "cratons grey", ARTIFEXIA_DEFAULT_STYLE },
			{ "cratons", ARTIFEXIA_DEFAULT_STYLE },
			{ "failed rifts", ARTIFEXIA_SILVER_STYLE },
			{ "flowlines", ARTIFEXIA_DEFAULT_STYLE },
			{ "island arcs", ARTIFEXIA_DEFAULT_STYLE },
			{ "mid ocean ridges", ARTIFEXIA_PURPLE_STYLE },
			{ "rifts", ARTIFEXIA_ORANGE_STYLE },
			{ "subduction zones", ARTIFEXIA_BLACK_STYLE },
			{ "topo", ARTIFEXIA_DEFAULT_STYLE },
			{ "MOR topo", ARTIFEXIA_RED_STYLE },
			{ "accreted terranes", ARTIFEXIA_DEFAULT_STYLE },
			{ "microcontinents", ARTIFEXIA_DEFAULT_STYLE },
			{ "ocean crust", ARTIFEXIA_DEFAULT_STYLE },
			{ "Convergent", ARTIFEXIA_BLUE_STYLE },
			{ "Divergent", ARTIFEXIA_DEFAULT_STYLE },
			{ "Transform faults", ARTIFEXIA_PINK_STYLE },
			{ "Transform", ARTIFEXIA_GREEN_STYLE },
			{ "sea blue", ARTIFEXIA_DEFAULT_STYLE },
			{ "lines", ARTIFEXIA_DEFAULT_STYLE },
			{ "extant ocean crust", ARTIFEXIA_FEATURE_AGE_DEFAULT_STYLE },
			{ "Plates", ARTIFEXIA_DEFAULT_STYLE },
			{ "active orogenies", ARTIFEXIA_DEFAULT_STYLE },
			{ "former orogenies", ARTIFEXIA_FEATURE_AGE_MONOCHROME_STYLE },
			{ "old orogenies", ARTIFEXIA_WHITE_STYLE },
			{ "active LIP", ARTIFEXIA_DEFAULT_STYLE },
			{ "former LIP", ARTIFEXIA_GOLD_STYLE },
			{ "Hotspots", ARTIFEXIA_DEFAULT_STYLE },
			{ "Hotspot trails", ARTIFEXIA_DEFAULT_STYLE },
			{ "former orogenies culled", ARTIFEXIA_DEFAULT_STYLE },
			{ "Initial Continental Crust", ARTIFEXIA_DEFAULT_STYLE },
			{ "Initial Cratons", ARTIFEXIA_DEFAULT_STYLE },
			{ "Provisional Mid-Ocean Ridges", ARTIFEXIA_PURPLE_STYLE },
			{ "Failed Rifts", ARTIFEXIA_SILVER_STYLE },
			{ "Active Mid-Ocean Ridges", ARTIFEXIA_PURPLE_STYLE },
			{ "Active Subduction Zones", ARTIFEXIA_BLACK_STYLE },
			{ "Collision Sutures", ARTIFEXIA_BLUE_STYLE }
		};

		// The exact feature-collection load order saved by Artifexia/arty.gproj. Raster,
		// topology and derived collections are included as empty shells so a new world has
		// the same human-readable filing system from the outset. The rotation file is
		// deliberately created later by workflow step 2.3, once its plate circuit is known.
		const char *WORLDPASTA_COLLECTION_FILENAMES[] =
		{
			"Continent Color.gpml",
			"continents.gpml",
			"cratons grey.gpml",
			"cratons.gpml",
			"failed rifts.gpml",
			"flowlines.gpml",
			"island arcs.gpml",
			"mid ocean ridges.gpml",
			"rifts.gpml",
			"subduction zones.gpml",
			"topo.gpml",
			"MOR topo.gpml",
			"accreted terranes.gpml",
			"microcontinents.gpml",
			"ocean crust.gpml",
			"Convergent.gpml",
			"Divergent.gpml",
			"Transform faults.gpml",
			"Transform.gpml",
			"sea blue.gpml",
			"lines.gpml",
			"extant ocean crust.gpml",
			"Plates.gpml",
			"active orogenies.gpml",
			"former orogenies.gpml",
			"old orogenies.gpml",
			"active LIP.gpml",
			"former LIP.gpml",
			"Hotspots.gpml",
			"Hotspot trails.gpml",
			"former orogenies culled.gpml"
		};

		const ArtifexiaDrawStyle *
		find_artifexia_draw_style_for_layer(
				const QString &layer_name)
		{
			for (unsigned int index = 0;
				index < sizeof(ARTIFEXIA_LAYER_STYLES) / sizeof(ARTIFEXIA_LAYER_STYLES[0]);
				++index)
			{
				if (layer_name.compare(
						QString::fromLatin1(ARTIFEXIA_LAYER_STYLES[index].layer_name),
						Qt::CaseInsensitive) == 0)
				{
					return &ARTIFEXIA_DRAW_STYLES[ARTIFEXIA_LAYER_STYLES[index].draw_style_index];
				}
			}
			return NULL;
		}

		const GPlatesGui::ConfigurationItem *
		find_style_configuration_item(
				const GPlatesGui::Configuration &configuration,
				const QString &configuration_name)
		{
			const GPlatesGui::ConfigurationItem *item = configuration.get(configuration_name);
			if (!item && configuration_name == "Color")
			{
				item = configuration.get("Colour");
			}
			if (!item)
			{
				const std::vector<QString> item_names = configuration.all_cfg_item_names();
				if (item_names.size() == 1)
				{
					item = configuration.get(item_names.front());
				}
			}
			return item;
		}

		GPlatesGui::ConfigurationItem *
		find_style_configuration_item(
				GPlatesGui::Configuration &configuration,
				const QString &configuration_name)
		{
			return const_cast<GPlatesGui::ConfigurationItem *>(
					find_style_configuration_item(
							static_cast<const GPlatesGui::Configuration &>(configuration),
							configuration_name));
		}

		const ArtifexiaDrawStyle *
		find_artifexia_draw_style_by_name(
				const QString &style_name)
		{
			if (style_name.compare(QString::fromLatin1("Default"), Qt::CaseInsensitive) == 0)
			{
				return &ARTIFEXIA_DRAW_STYLES[ARTIFEXIA_DEFAULT_STYLE];
			}
			for (unsigned int index = 0;
				 index < sizeof(ARTIFEXIA_DRAW_STYLES) / sizeof(ARTIFEXIA_DRAW_STYLES[0]);
				 ++index)
			{
				if (style_name.compare(
						QString::fromLatin1(ARTIFEXIA_DRAW_STYLES[index].style_name),
						Qt::CaseInsensitive) == 0)
				{
					return &ARTIFEXIA_DRAW_STYLES[index];
				}
			}
			return NULL;
		}

		bool
		style_matches_artifexia_configuration(
				const GPlatesGui::StyleAdapter &style,
				const ArtifexiaDrawStyle &artifexia_style)
		{
			const GPlatesGui::ConfigurationItem *configuration_item =
					find_style_configuration_item(
							style.configuration(),
							QString::fromLatin1(artifexia_style.configuration_name));
			return configuration_item &&
					configuration_item->value().toString().compare(
							QString::fromLatin1(artifexia_style.configuration_value),
							Qt::CaseInsensitive) == 0;
		}

		const GPlatesGui::StyleAdapter *
		resolve_artifexia_draw_style(
				const ArtifexiaDrawStyle &artifexia_style)
		{
			GPlatesGui::DrawStyleManager *style_manager = GPlatesGui::DrawStyleManager::instance();
			if (!artifexia_style.category_name[0])
			{
				return style_manager->default_style();
			}

			const QString category_name = QString::fromLatin1(artifexia_style.category_name);
			const QString style_name = QString::fromLatin1(artifexia_style.style_name);
			const GPlatesGui::StyleCategory *category = style_manager->get_catagory(category_name);
			if (!category)
			{
				return NULL;
			}

			const GPlatesGui::DrawStyleManager::StyleContainer styles =
					style_manager->get_styles(*category);
			BOOST_FOREACH(GPlatesGui::StyleAdapter *style, styles)
			{
				const bool compatible_name =
						style->name().compare(style_name, Qt::CaseInsensitive) == 0 ||
						style->name().startsWith(style_name + " (Artifexia", Qt::CaseInsensitive);
				if (compatible_name && style_matches_artifexia_configuration(*style, artifexia_style))
				{
					return style;
				}
			}

			const GPlatesGui::StyleAdapter *template_style =
					style_manager->get_template_style(*category);
			if (!template_style)
			{
				return NULL;
			}

			std::unique_ptr<GPlatesGui::StyleAdapter> new_style(template_style->deep_clone());
			if (!new_style)
			{
				return NULL;
			}

			QString unique_style_name = style_name;
			bool name_is_available = false;
			for (unsigned int suffix = 0; !name_is_available; ++suffix)
			{
				name_is_available = true;
				BOOST_FOREACH(GPlatesGui::StyleAdapter *style, styles)
				{
					if (style->name().compare(unique_style_name, Qt::CaseInsensitive) == 0)
					{
						name_is_available = false;
						unique_style_name = suffix == 0
								? style_name + " (Artifexia)"
								: style_name + QString(" (Artifexia %1)").arg(suffix + 1);
						break;
					}
				}
			}

			new_style->set_name(unique_style_name);
			GPlatesGui::ConfigurationItem *configuration_item =
					find_style_configuration_item(
							new_style->configuration(),
							QString::fromLatin1(artifexia_style.configuration_name));
			if (!configuration_item)
			{
				return NULL;
			}
			configuration_item->set_value(QString::fromLatin1(artifexia_style.configuration_value));

			GPlatesGui::StyleAdapter *registered_style = new_style.get();
			style_manager->register_style(registered_style);
			new_style.release();
			return registered_style;
		}

		struct ArtifexiaPresetResult
		{
			unsigned int feature_type_count;
			unsigned int matching_layer_count;
			unsigned int unavailable_style_count;
		};

		ArtifexiaPresetResult
		apply_artifexia_presentation(
				GPlatesQtWidgets::ViewportWindow &viewport_window)
		{
			// Inventory taken from all GPML collections in the Artifexia demo project.
			const QStringList artifexia_feature_types = QStringList()
					<< "gpml:ClosedPlateBoundary"
					<< "gpml:ContinentalCrust"
					<< "gpml:ContinentalRift"
					<< "gpml:Craton"
					<< "gpml:HotSpot"
					<< "gpml:IslandArc"
					<< "gpml:LargeIgneousProvince"
					<< "gpml:MidOceanRidge"
					<< "gpml:MotionPath"
					<< "gpml:OceanicCrust"
					<< "gpml:OrogenicBelt"
					<< "gpml:Raster"
					<< "gpml:SubductionZone"
					<< "gpml:Suture"
					<< "gpml:TerraneBoundary"
					<< "gpml:TopologicalClosedPlateBoundary"
					<< "gpml:Transform"
					<< "gpml:UnclassifiedFeature";

			QStringList hidden_feature_types;
			const GPlatesModel::Gpgim::feature_type_seq_type feature_types =
					GPlatesModel::Gpgim::instance().get_concrete_feature_types();
			BOOST_FOREACH(const GPlatesModel::FeatureType &feature_type, feature_types)
			{
				const QString qualified_name =
						GPlatesModel::convert_qualified_xml_name_to_qstring(feature_type);
				if (!artifexia_feature_types.contains(qualified_name))
				{
					hidden_feature_types.append(qualified_name);
				}
			}
			viewport_window.get_application_state().get_user_preferences().set_value(
					FeatureTypeDisplayPreferences::hidden_feature_types_key(),
					hidden_feature_types);

			ArtifexiaPresetResult result =
			{
				static_cast<unsigned int>(artifexia_feature_types.size()), 0, 0
			};
			GPlatesPresentation::VisualLayers &visual_layers =
					viewport_window.get_view_state().get_visual_layers();
			GPlatesQtWidgets::DrawStyleDialog &draw_style_dialog =
					viewport_window.dialogs().draw_style_dialog();
			for (size_t layer_index = 0; layer_index < visual_layers.size(); ++layer_index)
			{
				boost::weak_ptr<GPlatesPresentation::VisualLayer> visual_layer_ref =
						visual_layers.visual_layer_at(layer_index);
				boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer =
						visual_layer_ref.lock();
				if (!visual_layer)
				{
					continue;
				}

				const ArtifexiaDrawStyle *artifexia_style =
						find_artifexia_draw_style_for_layer(visual_layer->get_name().trimmed());
				if (!artifexia_style)
				{
					continue;
				}

				const GPlatesGui::StyleAdapter *style =
						resolve_artifexia_draw_style(*artifexia_style);
				if (!style)
				{
					++result.unavailable_style_count;
					continue;
				}

				draw_style_dialog.reset(visual_layer_ref, style);
				++result.matching_layer_count;
			}

			return result;
		}

		unsigned int
		apply_worldbuilding_workspace_profile(
				GPlatesQtWidgets::ViewportWindow &viewport_window,
				const GPlatesAppLogic::WorldbuildingProjectManifest::Manifest &manifest)
		{
			unsigned int applied_layer_count = 0;
			GPlatesPresentation::VisualLayers &visual_layers =
					viewport_window.get_view_state().get_visual_layers();
			GPlatesQtWidgets::DrawStyleDialog &draw_style_dialog =
					viewport_window.dialogs().draw_style_dialog();

			for (std::vector<GPlatesAppLogic::WorldbuildingProjectManifest::LayerProfile>::const_iterator
				 profile = manifest.layers.begin(); profile != manifest.layers.end(); ++profile)
			{
				QStringList matching_names;
				for (std::vector<GPlatesAppLogic::WorldbuildingProjectManifest::Collection>::const_iterator
					 collection = manifest.collections.begin(); collection != manifest.collections.end(); ++collection)
				{
					if (collection->role.compare(profile->collection_role, Qt::CaseInsensitive) == 0)
					{
						matching_names << collection->display_name
								<< QFileInfo(collection->file_name).completeBaseName();
						break;
					}
				}

				for (std::size_t layer_index = 0; layer_index < visual_layers.size(); ++layer_index)
				{
					boost::weak_ptr<GPlatesPresentation::VisualLayer> visual_layer_ref =
							visual_layers.visual_layer_at(layer_index);
					boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer = visual_layer_ref.lock();
					if (!visual_layer ||
						!matching_names.contains(visual_layer->get_name().trimmed(), Qt::CaseInsensitive))
					{
						continue;
					}

					visual_layer->set_visible(profile->visible);
					const ArtifexiaDrawStyle *draw_style =
							find_artifexia_draw_style_by_name(profile->draw_style);
					if (draw_style)
					{
						const GPlatesGui::StyleAdapter *style = resolve_artifexia_draw_style(*draw_style);
						if (style)
						{
							draw_style_dialog.reset(visual_layer_ref, style);
						}
					}

					const std::size_t target_index = static_cast<std::size_t>(
							std::max(0, std::min(profile->order, static_cast<int>(visual_layers.size()) - 1)));
					visual_layers.move_layer(layer_index, target_index);
					++applied_layer_count;
					break;
				}
			}
			return applied_layer_count;
		}

		void
		canvas_tool_status_message(
				GPlatesQtWidgets::ViewportWindow &viewport_window,
				const char *message)
		{
			viewport_window.status_message(
					GPlatesQtWidgets::ViewportWindow::tr(message) + " " +
					GPlatesQtWidgets::ViewportWindow::tr(
						viewport_window.reconstruction_view_widget().globe_is_active() ?
							STATUS_MESSAGE_SUFFIX_FOR_GLOBE : STATUS_MESSAGE_SUFFIX_FOR_MAP));
		}

		void
		activate_choose_feature_tool(
				GPlatesGui::CanvasToolWorkflows &canvas_tool_workflows)
		{
			canvas_tool_workflows.choose_canvas_tool(
					GPlatesGui::CanvasToolWorkflows::WORKFLOW_FEATURE_INSPECTION,
					GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY);
		}

		void
		add_shortcut_to_tooltip(
				QAction *action)
		{
			if (!action->shortcut().isEmpty())
			{
				action->setToolTip(QString()); // Reset to default.
				action->setToolTip(action->toolTip() + "  " +
						action->shortcut().toString(QKeySequence::NativeText));
			}
		}

		QVBoxLayout *
		create_scrollable_dialog_layout(
				QDialog *dialog)
		{
			QVBoxLayout *outer_layout = new QVBoxLayout(dialog);
			outer_layout->setContentsMargins(0, 0, 0, 0);
			QScrollArea *scroll_area = new QScrollArea(dialog);
			scroll_area->setWidgetResizable(true);
			scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
			QWidget *contents = new QWidget(scroll_area);
			QVBoxLayout *contents_layout = new QVBoxLayout(contents);
			scroll_area->setWidget(contents);
			outer_layout->addWidget(scroll_area);
			return contents_layout;
		}
	}
}


// ViewportWindow constructor
GPlatesQtWidgets::ViewportWindow::ViewportWindow(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_geometry_operation_state_ptr(
			new GPlatesCanvasTools::GeometryOperationState()),
	d_modify_geometry_state(
			new GPlatesCanvasTools::ModifyGeometryState()),
	d_measure_distance_state_ptr(
			new GPlatesCanvasTools::MeasureDistanceState(
				get_view_state().get_rendered_geometry_collection(),
				*d_geometry_operation_state_ptr)),
	d_canvas_tool_workflows(
			new GPlatesGui::CanvasToolWorkflows()),
	d_clone_operation_ptr(
			new GPlatesViewOperations::CloneOperation(
				*d_canvas_tool_workflows,
				get_view_state().get_digitise_geometry_builder(),
				get_view_state().get_focused_feature_geometry_builder(),
				get_view_state())),
	d_boolean_polygon_operation_ptr(
			new GPlatesViewOperations::BooleanPolygonOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_collision_orogeny_operation_ptr(
			new GPlatesViewOperations::CollisionOrogenyOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_post_collision_rift_operation_ptr(
			new GPlatesViewOperations::PostCollisionRiftOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_generate_mantle_events_operation_ptr(
			new GPlatesViewOperations::GenerateMantleEventsOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_delete_feature_operation_ptr(
			new GPlatesViewOperations::DeleteFeatureOperation(
				get_view_state().get_feature_focus(),
				get_application_state())),
	d_create_initial_continent_operation_ptr(
			new GPlatesViewOperations::CreateInitialContinentOperation(
					get_application_state(),
					get_view_state())),
	d_create_initial_rotation_file_operation_ptr(
			new GPlatesViewOperations::CreateInitialRotationFileOperation(
					get_application_state())),
	d_advance_plate_motion_operation_ptr(
			new GPlatesViewOperations::AdvancePlateMotionOperation(
					get_application_state(),
					get_view_state())),
	d_create_ocean_crust_operation_ptr(
			new GPlatesViewOperations::CreateOceanCrustOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_create_triple_junction_crust_operation_ptr(
			new GPlatesViewOperations::CreateTripleJunctionCrustOperation(
					*d_create_ocean_crust_operation_ptr,
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_create_pacific_plate_operation_ptr(
			new GPlatesViewOperations::CreatePacificPlateOperation(
					*d_create_ocean_crust_operation_ptr,
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_propose_initial_rifts_operation_ptr(
			new GPlatesViewOperations::ProposeInitialRiftsOperation(
					get_application_state(),
					get_view_state())),
	d_make_rift_operation_ptr(
			new GPlatesViewOperations::MakeRiftOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_generate_initial_subduction_operation_ptr(
			new GPlatesViewOperations::GenerateInitialSubductionOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_generate_subduction_effects_operation_ptr(
			new GPlatesViewOperations::GenerateSubductionEffectsOperation(
					get_view_state().get_feature_focus(),
					get_application_state(),
					get_view_state())),
	d_craton_plate_id_labels_ptr(
			new GPlatesViewOperations::CratonPlateIdLabels(
					get_application_state(),
					get_view_state())),
	d_split_plate_operation_ptr(
			new GPlatesViewOperations::SplitPlateOperation(
				get_view_state().get_feature_focus(),
				get_application_state())),
	d_naturalize_coastline_operation_ptr(
			new GPlatesViewOperations::NaturalizeCoastlineOperation(
				get_view_state().get_feature_focus(),
				get_application_state(),
				get_view_state().get_rendered_geometry_collection())),
	d_subduction_cutter_operation_ptr(
			new GPlatesViewOperations::SubductionCutterOperation(
				get_application_state(),
				get_view_state())),
	d_rotation_file_editor_operation_ptr(
			new GPlatesViewOperations::RotationFileEditorOperation(
				get_application_state())),
	d_plate_id_reassignment_operation_ptr(
			new GPlatesViewOperations::PlateIdReassignmentOperation(
				get_application_state(),
				get_view_state(),
				this)),
	d_plate_direction_arrows_operation_ptr(
			new GPlatesViewOperations::PlateDirectionArrowsOperation(
				get_application_state(),
				get_view_state(),
				get_view_state().get_rendered_geometry_collection(),
				this)),
	d_dialogs_ptr(
			new GPlatesGui::Dialogs(
				get_application_state(),
				get_view_state(),
				*this,
				this)),
	d_full_screen_mode(
			new GPlatesGui::FullScreenMode(*this)),
	d_trinket_area_ptr(
			new GPlatesGui::TrinketArea(*d_dialogs_ptr, *this)),
	d_unsaved_changes_tracker_ptr(
			new GPlatesGui::UnsavedChangesTracker(
				*this,
				get_application_state().get_feature_collection_file_state(),
				get_application_state().get_feature_collection_file_io(),
				get_application_state().get_project_document_registry(),
				get_view_state().get_session_management(),
				this)),
	d_file_io_feedback_ptr(
			new GPlatesGui::FileIOFeedback(
				get_application_state(),
				get_view_state(),
				*this,
				get_view_state().get_feature_focus(),
				this)),
	d_session_menu_ptr(
			new GPlatesGui::SessionMenu(
				get_application_state(),
				get_view_state(),
				*d_file_io_feedback_ptr,
				this)),
	d_import_menu_ptr(NULL), // Needs to be set up after call to setupUi.
	d_utilities_menu_ptr(NULL), // Needs to be set up after call to setupUi.
	d_dock_state_ptr(
			new GPlatesGui::DockState(
				*this,
				this)),
	d_search_results_dock_ptr(NULL),
	d_canvas_tools_dock_ptr(NULL),
	d_project_documents_dock_ptr(NULL),
	d_worldbuilding_pasta_dock_ptr(NULL),
	d_reconstruction_view_widget_ptr(
			new ReconstructionViewWidget(
				*this,
				get_view_state(),
				this)),
	d_task_panel_ptr(NULL),
	d_undo_action_ptr(
			GPlatesViewOperations::UndoRedo::instance().get_undo_group().createUndoAction(this, tr("&Undo"))),
	d_redo_action_ptr(
			GPlatesViewOperations::UndoRedo::instance().get_undo_group().createRedoAction(this, tr("Re&do"))),
	d_inside_update_undo_action_tooltip(false),
	d_inside_update_redo_action_tooltip(false)
{
	setupUi(this);

	QAction *reassign_plate_id_action = new QAction(
			tr("Reassign Focused Feature Without Jumping..."), this);
	reassign_plate_id_action->setObjectName("action_Reassign_Plate_ID_Without_Jumping");
	reassign_plate_id_action->setStatusTip(
			tr("Copy or move the focused feature to a new Plate ID while preserving its current position"));
	menu_Features->addSeparator();
	menu_Features->addAction(reassign_plate_id_action);
	QObject::connect(
			reassign_plate_id_action,
			SIGNAL(triggered()),
			d_plate_id_reassignment_operation_ptr.get(),
			SLOT(trigger()));

	QAction *bulk_plate_id_action = new QAction(
			tr("Bulk Plate ID Operations..."), this);
	bulk_plate_id_action->setObjectName("action_Bulk_Plate_ID_Operations");
	bulk_plate_id_action->setStatusTip(
			tr("Copy, move, or delete all visible features belonging to a Plate ID"));
	menu_Features->addAction(bulk_plate_id_action);
	QObject::connect(
			bulk_plate_id_action,
			SIGNAL(triggered()),
			d_plate_id_reassignment_operation_ptr.get(),
			SLOT(trigger_bulk()));

	QAction *show_plate_direction_arrows_action = new QAction(
			tr("Show Plate Direction Arrows..."), this);
	show_plate_direction_arrows_action->setObjectName("action_Show_Plate_Direction_Arrows");
	show_plate_direction_arrows_action->setStatusTip(
			tr("Draw motion arrows directly on visible features without a velocity domain"));
	QAction *clear_plate_direction_arrows_action = new QAction(
			tr("Clear Plate Direction Arrows"), this);
	clear_plate_direction_arrows_action->setObjectName("action_Clear_Plate_Direction_Arrows");
	menu_Reconstruction->addSeparator();
	menu_Reconstruction->addAction(show_plate_direction_arrows_action);
	menu_Reconstruction->addAction(clear_plate_direction_arrows_action);
	QObject::connect(
			show_plate_direction_arrows_action,
			SIGNAL(triggered()),
			d_plate_direction_arrows_operation_ptr.get(),
			SLOT(show_dialog()));
	QObject::connect(
			clear_plate_direction_arrows_action,
			SIGNAL(triggered()),
			d_plate_direction_arrows_operation_ptr.get(),
			SLOT(clear()));

	// FIXME: remove this when all non Qt widget state has been moved into ViewState.
	// This is a temporary solution to avoiding passing ViewportWindow references around
	// when only non Qt widget related view state is needed - currently ViewportWindow contains
	// some of this state so just get the ViewportWindow reference from ViewState -
	// the method will eventually get removed when the state has been moved over.
	get_view_state().set_other_view_state(*this);

	//
	// Currently the order of initialisation of canvas tools dock, canvas tool workflows and task panel
	// is precarious due to references to each other via ViewportWindow.
	// So the order that currently works is:
	//  (1) canvas tools dock,
	//  (2) task panel,
	//  (3) canvas tool workflows.
	//

	// Create the tabbed search results dock widget.
	d_search_results_dock_ptr = new SearchResultsDockWidget(
			*d_dock_state_ptr,
			get_view_state().get_feature_table_model(),
			*this);
	d_search_results_dock_ptr->dock_at_bottom();

	// Create the tabbed canvas toolbar dock widget.
	d_canvas_tools_dock_ptr = new CanvasToolBarDockWidget(
			*d_dock_state_ptr,
			canvas_tool_workflows(),
			*this);
	d_canvas_tools_dock_ptr->dock_at_left();

	// Project documents are ordinary Markdown files associated with this session.
	d_project_documents_dock_ptr = new ProjectDocumentsDockWidget(
			*d_dock_state_ptr,
			get_view_state(),
			*this);
	d_project_documents_dock_ptr->dock_at_right();
	d_project_documents_dock_ptr->hide();

	// Keep physical distance and area measurements synchronized with project metadata.
	d_measure_distance_state_ptr->set_radius(
			get_application_state().get_planetary_parameters().effective_radius_kilometres());
	QObject::connect(
			&get_application_state().get_planetary_parameters(),
			&GPlatesAppLogic::PlanetaryParameters::effective_radius_changed,
			this,
			[this](double radius_metres)
			{
				d_measure_distance_state_ptr->set_radius(radius_metres / 1000.0);
			});

	// Specify which dock widget area should occupy the bottom left corner of the main window.
	//
	// Previously the canvas tools were in a QToolBar which is a primary window around the central
	// widget (so it wasn't affected by the bottom 'search results' dock window).
	// See the layout diagram at http://doc.trolltech.com/4.4/qdockwidget.html#details
	// Now the canvas tools are in a QDockWindow (which is a secondary window) so we have to specify
	// which dock window gets the corner areas.
	//
	// Handle cases where user docks canvas toolbar to the left or right while search results dock is at *bottom*.
	setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
	// Handle cases where user docks canvas toolbar to the left or right while search results dock is at *top*.
	setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
	setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);

	d_task_panel_ptr = new TaskPanel(
			get_view_state().get_digitise_geometry_builder(),
			*d_geometry_operation_state_ptr,
			*d_modify_geometry_state,
			*d_measure_distance_state_ptr,
			d_undo_action_ptr,
			d_redo_action_ptr,
			*d_canvas_tool_workflows,
			get_view_state(),
			*this,
			this);

	d_select_last_created_feature_action = new QAction(tr("Select Last Created Feature"), this);
	d_select_last_created_feature_action->setObjectName("action_Select_Last_Created_Feature");
	d_select_last_created_feature_action->setStatusTip(
			tr("Focus the most recently created feature, including outside its valid time"));
	d_select_last_created_feature_action->setEnabled(false);
	menu_Features->addSeparator();
	menu_Features->addAction(d_select_last_created_feature_action);
	QObject::connect(
			d_select_last_created_feature_action,
			SIGNAL(triggered()),
			this,
			SLOT(select_last_created_feature()));
	QObject::connect(
			&d_task_panel_ptr->digitisation_widget().get_create_feature_dialog(),
			SIGNAL(feature_created(GPlatesModel::FeatureHandle::weak_ref)),
			this,
			SLOT(remember_created_feature(GPlatesModel::FeatureHandle::weak_ref)));

	// Switch to the appropriate task panel tab when a canvas tool is activated.
	QObject::connect(
			&canvas_tool_workflows(),
			SIGNAL(canvas_tool_activated(
					GPlatesGui::CanvasToolWorkflows::WorkflowType,
					GPlatesGui::CanvasToolWorkflows::ToolType)),
			this,
			SLOT(handle_canvas_tool_activated(
					GPlatesGui::CanvasToolWorkflows::WorkflowType,
					GPlatesGui::CanvasToolWorkflows::ToolType)));

	// Now that the canvas tools dock and task panel have been setup we can create the
	// canvas tool workflows/tools and activate the default canvas workflow/tool.
	// This will also activate the default canvas tool to start things going.
	canvas_tool_workflows().initialise(
			*d_geometry_operation_state_ptr,
			*d_modify_geometry_state,
			*d_measure_distance_state_ptr,
			boost::bind(&canvas_tool_status_message, boost::ref(*this), boost::placeholders::_1),
			get_view_state(),
			*this);

	// Feature statistics are a window-level context action, so they remain
	// available regardless of which canvas workflow is currently active.
	QObject::connect(
			&globe_canvas(),
			SIGNAL(mouse_clicked(
					const GPlatesMaths::PointOnSphere &,
					const GPlatesMaths::PointOnSphere &,
					bool, Qt::MouseButton, Qt::KeyboardModifiers)),
			this,
			SLOT(handle_globe_feature_context_menu(
					const GPlatesMaths::PointOnSphere &,
					const GPlatesMaths::PointOnSphere &,
					bool, Qt::MouseButton, Qt::KeyboardModifiers)));
	QObject::connect(
			&map_view(),
			SIGNAL(mouse_clicked(
					const QPointF &, bool, Qt::MouseButton, Qt::KeyboardModifiers)),
			this,
			SLOT(handle_map_feature_context_menu(
					const QPointF &, bool, Qt::MouseButton, Qt::KeyboardModifiers)));
	QObject::connect(
			&get_view_state().get_animation_controller(),
			&GPlatesGui::AnimationController::project_timestamp_navigation_message,
			this,
			[this](const QString &message)
			{
				status_message(message);
			});

	// Keep the Worldbuilding Pasta procedure in one compact, ordered palette. Multi-step
	// operations launch persistent modeless windows so globe selection remains available.
	d_worldbuilding_pasta_dock_ptr = new QDockWidget(tr("Worldbuilding Pasta"), this);
	d_worldbuilding_pasta_dock_ptr->setObjectName("WorldbuildingPastaDock");
	d_worldbuilding_pasta_dock_ptr->setAllowedAreas(
			Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	QScrollArea *worldbuilding_pasta_scroll_area = new QScrollArea(
			d_worldbuilding_pasta_dock_ptr);
	worldbuilding_pasta_scroll_area->setWidgetResizable(true);
	worldbuilding_pasta_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	QWidget *worldbuilding_pasta_palette = new QWidget(worldbuilding_pasta_scroll_area);
	QVBoxLayout *worldbuilding_pasta_layout = new QVBoxLayout(worldbuilding_pasta_palette);
	QPushButton *initialize_worldpasta_structure_button = new QPushButton(
			tr("Open / Update Worldbuilding Project..."), worldbuilding_pasta_palette);
	initialize_worldpasta_structure_button->setToolTip(tr(
			"Choose a directory, inspect its editable worldbuilding manifest, and create only missing managed collections after a dry-run conflict report."));
	worldbuilding_pasta_layout->addWidget(initialize_worldpasta_structure_button);
	QLabel *worldbuilding_pasta_description = new QLabel(
			tr("Follow the Worldbuilding Pasta sequence from stable continental core to active plate margins."),
			worldbuilding_pasta_palette);
	worldbuilding_pasta_description->setWordWrap(true);
	worldbuilding_pasta_layout->addWidget(worldbuilding_pasta_description);

	QGroupBox *project_time_group = new QGroupBox(
			tr("Project Timestamp Navigation"), worldbuilding_pasta_palette);
	QVBoxLayout *project_time_layout = new QVBoxLayout(project_time_group);
	QLabel *project_time_status = new QLabel(project_time_group);
	project_time_status->setWordWrap(true);
	project_time_layout->addWidget(project_time_status);
	QHBoxLayout *project_time_buttons = new QHBoxLayout();
	QPushButton *older_project_timestamp_button = new QPushButton(
			tr("Older"), project_time_group);
	QPushButton *younger_project_timestamp_button = new QPushButton(
			tr("Younger"), project_time_group);
	older_project_timestamp_button->setToolTip(tr(
			"Go to the next older required Project Timestamp. This is the visible equivalent of Alt plus the reverse timeline-step button."));
	younger_project_timestamp_button->setToolTip(tr(
			"Go to the next younger required Project Timestamp. This is the visible equivalent of Alt plus the forward timeline-step button."));
	project_time_buttons->addWidget(older_project_timestamp_button);
	project_time_buttons->addWidget(younger_project_timestamp_button);
	project_time_layout->addLayout(project_time_buttons);
	worldbuilding_pasta_layout->addWidget(project_time_group);

	GPlatesAppLogic::ProjectTimestampSchedule &project_timestamp_schedule =
			get_application_state().get_project_timestamp_schedule();
	GPlatesGui::AnimationController &animation_controller =
			get_view_state().get_animation_controller();
	const auto update_project_timestamp_status =
			[this, project_time_status, older_project_timestamp_button,
			 younger_project_timestamp_button]()
			{
				const GPlatesAppLogic::ProjectTimestampSchedule &schedule =
						get_application_state().get_project_timestamp_schedule();
				const double current_time =
						get_view_state().get_animation_controller().view_time();
				const boost::optional<double> older = schedule.next_older_timestamp(current_time);
				const boost::optional<double> younger = schedule.next_younger_timestamp(current_time);
				const bool active = schedule.source() ==
						GPlatesAppLogic::ProjectTimestampSchedule::PROJECT_MARKDOWN;
				older_project_timestamp_button->setEnabled(active && older);
				younger_project_timestamp_button->setEnabled(active && younger);

				if (active)
				{
					project_time_status->setText(tr(
							"View: %1 Ma. Required schedule active (%2 timestamps).%3%4")
							.arg(QLocale().toString(current_time, 'f', 1))
							.arg(static_cast<qulonglong>(
									schedule.timestamps_older_to_younger().size()))
							.arg(older
									? tr(" Older: %1 Ma.").arg(
											QLocale().toString(older.get(), 'f', 1))
									: tr(" At the oldest required timestamp."))
							.arg(younger
									? tr(" Younger: %1 Ma.").arg(
											QLocale().toString(younger.get(), 'f', 1))
									: tr(" At the youngest required timestamp.")));
				}
				else
				{
					project_time_status->setText(schedule.diagnostic().isEmpty()
							? tr("No required Project Timestamp schedule is active. Ordinary timeline stepping remains available.")
							: schedule.diagnostic());
				}
			};
	QObject::connect(
			&project_timestamp_schedule,
			&GPlatesAppLogic::ProjectTimestampSchedule::schedule_changed,
			worldbuilding_pasta_palette,
			update_project_timestamp_status);
	QObject::connect(
			&animation_controller,
			&GPlatesGui::AnimationController::view_time_changed,
			worldbuilding_pasta_palette,
			[update_project_timestamp_status](double)
			{
				update_project_timestamp_status();
			});
	QObject::connect(
			older_project_timestamp_button,
			&QPushButton::clicked,
			worldbuilding_pasta_palette,
			[this]()
			{
				const GPlatesAppLogic::ProjectTimestampSchedule &schedule =
						get_application_state().get_project_timestamp_schedule();
				const boost::optional<double> timestamp = schedule.next_older_timestamp(
						get_view_state().get_animation_controller().view_time());
				if (timestamp)
				{
					get_view_state().get_animation_controller().set_view_time(timestamp.get());
				}
			});
	QObject::connect(
			younger_project_timestamp_button,
			&QPushButton::clicked,
			worldbuilding_pasta_palette,
			[this]()
			{
				const GPlatesAppLogic::ProjectTimestampSchedule &schedule =
						get_application_state().get_project_timestamp_schedule();
				const boost::optional<double> timestamp = schedule.next_younger_timestamp(
						get_view_state().get_animation_controller().view_time());
				if (timestamp)
				{
					get_view_state().get_animation_controller().set_view_time(timestamp.get());
				}
			});
	update_project_timestamp_status();
	QLabel *worldbuilding_pasta_attribution = new QLabel(
			QString("%1 <a href=\"https://worldbuildingpasta.blogspot.com/\">%2</a>")
					.arg(tr("Method and inspiration:"), tr("Worldbuilding Pasta blog")),
			worldbuilding_pasta_palette);
	worldbuilding_pasta_attribution->setTextFormat(Qt::RichText);
	worldbuilding_pasta_attribution->setTextInteractionFlags(Qt::TextBrowserInteraction);
	worldbuilding_pasta_attribution->setOpenExternalLinks(true);
	worldbuilding_pasta_attribution->setToolTip(tr(
			"Open the original Worldbuilding Pasta blog, whose tectonic-worldbuilding method this workflow follows."));
	worldbuilding_pasta_layout->addWidget(worldbuilding_pasta_attribution);
	QLabel *gplates_tutorials_link = new QLabel(
			QString("<a href=\"https://sites.google.com/site/gplatestutorials/\">%1</a>")
					.arg(tr("Additional GPlates tutorials and guidance")),
			worldbuilding_pasta_palette);
	gplates_tutorials_link->setTextFormat(Qt::RichText);
	gplates_tutorials_link->setTextInteractionFlags(Qt::TextBrowserInteraction);
	gplates_tutorials_link->setOpenExternalLinks(true);
	worldbuilding_pasta_layout->addWidget(gplates_tutorials_link);

	QGroupBox *foundation_group = new QGroupBox(
			tr("1. Continental Foundation"), worldbuilding_pasta_palette);
	QVBoxLayout *foundation_layout = new QVBoxLayout(foundation_group);
	QPushButton *create_initial_continent_button = new QPushButton(
			tr("1.1  Create Initial Continent..."), foundation_group);
	foundation_layout->addWidget(create_initial_continent_button);
	worldbuilding_pasta_layout->addWidget(foundation_group);

	QGroupBox *rifting_group = new QGroupBox(
			tr("2. Initial Rifting"), worldbuilding_pasta_palette);
	QVBoxLayout *rifting_layout = new QVBoxLayout(rifting_group);
	QPushButton *propose_initial_rifts_button = new QPushButton(
			tr("2.1  Build Voronoi Rift Network..."), rifting_group);
	QPushButton *show_make_rift_button = new QPushButton(
			tr("2.2  Make Rift..."), rifting_group);
	QPushButton *create_initial_rotation_file_button = new QPushButton(
			tr("2.3  Create + Load Rotation File..."), rifting_group);
	create_initial_rotation_file_button->setToolTip(tr(
			"Build Pasta's initial identity-pole plate circuit, save it as rotation.rot, and load it immediately."));
	rifting_layout->addWidget(propose_initial_rifts_button);
	rifting_layout->addWidget(show_make_rift_button);
	rifting_layout->addWidget(create_initial_rotation_file_button);
	worldbuilding_pasta_layout->addWidget(rifting_group);

	QGroupBox *active_margin_group = new QGroupBox(
			tr("3. Plate Motion and Ocean Basins"), worldbuilding_pasta_palette);
	QVBoxLayout *active_margin_layout = new QVBoxLayout(active_margin_group);
	QPushButton *show_initial_subduction_button = new QPushButton(
			tr("3.1  Build Opposite-Margin Subduction Zone..."), active_margin_group);
	show_initial_subduction_button->setToolTip(tr(
			"Infer motion from a rifted continent and its half-stage MOR, then preview a broad far-margin trench."));
	active_margin_layout->addWidget(show_initial_subduction_button);
	QPushButton *advance_plate_motion_button = new QPushButton(
			tr("3.2  Advance Plate Motion..."), active_margin_group);
	advance_plate_motion_button->setToolTip(tr(
			"Use .rot history, normalized MOR push and normalized subduction pull to propose the next younger rotation poles."));
	active_margin_layout->addWidget(advance_plate_motion_button);
	QPushButton *create_ocean_crust_button = new QPushButton(
			tr("3.3  Generate Ocean Crust from MOR..."), active_margin_group);
	create_ocean_crust_button->setToolTip(tr(
			"Shift-click a half-stage MOR, use the Project Timeline interval and recorded .rot motion, then fill only open ocean space on each side."));
	active_margin_layout->addWidget(create_ocean_crust_button);
	QPushButton *create_triple_junction_crust_button = new QPushButton(
			tr("3.4  Generate RRR Triple-Junction Crust..."), active_margin_group);
	create_triple_junction_crust_button->setToolTip(tr(
			"Shift-click three connected half-stage MORs, conservatively extend them to one junction, and fill all three plate sectors using the shared crust-band builder."));
	active_margin_layout->addWidget(create_triple_junction_crust_button);
	QPushButton *create_pacific_plate_button = new QPushButton(
			tr("3.5  Create Pacific-Style Plate..."), active_margin_group);
	create_pacific_plate_button->setToolTip(tr(
			"Shift-click three surrounding half-stage MORs, click the local void, then atomically create its new plate, crust, bounding MORs, and rotation sequence."));
	active_margin_layout->addWidget(create_pacific_plate_button);
	QPushButton *retire_ocean_crust_button = new QPushButton(
			tr("3.6  Retire Subducted Oceanic Crust..."), active_margin_group);
	retire_ocean_crust_button->setToolTip(tr(
			"Preview where OceanicCrust first overlaps an overriding plate, split those pieces, and give them disappearance times."));
	active_margin_layout->addWidget(retire_ocean_crust_button);
	worldbuilding_pasta_layout->addWidget(active_margin_group);

	QGroupBox *convergent_margin_group = new QGroupBox(
			tr("4. Convergent-Margin Effects"), worldbuilding_pasta_palette);
	QVBoxLayout *convergent_margin_layout = new QVBoxLayout(convergent_margin_group);
	QPushButton *show_subduction_effects_button = new QPushButton(
			tr("4.1  Review Island Arcs + Mountains..."), convergent_margin_group);
	show_subduction_effects_button->setToolTip(tr(
			"Select a trench, review its polarity and extent, then propose an editable island-arc line and land-intersection mountains, or a direct active-margin belt."));
	convergent_margin_layout->addWidget(show_subduction_effects_button);
	worldbuilding_pasta_layout->addWidget(convergent_margin_group);

	QGroupBox *collision_group = new QGroupBox(
			tr("5. Continental Collision"), worldbuilding_pasta_palette);
	QVBoxLayout *collision_group_layout = new QVBoxLayout(collision_group);
	QPushButton *show_collision_button = new QPushButton(
			tr("5.1  Review Collision + Orogeny..."), collision_group);
	show_collision_button->setToolTip(tr(
			"Derive and review a suture, collision class, consumed trench and complete orogen lifecycle from two approaching continents."));
	collision_group_layout->addWidget(show_collision_button);
	worldbuilding_pasta_layout->addWidget(collision_group);

	QGroupBox *post_collision_group = new QGroupBox(
			tr("6. Post-Collision Rerifting"), worldbuilding_pasta_palette);
	QVBoxLayout *post_collision_group_layout = new QVBoxLayout(post_collision_group);
	QPushButton *show_post_collision_rift_button = new QPushButton(
			tr("6.1  Reactivate Suture + Rerift..."), post_collision_group);
	show_post_collision_rift_button->setToolTip(tr(
			"Propose a craton-safe rift near an inherited suture, redistribute the welded assemblage, create the MOR, and preserve older plate history."));
	post_collision_group_layout->addWidget(show_post_collision_rift_button);
	worldbuilding_pasta_layout->addWidget(post_collision_group);

	QGroupBox *mantle_events_group = new QGroupBox(
			tr("7. Mantle Events"), worldbuilding_pasta_palette);
	QVBoxLayout *mantle_events_group_layout = new QVBoxLayout(mantle_events_group);
	QPushButton *show_mantle_events_button = new QPushButton(
			tr("7.1  Review LIP + Hotspot Event..."), mantle_events_group);
	show_mantle_events_button->setToolTip(tr(
			"Place a contained rift-triggered or random LIP, then create its active/former lifecycle, mantle-fixed hotspot, and native MotionPath trail."));
	mantle_events_group_layout->addWidget(show_mantle_events_button);
	worldbuilding_pasta_layout->addWidget(mantle_events_group);

	QGroupBox *project_helpers_group = new QGroupBox(
			tr("Project Helpers"), worldbuilding_pasta_palette);
	QVBoxLayout *project_helpers_layout = new QVBoxLayout(project_helpers_group);
	QPushButton *artifexia_preset_button = new QPushButton(
			tr("Use Artifexia Types + Layer Styles"), project_helpers_group);
	artifexia_preset_button->setToolTip(tr(
			"Show the Artifexia feature-type set and apply its saved colouring to matching loaded or generated layers."));
	project_helpers_layout->addWidget(artifexia_preset_button);
	QPushButton *show_boolean_polygons_button = new QPushButton(
			tr("Boolean Polygons..."), project_helpers_group);
	show_boolean_polygons_button->setToolTip(tr(
			"Union, subtract, intersect, or symmetric-difference polygon features while keeping the first feature's properties."));
	project_helpers_layout->addWidget(show_boolean_polygons_button);
	worldbuilding_pasta_layout->addWidget(project_helpers_group);
	worldbuilding_pasta_layout->addStretch();

	// Modeless Boolean workflow. It remains visible while the user selects the
	// first polygon and any number of operands, then hides after a successful edit.
	d_boolean_polygon_dialog_ptr = new QDialog(this, Qt::Tool);
	d_boolean_polygon_dialog_ptr->setObjectName("WorldbuildingBooleanPolygonsDialog");
	d_boolean_polygon_dialog_ptr->setWindowTitle(tr("World Building - Boolean Polygons"));
	d_boolean_polygon_dialog_ptr->setModal(false);
	d_boolean_polygon_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *boolean_layout = create_scrollable_dialog_layout(d_boolean_polygon_dialog_ptr);
	QLabel *boolean_description = new QLabel(tr(
			"Keep this window open while selecting polygons on the globe or map. The first polygon supplies every output feature property. Operand features are read-only inputs."),
			d_boolean_polygon_dialog_ptr);
	boolean_description->setWordWrap(true);
	boolean_description->setMinimumWidth(500);
	boolean_layout->addWidget(boolean_description);

	QFormLayout *boolean_form = new QFormLayout();
	d_boolean_operation_combo_ptr = new QComboBox(d_boolean_polygon_dialog_ptr);
	d_boolean_operation_combo_ptr->addItem(tr("Union (first + operands)"));
	d_boolean_operation_combo_ptr->addItem(tr("Subtract (first - operands)"));
	d_boolean_operation_combo_ptr->addItem(tr("Intersection"));
	d_boolean_operation_combo_ptr->addItem(tr("Symmetric difference"));
	boolean_form->addRow(tr("Operation:"), d_boolean_operation_combo_ptr);
	boolean_layout->addLayout(boolean_form);

	d_boolean_select_first_button_ptr = new QPushButton(
			tr("1. Select First Polygon"), d_boolean_polygon_dialog_ptr);
	d_boolean_select_first_button_ptr->setCheckable(true);
	d_boolean_first_status_label_ptr = new QLabel(d_boolean_polygon_dialog_ptr);
	d_boolean_first_status_label_ptr->setWordWrap(true);
	boolean_layout->addWidget(d_boolean_select_first_button_ptr);
	boolean_layout->addWidget(d_boolean_first_status_label_ptr);

	QHBoxLayout *boolean_operand_buttons = new QHBoxLayout();
	d_boolean_select_operand_button_ptr = new QPushButton(
			tr("2. Add Operand Polygon"), d_boolean_polygon_dialog_ptr);
	d_boolean_select_operand_button_ptr->setCheckable(true);
	d_boolean_clear_operands_button_ptr = new QPushButton(
			tr("Clear Operands"), d_boolean_polygon_dialog_ptr);
	d_boolean_remove_operand_button_ptr = new QPushButton(
			tr("Remove Last"), d_boolean_polygon_dialog_ptr);
	boolean_operand_buttons->addWidget(d_boolean_select_operand_button_ptr);
	boolean_operand_buttons->addWidget(d_boolean_remove_operand_button_ptr);
	boolean_operand_buttons->addWidget(d_boolean_clear_operands_button_ptr);
	d_boolean_operands_status_label_ptr = new QLabel(d_boolean_polygon_dialog_ptr);
	d_boolean_operands_status_label_ptr->setWordWrap(true);
	boolean_layout->addLayout(boolean_operand_buttons);
	boolean_layout->addWidget(d_boolean_operands_status_label_ptr);

	d_boolean_instruction_label_ptr = new QLabel(d_boolean_polygon_dialog_ptr);
	d_boolean_instruction_label_ptr->setWordWrap(true);
	boolean_layout->addWidget(d_boolean_instruction_label_ptr);
	QHBoxLayout *boolean_finish_buttons = new QHBoxLayout();
	d_boolean_preview_button_ptr = new QPushButton(
			tr("3. Preview Result"), d_boolean_polygon_dialog_ptr);
	d_boolean_apply_button_ptr = new QPushButton(
			tr("4. Apply and Finish"), d_boolean_polygon_dialog_ptr);
	d_boolean_cancel_button_ptr = new QPushButton(
			tr("Cancel"), d_boolean_polygon_dialog_ptr);
	boolean_finish_buttons->addStretch();
	boolean_finish_buttons->addWidget(d_boolean_preview_button_ptr);
	boolean_finish_buttons->addWidget(d_boolean_apply_button_ptr);
	boolean_finish_buttons->addWidget(d_boolean_cancel_button_ptr);
	boolean_layout->addLayout(boolean_finish_buttons);
	d_boolean_polygon_dialog_ptr->hide();

	// Modeless Make Rift workflow. Closing it merely hides it; selections remain
	// captured until a successful cut resets the operation.
	d_make_rift_dialog_ptr = new QDialog(this, Qt::Tool);
	d_make_rift_dialog_ptr->setObjectName("WorldbuildingPastaMakeRiftDialog");
	d_make_rift_dialog_ptr->setWindowTitle(tr("Worldbuilding Pasta - Make Rift"));
	d_make_rift_dialog_ptr->setModal(false);
	d_make_rift_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *make_rift_layout = create_scrollable_dialog_layout(d_make_rift_dialog_ptr);
	QLabel *make_rift_description = new QLabel(tr(
			"Keep this window open while selecting in the globe or map. The cutter may be any polyline; it no longer has to be a provisional MOR."),
			d_make_rift_dialog_ptr);
	make_rift_description->setWordWrap(true);
	make_rift_layout->addWidget(make_rift_description);
	d_make_rift_select_continent_button_ptr = new QPushButton(
			tr("1. Select Continent"), d_make_rift_dialog_ptr);
	d_make_rift_select_continent_button_ptr->setCheckable(true);
	d_make_rift_continent_status_label_ptr = new QLabel(d_make_rift_dialog_ptr);
	d_make_rift_continent_status_label_ptr->setWordWrap(true);
	d_make_rift_select_rift_button_ptr = new QPushButton(
			tr("2. Select Rift Polyline"), d_make_rift_dialog_ptr);
	d_make_rift_select_rift_button_ptr->setCheckable(true);
	d_make_rift_rift_status_label_ptr = new QLabel(d_make_rift_dialog_ptr);
	d_make_rift_rift_status_label_ptr->setWordWrap(true);
	d_make_rift_cut_button_ptr = new QPushButton(
			tr("3. Cut / Make Rift..."), d_make_rift_dialog_ptr);
	d_make_rift_instruction_label_ptr = new QLabel(d_make_rift_dialog_ptr);
	d_make_rift_instruction_label_ptr->setWordWrap(true);
	make_rift_layout->addWidget(d_make_rift_select_continent_button_ptr);
	make_rift_layout->addWidget(d_make_rift_continent_status_label_ptr);
	make_rift_layout->addWidget(d_make_rift_select_rift_button_ptr);
	make_rift_layout->addWidget(d_make_rift_rift_status_label_ptr);
	make_rift_layout->addWidget(d_make_rift_cut_button_ptr);
	make_rift_layout->addWidget(d_make_rift_instruction_label_ptr);
	d_make_rift_dialog_ptr->resize(430, 330);
	d_make_rift_dialog_ptr->hide();

	// Modeless active-margin workflow, mirroring Make Rift's explicit capture flow.
	d_initial_subduction_dialog_ptr = new QDialog(this, Qt::Tool);
	d_initial_subduction_dialog_ptr->setObjectName("WorldbuildingPastaInitialSubductionDialog");
	d_initial_subduction_dialog_ptr->setWindowTitle(
			tr("Worldbuilding Pasta - Opposite-Margin Subduction"));
	d_initial_subduction_dialog_ptr->setModal(false);
	d_initial_subduction_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *initial_subduction_layout = create_scrollable_dialog_layout(d_initial_subduction_dialog_ptr);
	QLabel *initial_subduction_description = new QLabel(tr(
			"Select one rifted continent and its half-stage MOR. The preview uses the MOR-to-continent vector for motion, spans the far-side silhouette, smooths coastal detail, and adds a broad natural arc."),
			d_initial_subduction_dialog_ptr);
	initial_subduction_description->setWordWrap(true);
	initial_subduction_layout->addWidget(initial_subduction_description);
	d_initial_subduction_select_continent_button_ptr = new QPushButton(
			tr("1. Select Rifted Continent"), d_initial_subduction_dialog_ptr);
	d_initial_subduction_select_continent_button_ptr->setCheckable(true);
	d_initial_subduction_continent_status_label_ptr = new QLabel(d_initial_subduction_dialog_ptr);
	d_initial_subduction_continent_status_label_ptr->setWordWrap(true);
	d_initial_subduction_select_mor_button_ptr = new QPushButton(
			tr("2. Select Half-Stage MOR"), d_initial_subduction_dialog_ptr);
	d_initial_subduction_select_mor_button_ptr->setCheckable(true);
	d_initial_subduction_mor_status_label_ptr = new QLabel(d_initial_subduction_dialog_ptr);
	d_initial_subduction_mor_status_label_ptr->setWordWrap(true);
	d_initial_subduction_generate_button_ptr = new QPushButton(
			tr("3. Preview / Create Subduction Zone..."), d_initial_subduction_dialog_ptr);
	d_initial_subduction_instruction_label_ptr = new QLabel(d_initial_subduction_dialog_ptr);
	d_initial_subduction_instruction_label_ptr->setWordWrap(true);
	initial_subduction_layout->addWidget(d_initial_subduction_select_continent_button_ptr);
	initial_subduction_layout->addWidget(d_initial_subduction_continent_status_label_ptr);
	initial_subduction_layout->addWidget(d_initial_subduction_select_mor_button_ptr);
	initial_subduction_layout->addWidget(d_initial_subduction_mor_status_label_ptr);
	initial_subduction_layout->addWidget(d_initial_subduction_generate_button_ptr);
	initial_subduction_layout->addWidget(d_initial_subduction_instruction_label_ptr);
	d_initial_subduction_dialog_ptr->resize(470, 360);
	d_initial_subduction_dialog_ptr->hide();

	// Persistent proposal/review window: source selections remain captured while the
	// worldbuilder adjusts polarity, extent, offsets and geological interpretation.
	d_subduction_effects_dialog_ptr = new QDialog(this, Qt::Tool);
	d_subduction_effects_dialog_ptr->setObjectName("WorldbuildingPastaSubductionEffectsDialog");
	d_subduction_effects_dialog_ptr->setWindowTitle(
			tr("Worldbuilding Pasta - Island Arcs and Mountains"));
	d_subduction_effects_dialog_ptr->setModal(false);
	d_subduction_effects_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *subduction_effects_layout = create_scrollable_dialog_layout(d_subduction_effects_dialog_ptr);
	QLabel *subduction_effects_description = new QLabel(tr(
			"Select a trench, then optionally its overriding continental crust. Auto proposes an island-arc notation line without a selected crust and an Andean belt with one. Arc intersections with any visible ContinentalCrust or terrane automatically preview mountain building. Nothing is committed until review."),
			d_subduction_effects_dialog_ptr);
	subduction_effects_description->setWordWrap(true);
	subduction_effects_layout->addWidget(subduction_effects_description);
	d_subduction_effects_select_subduction_button_ptr = new QPushButton(
			tr("1. Select Subduction Zone"), d_subduction_effects_dialog_ptr);
	d_subduction_effects_select_subduction_button_ptr->setCheckable(true);
	d_subduction_effects_subduction_status_label_ptr = new QLabel(d_subduction_effects_dialog_ptr);
	d_subduction_effects_subduction_status_label_ptr->setWordWrap(true);
	subduction_effects_layout->addWidget(d_subduction_effects_select_subduction_button_ptr);
	subduction_effects_layout->addWidget(d_subduction_effects_subduction_status_label_ptr);
	QHBoxLayout *subduction_effects_crust_buttons = new QHBoxLayout();
	d_subduction_effects_select_continent_button_ptr = new QPushButton(
			tr("2. Select Overriding Crust"), d_subduction_effects_dialog_ptr);
	d_subduction_effects_select_continent_button_ptr->setCheckable(true);
	d_subduction_effects_clear_continent_button_ptr = new QPushButton(
			tr("Clear Crust"), d_subduction_effects_dialog_ptr);
	subduction_effects_crust_buttons->addWidget(d_subduction_effects_select_continent_button_ptr);
	subduction_effects_crust_buttons->addWidget(d_subduction_effects_clear_continent_button_ptr);
	d_subduction_effects_continent_status_label_ptr = new QLabel(d_subduction_effects_dialog_ptr);
	d_subduction_effects_continent_status_label_ptr->setWordWrap(true);
	subduction_effects_layout->addLayout(subduction_effects_crust_buttons);
	subduction_effects_layout->addWidget(d_subduction_effects_continent_status_label_ptr);

	QFormLayout *subduction_effects_form = new QFormLayout();
	d_subduction_effect_type_combo_ptr = new QComboBox(d_subduction_effects_dialog_ptr);
	d_subduction_effect_type_combo_ptr->addItem(tr("Auto (context-sensitive)"));
	d_subduction_effect_type_combo_ptr->addItem(tr("Island Arc"));
	d_subduction_effect_type_combo_ptr->addItem(tr("Andean Orogeny"));
	d_subduction_effect_type_combo_ptr->addItem(tr("Laramide Orogeny"));
	subduction_effects_form->addRow(tr("Interpretation:"), d_subduction_effect_type_combo_ptr);
	d_subduction_effects_flip_polarity_check_ptr = new QCheckBox(
			tr("Flip declared trench polarity"), d_subduction_effects_dialog_ptr);
	subduction_effects_form->addRow(QString(), d_subduction_effects_flip_polarity_check_ptr);
	d_subduction_effects_early_arc_check_ptr = new QCheckBox(
			tr("Allow island arc before delay"), d_subduction_effects_dialog_ptr);
	subduction_effects_form->addRow(QString(), d_subduction_effects_early_arc_check_ptr);

	const struct SpinSpec { QPointer<QDoubleSpinBox> *target; const char *label; double minimum; double maximum; double value; const char *suffix; } spin_specs[] =
	{
		{ &d_subduction_effects_arc_delay_spin_ptr, QT_TR_NOOP("Island-arc delay:"), 0, 200, 50, " Ma" },
		{ &d_subduction_effects_trim_start_spin_ptr, QT_TR_NOOP("Trim from start:"), 0, 40, 0, " %" },
		{ &d_subduction_effects_trim_end_spin_ptr, QT_TR_NOOP("Trim from end:"), 0, 40, 0, " %" },
		{ &d_subduction_effects_offset_spin_ptr, QT_TR_NOOP("Trench offset:"), 50, 800, 220, " km" },
		{ &d_subduction_effects_irregularity_spin_ptr, QT_TR_NOOP("Arc-line wiggle:"), 0, 45, 22, " %" },
		{ &d_subduction_effects_belt_width_spin_ptr, QT_TR_NOOP("Mountain-belt width:"), 50, 800, 100, " km" }
	};
	for (unsigned int index = 0; index < sizeof(spin_specs) / sizeof(spin_specs[0]); ++index)
	{
		*spin_specs[index].target = new QDoubleSpinBox(d_subduction_effects_dialog_ptr);
		(*spin_specs[index].target)->setRange(spin_specs[index].minimum, spin_specs[index].maximum);
		(*spin_specs[index].target)->setValue(spin_specs[index].value);
		(*spin_specs[index].target)->setSuffix(tr(spin_specs[index].suffix));
		(*spin_specs[index].target)->setDecimals(0);
		subduction_effects_form->addRow(tr(spin_specs[index].label), *spin_specs[index].target);
	}
	subduction_effects_layout->addLayout(subduction_effects_form);
	QHBoxLayout *subduction_effects_action_buttons = new QHBoxLayout();
	d_subduction_effects_preview_button_ptr = new QPushButton(
			tr("3. Preview Proposal"), d_subduction_effects_dialog_ptr);
	d_subduction_effects_commit_button_ptr = new QPushButton(
			tr("4. Commit Reviewed Features"), d_subduction_effects_dialog_ptr);
	subduction_effects_action_buttons->addWidget(d_subduction_effects_preview_button_ptr);
	subduction_effects_action_buttons->addWidget(d_subduction_effects_commit_button_ptr);
	subduction_effects_layout->addLayout(subduction_effects_action_buttons);
	d_subduction_effects_instruction_label_ptr = new QLabel(d_subduction_effects_dialog_ptr);
	d_subduction_effects_instruction_label_ptr->setWordWrap(true);
	subduction_effects_layout->addWidget(d_subduction_effects_instruction_label_ptr);
	d_subduction_effects_dialog_ptr->resize(520, 700);
	d_subduction_effects_dialog_ptr->hide();

	// Persistent continental-collision workflow with separately reviewable margin
	// deformation and no-jump incoming-plate retirement.
	d_collision_dialog_ptr = new QDialog(this, Qt::Tool);
	d_collision_dialog_ptr->setObjectName("WorldbuildingPastaCollisionOrogenyDialog");
	d_collision_dialog_ptr->setWindowTitle(
			tr("Worldbuilding Pasta - Collision and Orogeny"));
	d_collision_dialog_ptr->setModal(false);
	d_collision_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *collision_layout = create_scrollable_dialog_layout(d_collision_dialog_ptr);
	QLabel *collision_description = new QLabel(tr(
			"Select the incoming and receiving continental crust. Optionally select only the trench segment consumed by the collision. Preview estimates convergence, proposes a suture and mountain belt, and can time-slice a welded younger plate without erasing its older reconstruction history."),
			d_collision_dialog_ptr);
	collision_description->setWordWrap(true);
	collision_layout->addWidget(collision_description);
	d_collision_select_incoming_button_ptr = new QPushButton(
			tr("1. Select Incoming Continent"), d_collision_dialog_ptr);
	d_collision_select_incoming_button_ptr->setCheckable(true);
	d_collision_incoming_status_label_ptr = new QLabel(d_collision_dialog_ptr);
	d_collision_incoming_status_label_ptr->setWordWrap(true);
	collision_layout->addWidget(d_collision_select_incoming_button_ptr);
	collision_layout->addWidget(d_collision_incoming_status_label_ptr);
	d_collision_select_receiving_button_ptr = new QPushButton(
			tr("2. Select Receiving Continent"), d_collision_dialog_ptr);
	d_collision_select_receiving_button_ptr->setCheckable(true);
	d_collision_receiving_status_label_ptr = new QLabel(d_collision_dialog_ptr);
	d_collision_receiving_status_label_ptr->setWordWrap(true);
	collision_layout->addWidget(d_collision_select_receiving_button_ptr);
	collision_layout->addWidget(d_collision_receiving_status_label_ptr);
	QHBoxLayout *collision_trench_buttons = new QHBoxLayout();
	d_collision_select_trench_button_ptr = new QPushButton(
			tr("3. Select Consumed Trench"), d_collision_dialog_ptr);
	d_collision_select_trench_button_ptr->setCheckable(true);
	d_collision_clear_trench_button_ptr = new QPushButton(
			tr("Clear Trench"), d_collision_dialog_ptr);
	collision_trench_buttons->addWidget(d_collision_select_trench_button_ptr);
	collision_trench_buttons->addWidget(d_collision_clear_trench_button_ptr);
	d_collision_trench_status_label_ptr = new QLabel(d_collision_dialog_ptr);
	d_collision_trench_status_label_ptr->setWordWrap(true);
	collision_layout->addLayout(collision_trench_buttons);
	collision_layout->addWidget(d_collision_trench_status_label_ptr);

	QFormLayout *collision_form = new QFormLayout();
	d_collision_type_combo_ptr = new QComboBox(d_collision_dialog_ptr);
	d_collision_type_combo_ptr->addItem(tr("Auto (area + motion + history)"));
	d_collision_type_combo_ptr->addItem(tr("Arc / Terrane Accretion"));
	d_collision_type_combo_ptr->addItem(tr("Ural-type Collision"));
	d_collision_type_combo_ptr->addItem(tr("Himalayan-type Collision"));
	collision_form->addRow(tr("Interpretation:"), d_collision_type_combo_ptr);
	d_collision_precursor_spin_ptr = new QSpinBox(d_collision_dialog_ptr);
	d_collision_precursor_spin_ptr->setRange(0, 8);
	d_collision_precursor_spin_ptr->setValue(0);
	d_collision_precursor_spin_ptr->setToolTip(tr(
			"Count earlier arc, microcontinent or continent collisions affecting this contact. Two or more biases Auto toward Himalayan."));
	collision_form->addRow(tr("Precursor collisions:"), d_collision_precursor_spin_ptr);
	d_collision_contact_threshold_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_contact_threshold_spin_ptr->setRange(25, 800);
	d_collision_contact_threshold_spin_ptr->setValue(250);
	d_collision_contact_threshold_spin_ptr->setDecimals(0);
	d_collision_contact_threshold_spin_ptr->setSuffix(tr(" km"));
	collision_form->addRow(tr("Contact reach:"), d_collision_contact_threshold_spin_ptr);
	d_collision_auto_width_check_ptr = new QCheckBox(
			tr("Use classification width"), d_collision_dialog_ptr);
	d_collision_auto_width_check_ptr->setChecked(true);
	collision_form->addRow(QString(), d_collision_auto_width_check_ptr);
	d_collision_belt_width_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_belt_width_spin_ptr->setRange(50, 900);
	d_collision_belt_width_spin_ptr->setValue(180);
	d_collision_belt_width_spin_ptr->setDecimals(0);
	d_collision_belt_width_spin_ptr->setSuffix(tr(" km"));
	collision_form->addRow(tr("Mountain-belt width:"), d_collision_belt_width_spin_ptr);
	d_collision_smoothing_spin_ptr = new QSpinBox(d_collision_dialog_ptr);
	d_collision_smoothing_spin_ptr->setRange(0, 8);
	d_collision_smoothing_spin_ptr->setValue(2);
	collision_form->addRow(tr("Suture smoothing passes:"), d_collision_smoothing_spin_ptr);
	d_collision_irregularity_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_irregularity_spin_ptr->setRange(0, 35);
	d_collision_irregularity_spin_ptr->setValue(12);
	d_collision_irregularity_spin_ptr->setDecimals(0);
	d_collision_irregularity_spin_ptr->setSuffix(tr(" %"));
	collision_form->addRow(tr("Belt irregularity:"), d_collision_irregularity_spin_ptr);
	d_collision_deform_margins_check_ptr = new QCheckBox(
			tr("Deform both contact margins into the suture"), d_collision_dialog_ptr);
	d_collision_deform_margins_check_ptr->setChecked(true);
	collision_form->addRow(QString(), d_collision_deform_margins_check_ptr);
	d_collision_deformation_reach_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_deformation_reach_spin_ptr->setRange(100, 1200);
	d_collision_deformation_reach_spin_ptr->setValue(350);
	d_collision_deformation_reach_spin_ptr->setDecimals(0);
	d_collision_deformation_reach_spin_ptr->setSuffix(tr(" km"));
	d_collision_deformation_reach_spin_ptr->setToolTip(tr(
			"Margin vertices inside this corridor taper toward the proposed suture; the nearest 35% is welded exactly."));
	collision_form->addRow(tr("Deformation reach:"), d_collision_deformation_reach_spin_ptr);
	d_collision_retire_incoming_check_ptr = new QCheckBox(
			tr("Retire incoming crustal features into receiving plate (no jump)"),
			d_collision_dialog_ptr);
	d_collision_retire_incoming_check_ptr->setChecked(true);
	d_collision_retire_incoming_check_ptr->setToolTip(tr(
			"Creates younger successor slices for continental crust, cratons, rifts, arcs, LIPs, orogenies and sutures on the receiving Plate ID. Older source slices and the loaded .rot history remain intact."));
	collision_form->addRow(QString(), d_collision_retire_incoming_check_ptr);
	d_collision_active_duration_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_active_duration_spin_ptr->setRange(1, 200);
	d_collision_active_duration_spin_ptr->setValue(50);
	d_collision_active_duration_spin_ptr->setDecimals(0);
	d_collision_active_duration_spin_ptr->setSuffix(tr(" Ma"));
	collision_form->addRow(tr("Active duration:"), d_collision_active_duration_spin_ptr);
	d_collision_old_age_spin_ptr = new QDoubleSpinBox(d_collision_dialog_ptr);
	d_collision_old_age_spin_ptr->setRange(50, 1000);
	d_collision_old_age_spin_ptr->setValue(450);
	d_collision_old_age_spin_ptr->setDecimals(0);
	d_collision_old_age_spin_ptr->setSuffix(tr(" Ma"));
	collision_form->addRow(tr("Become old after:"), d_collision_old_age_spin_ptr);
	d_collision_terminate_trench_check_ptr = new QCheckBox(
			tr("End selected consumed trench at collision"), d_collision_dialog_ptr);
	d_collision_terminate_trench_check_ptr->setChecked(true);
	collision_form->addRow(QString(), d_collision_terminate_trench_check_ptr);
	d_collision_allow_nonconvergent_check_ptr = new QCheckBox(
			tr("Allow non-convergent rotation result"), d_collision_dialog_ptr);
	collision_form->addRow(QString(), d_collision_allow_nonconvergent_check_ptr);
	collision_layout->addLayout(collision_form);
	QHBoxLayout *collision_action_buttons = new QHBoxLayout();
	d_collision_preview_button_ptr = new QPushButton(
			tr("4. Preview Collision"), d_collision_dialog_ptr);
	d_collision_commit_button_ptr = new QPushButton(
			tr("5. Commit Reviewed Collision"), d_collision_dialog_ptr);
	collision_action_buttons->addWidget(d_collision_preview_button_ptr);
	collision_action_buttons->addWidget(d_collision_commit_button_ptr);
	collision_layout->addLayout(collision_action_buttons);
	d_collision_instruction_label_ptr = new QLabel(d_collision_dialog_ptr);
	d_collision_instruction_label_ptr->setWordWrap(true);
	collision_layout->addWidget(d_collision_instruction_label_ptr);
	d_collision_dialog_ptr->resize(580, 840);
	d_collision_dialog_ptr->hide();

	// Persistent post-collision rerifting workflow. The proposal is intentionally
	// offset from the inherited suture and is never committed without review.
	d_post_collision_rift_dialog_ptr = new QDialog(this, Qt::Tool);
	d_post_collision_rift_dialog_ptr->setObjectName("WorldbuildingPastaPostCollisionRiftDialog");
	d_post_collision_rift_dialog_ptr->setWindowTitle(
			tr("Worldbuilding Pasta - Post-Collision Suture Reactivation"));
	d_post_collision_rift_dialog_ptr->setModal(false);
	d_post_collision_rift_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *post_collision_rift_layout = create_scrollable_dialog_layout(d_post_collision_rift_dialog_ptr);
	QLabel *post_collision_rift_description = new QLabel(tr(
			"Select one crust polygon the new rift must cut and the inherited collision suture. The generator searches for a nearby, irregular, craton-safe route; commit time-slices the welded assemblage, creates the half-stage MOR, and adds only a missing no-jump rotation branch."),
			d_post_collision_rift_dialog_ptr);
	post_collision_rift_description->setWordWrap(true);
	post_collision_rift_layout->addWidget(post_collision_rift_description);
	d_post_collision_rift_select_host_button_ptr = new QPushButton(
			tr("1. Select Host Continental Crust"), d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_select_host_button_ptr->setCheckable(true);
	d_post_collision_rift_host_status_label_ptr = new QLabel(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_host_status_label_ptr->setWordWrap(true);
	d_post_collision_rift_select_suture_button_ptr = new QPushButton(
			tr("2. Select Inherited Suture"), d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_select_suture_button_ptr->setCheckable(true);
	d_post_collision_rift_suture_status_label_ptr = new QLabel(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_suture_status_label_ptr->setWordWrap(true);
	post_collision_rift_layout->addWidget(d_post_collision_rift_select_host_button_ptr);
	post_collision_rift_layout->addWidget(d_post_collision_rift_host_status_label_ptr);
	post_collision_rift_layout->addWidget(d_post_collision_rift_select_suture_button_ptr);
	post_collision_rift_layout->addWidget(d_post_collision_rift_suture_status_label_ptr);
	QFormLayout *post_collision_rift_form = new QFormLayout();
	d_post_collision_rift_offset_spin_ptr = new QDoubleSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_offset_spin_ptr->setRange(20, 800);
	d_post_collision_rift_offset_spin_ptr->setValue(120);
	d_post_collision_rift_offset_spin_ptr->setDecimals(0);
	d_post_collision_rift_offset_spin_ptr->setSuffix(tr(" km"));
	post_collision_rift_form->addRow(tr("Suture offset:"), d_post_collision_rift_offset_spin_ptr);
	d_post_collision_rift_wiggle_spin_ptr = new QDoubleSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_wiggle_spin_ptr->setRange(0, 80);
	d_post_collision_rift_wiggle_spin_ptr->setValue(25);
	d_post_collision_rift_wiggle_spin_ptr->setDecimals(0);
	d_post_collision_rift_wiggle_spin_ptr->setSuffix(tr(" %"));
	post_collision_rift_form->addRow(tr("Rift irregularity:"), d_post_collision_rift_wiggle_spin_ptr);
	d_post_collision_rift_segment_spin_ptr = new QDoubleSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_segment_spin_ptr->setRange(25, 200);
	d_post_collision_rift_segment_spin_ptr->setValue(90);
	d_post_collision_rift_segment_spin_ptr->setDecimals(0);
	d_post_collision_rift_segment_spin_ptr->setSuffix(tr(" km"));
	post_collision_rift_form->addRow(tr("Maximum segment:"), d_post_collision_rift_segment_spin_ptr);
	d_post_collision_rift_extension_spin_ptr = new QDoubleSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_extension_spin_ptr->setRange(100, 2000);
	d_post_collision_rift_extension_spin_ptr->setValue(500);
	d_post_collision_rift_extension_spin_ptr->setDecimals(0);
	d_post_collision_rift_extension_spin_ptr->setSuffix(tr(" km"));
	post_collision_rift_form->addRow(tr("End extension:"), d_post_collision_rift_extension_spin_ptr);
	d_post_collision_rift_side_combo_ptr = new QComboBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_side_combo_ptr->addItem(tr("Left of drawn suture"));
	d_post_collision_rift_side_combo_ptr->addItem(tr("Right of drawn suture"));
	post_collision_rift_form->addRow(tr("Preferred side:"), d_post_collision_rift_side_combo_ptr);
	d_post_collision_rift_seed_spin_ptr = new QSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_seed_spin_ptr->setRange(0, 999999);
	d_post_collision_rift_seed_spin_ptr->setValue(1);
	post_collision_rift_form->addRow(tr("Route seed:"), d_post_collision_rift_seed_spin_ptr);
	d_post_collision_rift_left_name_ptr = new QLineEdit(tr("Rerift Left Continent"), d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_right_name_ptr = new QLineEdit(tr("Rerift Right Continent"), d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_left_plate_spin_ptr = new QSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_right_plate_spin_ptr = new QSpinBox(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_left_plate_spin_ptr->setRange(0, 99999999);
	d_post_collision_rift_right_plate_spin_ptr->setRange(0, 99999999);
	post_collision_rift_form->addRow(tr("Left child name:"), d_post_collision_rift_left_name_ptr);
	post_collision_rift_form->addRow(tr("Left child Plate ID:"), d_post_collision_rift_left_plate_spin_ptr);
	post_collision_rift_form->addRow(tr("Right child name:"), d_post_collision_rift_right_name_ptr);
	post_collision_rift_form->addRow(tr("Right child Plate ID:"), d_post_collision_rift_right_plate_spin_ptr);
	post_collision_rift_layout->addLayout(post_collision_rift_form);
	QHBoxLayout *post_collision_rift_buttons = new QHBoxLayout();
	d_post_collision_rift_preview_button_ptr = new QPushButton(
			tr("3. Preview Rerift"), d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_commit_button_ptr = new QPushButton(
			tr("4. Commit Assemblage Rerift"), d_post_collision_rift_dialog_ptr);
	post_collision_rift_buttons->addWidget(d_post_collision_rift_preview_button_ptr);
	post_collision_rift_buttons->addWidget(d_post_collision_rift_commit_button_ptr);
	post_collision_rift_layout->addLayout(post_collision_rift_buttons);
	d_post_collision_rift_instruction_label_ptr = new QLabel(d_post_collision_rift_dialog_ptr);
	d_post_collision_rift_instruction_label_ptr->setWordWrap(true);
	post_collision_rift_layout->addWidget(d_post_collision_rift_instruction_label_ptr);
	d_post_collision_rift_dialog_ptr->resize(590, 760);
	d_post_collision_rift_dialog_ptr->hide();

	// Persistent mantle-event workflow. Active and former LIPs, the mantle-fixed
	// hotspot and its native MotionPath are reviewed as one geological event.
	d_mantle_events_dialog_ptr = new QDialog(this, Qt::Tool);
	d_mantle_events_dialog_ptr->setObjectName("WorldbuildingPastaMantleEventsDialog");
	d_mantle_events_dialog_ptr->setWindowTitle(
			tr("Worldbuilding Pasta - LIP and Hotspot Event"));
	d_mantle_events_dialog_ptr->setModal(false);
	d_mantle_events_dialog_ptr->setAttribute(Qt::WA_DeleteOnClose, false);
	QVBoxLayout *mantle_events_layout = create_scrollable_dialog_layout(d_mantle_events_dialog_ptr);
	QLabel *mantle_events_description = new QLabel(tr(
			"Select continental crust, then optionally a rift. Preview keeps the whole LIP inside the host. Commit creates Artifexia-aligned active/former LIP layers and, if enabled, a mantle-fixed HotSpot plus a native GPlates MotionPath trail."),
			d_mantle_events_dialog_ptr);
	mantle_events_description->setWordWrap(true);
	mantle_events_layout->addWidget(mantle_events_description);
	d_mantle_events_select_continent_button_ptr = new QPushButton(
			tr("1. Select Host Continental Crust"), d_mantle_events_dialog_ptr);
	d_mantle_events_select_continent_button_ptr->setCheckable(true);
	d_mantle_events_continent_status_label_ptr = new QLabel(d_mantle_events_dialog_ptr);
	d_mantle_events_continent_status_label_ptr->setWordWrap(true);
	QHBoxLayout *mantle_rift_buttons = new QHBoxLayout();
	d_mantle_events_select_rift_button_ptr = new QPushButton(
			tr("2. Select Optional Rift"), d_mantle_events_dialog_ptr);
	d_mantle_events_select_rift_button_ptr->setCheckable(true);
	d_mantle_events_clear_rift_button_ptr = new QPushButton(
			tr("Clear Rift"), d_mantle_events_dialog_ptr);
	mantle_rift_buttons->addWidget(d_mantle_events_select_rift_button_ptr);
	mantle_rift_buttons->addWidget(d_mantle_events_clear_rift_button_ptr);
	d_mantle_events_rift_status_label_ptr = new QLabel(d_mantle_events_dialog_ptr);
	d_mantle_events_rift_status_label_ptr->setWordWrap(true);
	mantle_events_layout->addWidget(d_mantle_events_select_continent_button_ptr);
	mantle_events_layout->addWidget(d_mantle_events_continent_status_label_ptr);
	mantle_events_layout->addLayout(mantle_rift_buttons);
	mantle_events_layout->addWidget(d_mantle_events_rift_status_label_ptr);
	QFormLayout *mantle_events_form = new QFormLayout();
	d_mantle_events_placement_combo_ptr = new QComboBox(d_mantle_events_dialog_ptr);
	d_mantle_events_placement_combo_ptr->addItem(tr("Rift-triggered"));
	d_mantle_events_placement_combo_ptr->addItem(tr("Random within host"));
	mantle_events_form->addRow(tr("Placement:"), d_mantle_events_placement_combo_ptr);
	d_mantle_events_diameter_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_diameter_spin_ptr->setRange(50, 4000);
	d_mantle_events_diameter_spin_ptr->setValue(700);
	d_mantle_events_diameter_spin_ptr->setDecimals(0);
	d_mantle_events_diameter_spin_ptr->setSuffix(tr(" km"));
	mantle_events_form->addRow(tr("Requested LIP diameter:"), d_mantle_events_diameter_spin_ptr);
	d_mantle_events_irregularity_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_irregularity_spin_ptr->setRange(0, 75);
	d_mantle_events_irregularity_spin_ptr->setValue(28);
	d_mantle_events_irregularity_spin_ptr->setDecimals(0);
	d_mantle_events_irregularity_spin_ptr->setSuffix(tr(" %"));
	mantle_events_form->addRow(tr("Footprint irregularity:"), d_mantle_events_irregularity_spin_ptr);
	d_mantle_events_segment_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_segment_spin_ptr->setRange(20, 200);
	d_mantle_events_segment_spin_ptr->setValue(75);
	d_mantle_events_segment_spin_ptr->setDecimals(0);
	d_mantle_events_segment_spin_ptr->setSuffix(tr(" km"));
	mantle_events_form->addRow(tr("Maximum boundary segment:"), d_mantle_events_segment_spin_ptr);
	d_mantle_events_seed_spin_ptr = new QSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_seed_spin_ptr->setRange(0, 999999);
	d_mantle_events_seed_spin_ptr->setValue(1);
	mantle_events_form->addRow(tr("Event seed:"), d_mantle_events_seed_spin_ptr);
	d_mantle_events_active_duration_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_active_duration_spin_ptr->setRange(1, 100);
	d_mantle_events_active_duration_spin_ptr->setValue(10);
	d_mantle_events_active_duration_spin_ptr->setDecimals(0);
	d_mantle_events_active_duration_spin_ptr->setSuffix(tr(" Ma"));
	mantle_events_form->addRow(tr("Active LIP duration:"), d_mantle_events_active_duration_spin_ptr);
	d_mantle_events_create_hotspot_check_ptr = new QCheckBox(
			tr("Create hotspot and native trail"), d_mantle_events_dialog_ptr);
	d_mantle_events_create_hotspot_check_ptr->setChecked(true);
	mantle_events_form->addRow(QString(), d_mantle_events_create_hotspot_check_ptr);
	d_mantle_events_hotspot_lifetime_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_hotspot_lifetime_spin_ptr->setRange(10, 400);
	d_mantle_events_hotspot_lifetime_spin_ptr->setValue(150);
	d_mantle_events_hotspot_lifetime_spin_ptr->setDecimals(0);
	d_mantle_events_hotspot_lifetime_spin_ptr->setSuffix(tr(" Ma"));
	mantle_events_form->addRow(tr("Hotspot lifetime:"), d_mantle_events_hotspot_lifetime_spin_ptr);
	d_mantle_events_trail_step_spin_ptr = new QDoubleSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_trail_step_spin_ptr->setRange(1, 50);
	d_mantle_events_trail_step_spin_ptr->setValue(10);
	d_mantle_events_trail_step_spin_ptr->setDecimals(0);
	d_mantle_events_trail_step_spin_ptr->setSuffix(tr(" Ma"));
	mantle_events_form->addRow(tr("MotionPath sample step:"), d_mantle_events_trail_step_spin_ptr);
	d_mantle_events_mantle_plate_spin_ptr = new QSpinBox(d_mantle_events_dialog_ptr);
	d_mantle_events_mantle_plate_spin_ptr->setRange(0, 99999999);
	d_mantle_events_mantle_plate_spin_ptr->setValue(1);
	mantle_events_form->addRow(tr("Mantle Plate ID:"), d_mantle_events_mantle_plate_spin_ptr);
	mantle_events_layout->addLayout(mantle_events_form);
	QHBoxLayout *mantle_events_action_buttons = new QHBoxLayout();
	d_mantle_events_preview_button_ptr = new QPushButton(
			tr("3. Preview Event"), d_mantle_events_dialog_ptr);
	d_mantle_events_commit_button_ptr = new QPushButton(
			tr("4. Commit LIP + Hotspot"), d_mantle_events_dialog_ptr);
	mantle_events_action_buttons->addWidget(d_mantle_events_preview_button_ptr);
	mantle_events_action_buttons->addWidget(d_mantle_events_commit_button_ptr);
	mantle_events_layout->addLayout(mantle_events_action_buttons);
	d_mantle_events_instruction_label_ptr = new QLabel(d_mantle_events_dialog_ptr);
	d_mantle_events_instruction_label_ptr->setWordWrap(true);
	mantle_events_layout->addWidget(d_mantle_events_instruction_label_ptr);
	d_mantle_events_dialog_ptr->resize(570, 760);
	d_mantle_events_dialog_ptr->hide();

	worldbuilding_pasta_layout->addStretch();
	worldbuilding_pasta_scroll_area->setWidget(worldbuilding_pasta_palette);
	d_worldbuilding_pasta_dock_ptr->setWidget(worldbuilding_pasta_scroll_area);
	addDockWidget(Qt::RightDockWidgetArea, d_worldbuilding_pasta_dock_ptr);
	d_worldbuilding_pasta_dock_ptr->setFloating(true);
	d_worldbuilding_pasta_dock_ptr->resize(420, 650);
	d_worldbuilding_pasta_dock_ptr->hide();

	QAction *worldbuilding_pasta_action = d_worldbuilding_pasta_dock_ptr->toggleViewAction();
	worldbuilding_pasta_action->setText(tr("Worldbuilding Pasta..."));
	worldbuilding_pasta_action->setObjectName("action_Worldbuilding_Pasta");
	worldbuilding_pasta_action->setStatusTip(
			tr("Show the Worldbuilding Pasta procedural-generation palette"));
	menu_World_Building->insertAction(action_Naturalize_Coastline, worldbuilding_pasta_action);
	menu_World_Building->insertSeparator(action_Naturalize_Coastline);
	QObject::connect(
			initialize_worldpasta_structure_button,
			&QPushButton::clicked,
			this,
			[this]()
			{
				const QString directory = QFileDialog::getExistingDirectory(
						this,
						tr("Open or Update Worldbuilding Project"),
						get_view_state().get_last_open_directory(),
						QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
				if (directory.isEmpty())
				{
					return;
				}

				get_view_state().get_last_open_directory() = directory;
				const QDir target_directory(directory);
				const QString project_filename = target_directory.filePath("worldpasta.gproj");
				const QString manifest_filename = target_directory.filePath(
						GPlatesAppLogic::WorldbuildingProjectManifest::default_file_name());
				const bool new_manifest = !QFileInfo::exists(manifest_filename);
				GPlatesAppLogic::WorldbuildingProjectManifest::Manifest manifest;
				QString manifest_error;
				if (new_manifest)
				{
					manifest = GPlatesAppLogic::WorldbuildingProjectManifest::default_manifest();
				}
				else if (!GPlatesAppLogic::WorldbuildingProjectManifest::load(
							manifest_filename, manifest, &manifest_error))
				{
					QMessageBox::critical(
							this,
							tr("Cannot Read Worldbuilding Manifest"),
							tr("The existing manifest was not changed:\n\n%1").arg(manifest_error));
					return;
				}

				const GPlatesAppLogic::WorldbuildingProjectManifest::Audit audit =
						GPlatesAppLogic::WorldbuildingProjectManifest::audit(directory, manifest);
				QMessageBox dry_run(this);
				dry_run.setWindowTitle(tr("Worldbuilding Project Dry Run"));
				dry_run.setIcon(audit.has_blocking_issues() ? QMessageBox::Critical : QMessageBox::Information);
				dry_run.setText(new_manifest
						? tr("A new editable manifest and the missing Worldbuilding Pasta collections can be created.")
						: tr("The existing editable manifest was audited. Only missing managed collections can be created."));
				dry_run.setInformativeText(audit.to_plain_text());
				dry_run.setDetailedText(tr(
						"Existing collection contents, layer visibility, order, draw styles, raster connections, reconstruction methods, and feature defaults are never replaced by this update. The manifest's workspace profile remains preview-only until a separate apply action is confirmed."));
				if (audit.has_blocking_issues())
				{
					dry_run.setStandardButtons(QMessageBox::Close);
					dry_run.exec();
					return;
				}
				bool apply_workspace_profile = new_manifest;
				QPushButton *files_only_button = NULL;
				QPushButton *files_and_profile_button = NULL;
				if (new_manifest)
				{
					dry_run.setStandardButtons(QMessageBox::Apply | QMessageBox::Cancel);
					dry_run.setDefaultButton(QMessageBox::Cancel);
				}
				else
				{
					dry_run.setStandardButtons(QMessageBox::Cancel);
					files_only_button = dry_run.addButton(
							tr("Create Missing Files Only"), QMessageBox::AcceptRole);
					files_and_profile_button = dry_run.addButton(
							tr("Create Files + Reapply Profile"), QMessageBox::ActionRole);
				}
				const int dry_run_result = dry_run.exec();
				if ((new_manifest && dry_run_result != QMessageBox::Apply) ||
					(!new_manifest && dry_run.clickedButton() != files_only_button &&
					 dry_run.clickedButton() != files_and_profile_button))
				{
					return;
				}
				if (!new_manifest)
				{
					apply_workspace_profile = dry_run.clickedButton() == files_and_profile_button;
				}

				QStringList collection_file_names = audit.missing_files;
				// Preserve the full Artifexia filing system on first creation. The manifest
				// assigns stable roles to the collections used by automated operations.
				if (new_manifest)
				{
					for (unsigned int index = 0;
						 index < sizeof(WORLDPASTA_COLLECTION_FILENAMES) /
								 sizeof(WORLDPASTA_COLLECTION_FILENAMES[0]);
						 ++index)
					{
						const QString file_name = QString::fromLatin1(WORLDPASTA_COLLECTION_FILENAMES[index]);
						if (!QFileInfo::exists(target_directory.filePath(file_name)) &&
							!collection_file_names.contains(file_name, Qt::CaseInsensitive))
						{
							collection_file_names.append(file_name);
						}
					}
				}

				QStringList unwritable_filenames;
				BOOST_FOREACH(const QString &collection_file_name, collection_file_names)
				{
					const QString target_filename = target_directory.filePath(collection_file_name);
					if (!GPlatesFileIO::is_writable(target_filename))
					{
						unwritable_filenames.append(QFileInfo(target_filename).fileName());
					}
				}
				if (new_manifest && !GPlatesFileIO::is_writable(manifest_filename))
				{
					unwritable_filenames.append(QFileInfo(manifest_filename).fileName());
				}
				if (!unwritable_filenames.isEmpty())
				{
					QMessageBox::critical(
							this,
							tr("Cannot Initialize Worldpasta Structure"),
							tr("The selected directory is not writable for: %1")
									.arg(unwritable_filenames.join(", ")));
					return;
				}

				if (new_manifest && !GPlatesAppLogic::WorldbuildingProjectManifest::save(
							manifest_filename, manifest, &manifest_error))
				{
					QMessageBox::critical(
							this,
							tr("Cannot Create Worldbuilding Manifest"),
							manifest_error);
					return;
				}

				QStringList created_filenames;
				BOOST_FOREACH(const QString &collection_file_name, collection_file_names)
				{
					const QString collection_filename = target_directory.filePath(collection_file_name);
					const GPlatesModel::FeatureCollectionHandle::non_null_ptr_type collection =
							GPlatesModel::FeatureCollectionHandle::create();
					const GPlatesFileIO::File::non_null_ptr_type file =
							GPlatesFileIO::File::create_file(
									GPlatesFileIO::FileInfo(collection_filename), collection);
					if (!file_io_feedback().create_file(file))
					{
						const QString message = tr(
								"Worldpasta initialization stopped while creating '%1'. %2 collection file(s) were already saved and loaded; no existing file was overwritten.")
									.arg(QFileInfo(collection_filename).fileName())
									.arg(created_filenames.size());
						status_message(message);
						QMessageBox::critical(
								this, tr("Worldpasta Initialization Incomplete"), message);
						return;
					}
					created_filenames.append(collection_filename);
				}

				ArtifexiaPresetResult preset_result = { 0, 0, 0 };
				unsigned int manifest_profile_layer_count = 0;
				if (new_manifest)
				{
					preset_result = apply_artifexia_presentation(*this);
				}
				else if (apply_workspace_profile)
				{
					manifest_profile_layer_count = apply_worldbuilding_workspace_profile(*this, manifest);
				}
				if (new_manifest && !file_io_feedback().save_project(project_filename))
				{
					const QString message = tr(
							"The %1 Artifexia-style collection files were saved and loaded, but the visual project could not be saved as '%2'. You can retry with File > Save Project As.")
								.arg(created_filenames.size())
								.arg(QFileInfo(project_filename).fileName());
					status_message(message);
					QMessageBox::warning(this, tr("Worldpasta Project Not Saved"), message);
					return;
				}

				const QString message = new_manifest
						? tr("Created and loaded %1 missing collection files in %2. Saved %3, saved the initial presentation as %4, and applied styles to %5 matching layer(s). Future updates preserve user customisations.")
							.arg(created_filenames.size())
							.arg(QDir::toNativeSeparators(directory))
							.arg(QFileInfo(manifest_filename).fileName())
							.arg(QFileInfo(project_filename).fileName())
							.arg(preset_result.matching_layer_count)
						: apply_workspace_profile
							? tr("Loaded %1 missing collection files from %2 and explicitly reapplied visibility, order, and draw style to %3 matching layers. Raster connections, reconstruction presets, and feature defaults remain recorded for operation-specific adapters.")
									.arg(created_filenames.size())
									.arg(QFileInfo(manifest_filename).fileName())
									.arg(manifest_profile_layer_count)
							: tr("Loaded %1 missing collection files from %2. The existing project and workspace customisations were not changed.")
									.arg(created_filenames.size())
									.arg(QFileInfo(manifest_filename).fileName());
				status_message(message);
				QMessageBox::information(this, tr("Worldbuilding Project Updated"), message);
			});
	QObject::connect(
			create_initial_continent_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_create_initial_continent()));
	QObject::connect(
			propose_initial_rifts_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_propose_initial_rifts()));
	QObject::connect(
			show_make_rift_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_make_rift_window()));
	QObject::connect(
			create_initial_rotation_file_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_create_initial_rotation_file()));
	QObject::connect(
			show_initial_subduction_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_initial_subduction_window()));
	QObject::connect(
			advance_plate_motion_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_advance_plate_motion()));
	QObject::connect(
			create_ocean_crust_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_create_ocean_crust()));
	QObject::connect(
			create_triple_junction_crust_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_create_triple_junction_crust()));
	QObject::connect(
			create_pacific_plate_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_create_pacific_plate()));
	QObject::connect(
			retire_ocean_crust_button,
			SIGNAL(clicked()),
			this,
			SLOT(handle_subduction_cutter()));
	QObject::connect(
			show_subduction_effects_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_subduction_effects_window()));
	QObject::connect(
			show_collision_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_collision_orogeny_window()));
	QObject::connect(
			show_post_collision_rift_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_post_collision_rift_window()));
	QObject::connect(
			show_mantle_events_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_mantle_events_window()));
	QObject::connect(
			artifexia_preset_button,
			SIGNAL(clicked()),
			this,
			SLOT(apply_artifexia_preset()));
	QObject::connect(
			show_boolean_polygons_button,
			SIGNAL(clicked()),
			this,
			SLOT(show_boolean_polygons_window()));
	QObject::connect(
			d_boolean_select_first_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_select_first()));
	QObject::connect(
			d_boolean_select_operand_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_select_operand()));
	QObject::connect(
			d_boolean_remove_operand_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_remove_operand()));
	QObject::connect(
			d_boolean_clear_operands_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_clear_operands()));
	QObject::connect(
			d_boolean_preview_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_preview()));
	QObject::connect(
			d_boolean_apply_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_apply()));
	QObject::connect(
			d_boolean_cancel_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_boolean_cancel()));
	QObject::connect(
			d_boolean_operation_combo_ptr,
			QOverload<int>::of(&QComboBox::currentIndexChanged),
			this,
			[this](int)
			{
				d_boolean_polygon_operation_ptr->clear_preview();
				update_boolean_palette(tr("Operation changed; preview the new result before applying."));
			});
	QObject::connect(
			d_boolean_polygon_dialog_ptr,
			SIGNAL(rejected()),
			this,
			SLOT(handle_boolean_cancel()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_boolean_focus_changed(GPlatesGui::FeatureFocus &)));
	QObject::connect(
			&get_view_state().get_animation_controller(),
			&GPlatesGui::AnimationController::view_time_changed,
			this,
			[this](double)
			{
				d_boolean_polygon_operation_ptr->reset();
				if (d_boolean_polygon_dialog_ptr->isVisible())
				{
					update_boolean_palette(tr(
							"View time changed; Boolean selections and preview were cleared."));
				}
			});
	update_boolean_palette();
	QObject::connect(
			d_make_rift_select_continent_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_make_rift_select_continent()));
	QObject::connect(
			d_make_rift_select_rift_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_make_rift_select_rift()));
	QObject::connect(
			d_make_rift_cut_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_make_rift()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_make_rift_focus_changed(GPlatesGui::FeatureFocus &)));
	update_make_rift_palette();
	QObject::connect(
			d_initial_subduction_select_continent_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_initial_subduction_select_continent()));
	QObject::connect(
			d_initial_subduction_select_mor_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_initial_subduction_select_mor()));
	QObject::connect(
			d_initial_subduction_generate_button_ptr,
			SIGNAL(clicked()),
			this,
			SLOT(handle_generate_initial_subduction()));
	QObject::connect(
			d_initial_subduction_dialog_ptr,
			SIGNAL(rejected()),
			this,
			SLOT(handle_initial_subduction_cancel_selection()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_initial_subduction_focus_changed(GPlatesGui::FeatureFocus &)));
	update_initial_subduction_palette();
	QObject::connect(
			d_subduction_effects_select_subduction_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_subduction_effects_select_subduction()));
	QObject::connect(
			d_subduction_effects_select_continent_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_subduction_effects_select_continent()));
	QObject::connect(
			d_subduction_effects_clear_continent_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_subduction_effects_clear_continent()));
	QObject::connect(
			d_subduction_effects_preview_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_subduction_effects_preview()));
	QObject::connect(
			d_subduction_effects_commit_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_subduction_effects_commit()));
	QObject::connect(
			d_subduction_effect_type_combo_ptr,
			SIGNAL(currentIndexChanged(int)), this, SLOT(handle_subduction_effect_type_changed(int)));
	QObject::connect(
			d_subduction_effects_flip_polarity_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_subduction_effects_controls_changed()));
	QObject::connect(
			d_subduction_effects_early_arc_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_subduction_effects_controls_changed()));
	for (unsigned int index = 0; index < sizeof(spin_specs) / sizeof(spin_specs[0]); ++index)
	{
		QObject::connect(*spin_specs[index].target,
				SIGNAL(valueChanged(double)), this, SLOT(handle_subduction_effects_controls_changed()));
	}
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_subduction_effects_focus_changed(GPlatesGui::FeatureFocus &)));
	update_subduction_effects_palette();
	QObject::connect(
			d_collision_select_incoming_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_select_incoming()));
	QObject::connect(
			d_collision_select_receiving_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_select_receiving()));
	QObject::connect(
			d_collision_select_trench_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_select_trench()));
	QObject::connect(
			d_collision_clear_trench_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_clear_trench()));
	QObject::connect(
			d_collision_preview_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_preview()));
	QObject::connect(
			d_collision_commit_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_collision_commit()));
	QObject::connect(
			d_collision_type_combo_ptr,
			SIGNAL(currentIndexChanged(int)), this, SLOT(handle_collision_type_changed(int)));
	QObject::connect(
			d_collision_precursor_spin_ptr,
			SIGNAL(valueChanged(int)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_smoothing_spin_ptr,
			SIGNAL(valueChanged(int)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_contact_threshold_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_belt_width_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_irregularity_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_active_duration_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_old_age_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_auto_width_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_terminate_trench_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_allow_nonconvergent_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_deform_margins_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_deformation_reach_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			d_collision_retire_incoming_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_collision_controls_changed()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_collision_focus_changed(GPlatesGui::FeatureFocus &)));
	update_collision_palette();
	QObject::connect(
			d_post_collision_rift_select_host_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_post_collision_rift_select_host()));
	QObject::connect(
			d_post_collision_rift_select_suture_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_post_collision_rift_select_suture()));
	QObject::connect(
			d_post_collision_rift_preview_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_post_collision_rift_preview()));
	QObject::connect(
			d_post_collision_rift_commit_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_post_collision_rift_commit()));
	QObject::connect(
			d_post_collision_rift_offset_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			d_post_collision_rift_wiggle_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			d_post_collision_rift_segment_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			d_post_collision_rift_extension_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			d_post_collision_rift_side_combo_ptr,
			SIGNAL(currentIndexChanged(int)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			d_post_collision_rift_seed_spin_ptr,
			SIGNAL(valueChanged(int)), this, SLOT(handle_post_collision_rift_controls_changed()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_post_collision_rift_focus_changed(GPlatesGui::FeatureFocus &)));
	update_post_collision_rift_palette();
	QObject::connect(
			d_mantle_events_select_continent_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_mantle_events_select_continent()));
	QObject::connect(
			d_mantle_events_select_rift_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_mantle_events_select_rift()));
	QObject::connect(
			d_mantle_events_clear_rift_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_mantle_events_clear_rift()));
	QObject::connect(
			d_mantle_events_preview_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_mantle_events_preview()));
	QObject::connect(
			d_mantle_events_commit_button_ptr,
			SIGNAL(clicked()), this, SLOT(handle_mantle_events_commit()));
	QObject::connect(
			d_mantle_events_placement_combo_ptr,
			SIGNAL(currentIndexChanged(int)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_diameter_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_irregularity_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_segment_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_seed_spin_ptr,
			SIGNAL(valueChanged(int)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_active_duration_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_create_hotspot_check_ptr,
			SIGNAL(toggled(bool)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_hotspot_lifetime_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_trail_step_spin_ptr,
			SIGNAL(valueChanged(double)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			d_mantle_events_mantle_plate_spin_ptr,
			SIGNAL(valueChanged(int)), this, SLOT(handle_mantle_events_controls_changed()));
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(handle_mantle_events_focus_changed(GPlatesGui::FeatureFocus &)));
	update_mantle_events_palette();

	// Connect all the Signal/Slot relationships of ViewportWindow's
	// toolbar buttons and menu items.
	connect_menu_actions();

	// World-building operations are kept in a dedicated menu so experimental,
	// recipe-driven generators do not crowd the core reconstruction menu.
	QMenu *world_building_menu = menuBar()->addMenu(tr("&World Building"));
	QAction *manage_flowlines_action = world_building_menu->addAction(tr("Manage Flowlines..."));
	GPlatesViewOperations::FlowlineManagerOperation *flowline_manager_operation =
			new GPlatesViewOperations::FlowlineManagerOperation(
					get_application_state(), get_view_state(), this);
	flowline_manager_operation->setParent(this);
	QObject::connect(
			manage_flowlines_action, SIGNAL(triggered()),
			flowline_manager_operation, SLOT(trigger()));
	QAction *generate_craters_action = world_building_menu->addAction(tr("Generate Impact Craters..."));
	GPlatesViewOperations::CraterGeneratorOperation *crater_generator_operation =
			new GPlatesViewOperations::CraterGeneratorOperation(get_application_state(), this);
	crater_generator_operation->setParent(this);
	QObject::connect(
			generate_craters_action, SIGNAL(triggered()),
			crater_generator_operation, SLOT(trigger()));

	// Duplicate the menu structure for the full-screen-mode GMenu.
	populate_gmenu_from_menubar();

	// Initialise various elements for full-screen-mode that must wait until after setupUi().
	d_full_screen_mode->init();

	// Initialise the Recent Session menu (that must wait until after setupUi()).
	d_session_menu_ptr->init(*menu_Open_Recent_Session);
	
	// FIXME: Set up the Task Panel in a more detailed fashion here.
#if 1
	d_reconstruction_view_widget_ptr->insert_task_panel(d_task_panel_ptr);
#else
	// Stretchable Task Panel hack for testing: Make the Task Panel
	// into a QDockWidget, undocked by default.
	QDockWidget *task_panel_dock = new QDockWidget(tr("Task Panel"), this);
	task_panel_dock->setObjectName("TaskPanelDock");
	task_panel_dock->setWidget(d_task_panel_ptr);
	task_panel_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	addDockWidget(Qt::RightDockWidgetArea, task_panel_dock);
	task_panel_dock->setFloating(true);
#endif
	set_up_task_panel_actions();

	// Disable the feature-specific Actions as there is no currently focused feature to act on.
	enable_or_disable_feature_actions(get_view_state().get_feature_focus());
	QObject::connect(
			&get_view_state().get_feature_focus(),
			SIGNAL(focus_changed(GPlatesGui::FeatureFocus &)),
			this,
			SLOT(enable_or_disable_feature_actions(GPlatesGui::FeatureFocus &)));

	// Set up the Reconstruction View widget.
	setCentralWidget(d_reconstruction_view_widget_ptr);

	// Listen for file read errors so can update/display read errors dialog.
	QObject::connect(
			&get_application_state().get_feature_collection_file_io(),
			SIGNAL(handle_read_errors(
					const GPlatesFileIO::ReadErrorAccumulation &)),
			this,
			SLOT(handle_read_errors(
					const GPlatesFileIO::ReadErrorAccumulation &)));


	// After modifying the move-nearby-vertices widget, this will reset the focus to the globe/map.
	// FIXME: Find a more general way to do this for changes made by the user in the task panel tabs.
	QObject::connect(
			d_modify_geometry_state.get(),
			SIGNAL(snap_vertices_setup_changed(bool,double,bool,GPlatesModel::integer_plate_id_type)),
			d_reconstruction_view_widget_ptr,
			SLOT(setFocus()));

	// Initialise the "Trinket Area", a class which manages the various icons present in the
	// status bar. This must occur after ViewportWindow::setupUi().
	d_trinket_area_ptr->init();

	// Initialise the "Unsaved Changes" tracking aspect of the GUI, now that setupUi() has
	// been called and all the widgets that are used to notify the user are in place.
	d_unsaved_changes_tracker_ptr->init();

	// Synchronise the "Show X Features" menu items with RenderSettings.
	GPlatesGui::RenderSettings &render_settings = get_view_state().get_render_settings();
	action_Show_Static_Points->setChecked(render_settings.show_static_points());
	action_Show_Static_Lines->setChecked(render_settings.show_static_lines());
	action_Show_Static_Polygons->setChecked(render_settings.show_static_polygons());
	action_Show_Static_Multipoints->setChecked(render_settings.show_static_multipoints());
	action_Show_Velocity_Arrows->setChecked(render_settings.show_velocity_arrows());
	action_Show_Topological_Sections->setChecked(render_settings.show_topological_sections());
	action_Show_Topological_Lines->setChecked(render_settings.show_topological_lines());
	action_Show_Topological_Polygons->setChecked(render_settings.show_topological_polygons());
	action_Show_Topological_Networks->setChecked(render_settings.show_topological_networks());
	action_Show_Rasters->setChecked(render_settings.show_rasters());
	action_Show_3D_Scalar_Fields->setChecked(render_settings.show_3d_scalar_fields());
	action_Show_Scalar_Coverages->setChecked(render_settings.show_scalar_coverages());

	// Synchronise "Show Stars" with what's in ViewState.
	action_Show_Stars->setChecked(get_view_state().get_show_stars());

	// Repaint the globe/map when the colour scheme delegator's target changes.
	QObject::connect(
			get_view_state().get_colour_scheme_delegator().get(),
			SIGNAL(changed()),
			this,
			SLOT(handle_colour_scheme_delegator_changed()));

	// Get notified about visual layers being added so we can open the layers dialog.
	QObject::connect(
			&(get_view_state().get_visual_layers()),
			SIGNAL(layer_added(size_t)),
			this,
			SLOT(handle_visual_layer_added(size_t)));

	// Get notified about project filename changes so we can change the project filename in
	// the window title.
	QObject::connect(
			&(get_view_state().get_session_management()),
			SIGNAL(changed_project_filename(boost::optional<QString>)),
			this,
			SLOT(handle_changed_project_filename(boost::optional<QString>)));

	set_window_title();

    // Create the visual layers dialog (but don't show it yet).
    // This is so it can listen for signals before it first pops up
    // (which is when the first layer is added).
	dialogs().visual_layers_dialog();
}


GPlatesQtWidgets::ViewportWindow::~ViewportWindow()
{
	// boost::scoped_ptr destructors need complete type.
}


void
GPlatesQtWidgets::ViewportWindow::load_project(
		const QString &project_filename)
{
	d_file_io_feedback_ptr->open_project(project_filename);
}


void
GPlatesQtWidgets::ViewportWindow::load_feature_collections(
		const QStringList &filenames)
{
	d_file_io_feedback_ptr->open_files(filenames);
}


void
GPlatesQtWidgets::ViewportWindow::display()
{
	//
	// First show the main window so that everything is visible.
	//

	show();

	//
	// Then call functions that rely on the main window being visible.
	//

	// Re-position the visual layers dialog relative to the main window.
	//
	// We do this here because the visual layers dialog now gets created in the ViewportWindow
	// constructor (so it can receive signals earlier) and hence parent (ViewportWindow) geometry
	// queries are not valid (because ViewportWindow is not yet visible at that stage).
	GPlatesQtWidgets::QtWidgetUtils::reposition_to_side_of_parent(&dialogs().visual_layers_dialog());

	// Activate the default canvas tool which checks for visibility of the main view canvas.
	// In particular this needs to be done after the globe canvas is visible otherwise the default
	// canvas tool will not get activated.
	canvas_tool_workflows().activate();
}


void
GPlatesQtWidgets::ViewportWindow::handle_read_errors(
		const GPlatesFileIO::ReadErrorAccumulation &new_read_errors)
{
	// Pop up errors only if we have new read errors.
	if (new_read_errors.is_empty())
	{
		return;
	}

	// Populate the read errors dialog.
	ReadErrorAccumulationDialog &read_errors_dialog = dialogs().read_error_accumulation_dialog();
	read_errors_dialog.clear();
	read_errors_dialog.read_errors().accumulate(new_read_errors);
	read_errors_dialog.update();
	
	// At this point we can either throw the dialog in the user's face,
	// or pop up a small icon in the status bar which they can click to see the errors.
	// How do we decide? Well, until we get UserPreferences, let's just pop up the icon
	// on warnings, and show the whole dialog on any kind of real error.
	GPlatesFileIO::ReadErrors::Severity severity = new_read_errors.most_severe_error_type();
	if (severity > GPlatesFileIO::ReadErrors::Warning)
	{
		read_errors_dialog.show();
	}
	else
	{
		d_trinket_area_ptr->read_errors_trinket().setVisible(true);
	}
}


void	
GPlatesQtWidgets::ViewportWindow::connect_menu_actions()
{
	// If you want to add a new menu action, the steps are:
	// 0. Open ViewportWindowUi.ui in the Designer.
	// 1. Create a QAction in the Designer's Action Editor, called action_Something.
	// 2. Assign icons, tooltips, and shortcuts as necessary.
	// 3. Drag this action to a menu.
	// 4. If your shortcut key uses 'Ctrl', it is most likely an Application shortcut
	//    that should be usable from within any window or non-modal dialog of GPlates.
	//    It's not immediately obvious how to set this via Designer.
	//    First, select the QAction, either in the Action Editor or the Object inspector.
	//    This will adjust the Property Editor window - find the "shortcutContext" property,
	//    and set it to "ApplicationShortcut".
	// 5. Add code for the triggered() signal your action generates here.
	//    Please keep this function sorted in the same order as menu items appear.

	connect_file_menu_actions();
	connect_edit_menu_actions();
	connect_view_menu_actions();
	connect_features_menu_actions();
	connect_reconstruction_menu_actions();
	connect_utilities_menu_actions();
	connect_tools_menu_actions();
	connect_world_building_menu_actions();
	connect_window_menu_actions();
	connect_help_menu_actions();
}


void
GPlatesQtWidgets::ViewportWindow::connect_file_menu_actions()
{
	QObject::connect(action_Open_Feature_Collection, SIGNAL(triggered()),
			d_file_io_feedback_ptr, SLOT(open_files()));

	QObject::connect(action_Open_Project, SIGNAL(triggered()),
			d_file_io_feedback_ptr, SLOT(open_project()));

	QObject::connect(action_Save_Project, SIGNAL(triggered()),
			d_file_io_feedback_ptr, SLOT(save_project()));

	QObject::connect(action_Save_Project_As, SIGNAL(triggered()),
			d_file_io_feedback_ptr, SLOT(save_project_as()));

	QObject::connect(action_Clear_Session, SIGNAL(triggered()),
			d_file_io_feedback_ptr, SLOT(clear_session()));

	// Show the status tips related to projects/sessions in the status bar.
	// This is needed since the status tips don't appear to show on some platforms.
	QObject::connect(
			action_Open_Project, SIGNAL(hovered()),
			this, SLOT(show_menu_item_status_tip_in_status_bar()));
	QObject::connect(
			action_Save_Project, SIGNAL(hovered()),
			this, SLOT(show_menu_item_status_tip_in_status_bar()));
	QObject::connect(
			action_Save_Project_As, SIGNAL(hovered()),
			this, SLOT(show_menu_item_status_tip_in_status_bar()));
	QObject::connect(
			action_Clear_Session, SIGNAL(hovered()),
			this, SLOT(show_menu_item_status_tip_in_status_bar()));
	QObject::connect(
			menu_Open_Recent_Session, SIGNAL(aboutToShow()),
			this, SLOT(show_menu_item_status_tip_in_status_bar()));

	// ----
	// Import submenu logic is handled by the ImportMenu class.
	// Note: items to the Import submenu should be added programmatically, through
	// @a d_import_menu_ptr, instead of via the designer.
	d_import_menu_ptr = new GPlatesGui::ImportMenu(
			menu_Import,
			menu_File,
			this);
	// Import raster...
	d_import_menu_ptr->add_import(
			GPlatesGui::ImportMenu::RASTER,
			"Import &Raster...",
			boost::bind(&ViewportWindow::pop_up_import_raster_dialog, boost::ref(*this)));
	// Import time-dependent raster...
	d_import_menu_ptr->add_import(
			GPlatesGui::ImportMenu::RASTER,
			"Import &Time-Dependent Raster...",
			boost::bind(&ViewportWindow::pop_up_import_time_dependent_raster_dialog, boost::ref(*this)));
	// Import 3D scalar field...
	d_import_menu_ptr->add_import(
			GPlatesGui::ImportMenu::SCALAR_FIELD_3D,
			"Import 3D &Scalar Field...",
			boost::bind(&ViewportWindow::pop_up_import_scalar_field_3d_dialog, boost::ref(*this)));

	// ----
	QObject::connect(action_Manage_Feature_Collections, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_manage_feature_collections_dialog()));
	QObject::connect(action_File_Errors, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_read_error_accumulation_dialog()));

	// ----
	QObject::connect(action_Quit, SIGNAL(triggered()),
			this, SLOT(close()));

	// Qt6 removed the QtXmlPatterns module providing support for XPath, XQuery, XSLT, and XML Schema validation.
	// It has been deprecated since Qt 5.13.
	//
	// TODO: Find a replacement library.
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
	QObject::connect(actionConnect_WFS, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_connect_wfs_dialog()));
#endif
}


void
GPlatesQtWidgets::ViewportWindow::connect_edit_menu_actions()
{
	// Unfortunately, the Undo and Redo actions cannot be added in the Designer,
	// or at least, not nicely. We need to ask the QUndoGroup to create some
	// QActions for us, and add them programmatically. To follow the principle
	// of least surprise, placeholder actions are set up in the designer, which
	// this code can use to insert the actions in the correct place with the
	// correct shortcut.
	// The new actions will be linked to the QUndoGroup appropriately.
	d_undo_action_ptr->setShortcut(action_Undo_Placeholder->shortcut());
	d_redo_action_ptr->setShortcut(action_Redo_Placeholder->shortcut());
	d_undo_action_ptr->setIcon(action_Undo_Placeholder->icon());
	d_redo_action_ptr->setIcon(action_Redo_Placeholder->icon());
	menu_Edit->insertAction(action_Undo_Placeholder, d_undo_action_ptr);
	menu_Edit->insertAction(action_Redo_Placeholder, d_redo_action_ptr);
	menu_Edit->removeAction(action_Undo_Placeholder);
	menu_Edit->removeAction(action_Redo_Placeholder);
	QObject::connect(
			d_undo_action_ptr,
			SIGNAL(changed()),
			this,
			SLOT(update_undo_action_tooltip()));
	QObject::connect(
			d_redo_action_ptr,
			SIGNAL(changed()),
			this,
			SLOT(update_redo_action_tooltip()));
	add_shortcut_to_tooltip(d_undo_action_ptr);
	add_shortcut_to_tooltip(d_redo_action_ptr);
	// ----
	QObject::connect(action_Query_Feature, SIGNAL(triggered()),
			&dialogs().feature_properties_dialog(), SLOT(choose_query_widget_and_open()));
	QObject::connect(action_Edit_Feature, SIGNAL(triggered()),
			&dialogs().feature_properties_dialog(), SLOT(choose_edit_widget_and_open()));
	QObject::connect(action_Clone_Geometry, SIGNAL(triggered()),
			d_clone_operation_ptr.get(), SLOT(clone_focused_geometry()));
	QObject::connect(action_Clone_Feature, SIGNAL(triggered()),
			this, SLOT(clone_feature_with_dialog()));
	QObject::connect(action_Delete_Feature, SIGNAL(triggered()),
			d_delete_feature_operation_ptr.get(), SLOT(delete_focused_feature()));
	add_shortcut_to_tooltip(action_Query_Feature);
	add_shortcut_to_tooltip(action_Edit_Feature);
	add_shortcut_to_tooltip(action_Clone_Geometry);
	add_shortcut_to_tooltip(action_Clone_Feature);
	add_shortcut_to_tooltip(action_Delete_Feature);
	// ----
	// Replace action_Clear_Placeholder with the TaskPanel's clear action.
	QAction *clear_action_ptr = d_task_panel_ptr->get_clear_action();
	clear_action_ptr->setShortcut(action_Clear_Placeholder->shortcut());
	menu_Edit->insertAction(action_Clear_Placeholder, clear_action_ptr);
	menu_Edit->removeAction(action_Clear_Placeholder);
	// ----
	QObject::connect(action_Preferences, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_preferences_dialog()));
}


void
GPlatesQtWidgets::ViewportWindow::connect_view_menu_actions()
{
	QObject::connect(action_Set_Projection, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_set_projection_dialog()));
	// ----
	QObject::connect(action_Set_Camera_Viewpoint, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_set_camera_viewpoint_dialog()));
	QObject::connect(action_Move_Camera_Up, SIGNAL(triggered()),
			this, SLOT(handle_move_camera_up()));
	QObject::connect(action_Move_Camera_Down, SIGNAL(triggered()),
			this, SLOT(handle_move_camera_down()));
	QObject::connect(action_Move_Camera_Left, SIGNAL(triggered()),
			this, SLOT(handle_move_camera_left()));
	QObject::connect(action_Move_Camera_Right, SIGNAL(triggered()),
			this, SLOT(handle_move_camera_right()));
	// ----
	QObject::connect(action_Rotate_Camera_Clockwise, SIGNAL(triggered()),
			this, SLOT(handle_rotate_camera_clockwise()));
	QObject::connect(action_Rotate_Camera_Anticlockwise, SIGNAL(triggered()),
			this, SLOT(handle_rotate_camera_anticlockwise()));
	QObject::connect(action_Reset_Camera_Orientation, SIGNAL(triggered()),
			this, SLOT(handle_reset_camera_orientation()));
	// ----
	QObject::connect(action_Set_Zoom, SIGNAL(triggered()),
			d_reconstruction_view_widget_ptr, SLOT(activate_zoom_spinbox()));
	QObject::connect(action_Zoom_In, SIGNAL(triggered()),
			&get_view_state().get_viewport_zoom(), SLOT(zoom_in()));
	QObject::connect(action_Zoom_Out, SIGNAL(triggered()),
			&get_view_state().get_viewport_zoom(), SLOT(zoom_out()));
	QObject::connect(action_Reset_Zoom_Level, SIGNAL(triggered()),
			&get_view_state().get_viewport_zoom(), SLOT(reset_zoom()));
	// ----
	QObject::connect(action_Configure_Text_Overlay, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_configure_text_overlay_dialog()));
	QObject::connect(action_Configure_Velocity_Legend, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_configure_velocity_legend_overlay_dialog()));
	QObject::connect(action_Configure_Graticules, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_configure_graticules_dialog()));
	QObject::connect(action_Choose_Background_Colour, SIGNAL(triggered()),
			this, SLOT(pop_up_background_colour_picker()));
	QObject::connect(action_Show_Stars, SIGNAL(triggered()),
			this, SLOT(enable_stars_display()));
	// ----
	QObject::connect(action_Show_Static_Points, SIGNAL(triggered()),
			this, SLOT(enable_static_point_display()));
	QObject::connect(action_Show_Static_Lines, SIGNAL(triggered()),
			this, SLOT(enable_static_line_display()));
	QObject::connect(action_Show_Static_Polygons, SIGNAL(triggered()),
			this, SLOT(enable_static_polygon_display()));
	QObject::connect(action_Show_Static_Multipoints, SIGNAL(triggered()),
			this, SLOT(enable_static_multipoint_display()));
	QObject::connect(action_Show_Velocity_Arrows, SIGNAL(triggered()),
			this, SLOT(enable_velocity_arrow_display()));
	QObject::connect(action_Show_Topological_Sections, SIGNAL(triggered()),
			this, SLOT(enable_topological_section_display()));
	QObject::connect(action_Show_Topological_Lines, SIGNAL(triggered()),
			this, SLOT(enable_topological_line_display()));
	QObject::connect(action_Show_Topological_Polygons, SIGNAL(triggered()),
			this, SLOT(enable_topological_polygon_display()));
	QObject::connect(action_Show_Topological_Networks, SIGNAL(triggered()),
			this, SLOT(enable_topological_network_display()));
	QObject::connect(action_Show_Rasters, SIGNAL(triggered()),
			this, SLOT(enable_raster_display()));
	QObject::connect(action_Show_3D_Scalar_Fields, SIGNAL(triggered()),
			this, SLOT(enable_3d_scalar_field_display()));
	QObject::connect(action_Show_Scalar_Coverages, SIGNAL(triggered()),
			this, SLOT(enable_scalar_coverage_display()));
	QObject::connect(action_Show_All_Geometries, SIGNAL(triggered()),
			this, SLOT(enable_all_geometries_display()));
	// Also update the GUI when the RenderSettings change.
	QObject::connect(&get_view_state().get_render_settings(), SIGNAL(settings_changed()),
			this, SLOT(handle_render_settings_changed()));
}


void
GPlatesQtWidgets::ViewportWindow::connect_features_menu_actions()
{
	if(!GPlatesUtils::ComponentManager::instance().is_enabled(GPlatesUtils::ComponentManager::Component::python()))
	{
		QObject::connect(action_Manage_Colouring, SIGNAL(triggered()),
				&dialogs(), SLOT(pop_up_colouring_dialog()));
	}
	else
	{
		QObject::connect(action_Manage_Colouring, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_draw_style_dialog()));
	}
	// ----
	QObject::connect(action_Load_Symbol, SIGNAL(triggered()),
			this, SLOT(handle_load_symbol_file()));
	QObject::connect(action_Unload_Symbol, SIGNAL(triggered()),
			this, SLOT(handle_unload_symbol_file()));
	// ----
	QObject::connect(action_View_Total_Reconstruction_Sequences, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_total_reconstruction_sequences_dialog()));
	QObject::connect(action_View_Shapefile_Attributes, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_shapefile_attribute_viewer_dialog()));
	// ----
	QObject::connect(action_Create_VGP, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_create_vgp_dialog()));
	QObject::connect(action_Assign_Plate_IDs, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_assign_reconstruction_plate_ids_dialog()));
	QObject::connect(action_Generate_Citcoms_Velocity_Domain, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_velocity_domain_citcoms_dialog()));
	QObject::connect(action_Generate_Terra_Velocity_Domain, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_velocity_domain_terra_dialog()));
	QObject::connect(action_Generate_LatitudeLongitude_Velocity_Domain, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_velocity_domain_lat_lon_dialog()));
	QObject::connect(action_Generate_Deforming_Mesh_Points, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_generate_deforming_mesh_points_dialog()));
}


void
GPlatesQtWidgets::ViewportWindow::remember_created_feature(
		GPlatesModel::FeatureHandle::weak_ref feature)
{
	d_last_created_feature = feature;
	d_select_last_created_feature_action->setEnabled(feature.is_valid());
}


void
GPlatesQtWidgets::ViewportWindow::select_last_created_feature()
{
	if (!d_last_created_feature.is_valid())
	{
		d_select_last_created_feature_action->setEnabled(false);
		status_message(tr("The last-created feature is no longer available."));
		return;
	}

	GPlatesGui::FeatureFocus &feature_focus = get_view_state().get_feature_focus();
	feature_focus.set_focus(d_last_created_feature);
	if (feature_focus.focused_feature() != d_last_created_feature)
	{
		status_message(tr("The last-created feature has no geometry and cannot be selected."));
		return;
	}
	status_message(tr("Selected the last-created feature."));
}


void
GPlatesQtWidgets::ViewportWindow::connect_reconstruction_menu_actions()
{
	QObject::connect(action_Reconstruct_to_Time, SIGNAL(triggered()),
			d_reconstruction_view_widget_ptr, SLOT(activate_time_spinbox()));
	QObject::connect(action_Increment_Animation_Time_Forwards, SIGNAL(triggered()),
			&get_view_state().get_animation_controller(), SLOT(step_forward()));
	QObject::connect(action_Increment_Animation_Time_Backwards, SIGNAL(triggered()),
			&get_view_state().get_animation_controller(), SLOT(step_back()));
	QObject::connect(action_Reset_Animation, SIGNAL(triggered()),
			&get_view_state().get_animation_controller(), SLOT(seek_beginning()));
	QObject::connect(action_Play, SIGNAL(triggered(bool)),
			&get_view_state().get_animation_controller(), SLOT(set_play_or_pause(bool)));
	QObject::connect(&get_view_state().get_animation_controller(), SIGNAL(animation_state_changed(bool)),
			action_Play, SLOT(setChecked(bool)));
	QObject::connect(action_Animate, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_animate_dialog()));
	// ----
	QObject::connect(action_Specify_Anchored_Plate_ID, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_specify_anchored_plate_id_dialog()));
	QObject::connect(action_View_Reconstruction_Poles, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_total_reconstruction_poles_dialog()));
	QObject::connect(action_View_Rotation_Hierarchy, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_rotation_hierarchy_dialog()));
	// ----
	QObject::connect(action_Export, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_export_animation_dialog()));
}


void
GPlatesQtWidgets::ViewportWindow::connect_utilities_menu_actions()
{
	QObject::connect(action_Calculate_Reconstruction_Pole, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_calculate_reconstruction_pole_dialog()));

	QObject::connect(action_Finite_Rotation_Calculator, SIGNAL(triggered()),
		&dialogs(), SLOT(pop_up_finite_rotation_calculator_dialog()));

	QObject::connect(action_Open_Kinematics_Tool, SIGNAL(triggered()),
					 &dialogs(), SLOT(pop_up_kinematics_tool_dialog()));

	if (GPlatesUtils::ComponentManager::instance().is_enabled(
				GPlatesUtils::ComponentManager::Component::hellinger_three_plate()))
	{
		action_Manage_Age_Models->setVisible(true);
		QObject::connect(action_Manage_Age_Models, SIGNAL(triggered()),
				&dialogs(), SLOT(pop_up_age_model_manager_dialog()));

	}

	if(GPlatesUtils::ComponentManager::instance().is_enabled(
			GPlatesUtils::ComponentManager::Component::python()))
	{
		d_utilities_menu_ptr = new GPlatesGui::UtilitiesMenu(
				menu_Utilities,
				action_Open_Python_Console,
				get_view_state().get_python_manager(),
				this);
		
		// ----
		QObject::connect(action_Open_Python_Console, SIGNAL(triggered()),
				this, SLOT(pop_up_python_console()));
	}
	else
	{
		hide_python_menu();
	}
}


void
GPlatesQtWidgets::ViewportWindow::connect_tools_menu_actions()
{
	// Tools menu. This is mostly auto-populated with Canvas Tool actions,
	// which don't need to be hooked up here, however there are a few little
	// extras which aren't regular canvas tools and should be connected here:-
	QObject::connect(action_Use_Small_Icons, SIGNAL(toggled(bool)),
		d_canvas_tools_dock_ptr, SLOT(use_small_canvas_tool_icons(bool)));
	QObject::connect(action_Configure_Geometry_Rendering, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_configure_canvas_tool_geometry_render_parameters_dialog()));
	QObject::connect(action_Rotation_File_Editor, SIGNAL(triggered()),
			this, SLOT(handle_rotation_file_editor()));

	// Populate the Tools menu with a sub-menu for each canvas tool workflow.
	// And for each workflow populate its sub-menu with the workflow tool actions.
	for (unsigned int tool_workflow = 0;
		tool_workflow < GPlatesGui::CanvasToolWorkflows::NUM_WORKFLOWS;
		++tool_workflow)
	{
		const GPlatesGui::CanvasToolWorkflows::WorkflowType canvas_tool_workflow =
				static_cast<GPlatesGui::CanvasToolWorkflows::WorkflowType>(tool_workflow);

		// Create a new sub-menu for the current workflow.
		const QString workflow_menu_name =
				d_canvas_tools_dock_ptr->get_workflow_tool_menu_name(canvas_tool_workflow);
		QMenu *canvas_workflow_menu = new QMenu(workflow_menu_name, menu_Tools);
		menu_Tools->addMenu(canvas_workflow_menu);

		// Get the tool actions for the current workflow.
		QList<QAction *> canvas_tool_actions =
				d_canvas_tools_dock_ptr->get_workflow_tool_menu_actions(canvas_tool_workflow);

		// Add the workflow tool actions to the sub-menu.
		Q_FOREACH(QAction *canvas_tool_action, canvas_tool_actions)
		{
			canvas_workflow_menu->addAction(canvas_tool_action);
		}
	}
}


void
GPlatesQtWidgets::ViewportWindow::connect_window_menu_actions()
{
	QObject::connect(menu_Window, SIGNAL(aboutToShow()),
			this, SLOT(handle_window_menu_about_to_show()));
	QObject::connect(action_New_Window, SIGNAL(triggered()),
			this, SLOT(open_new_window()));
	// ----
	QObject::connect(action_Show_Layers, SIGNAL(triggered(bool)),
			this, SLOT(set_visual_layers_dialog_visibility(bool)));

	QAction *action_show_bottom_panel = d_search_results_dock_ptr->toggleViewAction();
	action_show_bottom_panel->setText(tr("Show &Bottom Panel"));
	action_show_bottom_panel->setObjectName("action_Show_Bottom_Panel");
	menu_Window->insertAction(action_Show_Bottom_Panel_Placeholder, action_show_bottom_panel);
	menu_Window->removeAction(action_Show_Bottom_Panel_Placeholder);

	QAction *action_show_project_documents = d_project_documents_dock_ptr->toggleViewAction();
	action_show_project_documents->setText(tr("Show Project &Documents"));
	action_show_project_documents->setObjectName("action_Show_Project_Documents");
	menu_Window->insertAction(action_Log_Dialog, action_show_project_documents);

	QObject::connect(action_Log_Dialog, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_log_dialog()));
	// ----
	QObject::connect(action_Full_Screen, SIGNAL(triggered(bool)),
			d_full_screen_mode, SLOT(toggle_full_screen(bool)));
}


void
GPlatesQtWidgets::ViewportWindow::connect_world_building_menu_actions()
{
	QObject::connect(
			action_Split_Plate,
			SIGNAL(triggered()),
			this,
			SLOT(handle_split_plate()));
	QObject::connect(
			action_Boolean_Polygons,
			SIGNAL(triggered()),
			this,
			SLOT(show_boolean_polygons_window()));
	QObject::connect(
			action_Create_Ocean_Crust,
			SIGNAL(triggered()),
			this,
			SLOT(handle_create_ocean_crust()));
	QObject::connect(
			action_Create_Triple_Junction_Crust,
			SIGNAL(triggered()),
			this,
			SLOT(handle_create_triple_junction_crust()));
	QObject::connect(
			action_Create_Pacific_Plate,
			SIGNAL(triggered()),
			this,
			SLOT(handle_create_pacific_plate()));
	QObject::connect(
			action_Naturalize_Coastline,
			SIGNAL(triggered()),
			this,
			SLOT(handle_naturalize_coastline()));
	QObject::connect(
			action_Subduction_Cutter,
			SIGNAL(triggered()),
			this,
			SLOT(handle_subduction_cutter()));
}


void
GPlatesQtWidgets::ViewportWindow::connect_help_menu_actions()
{
	QObject::connect(action_View_Online_Documentation, SIGNAL(triggered()),
			this, SLOT(open_online_documentation()));
	// ----
	QObject::connect(action_About, SIGNAL(triggered()),
			&dialogs(), SLOT(pop_up_about_dialog()));

	QObject::connect(action_About_GPlates_Dataset, SIGNAL(triggered()),
			this, SLOT(open_dataset_webpage()));
}


GPlatesQtWidgets::CanvasToolBarDockWidget &
GPlatesQtWidgets::ViewportWindow::canvas_tool_bar_dock_widget()
{
	return *d_canvas_tools_dock_ptr;
}


void
GPlatesQtWidgets::ViewportWindow::handle_globe_feature_context_menu(
		const GPlatesMaths::PointOnSphere &click_position,
		const GPlatesMaths::PointOnSphere &oriented_click_position,
		bool is_on_globe,
		Qt::MouseButton button,
		Qt::KeyboardModifiers modifiers)
{
	Q_UNUSED(modifiers);

	if (button != Qt::RightButton || !is_on_globe)
	{
		return;
	}
	show_feature_context_menu_at_point(
			oriented_click_position,
			globe_canvas().current_proximity_inclusion_threshold(click_position));
}


void
GPlatesQtWidgets::ViewportWindow::handle_map_feature_context_menu(
		const QPointF &click_position,
		bool is_on_surface,
		Qt::MouseButton button,
		Qt::KeyboardModifiers modifiers)
{
	Q_UNUSED(modifiers);

	if (button != Qt::RightButton || !is_on_surface)
	{
		return;
	}
	const boost::optional<GPlatesMaths::LatLonPoint> lat_lon =
			map_view().map_canvas().map().projection().inverse_transform(click_position);
	if (!lat_lon)
	{
		return;
	}
	const GPlatesMaths::PointOnSphere point_on_sphere = GPlatesMaths::make_point_on_sphere(*lat_lon);
	show_feature_context_menu_at_point(
			point_on_sphere,
			map_view().current_proximity_inclusion_threshold(point_on_sphere));
}


void
GPlatesQtWidgets::ViewportWindow::show_feature_context_menu_at_point(
		const GPlatesMaths::PointOnSphere &point_on_sphere,
		double proximity_inclusion_threshold)
{
	std::vector<GPlatesAppLogic::ReconstructionGeometry::non_null_ptr_to_const_type> clicked_geometries;
	GPlatesGui::get_clicked_geometries(
			clicked_geometries,
			point_on_sphere,
			proximity_inclusion_threshold,
			get_view_state().get_rendered_geometry_collection());
	if (clicked_geometries.empty())
	{
		return;
	}

	GPlatesGui::add_clicked_geometries_to_feature_table(
			clicked_geometries,
			*this,
			get_view_state().get_feature_table_model(),
			get_view_state().get_feature_focus(),
			get_application_state().get_reconstruct_graph());
	show_focused_feature_context_menu(QCursor::pos());
}


void
GPlatesQtWidgets::ViewportWindow::show_focused_feature_context_menu(
		const QPoint &global_position)
{
	GPlatesAppLogic::ReconstructionGeometry::maybe_null_ptr_to_const_type reconstruction_geometry =
			get_view_state().get_feature_focus().associated_reconstruction_geometry();
	if (!reconstruction_geometry)
	{
		return;
	}

	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(reconstruction_geometry);
	if (!rfg)
	{
		return;
	}

	const GPlatesMaths::GeometryOnSphere &geometry = *(*rfg)->reconstructed_geometry();
	boost::optional<GPlatesMaths::UnitVector3D> centroid;
	try
	{
		if (const GPlatesMaths::PointOnSphere *point = dynamic_cast<const GPlatesMaths::PointOnSphere *>(&geometry))
		{
			centroid = point->position_vector();
		}
		else if (const GPlatesMaths::MultiPointOnSphere *multi_point =
				dynamic_cast<const GPlatesMaths::MultiPointOnSphere *>(&geometry))
		{
			centroid = GPlatesMaths::Centroid::calculate_points_centroid(*multi_point);
		}
		else if (const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(&geometry))
		{
			centroid = GPlatesMaths::Centroid::calculate_outline_centroid(*polyline);
		}
		else if (const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(&geometry))
		{
			centroid = GPlatesMaths::Centroid::calculate_interior_centroid(*polygon);
		}
	}
	catch (...)
	{
		centroid = boost::none;
	}

	QString speed_text = tr("Average speed over previous 50 My: unavailable");
	if (centroid && (*rfg)->reconstruction_plate_id())
	{
		try
		{
			const double reconstruction_time =
					get_application_state().get_current_reconstruction().get_reconstruction_time();
			const GPlatesMaths::Vector3D velocity =
					GPlatesAppLogic::PlateVelocityUtils::calculate_velocity_vector(
							GPlatesMaths::PointOnSphere(*centroid),
							*(*rfg)->reconstruction_plate_id(),
							(*rfg)->get_reconstruction_tree_creator(),
							reconstruction_time,
							50.0,
							GPlatesAppLogic::VelocityDeltaTime::T_PLUS_DELTA_T_TO_T);
			speed_text = tr("Average speed over previous 50 My: %1 cm/yr")
					.arg(QLocale().toString(velocity.magnitude().dval(), 'f', 2));
		}
		catch (...)
		{
			// Leave the unavailable label for incomplete or degenerate rotation data.
		}
	}

	QString area_text = tr("Total area: not applicable to this geometry");
	if (const GPlatesMaths::PolygonOnSphere *polygon =
			dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(&geometry))
	{
		const double radius_km = GPlatesUtils::Earth::MEAN_RADIUS_KMS;
		const double area_sq_km = polygon->get_area().dval() * radius_km * radius_km;
		area_text = tr("Total area: %1 km²").arg(QLocale().toString(area_sq_km, 'f', 0));
	}

	QMenu menu(this);
	QAction *heading = menu.addAction(tr("Feature Statistics"));
	heading->setEnabled(false);
	menu.addSeparator();
	QAction *speed_action = menu.addAction(speed_text);
	QAction *area_action = menu.addAction(area_text);
	speed_action->setEnabled(false);
	area_action->setEnabled(false);
	menu.exec(global_position);
}


GPlatesQtWidgets::SearchResultsDockWidget &
GPlatesQtWidgets::ViewportWindow::search_results_dock_widget()
{
	return *d_search_results_dock_ptr;
}


GPlatesQtWidgets::ProjectDocumentsDockWidget &
GPlatesQtWidgets::ViewportWindow::project_documents_dock_widget()
{
	return *d_project_documents_dock_ptr;
}


GPlatesQtWidgets::GlobeCanvas &
GPlatesQtWidgets::ViewportWindow::globe_canvas()
{
	return d_reconstruction_view_widget_ptr->globe_canvas();
}


const GPlatesQtWidgets::GlobeCanvas &
GPlatesQtWidgets::ViewportWindow::globe_canvas() const
{
	return d_reconstruction_view_widget_ptr->globe_canvas();
}


GPlatesQtWidgets::MapView &
GPlatesQtWidgets::ViewportWindow::map_view()
{
	return d_reconstruction_view_widget_ptr->map_view();
}


const GPlatesQtWidgets::MapView &
GPlatesQtWidgets::ViewportWindow::map_view() const
{
	return d_reconstruction_view_widget_ptr->map_view();
}


GPlatesGui::Dialogs &
GPlatesQtWidgets::ViewportWindow::dialogs() const
{
	return *d_dialogs_ptr;
}


GPlatesGui::FileIOFeedback &
GPlatesQtWidgets::ViewportWindow::file_io_feedback()
{
	return *d_file_io_feedback_ptr;
}


GPlatesGui::CanvasToolWorkflows &
GPlatesQtWidgets::ViewportWindow::canvas_tool_workflows()
{
	return *d_canvas_tool_workflows;
}



GPlatesGui::TrinketArea &
GPlatesQtWidgets::ViewportWindow::trinket_area()
{
	return *d_trinket_area_ptr;
}


GPlatesQtWidgets::TaskPanel *
GPlatesQtWidgets::ViewportWindow::task_panel_ptr() const
{
	return d_task_panel_ptr;
}


GPlatesGui::ImportMenu &
GPlatesQtWidgets::ViewportWindow::import_menu()
{
	return *d_import_menu_ptr;
}


GPlatesGui::UtilitiesMenu &
GPlatesQtWidgets::ViewportWindow::utilities_menu()
{
	return *d_utilities_menu_ptr;
}


void
GPlatesQtWidgets::ViewportWindow::populate_gmenu_from_menubar()
{
	// Populate the GMenu with the menubar's menu actions.
	// For now, this has to be done here in ViewportWindow so we can get at 'menubar'.
	// It is also difficult to do this in GMenu's constructor, where I'd prefer to put
	// it, because at that time @a ViewportWindow::setupUi() hasn't been called yet,
	// so the menu structure does not exist.
	
	// Find the GMenu by Qt object name. This is a lot more convenient for this kind
	// of one-off setup than going through ReconstructionViewWidget, etc.
	QMenu *gmenu = findChild<QMenu *>("GMenu");
	if (gmenu) {
		// Add each of the top-level menu items from the main menu bar.
		QList<QAction *> main_menubar_actions = menubar->actions();
		Q_FOREACH(QAction *action, main_menubar_actions) {
			gmenu->addAction(action);
		}
	}
}


GPlatesAppLogic::ApplicationState &
GPlatesQtWidgets::ViewportWindow::get_application_state()
{
	return d_application_state;
}


GPlatesPresentation::ViewState &
GPlatesQtWidgets::ViewportWindow::get_view_state()
{
	return d_view_state;
}


bool
GPlatesQtWidgets::ViewportWindow::try_select_worldbuilding_mor()
{
	if (!d_create_ocean_crust_operation_ptr)
	{
		return false;
	}
	QString message;
	const bool handled = d_create_ocean_crust_operation_ptr->select_focused_mor(message);
	if (handled)
	{
		if (d_create_pacific_plate_operation_ptr)
		{
			if (d_create_ocean_crust_operation_ptr->selected_mors().size() == 3)
			{
				d_create_pacific_plate_operation_ptr->arm_seed_capture();
				message += tr(" The next ordinary click captures the local void seed for Pacific-style plate birth.");
			}
			else
			{
				d_create_pacific_plate_operation_ptr->clear_seed();
			}
		}
		status_message(message);
	}
	return handled;
}


bool
GPlatesQtWidgets::ViewportWindow::try_capture_pacific_void_seed(
		const GPlatesMaths::PointOnSphere &point_on_sphere,
		bool is_on_earth)
{
	if (!d_create_pacific_plate_operation_ptr)
	{
		return false;
	}
	QString message;
	const bool handled = d_create_pacific_plate_operation_ptr->capture_seed(
			point_on_sphere, is_on_earth, message);
	if (handled)
	{
		status_message(message);
	}
	return handled;
}


GPlatesQtWidgets::ReconstructionViewWidget &
GPlatesQtWidgets::ViewportWindow::reconstruction_view_widget()
{
	return *d_reconstruction_view_widget_ptr;
}

const GPlatesQtWidgets::ReconstructionViewWidget &
GPlatesQtWidgets::ViewportWindow::reconstruction_view_widget() const
{
	return *d_reconstruction_view_widget_ptr;
}


void	
GPlatesQtWidgets::ViewportWindow::set_up_task_panel_actions()
{
	ActionButtonBox &feature_actions = d_task_panel_ptr->feature_action_button_box();

	// If you want to add a new action button, the steps are:
	// 0. Open ViewportWindowUi.ui in the Designer.
	// 1. Create a QAction in the Designer's Action Editor, called action_Something.
	// 2. Assign icons, tooltips, and shortcuts as necessary.
	// 3. Drag this action to a menu (optional).
	// 4. Add code for the triggered() signal your action generates,
	//    see ViewportWindow::connect_menu_actions().
	// 5. Add a new line of code here adding the QAction to the ActionButtonBox.

	feature_actions.add_action(action_Query_Feature);
	feature_actions.add_action(action_Edit_Feature);
	feature_actions.add_action(action_Clone_Geometry);
	feature_actions.add_action(action_Clone_Feature);
	feature_actions.add_action(action_Delete_Feature);
}


void
GPlatesQtWidgets::ViewportWindow::set_visual_layers_dialog_visibility(
		bool visible)
{
	if (visible)
	{
		dialogs().visual_layers_dialog().pop_up();
	}
	else
	{
		dialogs().visual_layers_dialog().hide();
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_window_menu_about_to_show()
{
	action_Show_Layers->setChecked(dialogs().visual_layers_dialog().isVisible());
}


void
GPlatesQtWidgets::ViewportWindow::handle_load_symbol_file()
{
    QString filename = QFileDialog::getOpenFileName(
		    this,
		    QObject::tr("Open symbol file"),
		    get_view_state().get_last_open_directory(),
		    QObject::tr("Symbol file (*.sym)"));
	if (filename.isNull())
	{
		return;
	}

    try
	{
		GPlatesFileIO::SymbolFileReader::read_file(
			filename,
			get_view_state().get_feature_type_symbol_map());
    }
	catch (std::exception &exc)
	{
		qWarning() << "Failed to load symbol file: " << exc.what();
	}
	catch (...)
	{
		qWarning() << "Failed to load symbol file: unknown error";
    }

    get_application_state().reconstruct();
}

void
GPlatesQtWidgets::ViewportWindow::handle_unload_symbol_file()
{
    get_view_state().get_feature_type_symbol_map().clear();
    get_application_state().reconstruct();
}


void
GPlatesQtWidgets::ViewportWindow::enable_or_disable_feature_actions(
		GPlatesGui::FeatureFocus &feature_focus)
{
	// Note: Enabling/disabling canvas tools is now done in class 'EnableCanvasTool'.
	bool is_feature_focused = feature_focus.focused_feature().is_valid();
	
	action_Query_Feature->setEnabled(is_feature_focused);
	action_Edit_Feature->setEnabled(is_feature_focused);
	action_Delete_Feature->setEnabled(is_feature_focused);
	action_Clone_Feature->setEnabled(is_feature_focused);

	action_Clone_Geometry->setEnabled(is_feature_focused &&
			get_view_state().get_focused_feature_geometry_builder().has_geometry());

#if 0
	// FIXME: Add to Selection is unimplemented and should stay disabled for now.
	// FIXME: To handle the "Remove from Selection", "Clear Selection" actions,
	// we may want to modify this method to also test for a nonempty selection of features.
	action_Add_Feature_To_Selection->setEnabled(enable_canvas_tool_actions);
#endif
}


void
GPlatesQtWidgets::ViewportWindow::closeEvent(
		QCloseEvent *close_event)
{
	// FIXME: Refactor the code below into some app-logic type thing,
	//        and then merely call it from here (checking the bool return value naturally)

	// STEP 1: UNSAVED CHANGES WARNING

	// Check for unsaved changes and ask the user what to do if there are any.
	const GPlatesGui::UnsavedChangesTracker::UnsavedChangesResult unsaved_changes_result =
			d_unsaved_changes_tracker_ptr->close_event_hook();
	if (unsaved_changes_result == GPlatesGui::UnsavedChangesTracker::DONT_DISCARD_UNSAVED_CHANGES)
	{
		// User is Not OK with quitting GPlates at this point.
		close_event->ignore();
		return;
	}

	// STEP 2: RECORDING SESSION DETAILS

	// Remember the current session for next time unless user is discarding unsaved changes
	// (feature collections and/or project session changes).
	if (unsaved_changes_result != GPlatesGui::UnsavedChangesTracker::DISCARD_UNSAVED_CHANGES)
	{
		get_view_state().get_session_management().close_event_hook();
	}

	// STEP 3: FINAL TIDY-UP BEFORE QUITTING

	// User is OK with quitting GPlates at this point.
	close_event->accept();
	// If we decide to accept the close event, we should also tidy up after ourselves.
	dialogs().close_all_dialogs();
	// Make sure we really do quit - stray dialogs not caught by @a close_all_dialogs()
	// (e.g. PyQt windows) will keep GPlates open.
	QCoreApplication::quit();

	//
	// Optimisations to avoid long shutdown times for GPlates.
	//

	//
	// NOTE: This is a very SIGNIFICANT optimisation for large files.
	//
	// For small files it's not noticeable, but for very large files it can reduce shutdown
	// times from minutes to a few seconds.
	//
	// NOTE: This used to be in the destructor of class Application but was moved here because
	// there was a lot of slowdown between here and the Application destructor due to the main
	// event loop emitting events while shutting down (after which the Application destructor starts).
	//
	// Prevent modifications to any rendered geometry collection from signaling updates
	// to various listening clients. We're shutting down so rendered geometry updates are not
	// getting drawn (or used for export, etc).
	// Note that this call starts blocking updates and also we don't subsequently call
	// 'end_update_all_registered_collections()' to unblock them as this is not necessary
	// (if we did it would be limited to this scope anyway and wouldn't block updates from here onwards,
	// including the destructor of class Application and the destructors of all its sub-objects, etc).
	//
	GPlatesViewOperations::RenderedGeometryCollection::begin_update_all_registered_collections();
}


void
GPlatesQtWidgets::ViewportWindow::dragEnterEvent(
		QDragEnterEvent *ev)
{
	if (ev->mimeData()->hasUrls())
	{
		// If there's exactly one project filename then allow it.
		// If there's more than one then ignore altogether.
		const QStringList project_filenames =
				d_file_io_feedback_ptr->extract_project_filenames_from_file_urls(
						ev->mimeData()->urls());
		if (project_filenames.size() == 1)
		{
			ev->acceptProposedAction();
			return;
		}

		const QStringList feature_collection_filenames =
				d_file_io_feedback_ptr->extract_feature_collection_filenames_from_file_urls(
						ev->mimeData()->urls());
		if (!feature_collection_filenames.isEmpty())
		{
			ev->acceptProposedAction();
			return;
		}
	}

	ev->ignore();
}


void
GPlatesQtWidgets::ViewportWindow::dropEvent(
		QDropEvent *ev)
{
	if (ev->mimeData()->hasUrls())
	{
		// If there's exactly one project filename then open it.
		// If there's more than one then ignore altogether.
		const QStringList project_filenames =
				d_file_io_feedback_ptr->extract_project_filenames_from_file_urls(
						ev->mimeData()->urls());
		if (project_filenames.size() == 1)
		{
			ev->acceptProposedAction();
			d_file_io_feedback_ptr->open_project(project_filenames[0]);
			return;
		}

		// Else if there are any feature collection filenames then open them.
		const QStringList feature_collection_filenames =
				d_file_io_feedback_ptr->extract_feature_collection_filenames_from_file_urls(
						ev->mimeData()->urls());
		if (!feature_collection_filenames.isEmpty())
		{
			ev->acceptProposedAction();
			d_file_io_feedback_ptr->open_files(feature_collection_filenames);
			return;
		}
	}

	ev->ignore();
}


void
GPlatesQtWidgets::ViewportWindow::enable_static_point_display()
{
	get_view_state().get_render_settings().set_show_static_points(
			action_Show_Static_Points->isChecked());
}


void
GPlatesQtWidgets::ViewportWindow::enable_static_line_display()
{
	get_view_state().get_render_settings().set_show_static_lines(
			action_Show_Static_Lines->isChecked());
}


void
GPlatesQtWidgets::ViewportWindow::enable_static_polygon_display()
{
	get_view_state().get_render_settings().set_show_static_polygons(
			action_Show_Static_Polygons->isChecked());
}


void
GPlatesQtWidgets::ViewportWindow::enable_static_multipoint_display()
{
	get_view_state().get_render_settings().set_show_static_multipoints(
			action_Show_Static_Multipoints->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_velocity_arrow_display()
{
	get_view_state().get_render_settings().set_show_velocity_arrows(
			action_Show_Velocity_Arrows->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_topological_section_display()
{
	get_view_state().get_render_settings().set_show_topological_sections(
			action_Show_Topological_Sections->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_topological_line_display()
{
	get_view_state().get_render_settings().set_show_topological_lines(
			action_Show_Topological_Lines->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_topological_polygon_display()
{
	get_view_state().get_render_settings().set_show_topological_polygons(
			action_Show_Topological_Polygons->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_topological_network_display()
{
	get_view_state().get_render_settings().set_show_topological_networks(
			action_Show_Topological_Networks->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_raster_display()
{
	get_view_state().get_render_settings().set_show_rasters(
			action_Show_Rasters->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_3d_scalar_field_display()
{
	get_view_state().get_render_settings().set_show_3d_scalar_fields(
			action_Show_3D_Scalar_Fields->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_scalar_coverage_display()
{
	get_view_state().get_render_settings().set_show_scalar_coverages(
			action_Show_Scalar_Coverages->isChecked());
}

void
GPlatesQtWidgets::ViewportWindow::enable_all_geometries_display()
{
	const bool show_all = action_Show_All_Geometries->isChecked();

	// This will show/hide all geometries in the render settings which will also
	// signal 'handle_render_settings_changed()' to change the individual checkboxes.
	get_view_state().get_render_settings().set_show_all(show_all);
}

void
GPlatesQtWidgets::ViewportWindow::handle_render_settings_changed()
{
	GPlatesGui::RenderSettings &render_settings = get_view_state().get_render_settings();

	// Note: Calling 'setChecked()' does not result in the QAction 'triggered' signal so
	// we don't need to worry about infinite recursion.
	action_Show_Static_Points->setChecked(render_settings.show_static_points());
	action_Show_Static_Lines->setChecked(render_settings.show_static_lines());
	action_Show_Static_Polygons->setChecked(render_settings.show_static_polygons());
	action_Show_Static_Multipoints->setChecked(render_settings.show_static_multipoints());
	action_Show_Velocity_Arrows->setChecked(render_settings.show_velocity_arrows());
	action_Show_Topological_Sections->setChecked(render_settings.show_topological_sections());
	action_Show_Topological_Lines->setChecked(render_settings.show_topological_lines());
	action_Show_Topological_Polygons->setChecked(render_settings.show_topological_polygons());
	action_Show_Topological_Networks->setChecked(render_settings.show_topological_networks());
	action_Show_Rasters->setChecked(render_settings.show_rasters());
	action_Show_3D_Scalar_Fields->setChecked(render_settings.show_3d_scalar_fields());
	action_Show_Scalar_Coverages->setChecked(render_settings.show_scalar_coverages());
}

void
GPlatesQtWidgets::ViewportWindow::enable_stars_display()
{
	get_view_state().set_show_stars(
			action_Show_Stars->isChecked());
	if (reconstruction_view_widget().globe_is_active())
	{
		globe_canvas().update_canvas();
	}
}

void
GPlatesQtWidgets::ViewportWindow::update_tools_and_status_message()
{	
// FIXME: 
// There seems to be some sequencing bug where these actions_ never get enabled?
// for now, just comment out ... 
#if 0
	bool globe_is_active = d_reconstruction_view_widget_ptr->globe_is_active();
	action_Show_Arrow_Decorations->setEnabled(globe_is_active);
	action_Show_Stars->setEnabled(globe_is_active);
#endif
}


void
GPlatesQtWidgets::ViewportWindow::handle_move_camera_up()
{
	d_reconstruction_view_widget_ptr->active_view().move_camera_up();
}

void
GPlatesQtWidgets::ViewportWindow::handle_move_camera_down()
{
	d_reconstruction_view_widget_ptr->active_view().move_camera_down();
}

void
GPlatesQtWidgets::ViewportWindow::handle_move_camera_left()
{
	d_reconstruction_view_widget_ptr->active_view().move_camera_left();
}

void
GPlatesQtWidgets::ViewportWindow::handle_move_camera_right()
{
	d_reconstruction_view_widget_ptr->active_view().move_camera_right();
}

void
GPlatesQtWidgets::ViewportWindow::handle_rotate_camera_clockwise()
{
	d_reconstruction_view_widget_ptr->active_view().rotate_camera_clockwise();
}

void
GPlatesQtWidgets::ViewportWindow::handle_rotate_camera_anticlockwise()
{
	d_reconstruction_view_widget_ptr->active_view().rotate_camera_anticlockwise();
}

void
GPlatesQtWidgets::ViewportWindow::handle_reset_camera_orientation()
{
	d_reconstruction_view_widget_ptr->active_view().reset_camera_orientation();
}

void
GPlatesQtWidgets::ViewportWindow::handle_canvas_tool_activated(
		GPlatesGui::CanvasToolWorkflows::WorkflowType workflow,
		GPlatesGui::CanvasToolWorkflows::ToolType tool)
{
	// Switch to the proper task panel tab depending on which canvas tool was just activated.
	switch (tool)
	{
	case GPlatesGui::CanvasToolWorkflows::TOOL_DRAG_GLOBE:
	case GPlatesGui::CanvasToolWorkflows::TOOL_ZOOM_GLOBE:
		// We don't pick a tab - the previous tab from another canvas tool workflow will remain.
		break;

#if 0 // Disable lighting tool until volume visualisation is officially released (in GPlates 1.5)...
	case GPlatesGui::CanvasToolWorkflows::TOOL_CHANGE_LIGHTING:
		d_task_panel_ptr->choose_lighting_tab();
		break;
#endif

	case GPlatesGui::CanvasToolWorkflows::TOOL_CLICK_GEOMETRY:
		d_task_panel_ptr->choose_feature_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_DIGITISE_NEW_POLYLINE:
		d_task_panel_ptr->choose_digitisation_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_DIGITISE_NEW_MULTIPOINT:
		d_task_panel_ptr->choose_digitisation_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_DIGITISE_NEW_POLYGON:
		d_task_panel_ptr->choose_digitisation_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_MOVE_VERTEX:
		d_task_panel_ptr->choose_modify_geometry_tab(true/*enable_move_nearby_vertices*/);
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_DELETE_VERTEX:
		d_task_panel_ptr->choose_modify_geometry_tab(false/*enable_move_nearby_vertices*/);
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_INSERT_VERTEX:
		d_task_panel_ptr->choose_modify_geometry_tab(false/*enable_move_nearby_vertices*/);
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_SPLIT_FEATURE:
		d_task_panel_ptr->choose_modify_geometry_tab(false/*enable_move_nearby_vertices*/);
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_MEASURE_DISTANCE:
		d_task_panel_ptr->choose_measure_distance_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_MANIPULATE_POLE:
		d_task_panel_ptr->choose_modify_pole_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_MOVE_POLE:
		d_task_panel_ptr->choose_move_pole_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_BUILD_LINE_TOPOLOGY:
		d_task_panel_ptr->choose_topology_tools_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_BUILD_BOUNDARY_TOPOLOGY:
		d_task_panel_ptr->choose_topology_tools_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_BUILD_NETWORK_TOPOLOGY:
		d_task_panel_ptr->choose_topology_tools_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_EDIT_TOPOLOGY:
		d_task_panel_ptr->choose_topology_tools_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_CREATE_SMALL_CIRCLE:
		d_task_panel_ptr->choose_small_circle_tab();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_SELECT_HELLINGER_GEOMETRIES:
		// NOTE: We don't currently have any hellinger task panels
		// (and we may never have any), so we just open the
		// Hellinger dialog here, unlike most other tools which will
		// activate their associated task panels.
		dialogs().pop_up_and_reposition_hellinger_dialog();
		break;

	case GPlatesGui::CanvasToolWorkflows::TOOL_ADJUST_FITTED_POLE_ESTIMATE:
		dialogs().pop_up_hellinger_dialog();
		break;

	default:
		// Shouldn't get here.
		GPlatesGlobal::Abort(GPLATES_ASSERTION_SOURCE);
		break;
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_changed_project_filename(
		boost::optional<QString> project_filename)
{
	// Add the project filename (if any) in the window title.
	if (project_filename)
	{
		// 'completeBaseName()' removes the path and the last extension.
		const QString project_file_display_name = QFileInfo(project_filename.get()).completeBaseName();

		set_window_title(project_file_display_name);
	}
	else
	{
		set_window_title();
	}
}


void
GPlatesQtWidgets::ViewportWindow::show_menu_item_status_tip_in_status_bar()
{
	// Get the QObject that triggered this slot.
	QObject *signal_sender = sender();
	// Return early in case this slot not activated by a signal - shouldn't happen.
	if (!signal_sender)
	{
		return;
	}

	// See if a QAction triggered this slot.
	QAction *action = qobject_cast<QAction *>(signal_sender);
	if (action)
	{
		status_message(action->statusTip());
		return;
	}

	// See if a QMenu triggered this slot.
	QMenu *menu = qobject_cast<QMenu *>(signal_sender);
	if (menu)
	{
		status_message(menu->statusTip());
		return;
	}
}


void
GPlatesQtWidgets::ViewportWindow::install_gui_debug_menu()
{
	// Add the GUI Debug menu and associated functionality.
	// This is okay, we're not bleeding memory out of our ears, Qt parents it
	// to ViewportWindow and cleans up after us. We don't really need to keep
	// a reference to this class around afterwards, which will help keep us
	// be free of header and initialiser list spaghetti.
	static GPlatesGui::GuiDebug *gui_debug =
			new GPlatesGui::GuiDebug(*this, get_view_state(), get_application_state(), this);

	gui_debug->setObjectName("GuiDebug");
}


void
GPlatesQtWidgets::ViewportWindow::handle_colour_scheme_delegator_changed()
{
	d_reconstruction_view_widget_ptr->globe_and_map_widget().update_canvas();
}


void
GPlatesQtWidgets::ViewportWindow::set_window_title(
		boost::optional<QString> project_filename)
{
	QString window_title("GPlates");

	// Append the GPlates version if not an official public release (including pre-release suffix, eg, 2.3.0-dev1).
	// Otherwise just leave as "GPlates" for official public releases.
#if !defined(GPLATES_PUBLIC_RELEASE)  // Flag defined by CMake build system (in "global/config.h").
	const QString FORMAT = " (%1)";
	window_title.append(FORMAT.arg(GPlatesGlobal::Version::get_GPlates_version()));
#endif

	// Add the project filename if there is one.
	if (project_filename)
	{
		window_title.append(" - ");
		window_title.append(project_filename.get());
	}

	setWindowTitle(window_title);
}


void
GPlatesQtWidgets::ViewportWindow::handle_visual_layer_added(
		size_t added)
{
	set_visual_layers_dialog_visibility(true);

	// Disconnect from signal so that we only open the Layers dialog automatically
	// the first time a visual layer is added.
	QObject::disconnect(
			&(get_view_state().get_visual_layers()),
			SIGNAL(layer_added(size_t)),
			this,
			SLOT(handle_visual_layer_added(size_t)));
}


void
GPlatesQtWidgets::ViewportWindow::open_new_window()
{
	QString file_path = QCoreApplication::applicationFilePath();

	// Note that even if we are not interested in passing arguments to the new
	// instance, we need to use this overload:
	//     bool QProcess::startDetached ( const QString & program, const QStringList & arguments )
	// instead of
	//     bool QProcess::startDetached ( const QString & program )
	// because with the latter, the program string contains both the program
	// name and the arguments, separated by whitespace. Thus, it does the wrong
	// thing if the path to the executable has whitespace, e.g. on Windows.
	if (!QProcess::startDetached(file_path, QStringList()))
	{
		qDebug() << "ViewportWindow::open_new_window: new instance could not be started";
	}
}


void
GPlatesQtWidgets::ViewportWindow::status_message(
		const QString &message,
		int timeout)
{
#ifdef Q_OS_MACOS
	static const QString CLOVERLEAF(QChar(0x2318));
	QString fixed_message = message;
	fixed_message.replace(QString("ctrl"), CLOVERLEAF, Qt::CaseInsensitive);
	statusBar()->showMessage(fixed_message, timeout);
#else
	statusBar()->showMessage(message, timeout);
#endif
}


void
GPlatesQtWidgets::ViewportWindow::pop_up_background_colour_picker()
{
	boost::optional<GPlatesGui::Colour> new_colour =
		QtWidgetUtils::get_colour_with_alpha(get_view_state().get_background_colour(), this);
	if (new_colour)
	{
		get_view_state().set_background_colour(*new_colour);
		reconstruction_view_widget().update();
	}
}


void
GPlatesQtWidgets::ViewportWindow::pop_up_import_raster_dialog(
		bool time_dependent_raster)
{
	// Note: the ImportRasterDialog needs to be constructed each time we want to
	// use it, unlike the other dialogs, otherwise the pages are incorrectly initialised.
	ImportRasterDialog import_raster_dialog(
			get_application_state(),
			get_view_state(),
			d_unsaved_changes_tracker_ptr.data(),
			d_file_io_feedback_ptr.data(),
			this);

	ReadErrorAccumulationDialog &read_errors_dialog = dialogs().read_error_accumulation_dialog();

	GPlatesFileIO::ReadErrorAccumulation &read_errors = read_errors_dialog.read_errors();
	GPlatesFileIO::ReadErrorAccumulation::size_type num_initial_errors = read_errors.size();

	import_raster_dialog.display(time_dependent_raster, &read_errors);

	read_errors_dialog.update();
	GPlatesFileIO::ReadErrorAccumulation::size_type num_final_errors = read_errors.size();
	if (num_initial_errors != num_final_errors)
	{
		read_errors_dialog.show();
	}
}

void
GPlatesQtWidgets::ViewportWindow::pop_up_import_raster_dialog()
{
	pop_up_import_raster_dialog(false);
}

void
GPlatesQtWidgets::ViewportWindow::pop_up_import_time_dependent_raster_dialog()
{
	pop_up_import_raster_dialog(true);
}


void
GPlatesQtWidgets::ViewportWindow::pop_up_import_scalar_field_3d_dialog()
{
	// Note: the ImportScalarField3DDialog needs to be constructed each time we want to
	// use it, unlike the other dialogs, otherwise the pages are incorrectly initialised.
	ImportScalarField3DDialog import_scalar_field_dialog(
			get_application_state(),
			get_view_state(),
			*this,
			d_unsaved_changes_tracker_ptr.data(),
			d_file_io_feedback_ptr.data(),
			this);

	ReadErrorAccumulationDialog &read_errors_dialog = dialogs().read_error_accumulation_dialog();

	GPlatesFileIO::ReadErrorAccumulation &read_errors = read_errors_dialog.read_errors();
	GPlatesFileIO::ReadErrorAccumulation::size_type num_initial_errors = read_errors.size();

	import_scalar_field_dialog.display(&read_errors);

	read_errors_dialog.update();
	GPlatesFileIO::ReadErrorAccumulation::size_type num_final_errors = read_errors.size();
	if (num_initial_errors != num_final_errors)
	{
		read_errors_dialog.show();
	}
}


void
GPlatesQtWidgets::ViewportWindow::clone_feature_with_dialog()
{
	ChooseFeatureCollectionDialog &choose_feature_collection_dialog =
			dialogs().choose_feature_collection_dialog();;

	GPlatesModel::FeatureHandle::weak_ref feature_ref = get_view_state().get_feature_focus().focused_feature();
	if (!feature_ref.is_valid())
	{
		return;
	}

	GPlatesModel::FeatureCollectionHandle *feature_collection_ptr = feature_ref->parent_ptr();
	if (!feature_collection_ptr)
	{
		return;
	}

	boost::optional<std::pair<GPlatesAppLogic::FeatureCollectionFileState::file_reference, bool> > dialog_result =
		choose_feature_collection_dialog.get_file_reference(feature_collection_ptr->reference());
	if (dialog_result)
	{
		d_clone_operation_ptr->clone_focused_feature(
				dialog_result->first.get_file().get_feature_collection());
	}
}


void
GPlatesQtWidgets::ViewportWindow::update_undo_action_tooltip()
{
	if (d_inside_update_undo_action_tooltip)
	{
		return;
	}

	d_inside_update_undo_action_tooltip = true;
	add_shortcut_to_tooltip(d_undo_action_ptr);
	d_inside_update_undo_action_tooltip = false;
}


void
GPlatesQtWidgets::ViewportWindow::update_redo_action_tooltip()
{
	if (d_inside_update_redo_action_tooltip)
	{
		return;
	}

	d_inside_update_redo_action_tooltip = true;
	add_shortcut_to_tooltip(d_redo_action_ptr);
	d_inside_update_redo_action_tooltip = false;
}


void
GPlatesQtWidgets::ViewportWindow::open_online_documentation()
{
	QDesktopServices::openUrl(QUrl("http://www.gplates.org/docs.html"));
}


void
GPlatesQtWidgets::ViewportWindow::pop_up_python_console()
{
	d_view_state.get_python_manager().pop_up_python_console();
}


void
GPlatesQtWidgets::ViewportWindow::handle_create_initial_continent()
{
	const GPlatesViewOperations::CreateInitialContinentOperation::Result result =
			d_create_initial_continent_operation_ptr->trigger(this);
	status_message(result.message);

	if (result.outcome == GPlatesViewOperations::CreateInitialContinentOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Create Initial Continent"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateInitialContinentOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Initial Continent Created"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_propose_initial_rifts()
{
	const GPlatesViewOperations::ProposeInitialRiftsOperation::Result result =
			d_propose_initial_rifts_operation_ptr->trigger(this);
	status_message(result.message);

	if (result.outcome == GPlatesViewOperations::ProposeInitialRiftsOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Propose Initial Rifts"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::ProposeInitialRiftsOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Initial Rifts Committed"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::show_make_rift_window()
{
	update_make_rift_palette();
	d_make_rift_dialog_ptr->show();
	d_make_rift_dialog_ptr->raise();
	d_make_rift_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_make_rift_select_continent()
{
	const GPlatesViewOperations::MakeRiftOperation::Result result =
			d_make_rift_operation_ptr->arm_continent_selection();
	if (d_make_rift_operation_ptr->selection_mode() ==
			GPlatesViewOperations::MakeRiftOperation::SELECTING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_make_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_make_rift_select_rift()
{
	const GPlatesViewOperations::MakeRiftOperation::Result result =
			d_make_rift_operation_ptr->arm_rift_selection();
	status_message(result.message);
	update_make_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_make_rift_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::MakeRiftOperation::Result result =
			d_make_rift_operation_ptr->capture_armed_selection();
	if (result.outcome == GPlatesViewOperations::MakeRiftOperation::OPERATION_CANCELLED)
	{
		return;
	}
	status_message(result.message);
	update_make_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_make_rift()
{
	const GPlatesViewOperations::MakeRiftOperation::Result result =
			d_make_rift_operation_ptr->cut(this);
	status_message(result.message);
	update_make_rift_palette(result.message);

	switch (result.outcome)
	{
	case GPlatesViewOperations::MakeRiftOperation::RIFT_COMPLETED:
		QMessageBox::information(this, tr("Rift Created"), result.message);
		d_make_rift_dialog_ptr->hide();
		break;
	case GPlatesViewOperations::MakeRiftOperation::OPERATION_ERROR:
		QMessageBox::warning(this, tr("Make Rift"), result.message);
		break;
	case GPlatesViewOperations::MakeRiftOperation::OPERATION_CANCELLED:
		break;
	case GPlatesViewOperations::MakeRiftOperation::SELECTION_ARMED:
	case GPlatesViewOperations::MakeRiftOperation::CONTINENT_CAPTURED:
	case GPlatesViewOperations::MakeRiftOperation::RIFT_CAPTURED:
		break;
	}
}


void
GPlatesQtWidgets::ViewportWindow::update_make_rift_palette(
		const QString &message)
{
	if (!d_make_rift_select_continent_button_ptr ||
			!d_make_rift_select_rift_button_ptr ||
			!d_make_rift_cut_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::MakeRiftOperation::SelectionMode mode =
			d_make_rift_operation_ptr->selection_mode();
	d_make_rift_select_continent_button_ptr->setChecked(
			mode == GPlatesViewOperations::MakeRiftOperation::SELECTING_CONTINENT);
	d_make_rift_select_rift_button_ptr->setChecked(
			mode == GPlatesViewOperations::MakeRiftOperation::SELECTING_RIFT);
	d_make_rift_cut_button_ptr->setEnabled(d_make_rift_operation_ptr->can_cut());
	d_make_rift_continent_status_label_ptr->setText(
			d_make_rift_operation_ptr->continent_status());
	d_make_rift_rift_status_label_ptr->setText(
			d_make_rift_operation_ptr->rift_status());
	d_make_rift_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("Selections stay captured until the cut succeeds or a new continent is chosen.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::show_initial_subduction_window()
{
	update_initial_subduction_palette();
	d_initial_subduction_dialog_ptr->show();
	d_initial_subduction_dialog_ptr->raise();
	d_initial_subduction_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_initial_subduction_select_continent()
{
	const GPlatesViewOperations::GenerateInitialSubductionOperation::Result result =
			d_generate_initial_subduction_operation_ptr->selection_mode() ==
					GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_CONTINENT
				? d_generate_initial_subduction_operation_ptr->cancel_selection()
				: d_generate_initial_subduction_operation_ptr->arm_continent_selection();
	if (d_generate_initial_subduction_operation_ptr->selection_mode() ==
			GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_initial_subduction_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_initial_subduction_select_mor()
{
	const GPlatesViewOperations::GenerateInitialSubductionOperation::Result result =
			d_generate_initial_subduction_operation_ptr->selection_mode() ==
					GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_MOR
				? d_generate_initial_subduction_operation_ptr->cancel_selection()
				: d_generate_initial_subduction_operation_ptr->arm_mor_selection();
	if (d_generate_initial_subduction_operation_ptr->selection_mode() ==
			GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_MOR)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_initial_subduction_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_initial_subduction_cancel_selection()
{
	const GPlatesViewOperations::GenerateInitialSubductionOperation::Result result =
			d_generate_initial_subduction_operation_ptr->cancel_selection();
	if (!result.message.isEmpty())
	{
		status_message(result.message);
	}
	update_initial_subduction_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_initial_subduction_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::GenerateInitialSubductionOperation::Result result =
			d_generate_initial_subduction_operation_ptr->capture_armed_selection();
	if (result.outcome == GPlatesViewOperations::GenerateInitialSubductionOperation::OPERATION_CANCELLED)
	{
		return;
	}
	status_message(result.message);
	update_initial_subduction_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_generate_initial_subduction()
{
	const GPlatesViewOperations::GenerateInitialSubductionOperation::Result result =
			d_generate_initial_subduction_operation_ptr->generate(this);
	status_message(result.message);
	update_initial_subduction_palette(result.message);

	switch (result.outcome)
	{
	case GPlatesViewOperations::GenerateInitialSubductionOperation::SUBDUCTION_COMPLETED:
		QMessageBox::information(this, tr("Subduction Zone Created"), result.message);
		d_initial_subduction_dialog_ptr->hide();
		break;
	case GPlatesViewOperations::GenerateInitialSubductionOperation::OPERATION_ERROR:
		QMessageBox::warning(this, tr("Opposite-Margin Subduction"), result.message);
		break;
	case GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTION_ARMED:
	case GPlatesViewOperations::GenerateInitialSubductionOperation::CONTINENT_CAPTURED:
	case GPlatesViewOperations::GenerateInitialSubductionOperation::MOR_CAPTURED:
	case GPlatesViewOperations::GenerateInitialSubductionOperation::OPERATION_CANCELLED:
		break;
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_advance_plate_motion()
{
	const GPlatesViewOperations::AdvancePlateMotionOperation::Result result =
			d_advance_plate_motion_operation_ptr->trigger(this);
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::AdvancePlateMotionOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Plate Motion Advanced"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::AdvancePlateMotionOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Advance Plate Motion"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_create_ocean_crust()
{
	const GPlatesViewOperations::CreateOceanCrustOperation::Result result =
			d_create_ocean_crust_operation_ptr->trigger(this);
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::CreateOceanCrustOperation::SELECTION_REQUIRED)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
		QMessageBox::information(this, tr("Shift-Click Half-Stage MOR"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateOceanCrustOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Oceanic Crust Generated"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateOceanCrustOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Generate Oceanic Crust from MOR"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_create_initial_rotation_file()
{
	const GPlatesViewOperations::CreateInitialRotationFileOperation::Result result =
			d_create_initial_rotation_file_operation_ptr->trigger(this);
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::CreateInitialRotationFileOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Rotation File Ready"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateInitialRotationFileOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Create Rotation File"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::update_initial_subduction_palette(
		const QString &message)
{
	if (!d_initial_subduction_select_continent_button_ptr ||
			!d_initial_subduction_select_mor_button_ptr ||
			!d_initial_subduction_generate_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::GenerateInitialSubductionOperation::SelectionMode mode =
			d_generate_initial_subduction_operation_ptr->selection_mode();
	d_initial_subduction_select_continent_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_CONTINENT);
	d_initial_subduction_select_mor_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateInitialSubductionOperation::SELECTING_MOR);
	d_initial_subduction_generate_button_ptr->setEnabled(
			d_generate_initial_subduction_operation_ptr->can_generate());
	d_initial_subduction_continent_status_label_ptr->setText(
			d_generate_initial_subduction_operation_ptr->continent_status());
	d_initial_subduction_mor_status_label_ptr->setText(
			d_generate_initial_subduction_operation_ptr->mor_status());
	d_initial_subduction_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("The yellow preview is the proposed trench; the green arrow is the inferred push direction. Nothing is written until you confirm.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::show_subduction_effects_window()
{
	update_subduction_effects_palette();
	d_subduction_effects_dialog_ptr->show();
	d_subduction_effects_dialog_ptr->raise();
	d_subduction_effects_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_select_subduction()
{
	const GPlatesViewOperations::GenerateSubductionEffectsOperation::Result result =
			d_generate_subduction_effects_operation_ptr->arm_subduction_selection();
	if (d_generate_subduction_effects_operation_ptr->selection_mode() ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::SELECTING_SUBDUCTION)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_subduction_effects_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_select_continent()
{
	const GPlatesViewOperations::GenerateSubductionEffectsOperation::Result result =
			d_generate_subduction_effects_operation_ptr->arm_continent_selection();
	if (d_generate_subduction_effects_operation_ptr->selection_mode() ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::SELECTING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_subduction_effects_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_clear_continent()
{
	d_generate_subduction_effects_operation_ptr->clear_continent();
	const QString message = tr(
			"Overriding crust reference cleared. Auto mode now proposes an island-arc notation line and still detects every visible land intersection.");
	status_message(message);
	update_subduction_effects_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::GenerateSubductionEffectsOperation::Result result =
			d_generate_subduction_effects_operation_ptr->capture_armed_selection();
	if (result.outcome ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::OPERATION_CANCELLED)
	{
		return;
	}
	QString message = result.message;
	if (result.outcome ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::CONTINENT_CAPTURED)
	{
		const bool recommended_flip =
				d_generate_subduction_effects_operation_ptr->recommended_polarity_flip();
		QSignalBlocker blocker(d_subduction_effects_flip_polarity_check_ptr);
		d_subduction_effects_flip_polarity_check_ptr->setChecked(recommended_flip);
		if (recommended_flip)
		{
			message += tr(" The crust-side check reversed the proposal polarity; you can still edit that choice.");
		}
	}
	status_message(message);
	update_subduction_effects_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_preview()
{
	GPlatesViewOperations::GenerateSubductionEffectsOperation::Options options;
	const int interpretation = d_subduction_effect_type_combo_ptr->currentIndex();
	if (interpretation == 0)
	{
		options.effect_type = d_generate_subduction_effects_operation_ptr->has_continent()
				? GPlatesViewOperations::SubductionEffectsGeometry::ANDEAN_OROGENY
				: GPlatesViewOperations::SubductionEffectsGeometry::ISLAND_ARC;
	}
	else if (interpretation == 1)
	{
		options.effect_type = GPlatesViewOperations::SubductionEffectsGeometry::ISLAND_ARC;
	}
	else if (interpretation == 2)
	{
		options.effect_type = GPlatesViewOperations::SubductionEffectsGeometry::ANDEAN_OROGENY;
	}
	else
	{
		options.effect_type = GPlatesViewOperations::SubductionEffectsGeometry::LARAMIDE_OROGENY;
	}
	options.flip_declared_polarity = d_subduction_effects_flip_polarity_check_ptr->isChecked();
	options.allow_early_island_arc = d_subduction_effects_early_arc_check_ptr->isChecked();
	options.island_arc_delay_ma = d_subduction_effects_arc_delay_spin_ptr->value();
	options.trim_start_percent = d_subduction_effects_trim_start_spin_ptr->value();
	options.trim_end_percent = d_subduction_effects_trim_end_spin_ptr->value();
	options.offset_km = d_subduction_effects_offset_spin_ptr->value();
	options.island_irregularity = d_subduction_effects_irregularity_spin_ptr->value() / 100.0;
	options.belt_width_km = d_subduction_effects_belt_width_spin_ptr->value();

	const GPlatesViewOperations::GenerateSubductionEffectsOperation::Result result =
			d_generate_subduction_effects_operation_ptr->preview(options);
	status_message(result.message);
	update_subduction_effects_palette(result.message);
	if (result.outcome ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Subduction Effects Preview"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_commit()
{
	const GPlatesViewOperations::GenerateSubductionEffectsOperation::Result result =
			d_generate_subduction_effects_operation_ptr->commit();
	status_message(result.message);
	update_subduction_effects_palette(result.message);
	if (result.outcome ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::EFFECTS_COMMITTED)
	{
		QMessageBox::information(this, tr("Subduction Effects Created"), result.message);
	}
	else if (result.outcome ==
			GPlatesViewOperations::GenerateSubductionEffectsOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Commit Subduction Effects"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effects_controls_changed()
{
	d_generate_subduction_effects_operation_ptr->clear_preview();
	update_subduction_effects_palette(tr("Controls changed; preview the revised proposal before committing."));
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_effect_type_changed(
		int index)
{
	if (index == 1)
	{
		d_subduction_effects_offset_spin_ptr->setValue(220);
	}
	else if (index == 2)
	{
		d_subduction_effects_offset_spin_ptr->setValue(180);
		d_subduction_effects_belt_width_spin_ptr->setValue(100);
	}
	else if (index == 3)
	{
		d_subduction_effects_offset_spin_ptr->setValue(300);
		d_subduction_effects_belt_width_spin_ptr->setValue(400);
	}
	handle_subduction_effects_controls_changed();
}


void
GPlatesQtWidgets::ViewportWindow::update_subduction_effects_palette(
		const QString &message)
{
	if (!d_subduction_effects_preview_button_ptr ||
			!d_subduction_effects_commit_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::GenerateSubductionEffectsOperation::SelectionMode mode =
			d_generate_subduction_effects_operation_ptr->selection_mode();
	d_subduction_effects_select_subduction_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateSubductionEffectsOperation::SELECTING_SUBDUCTION);
	d_subduction_effects_select_continent_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateSubductionEffectsOperation::SELECTING_CONTINENT);
	d_subduction_effects_subduction_status_label_ptr->setText(
			d_generate_subduction_effects_operation_ptr->subduction_status());
	d_subduction_effects_continent_status_label_ptr->setText(
			d_generate_subduction_effects_operation_ptr->continent_status());
	d_subduction_effects_clear_continent_button_ptr->setEnabled(
			d_generate_subduction_effects_operation_ptr->has_continent());
	const bool needs_continent = d_subduction_effect_type_combo_ptr->currentIndex() >= 2;
	d_subduction_effects_preview_button_ptr->setEnabled(
			d_generate_subduction_effects_operation_ptr->has_subduction() &&
			(!needs_continent || d_generate_subduction_effects_operation_ptr->has_continent()));
	d_subduction_effects_commit_button_ptr->setEnabled(
			d_generate_subduction_effects_operation_ptr->has_preview());
	d_subduction_effects_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("Aqua is the proposed island-arc notation line on the overriding plate; orange polygons are land-only active mountain belts. Yellow teeth point toward the selected overriding side. Use Flip polarity if that side is wrong.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::show_collision_orogeny_window()
{
	update_collision_palette();
	d_collision_dialog_ptr->show();
	d_collision_dialog_ptr->raise();
	d_collision_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_select_incoming()
{
	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->arm_incoming_selection();
	if (d_collision_orogeny_operation_ptr->selection_mode() ==
			GPlatesViewOperations::CollisionOrogenyOperation::SELECTING_INCOMING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_collision_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_select_receiving()
{
	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->arm_receiving_selection();
	if (d_collision_orogeny_operation_ptr->selection_mode() ==
			GPlatesViewOperations::CollisionOrogenyOperation::SELECTING_RECEIVING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_collision_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_select_trench()
{
	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->arm_trench_selection();
	status_message(result.message);
	update_collision_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_clear_trench()
{
	d_collision_orogeny_operation_ptr->clear_trench();
	const QString message = tr("Consumed trench cleared; collision features can still be previewed and committed.");
	status_message(message);
	update_collision_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->capture_armed_selection();
	if (result.outcome == GPlatesViewOperations::CollisionOrogenyOperation::OPERATION_CANCELLED)
	{
		return;
	}
	status_message(result.message);
	update_collision_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_preview()
{
	GPlatesViewOperations::CollisionOrogenyOperation::Options options;
	const int interpretation = d_collision_type_combo_ptr->currentIndex();
	options.auto_classify = interpretation == 0;
	if (interpretation == 1)
	{
		options.collision_type =
				GPlatesViewOperations::CollisionGeometry::ARC_OR_TERRANE_ACCRETION;
	}
	else if (interpretation == 3)
	{
		options.collision_type =
				GPlatesViewOperations::CollisionGeometry::HIMALAYAN_OROGENY;
	}
	else
	{
		options.collision_type = GPlatesViewOperations::CollisionGeometry::URAL_OROGENY;
	}
	options.precursor_collision_count = d_collision_precursor_spin_ptr->value();
	options.contact_threshold_km = d_collision_contact_threshold_spin_ptr->value();
	options.automatic_belt_width = d_collision_auto_width_check_ptr->isChecked();
	options.belt_width_km = d_collision_belt_width_spin_ptr->value();
	options.smoothing_iterations = d_collision_smoothing_spin_ptr->value();
	options.belt_irregularity = d_collision_irregularity_spin_ptr->value() / 100.0;
	options.active_duration_ma = d_collision_active_duration_spin_ptr->value();
	options.old_orogen_age_ma = d_collision_old_age_spin_ptr->value();
	options.terminate_consumed_trench = d_collision_terminate_trench_check_ptr->isChecked();
	options.allow_non_convergent = d_collision_allow_nonconvergent_check_ptr->isChecked();
	options.deform_contact_margins = d_collision_deform_margins_check_ptr->isChecked();
	options.deformation_reach_km = d_collision_deformation_reach_spin_ptr->value();
	options.retire_incoming_plate = d_collision_retire_incoming_check_ptr->isChecked();

	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->preview(options);
	status_message(result.message);
	update_collision_palette(result.message);
	if (result.outcome == GPlatesViewOperations::CollisionOrogenyOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Collision Preview"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_commit()
{
	const GPlatesViewOperations::CollisionOrogenyOperation::Result result =
			d_collision_orogeny_operation_ptr->commit();
	status_message(result.message);
	update_collision_palette(result.message);
	if (result.outcome == GPlatesViewOperations::CollisionOrogenyOperation::COLLISION_COMMITTED)
	{
		QMessageBox::information(this, tr("Collision Committed"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CollisionOrogenyOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Commit Collision"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_controls_changed()
{
	d_collision_orogeny_operation_ptr->clear_preview();
	update_collision_palette(tr("Controls changed; preview the revised collision before committing."));
}


void
GPlatesQtWidgets::ViewportWindow::handle_collision_type_changed(
		int index)
{
	if (index == 1)
	{
		d_collision_belt_width_spin_ptr->setValue(120);
	}
	else if (index == 2)
	{
		d_collision_belt_width_spin_ptr->setValue(180);
	}
	else if (index == 3)
	{
		d_collision_belt_width_spin_ptr->setValue(450);
	}
	handle_collision_controls_changed();
}


void
GPlatesQtWidgets::ViewportWindow::update_collision_palette(
		const QString &message)
{
	if (!d_collision_preview_button_ptr || !d_collision_commit_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::CollisionOrogenyOperation::SelectionMode mode =
			d_collision_orogeny_operation_ptr->selection_mode();
	d_collision_select_incoming_button_ptr->setChecked(
			mode == GPlatesViewOperations::CollisionOrogenyOperation::SELECTING_INCOMING_CONTINENT);
	d_collision_select_receiving_button_ptr->setChecked(
			mode == GPlatesViewOperations::CollisionOrogenyOperation::SELECTING_RECEIVING_CONTINENT);
	d_collision_select_trench_button_ptr->setChecked(
			mode == GPlatesViewOperations::CollisionOrogenyOperation::SELECTING_CONSUMED_TRENCH);
	d_collision_incoming_status_label_ptr->setText(
			d_collision_orogeny_operation_ptr->incoming_status());
	d_collision_receiving_status_label_ptr->setText(
			d_collision_orogeny_operation_ptr->receiving_status());
	d_collision_trench_status_label_ptr->setText(
			d_collision_orogeny_operation_ptr->trench_status());
	d_collision_clear_trench_button_ptr->setEnabled(
			d_collision_orogeny_operation_ptr->has_trench());
	d_collision_terminate_trench_check_ptr->setEnabled(
			d_collision_orogeny_operation_ptr->has_trench());
	d_collision_belt_width_spin_ptr->setEnabled(
			!d_collision_auto_width_check_ptr->isChecked());
	d_collision_deformation_reach_spin_ptr->setEnabled(
			d_collision_deform_margins_check_ptr->isChecked());
	d_collision_preview_button_ptr->setEnabled(
			d_collision_orogeny_operation_ptr->has_incoming() &&
			d_collision_orogeny_operation_ptr->has_receiving());
	d_collision_commit_button_ptr->setEnabled(
			d_collision_orogeny_operation_ptr->has_preview());
	d_collision_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("Fuchsia and blue are the original crust; green previews the welded younger margins; yellow is the optional consumed trench; aqua is the suture; orange is the orogenic belt. Plate retirement time-slices inherited crustal features onto the receiving Plate ID without rewriting older .rot history.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::show_post_collision_rift_window()
{
	update_post_collision_rift_palette();
	d_post_collision_rift_dialog_ptr->show();
	d_post_collision_rift_dialog_ptr->raise();
	d_post_collision_rift_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_select_host()
{
	const GPlatesViewOperations::PostCollisionRiftOperation::Result result =
			d_post_collision_rift_operation_ptr->arm_host_selection();
	if (d_post_collision_rift_operation_ptr->selection_mode() ==
			GPlatesViewOperations::PostCollisionRiftOperation::SELECTING_HOST_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_post_collision_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_select_suture()
{
	const GPlatesViewOperations::PostCollisionRiftOperation::Result result =
			d_post_collision_rift_operation_ptr->arm_suture_selection();
	status_message(result.message);
	update_post_collision_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::PostCollisionRiftOperation::Result result =
			d_post_collision_rift_operation_ptr->capture_armed_selection();
	if (result.outcome == GPlatesViewOperations::PostCollisionRiftOperation::OPERATION_CANCELLED)
	{
		return;
	}
	if (result.outcome == GPlatesViewOperations::PostCollisionRiftOperation::HOST_CAPTURED)
	{
		const int source = static_cast<int>(
				d_post_collision_rift_operation_ptr->source_plate_id());
		const int suggested = static_cast<int>(
				d_post_collision_rift_operation_ptr->suggest_new_plate_id());
		d_post_collision_rift_left_plate_spin_ptr->setValue(source);
		d_post_collision_rift_right_plate_spin_ptr->setValue(suggested);
		d_post_collision_rift_left_name_ptr->setText(
				tr("Plate %1 Rerift Continent").arg(source));
		d_post_collision_rift_right_name_ptr->setText(
				tr("Plate %1 Rerift Continent").arg(suggested));
	}
	status_message(result.message);
	update_post_collision_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_preview()
{
	GPlatesViewOperations::PostCollisionRiftOperation::Options options;
	options.suture_offset_km = d_post_collision_rift_offset_spin_ptr->value();
	options.wiggle = d_post_collision_rift_wiggle_spin_ptr->value() / 100.0;
	options.maximum_segment_length_km = d_post_collision_rift_segment_spin_ptr->value();
	options.end_extension_km = d_post_collision_rift_extension_spin_ptr->value();
	options.side = d_post_collision_rift_side_combo_ptr->currentIndex() == 0 ? 1 : -1;
	options.random_seed = static_cast<unsigned int>(d_post_collision_rift_seed_spin_ptr->value());
	const GPlatesViewOperations::PostCollisionRiftOperation::Result result =
			d_post_collision_rift_operation_ptr->preview(options);
	status_message(result.message);
	update_post_collision_rift_palette(result.message);
	if (result.outcome == GPlatesViewOperations::PostCollisionRiftOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Post-Collision Rerift Preview"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_commit()
{
	const GPlatesViewOperations::PostCollisionRiftOperation::Result result =
			d_post_collision_rift_operation_ptr->commit(
				d_post_collision_rift_left_name_ptr->text(),
				static_cast<GPlatesModel::integer_plate_id_type>(
						d_post_collision_rift_left_plate_spin_ptr->value()),
				d_post_collision_rift_right_name_ptr->text(),
				static_cast<GPlatesModel::integer_plate_id_type>(
						d_post_collision_rift_right_plate_spin_ptr->value()));
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::PostCollisionRiftOperation::RERIFT_COMMITTED)
	{
		d_post_collision_rift_operation_ptr->reset();
		QMessageBox::information(this, tr("Post-Collision Rerift Committed"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::PostCollisionRiftOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Commit Post-Collision Rerift"), result.message);
	}
	update_post_collision_rift_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_post_collision_rift_controls_changed()
{
	d_post_collision_rift_operation_ptr->clear_preview();
	update_post_collision_rift_palette(
			tr("Controls changed; preview a new craton-safe route before committing."));
}


void
GPlatesQtWidgets::ViewportWindow::update_post_collision_rift_palette(
		const QString &message)
{
	if (!d_post_collision_rift_preview_button_ptr || !d_post_collision_rift_commit_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::PostCollisionRiftOperation::SelectionMode mode =
			d_post_collision_rift_operation_ptr->selection_mode();
	d_post_collision_rift_select_host_button_ptr->setChecked(
			mode == GPlatesViewOperations::PostCollisionRiftOperation::SELECTING_HOST_CONTINENT);
	d_post_collision_rift_select_suture_button_ptr->setChecked(
			mode == GPlatesViewOperations::PostCollisionRiftOperation::SELECTING_SUTURE);
	d_post_collision_rift_host_status_label_ptr->setText(
			d_post_collision_rift_operation_ptr->host_status());
	d_post_collision_rift_suture_status_label_ptr->setText(
			d_post_collision_rift_operation_ptr->suture_status());
	d_post_collision_rift_preview_button_ptr->setEnabled(
			d_post_collision_rift_operation_ptr->has_host() &&
			d_post_collision_rift_operation_ptr->has_suture());
	d_post_collision_rift_commit_button_ptr->setEnabled(
			d_post_collision_rift_operation_ptr->has_preview());
	d_post_collision_rift_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("Blue is the inherited suture; yellow is the proposed rerift/MOR; aqua and orange are the two child sides. One child must retain the source Plate ID. Alternate seeds and small offset increases are tried automatically to avoid cratons.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::show_mantle_events_window()
{
	update_mantle_events_palette();
	d_mantle_events_dialog_ptr->show();
	d_mantle_events_dialog_ptr->raise();
	d_mantle_events_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_select_continent()
{
	const GPlatesViewOperations::GenerateMantleEventsOperation::Result result =
			d_generate_mantle_events_operation_ptr->arm_continent_selection();
	if (d_generate_mantle_events_operation_ptr->selection_mode() ==
			GPlatesViewOperations::GenerateMantleEventsOperation::SELECTING_CONTINENT)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
	}
	status_message(result.message);
	update_mantle_events_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_select_rift()
{
	const GPlatesViewOperations::GenerateMantleEventsOperation::Result result =
			d_generate_mantle_events_operation_ptr->arm_rift_selection();
	status_message(result.message);
	update_mantle_events_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_clear_rift()
{
	d_generate_mantle_events_operation_ptr->clear_rift();
	const QString message = tr("Rift trigger cleared; choose Random placement or select another rift.");
	status_message(message);
	update_mantle_events_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	const GPlatesViewOperations::GenerateMantleEventsOperation::Result result =
			d_generate_mantle_events_operation_ptr->capture_armed_selection();
	if (result.outcome == GPlatesViewOperations::GenerateMantleEventsOperation::OPERATION_CANCELLED)
	{
		return;
	}
	status_message(result.message);
	update_mantle_events_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_preview()
{
	GPlatesViewOperations::GenerateMantleEventsOperation::Options options;
	options.rift_triggered = d_mantle_events_placement_combo_ptr->currentIndex() == 0;
	options.lip_diameter_km = d_mantle_events_diameter_spin_ptr->value();
	options.lip_irregularity = d_mantle_events_irregularity_spin_ptr->value() / 100.0;
	options.maximum_segment_length_km = d_mantle_events_segment_spin_ptr->value();
	options.random_seed = static_cast<unsigned int>(d_mantle_events_seed_spin_ptr->value());
	options.active_lip_duration_ma = d_mantle_events_active_duration_spin_ptr->value();
	options.create_hotspot = d_mantle_events_create_hotspot_check_ptr->isChecked();
	options.hotspot_lifetime_ma = d_mantle_events_hotspot_lifetime_spin_ptr->value();
	options.trail_step_ma = d_mantle_events_trail_step_spin_ptr->value();
	options.mantle_plate_id = static_cast<GPlatesModel::integer_plate_id_type>(
			d_mantle_events_mantle_plate_spin_ptr->value());
	const GPlatesViewOperations::GenerateMantleEventsOperation::Result result =
			d_generate_mantle_events_operation_ptr->preview(options);
	status_message(result.message);
	update_mantle_events_palette(result.message);
	if (result.outcome == GPlatesViewOperations::GenerateMantleEventsOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("LIP and Hotspot Preview"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_commit()
{
	const GPlatesViewOperations::GenerateMantleEventsOperation::Result result =
			d_generate_mantle_events_operation_ptr->commit();
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::GenerateMantleEventsOperation::EVENTS_COMMITTED)
	{
		d_generate_mantle_events_operation_ptr->reset();
		QMessageBox::information(this, tr("LIP and Hotspot Event Committed"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::GenerateMantleEventsOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Commit LIP and Hotspot Event"), result.message);
	}
	update_mantle_events_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_mantle_events_controls_changed()
{
	d_generate_mantle_events_operation_ptr->clear_preview();
	update_mantle_events_palette(
			tr("Controls changed; preview the revised mantle event before committing."));
}


void
GPlatesQtWidgets::ViewportWindow::update_mantle_events_palette(
		const QString &message)
{
	if (!d_mantle_events_preview_button_ptr || !d_mantle_events_commit_button_ptr)
	{
		return;
	}
	const GPlatesViewOperations::GenerateMantleEventsOperation::SelectionMode mode =
			d_generate_mantle_events_operation_ptr->selection_mode();
	d_mantle_events_select_continent_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateMantleEventsOperation::SELECTING_CONTINENT);
	d_mantle_events_select_rift_button_ptr->setChecked(
			mode == GPlatesViewOperations::GenerateMantleEventsOperation::SELECTING_RIFT);
	d_mantle_events_continent_status_label_ptr->setText(
			d_generate_mantle_events_operation_ptr->continent_status());
	d_mantle_events_rift_status_label_ptr->setText(
			d_generate_mantle_events_operation_ptr->rift_status());
	d_mantle_events_clear_rift_button_ptr->setEnabled(
			d_generate_mantle_events_operation_ptr->has_rift());
	const bool rift_required = d_mantle_events_placement_combo_ptr->currentIndex() == 0;
	d_mantle_events_preview_button_ptr->setEnabled(
			d_generate_mantle_events_operation_ptr->has_continent() &&
			(!rift_required || d_generate_mantle_events_operation_ptr->has_rift()));
	d_mantle_events_commit_button_ptr->setEnabled(
			d_generate_mantle_events_operation_ptr->has_preview());
	const bool create_hotspot = d_mantle_events_create_hotspot_check_ptr->isChecked();
	d_mantle_events_hotspot_lifetime_spin_ptr->setEnabled(create_hotspot);
	d_mantle_events_trail_step_spin_ptr->setEnabled(create_hotspot);
	d_mantle_events_mantle_plate_spin_ptr->setEnabled(create_hotspot);
	d_mantle_events_instruction_label_ptr->setText(
			message.isEmpty()
					? tr("Magenta is the contained LIP footprint, aqua is the mantle-fixed hotspot seed, orange is the optional rift trigger, and white is the selected host crust. Oversized proposals shrink visibly and report their effective diameter.")
					: message);
}


void
GPlatesQtWidgets::ViewportWindow::apply_artifexia_preset()
{
	const ArtifexiaPresetResult result = apply_artifexia_presentation(*this);

	const QString message = tr(
			"Feature-type lists now show Artifexia's %1 types. Its saved draw styles were applied to %2 matching loaded/generated layer(s)%3.")
			.arg(result.feature_type_count)
			.arg(result.matching_layer_count)
			.arg(result.unavailable_style_count
					? tr("; %1 style(s) were unavailable").arg(result.unavailable_style_count)
					: QString());
	status_message(message);
	QMessageBox::information(this, tr("Artifexia Project Preset"), message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_create_triple_junction_crust()
{
	const GPlatesViewOperations::CreateTripleJunctionCrustOperation::Result result =
			d_create_triple_junction_crust_operation_ptr->trigger(this);
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::CreateTripleJunctionCrustOperation::SELECTION_REQUIRED)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
		QMessageBox::information(this, tr("Shift-Click Three Half-Stage MORs"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateTripleJunctionCrustOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("RRR Triple-Junction Crust Generated"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreateTripleJunctionCrustOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Generate RRR Triple-Junction Crust"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_create_pacific_plate()
{
	const GPlatesViewOperations::CreatePacificPlateOperation::Result result =
			d_create_pacific_plate_operation_ptr->trigger(this);
	status_message(result.message);
	if (result.outcome == GPlatesViewOperations::CreatePacificPlateOperation::SELECTION_REQUIRED)
	{
		activate_choose_feature_tool(canvas_tool_workflows());
		QMessageBox::information(this, tr("Select MORs and Local Void"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::CreatePacificPlateOperation::OPERATION_COMPLETED)
	{
		QMessageBox follow_up(
				QMessageBox::Information,
				tr("Pacific-Style Plate Created"),
				result.message,
				QMessageBox::NoButton,
				this);
		QPushButton *open_rotation_editor = follow_up.addButton(
				tr("Open Rotation File Editor"), QMessageBox::ActionRole);
		follow_up.addButton(QMessageBox::Close);
		follow_up.exec();
		if (follow_up.clickedButton() == open_rotation_editor)
		{
			handle_rotation_file_editor();
		}
	}
	else if (result.outcome == GPlatesViewOperations::CreatePacificPlateOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Create Pacific-Style Plate"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::show_boolean_polygons_window()
{
	update_boolean_palette();
	d_boolean_polygon_dialog_ptr->show();
	d_boolean_polygon_dialog_ptr->raise();
	d_boolean_polygon_dialog_ptr->activateWindow();
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_select_first()
{
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_boolean_polygon_operation_ptr->arm_first_selection();
	status_message(result.message);
	update_boolean_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_select_operand()
{
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_boolean_polygon_operation_ptr->arm_operand_selection();
	status_message(result.message);
	update_boolean_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_remove_operand()
{
	d_boolean_polygon_operation_ptr->remove_last_operand();
	const QString message = tr("Removed the most recently selected operand; preview invalidated.");
	status_message(message);
	update_boolean_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_clear_operands()
{
	d_boolean_polygon_operation_ptr->clear_operands();
	const QString message = tr("Operand selection cleared; the first polygon is unchanged.");
	status_message(message);
	update_boolean_palette(message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_preview()
{
	GPlatesViewOperations::SubductionCutterGeometry::BooleanOperation operation =
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_UNION;
	switch (d_boolean_operation_combo_ptr->currentIndex())
	{
	case 1:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_DIFFERENCE;
		break;
	case 2:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_INTERSECTION;
		break;
	case 3:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_SYMMETRIC_DIFFERENCE;
		break;
	default:
		break;
	}
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_boolean_polygon_operation_ptr->preview(operation);
	status_message(result.message);
	update_boolean_palette(result.message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_focus_changed(
		GPlatesGui::FeatureFocus &)
{
	if (d_boolean_polygon_operation_ptr->selection_mode() ==
			GPlatesViewOperations::BooleanPolygonOperation::NOT_SELECTING)
	{
		return;
	}
	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_boolean_polygon_operation_ptr->capture_armed_selection();
	if (!result.message.isEmpty())
	{
		status_message(result.message);
		update_boolean_palette(result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_apply()
{
	GPlatesViewOperations::SubductionCutterGeometry::BooleanOperation operation =
			GPlatesViewOperations::SubductionCutterGeometry::POLYGON_UNION;
	switch (d_boolean_operation_combo_ptr->currentIndex())
	{
	case 1:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_DIFFERENCE;
		break;
	case 2:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_INTERSECTION;
		break;
	case 3:
		operation = GPlatesViewOperations::SubductionCutterGeometry::POLYGON_SYMMETRIC_DIFFERENCE;
		break;
	default:
		break;
	}

	const GPlatesViewOperations::BooleanPolygonOperation::Result result =
			d_boolean_polygon_operation_ptr->apply(operation);
	status_message(result.message);
	update_boolean_palette(result.message);
	if (result.outcome == GPlatesViewOperations::BooleanPolygonOperation::BOOLEAN_COMPLETED)
	{
		d_boolean_polygon_dialog_ptr->hide();
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_boolean_cancel()
{
	d_boolean_polygon_operation_ptr->reset();
	d_boolean_polygon_dialog_ptr->hide();
	update_boolean_palette(tr("Boolean Polygons cancelled; no data changed."));
}


void
GPlatesQtWidgets::ViewportWindow::update_boolean_palette(
		const QString &message)
{
	const GPlatesViewOperations::BooleanPolygonOperation::SelectionMode mode =
			d_boolean_polygon_operation_ptr->selection_mode();
	{
		const QSignalBlocker blocker(d_boolean_select_first_button_ptr);
		d_boolean_select_first_button_ptr->setChecked(
				mode == GPlatesViewOperations::BooleanPolygonOperation::SELECTING_FIRST);
	}
	{
		const QSignalBlocker blocker(d_boolean_select_operand_button_ptr);
		d_boolean_select_operand_button_ptr->setChecked(
				mode == GPlatesViewOperations::BooleanPolygonOperation::SELECTING_OPERAND);
	}

	d_boolean_first_status_label_ptr->setText(
			d_boolean_polygon_operation_ptr->first_status());
	d_boolean_operands_status_label_ptr->setText(
			d_boolean_polygon_operation_ptr->operands_status());
	d_boolean_select_operand_button_ptr->setEnabled(
			d_boolean_polygon_operation_ptr->has_first());
	d_boolean_clear_operands_button_ptr->setEnabled(
			d_boolean_polygon_operation_ptr->operand_count() > 0);
	d_boolean_remove_operand_button_ptr->setEnabled(
			d_boolean_polygon_operation_ptr->operand_count() > 0);
	d_boolean_preview_button_ptr->setEnabled(
			d_boolean_polygon_operation_ptr->has_first() &&
			d_boolean_polygon_operation_ptr->operand_count() > 0);
	d_boolean_apply_button_ptr->setEnabled(
			d_boolean_polygon_operation_ptr->preview_ready());
	d_boolean_instruction_label_ptr->setText(message.isEmpty()
			? tr("Select the first polygon, add one or more operands, choose the operation, and review the green non-destructive preview. Apply and Finish becomes available only for that reviewed result.")
			: message);
}


void
GPlatesQtWidgets::ViewportWindow::handle_split_plate()
{
	const GPlatesViewOperations::SplitPlateOperation::Result result =
			d_split_plate_operation_ptr->trigger();

	status_message(result.message);

	switch (result.outcome)
	{
	case GPlatesViewOperations::SplitPlateOperation::POLYGON_CAPTURED:
		QMessageBox::information(
				this,
				tr("Split Plate — Polygon Captured"),
				result.message);
		break;

	case GPlatesViewOperations::SplitPlateOperation::SPLIT_COMPLETED:
		QMessageBox::information(
				this,
				tr("Split Plate Complete"),
				result.message);
		break;

	case GPlatesViewOperations::SplitPlateOperation::OPERATION_ERROR:
		QMessageBox::warning(
				this,
				tr("Split Plate"),
				result.message);
		break;
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_naturalize_coastline()
{
	const GPlatesViewOperations::NaturalizeCoastlineOperation::Result result =
			d_naturalize_coastline_operation_ptr->trigger(this);
	status_message(result.message);

	if (result.outcome == GPlatesViewOperations::NaturalizeCoastlineOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Naturalize Coastline"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::NaturalizeCoastlineOperation::NATURALIZE_COMPLETED)
	{
		QMessageBox::information(this, tr("Naturalize Coastline Complete"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_subduction_cutter()
{
	const GPlatesViewOperations::SubductionCutterOperation::Result result =
			d_subduction_cutter_operation_ptr->trigger(this);
	status_message(result.message);

	if (result.outcome == GPlatesViewOperations::SubductionCutterOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Retire Subducted Oceanic Crust"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::SubductionCutterOperation::CUT_COMPLETED)
	{
		QMessageBox::information(this, tr("Oceanic-Crust Retirement Complete"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::handle_rotation_file_editor()
{
	const GPlatesViewOperations::RotationFileEditorOperation::Result result =
			d_rotation_file_editor_operation_ptr->trigger(this);
	status_message(result.message);

	if (result.outcome == GPlatesViewOperations::RotationFileEditorOperation::OPERATION_ERROR)
	{
		QMessageBox::warning(this, tr("Rotation File Editor"), result.message);
	}
	else if (result.outcome == GPlatesViewOperations::RotationFileEditorOperation::OPERATION_COMPLETED)
	{
		QMessageBox::information(this, tr("Rotation File Editor"), result.message);
	}
}


void
GPlatesQtWidgets::ViewportWindow::open_dataset_webpage()
{
	QDesktopServices::openUrl(
			QUrl("http://www.earthbyte.org/gplates-2-5-software-and-data-sets"));
}


void
GPlatesQtWidgets::ViewportWindow::showEvent(
		QShowEvent *ev)
{
	// We wait until the main window is visible before checking our status messages and
	// View-dependent menu items are configured appropriately; this is largely because of
	// the definition of ReconstructionViewWidget::globe_is_active().
	update_tools_and_status_message();
}
