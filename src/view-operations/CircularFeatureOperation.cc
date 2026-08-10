/*
 * Copyright (C) 2026 The GPlates development team.
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#include "CircularFeatureOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>

#include <QButtonGroup>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/PlanetaryParameters.h"
#include "app-logic/ProjectDocumentRegistry.h"
#include "app-logic/ProjectMetadata.h"

#include "gui/CanvasToolWorkflows.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "maths/SmallCircle.h"
#include "maths/UnitVector3D.h"
#include "maths/Vector3D.h"

#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "qt-widgets/SmallCircleWidget.h"
#include "qt-widgets/TaskPanel.h"
#include "qt-widgets/ViewportWindow.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/UndoRedo.h"
#include "view-operations/WorldbuildingFeatureCollectionUtils.h"


namespace
{
	const double PI = 3.141592653589793238462643383279502884;
	const unsigned int CIRCULAR_VERTEX_COUNT = 96;
	const unsigned int MINIMUM_REGULAR_VERTEX_COUNT = 3;

	// Used only when a Primary Project Document exists but says nothing about resolution -
	// matches the template's own default_km, so Regular mode has a sane fallback rather than
	// inventing an unrelated number.
	const double FALLBACK_MAX_SEGMENT_KM = 500.0;

	enum GeometryType
	{
		POLYLINE_GEOMETRY,
		POLYGON_GEOMETRY
	};

	enum CircleStyle
	{
		CIRCULAR_STYLE,
		REGULAR_STYLE
	};


	/**
	 * The longest segment a Regular-style circle's edge should have, in kilometres.
	 *
	 * Reads the same gplates.resolution front matter as the project document, keyed to
	 * gpml:TerraneBoundary if the project has an override for it, else the project's
	 * default_km, else a hard-coded fallback if there is no readable resolution section at all.
	 * This is a one-shot read rather than a cached/reactive value, since the dialog is
	 * reopened fresh each time and the document could have changed since the last placement.
	 */
	double
	max_segment_length_km(
			GPlatesAppLogic::ApplicationState &application_state)
	{
		const QString primary_document_path =
				application_state.get_project_document_registry().primary_document_path();
		if (!primary_document_path.isEmpty())
		{
			QFile primary_document(primary_document_path);
			if (primary_document.open(QIODevice::ReadOnly))
			{
				const GPlatesAppLogic::ProjectMetadata metadata =
						GPlatesAppLogic::ProjectMetadataParser::parse(
								QString::fromUtf8(primary_document.readAll()));
				const QMap<QString, double>::const_iterator by_type_iter =
						metadata.resolution_km_by_feature_type.constFind("gpml:TerraneBoundary");
				if (by_type_iter != metadata.resolution_km_by_feature_type.constEnd())
				{
					return by_type_iter.value();
				}
				if (metadata.default_resolution_km)
				{
					return metadata.default_resolution_km.get();
				}
			}
		}
		return FALLBACK_MAX_SEGMENT_KM;
	}


	/**
	 * How many vertices a circle of the given radius needs so every edge segment of its
	 * circumference stays under @a max_segment_km.
	 */
	unsigned int
	regular_vertex_count(
			double radius_km,
			double max_segment_km)
	{
		if (max_segment_km <= 0.0)
		{
			return CIRCULAR_VERTEX_COUNT;
		}
		const double circumference_km = 2.0 * PI * radius_km;
		const unsigned int required_vertices = static_cast<unsigned int>(
				std::ceil(circumference_km / max_segment_km));
		return std::max(required_vertices, MINIMUM_REGULAR_VERTEX_COUNT);
	}


	void
	add_required_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &property_value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, property_name, property_value))
		{
			throw std::runtime_error(
					QString("Unable to add required GPML property '%1'.")
							.arg(property_name.get_name().qstring())
							.toStdString());
		}
	}


	std::vector<GPlatesMaths::PointOnSphere>
	create_circle_vertices(
			const GPlatesMaths::SmallCircle &circle,
			unsigned int vertex_count)
	{
		const GPlatesMaths::UnitVector3D centre = circle.axis_vector();
		const GPlatesMaths::UnitVector3D tangent_x =
				GPlatesMaths::generate_perpendicular(centre);
		const GPlatesMaths::UnitVector3D tangent_y =
				GPlatesMaths::cross(centre, tangent_x).get_normalisation();
		const double angular_radius = circle.colatitude().dval();
		const double centre_scale = std::cos(angular_radius);
		const double tangent_scale = std::sin(angular_radius);

		std::vector<GPlatesMaths::PointOnSphere> vertices;
		vertices.reserve(vertex_count);
		for (unsigned int vertex_index = 0;
				vertex_index < vertex_count;
				++vertex_index)
		{
			const double azimuth = 2.0 * PI * vertex_index / vertex_count;
			const double tangent_x_scale = tangent_scale * std::cos(azimuth);
			const double tangent_y_scale = tangent_scale * std::sin(azimuth);
			const GPlatesMaths::Vector3D vertex(
					centre_scale * centre.x().dval() +
						tangent_x_scale * tangent_x.x().dval() +
						tangent_y_scale * tangent_y.x().dval(),
					centre_scale * centre.y().dval() +
						tangent_x_scale * tangent_x.y().dval() +
						tangent_y_scale * tangent_y.y().dval(),
					centre_scale * centre.z().dval() +
						tangent_x_scale * tangent_x.z().dval() +
						tangent_y_scale * tangent_y.z().dval());
			vertices.push_back(GPlatesMaths::PointOnSphere(vertex.get_normalisation()));
		}

		return vertices;
	}


	class AddCircularFeatureUndoCommand :
			public QUndoCommand
	{
	public:
		AddCircularFeatureUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &feature) :
			d_model_interface(model_interface),
			d_collection(collection),
			d_feature(feature)
		{
			setText(QObject::tr("place circular feature"));
		}

		virtual void
		redo()
		{
			if (!d_collection.is_valid() || d_feature->parent_ptr())
			{
				return;
			}

			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_collection->add(d_feature);
			guard.release_guard();
		}

		virtual void
		undo()
		{
			if (!d_feature->parent_ptr())
			{
				return;
			}

			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_feature->remove_from_parent();
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_feature;
	};
}


GPlatesViewOperations::CircularFeatureOperation::CircularFeatureOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesQtWidgets::ViewportWindow &viewport_window) :
	d_application_state(application_state),
	d_viewport_window(viewport_window),
	d_small_circle_widget(viewport_window.task_panel_ptr()->small_circle_widget()),
	d_placement_active(false),
	d_feature_count(0)
{
	QObject::connect(
			&d_small_circle_widget,
			SIGNAL(circle_completed()),
			this,
			SLOT(handle_circle_completed()));
	QObject::connect(
			&d_application_state.get_feature_collection_file_state(),
			SIGNAL(file_state_changed(GPlatesAppLogic::FeatureCollectionFileState &)),
			this,
			SLOT(refresh_output_collections()));
}


void
GPlatesViewOperations::CircularFeatureOperation::trigger()
{
	if (!d_dialog)
	{
		create_dialog();
	}

	d_dialog->show();
	d_dialog->raise();
	d_dialog->activateWindow();
	activate_placement();
}


void
GPlatesViewOperations::CircularFeatureOperation::create_dialog()
{
	d_dialog = new QDialog(
			&d_viewport_window,
			Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
			Qt::WindowCloseButtonHint);
	d_dialog->setObjectName("WorldbuildingCircularFeaturesDialog");
	d_dialog->setWindowTitle(tr("World Building - Circular Features"));
	d_dialog->setModal(false);

	QVBoxLayout *layout = new QVBoxLayout(d_dialog);
	QLabel *introduction = new QLabel(tr(
			"Place a centre with the first click, move the pointer to size the circle, "
			"then click again to create the feature. The radius stops growing at the "
			"configured maximum."), d_dialog);
	introduction->setWordWrap(true);
	layout->addWidget(introduction);

	QHBoxLayout *style_layout = new QHBoxLayout();
	style_layout->addWidget(new QLabel(tr("Circle style:"), d_dialog));
	d_circular_style_radio = new QRadioButton(tr("Circular"), d_dialog);
	d_circular_style_radio->setChecked(true);
	d_circular_style_radio->setToolTip(tr(
			"A smooth true circle - the same fixed vertex count regardless of radius."));
	d_regular_style_radio = new QRadioButton(tr("Regular"), d_dialog);
	d_regular_style_radio->setToolTip(tr(
			"Vertex spacing follows the project's intended resolution (gplates.resolution in the"
			" Primary Project Document - by_feature_type: TerraneBoundary if set, else"
			" default_km), so every edge segment of the circumference stays under that length."
			" Larger circles get more vertices, not coarser ones."));
	QButtonGroup *style_group = new QButtonGroup(d_dialog);
	style_group->addButton(d_circular_style_radio);
	style_group->addButton(d_regular_style_radio);
	style_layout->addWidget(d_circular_style_radio);
	style_layout->addWidget(d_regular_style_radio);
	style_layout->addStretch();
	layout->addLayout(style_layout);

	QFormLayout *form = new QFormLayout();
	d_appearance_time_spin = new QDoubleSpinBox(d_dialog);
	d_appearance_time_spin->setRange(0.0, 10000.0);
	d_appearance_time_spin->setDecimals(3);
	d_appearance_time_spin->setSuffix(tr(" Ma"));
	d_appearance_time_spin->setValue(
			d_application_state.get_current_reconstruction_time());
	d_appearance_time_spin->setToolTip(tr(
			"The feature is valid from this geological age through 0 Ma (the present day)."));
	form->addRow(tr("Appears at:"), d_appearance_time_spin);

	d_geometry_type_combo = new QComboBox(d_dialog);
	d_geometry_type_combo->addItem(tr("Polygon"), POLYGON_GEOMETRY);
	d_geometry_type_combo->addItem(tr("Polyline"), POLYLINE_GEOMETRY);
	form->addRow(tr("Geometry:"), d_geometry_type_combo);

	const double planet_radius_km =
			d_application_state.get_planetary_parameters().effective_radius_kilometres();
	d_maximum_radius_spin = new QDoubleSpinBox(d_dialog);
	d_maximum_radius_spin->setRange(0.1, std::max(0.1, 0.99 * PI * planet_radius_km));
	d_maximum_radius_spin->setDecimals(1);
	d_maximum_radius_spin->setSuffix(tr(" km"));
	d_maximum_radius_spin->setValue(std::min(1000.0, 0.25 * PI * planet_radius_km));
	d_maximum_radius_spin->setToolTip(tr(
			"Maximum centre-to-edge distance, measured using the current project planet radius."));
	form->addRow(tr("Maximum radius:"), d_maximum_radius_spin);

	d_output_collection_combo = new QComboBox(d_dialog);
	d_output_collection_combo->setToolTip(tr(
			"The feature collection that will own each completed circle. Pick any currently "
			"loaded layer to add circles to it, or choose \"Create a new...\" to start a fresh "
			"collection just for this tool's output."));
	form->addRow(tr("Output layer / collection:"), d_output_collection_combo);
	layout->addLayout(form);

	QLabel *output_note = new QLabel(tr(
			"Every completed circle is a real feature in the selected collection, assigned to "
			"Plate ID 0 and marked as modified immediately. Save the collection or project to "
			"keep it across sessions; use the normal feature tools to reassign its Plate ID."),
			d_dialog);
	output_note->setWordWrap(true);
	layout->addWidget(output_note);

	d_status_label = new QLabel(d_dialog);
	d_status_label->setWordWrap(true);
	layout->addWidget(d_status_label);

	QHBoxLayout *action_layout = new QHBoxLayout();
	QPushButton *start_button = new QPushButton(tr("Start placing"), d_dialog);
	start_button->setDefault(true);
	action_layout->addWidget(start_button);
	action_layout->addStretch();
	layout->addLayout(action_layout);

	QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, d_dialog);
	layout->addWidget(buttons);

	QObject::connect(start_button, SIGNAL(clicked()), this, SLOT(activate_placement()));
	QObject::connect(
			d_maximum_radius_spin,
			SIGNAL(valueChanged(double)),
			this,
			SLOT(handle_maximum_radius_changed(double)));
	QObject::connect(buttons, SIGNAL(rejected()), d_dialog, SLOT(reject()));
	QObject::connect(
			d_dialog,
			SIGNAL(finished(int)),
			this,
			SLOT(handle_dialog_finished(int)));

	refresh_output_collections();
	d_dialog->resize(430, d_dialog->sizeHint().height());
}


void
GPlatesViewOperations::CircularFeatureOperation::activate_placement()
{
	if (!d_dialog)
	{
		return;
	}

	d_placement_active = true;
	handle_maximum_radius_changed(d_maximum_radius_spin->value());
	d_viewport_window.canvas_tool_workflows().choose_canvas_tool(
			GPlatesGui::CanvasToolWorkflows::WORKFLOW_SMALL_CIRCLE,
			GPlatesGui::CanvasToolWorkflows::TOOL_CREATE_SMALL_CIRCLE);
	d_status_label->setText(tr(
			"Ready: click the circle centre, move to choose its radius, then click again."));
}


void
GPlatesViewOperations::CircularFeatureOperation::handle_maximum_radius_changed(
		double maximum_radius_km)
{
	if (!d_placement_active)
	{
		return;
	}

	const double planet_radius_km =
			d_application_state.get_planetary_parameters().effective_radius_kilometres();
	d_small_circle_widget.set_maximum_radius_radians(
			maximum_radius_km / planet_radius_km);
}


void
GPlatesViewOperations::CircularFeatureOperation::handle_dialog_finished(
		int)
{
	d_placement_active = false;
	d_small_circle_widget.set_maximum_radius_radians(boost::none);
}


void
GPlatesViewOperations::CircularFeatureOperation::refresh_output_collections()
{
	if (!d_output_collection_combo)
	{
		return;
	}

	boost::optional<GPlatesModel::FeatureCollectionHandle::weak_ref> previous_collection;
	const int previous_collection_index = d_output_collection_combo->currentData().toInt();
	if (previous_collection_index >= 0 &&
			previous_collection_index < static_cast<int>(d_output_collections.size()) &&
			d_output_collections[previous_collection_index].is_valid())
	{
		previous_collection = d_output_collections[previous_collection_index];
	}

	const QSignalBlocker blocker(d_output_collection_combo);
	d_output_collection_combo->clear();
	d_output_collections.clear();
	d_output_collection_combo->addItem(
			tr("Create a new \"Circular Features\" collection"), -1);

	int selected_combo_index = 0;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
			d_application_state.get_feature_collection_file_state().get_loaded_files();
	for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
			file_iter = files.begin(); file_iter != files.end(); ++file_iter)
	{
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file_iter->get_file().get_feature_collection();
		const int collection_index = static_cast<int>(d_output_collections.size());
		d_output_collections.push_back(collection);
		d_output_collection_combo->addItem(
				file_iter->get_file().get_file_info().get_display_name(true),
				collection_index);
		if (previous_collection && collection == previous_collection.get())
		{
			selected_combo_index = d_output_collection_combo->count() - 1;
		}
	}

	d_output_collection_combo->setCurrentIndex(selected_combo_index);
}


GPlatesModel::FeatureCollectionHandle::weak_ref
GPlatesViewOperations::CircularFeatureOperation::ensure_output_collection()
{
	const int collection_index = d_output_collection_combo->currentData().toInt();
	if (collection_index >= 0 &&
			collection_index < static_cast<int>(d_output_collections.size()) &&
			d_output_collections[collection_index].is_valid())
	{
		return d_output_collections[collection_index];
	}
	if (collection_index >= 0)
	{
		throw std::runtime_error("The selected output collection is no longer loaded.");
	}

	GPlatesAppLogic::FeatureCollectionFileState::file_reference output_file =
			create_named_empty_feature_collection(
					d_application_state.get_feature_collection_file_io(),
					tr("Circular Features"));
	const GPlatesModel::FeatureCollectionHandle::weak_ref output_collection =
			output_file.get_file().get_feature_collection();

	// File-state signals normally refreshed the combo while the collection was
	// created and named. Find and select that durable collection explicitly.
	for (int output_index = 0;
			output_index < static_cast<int>(d_output_collections.size());
			++output_index)
	{
		if (d_output_collections[output_index] == output_collection)
		{
			const int combo_index = d_output_collection_combo->findData(output_index);
			if (combo_index >= 0)
			{
				d_output_collection_combo->setCurrentIndex(combo_index);
			}
			return output_collection;
		}
	}

	// Be defensive if a host suppresses the usual file-state notification.
	const int output_index = static_cast<int>(d_output_collections.size());
	d_output_collections.push_back(output_collection);
	d_output_collection_combo->addItem(tr("Circular Features (unsaved)"), output_index);
	d_output_collection_combo->setCurrentIndex(d_output_collection_combo->count() - 1);
	return output_collection;
}


void
GPlatesViewOperations::CircularFeatureOperation::handle_circle_completed()
{
	if (!d_placement_active || d_small_circle_widget.small_circle_collection().empty())
	{
		return;
	}

	try
	{
		create_circular_feature(d_small_circle_widget.small_circle_collection().back());
	}
	catch (const std::exception &exception)
	{
		d_status_label->setText(tr("The circle was not created: %1")
				.arg(QString::fromUtf8(exception.what())));
		QMessageBox::warning(
				d_dialog,
				tr("Circular Features"),
				d_status_label->text());
	}
}


void
GPlatesViewOperations::CircularFeatureOperation::create_circular_feature(
		const GPlatesMaths::SmallCircle &circle)
{
	const double angular_radius = circle.colatitude().dval();
	if (angular_radius <= 0.0)
	{
		throw std::runtime_error("Circle radius must be greater than zero.");
	}

	const double planet_radius_km =
			d_application_state.get_planetary_parameters().effective_radius_kilometres();
	const double radius_km = angular_radius * planet_radius_km;

	const CircleStyle circle_style =
			d_regular_style_radio->isChecked() ? REGULAR_STYLE : CIRCULAR_STYLE;
	const unsigned int vertex_count = circle_style == REGULAR_STYLE
			? regular_vertex_count(radius_km, max_segment_length_km(d_application_state))
			: CIRCULAR_VERTEX_COUNT;

	const GeometryType geometry_type = static_cast<GeometryType>(
			d_geometry_type_combo->currentData().toInt());
	std::vector<GPlatesMaths::PointOnSphere> vertices = create_circle_vertices(circle, vertex_count);
	boost::optional<GPlatesModel::PropertyValue::non_null_ptr_type> geometry_value;
	if (geometry_type == POLYGON_GEOMETRY)
	{
		geometry_value = GPlatesAppLogic::GeometryUtils::create_geometry_property_value(
				GPlatesMaths::PolygonOnSphere::create(vertices));
	}
	else
	{
		vertices.push_back(vertices.front());
		geometry_value = GPlatesAppLogic::GeometryUtils::create_geometry_property_value(
				GPlatesMaths::PolylineOnSphere::create(vertices));
	}
	if (!geometry_value)
	{
		throw std::runtime_error("Could not encode the circular geometry.");
	}

	const unsigned int next_feature_number = d_feature_count + 1;
	const QString geometry_label = geometry_type == POLYGON_GEOMETRY
			? tr("Circular polygon")
			: tr("Circular polyline");
	const double appearance_time = d_appearance_time_spin->value();
	const GPlatesMaths::LatLonPoint centre = GPlatesMaths::make_lat_lon_point(
			GPlatesMaths::PointOnSphere(circle.axis_vector()));

	GPlatesModel::FeatureHandle::non_null_ptr_type feature =
			GPlatesModel::FeatureHandle::create(
				GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
	const GPlatesModel::FeatureHandle::weak_ref feature_ref = feature->reference();
	add_required_property(
			feature_ref,
			GPlatesModel::PropertyName::create_gml("name"),
			GPlatesPropertyValues::XsString::create(
				GPlatesUtils::make_icu_string_from_qstring(
					QString("%1 %2").arg(geometry_label).arg(next_feature_number))));
	add_required_property(
			feature_ref,
			GPlatesModel::PropertyName::create_gml("description"),
			GPlatesPropertyValues::XsString::create(
				GPlatesUtils::make_icu_string_from_qstring(tr(
					"World Building circular feature; radius_km=%1; centre_lat=%2; "
					"centre_lon=%3; appears_at_ma=%4; persists_to_present=true;")
							.arg(radius_km, 0, 'f', 3)
							.arg(centre.latitude(), 0, 'f', 6)
							.arg(centre.longitude(), 0, 'f', 6)
							.arg(appearance_time, 0, 'f', 3))));
	add_required_property(
			feature_ref,
			GPlatesModel::PropertyName::create_gml("validTime"),
			GPlatesPropertyValues::GmlTimePeriod::create(
				GPlatesModel::ModelUtils::create_gml_time_instant(
					GPlatesPropertyValues::GeoTimeInstant(appearance_time)),
				GPlatesModel::ModelUtils::create_gml_time_instant(
					GPlatesPropertyValues::GeoTimeInstant(0.0))));
	add_required_property(
			feature_ref,
			GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
			GPlatesPropertyValues::GpmlPlateId::create(0));
	add_required_property(
			feature_ref,
			GPlatesModel::PropertyName::create_gpml("unclassifiedGeometry"),
			geometry_value.get());

	const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
			ensure_output_collection();
	std::unique_ptr<QUndoCommand> command(new AddCircularFeatureUndoCommand(
			d_application_state.get_model_interface(),
			collection,
			feature));
	UndoRedo::instance().get_active_undo_stack().push(command.release());

	d_feature_count = next_feature_number;
	d_status_label->setText(tr(
			"Created %1 %2 with a %3 km radius (%4, %5 vertices) in %6, valid from %7 Ma to the "
			"present. Save that collection or the project to keep it, then click a new centre to "
			"place another.")
				.arg(geometry_label.toLower())
				.arg(d_feature_count)
				.arg(radius_km, 0, 'f', 1)
				.arg(circle_style == REGULAR_STYLE ? tr("Regular") : tr("Circular"))
				.arg(vertex_count)
				.arg(d_output_collection_combo->currentText())
				.arg(appearance_time, 0, 'f', 3));
}
