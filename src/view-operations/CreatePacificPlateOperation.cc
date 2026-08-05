/* $Id$ */

/**
 * \file
 * Implements local, seed-bounded Pacific-style plate birth.
 */

#include "CreatePacificPlateOperation.h"

#include <algorithm>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "CreateOceanCrustOperation.h"
#include "MORFeatureBuilder.h"
#include "OceanCrustBandBuilder.h"
#include "PacificPlateGeometry.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "RotationPlateCreation.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/PlanetaryParameters.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/TRSUtils.h"

#include "feature-visitors/PropertyValueFinder.h"

#include "file-io/File.h"

#include "gui/AnimationController.h"
#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/GeometryOnSphere.h"
#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"

#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"

#include "qt-widgets/ChooseFeatureCollectionWidget.h"


namespace
{
	const double TIME_EPSILON = 1e-9;
	typedef GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry_ptr_type;

	struct Ridge
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::integer_plate_id_type left_plate;
		GPlatesModel::integer_plate_id_type right_plate;
		geometry_ptr_type source_geometry;
	};

	struct TimeChange
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};

	std::pair<GPlatesModel::integer_plate_id_type, GPlatesModel::integer_plate_id_type>
	ordered_pair(
			GPlatesModel::integer_plate_id_type first,
			GPlatesModel::integer_plate_id_type second)
	{
		return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
	}

	boost::optional<geometry_ptr_type>
	get_single_active_geometry(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			double reconstruction_time,
			QString &reason)
	{
		boost::optional<geometry_ptr_type> active_geometry;
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
			property_iter != feature->end(); ++property_iter)
		{
			const boost::optional<geometry_ptr_type> candidate =
					GPlatesAppLogic::GeometryUtils::get_geometry_from_property(
							property_iter, reconstruction_time);
			if (!candidate)
			{
				continue;
			}
			if (active_geometry)
			{
				reason = QObject::tr("A selected MOR has more than one active geometry property.");
				return boost::none;
			}
			active_geometry = *candidate;
		}
		if (!active_geometry)
		{
			reason = QObject::tr("A selected MOR has no active geometry at %1 Ma.")
					.arg(reconstruction_time, 0, 'f', 2);
		}
		return active_geometry;
	}

	bool
	collection_contains_rotations(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		if (!collection.is_valid())
		{
			return false;
		}
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
			feature_iter != collection->end(); ++feature_iter)
		{
			GPlatesAppLogic::TRSUtils::TRSFinder finder;
			finder.visit_feature((*feature_iter)->reference());
			if (finder.can_process_trs())
			{
				return true;
			}
		}
		return false;
	}

	GPlatesModel::integer_plate_id_type
	common_plate(
			const Ridge &first,
			const Ridge &second)
	{
		const GPlatesModel::integer_plate_id_type first_ids[2] =
				{ first.left_plate, first.right_plate };
		const GPlatesModel::integer_plate_id_type second_ids[2] =
				{ second.left_plate, second.right_plate };
		std::set<GPlatesModel::integer_plate_id_type> common;
		for (unsigned int first_index = 0; first_index < 2; ++first_index)
		{
			for (unsigned int second_index = 0; second_index < 2; ++second_index)
			{
				if (first_ids[first_index] == second_ids[second_index])
				{
					common.insert(first_ids[first_index]);
				}
			}
		}
		if (common.size() != 1)
		{
			throw std::runtime_error("Adjacent selected MOR pairs do not identify exactly one old neighbouring plate.");
		}
		return *common.begin();
	}

	TimeChange
	end_feature_at_time(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			double end_time)
	{
		const GPlatesModel::PropertyName valid_time_name =
				GPlatesModel::PropertyName::create_gml("validTime");
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
			property_iter != feature->end(); ++property_iter)
		{
			if ((*property_iter)->get_property_name() != valid_time_name)
			{
				continue;
			}
			const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type> period =
					GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GmlTimePeriod>(
							feature, valid_time_name);
			if (!period)
			{
				break;
			}
			const GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_type replacement =
					GPlatesModel::ModelUtils::create_gml_time_period(
							(*period)->begin()->get_time_position(),
							GPlatesPropertyValues::GeoTimeInstant(end_time));
			TimeChange change = {
				feature,
				property_iter,
				(*property_iter)->clone(),
				GPlatesModel::TopLevelPropertyInline::create(valid_time_name, replacement)
			};
			return change;
		}
		throw std::runtime_error("A selected MOR has no editable gml:validTime period.");
	}

	class PacificPlateUndoCommand : public QUndoCommand
	{
	public:
		PacificPlateUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<TimeChange> &old_mor_changes,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &rotation_collection,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &rotation_feature,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &geometry_collection,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &geometry_features) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_old_mor_changes(old_mor_changes),
			d_rotation_collection(rotation_collection),
			d_rotation_feature(rotation_feature),
			d_geometry_collection(geometry_collection),
			d_geometry_features(geometry_features)
		{
			setText(QObject::tr("create Pacific-style oceanic plate"));
		}

		virtual void redo()
		{
			if (!d_rotation_collection.is_valid() || !d_geometry_collection.is_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<TimeChange>::iterator change_iter = d_old_mor_changes.begin();
				change_iter != d_old_mor_changes.end(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->after->clone());
				}
			}
			if (!d_rotation_feature->parent_ptr())
			{
				const GPlatesModel::FeatureCollectionHandle::iterator inserted =
						d_rotation_collection->add(d_rotation_feature);
				d_rotation_feature = *inserted;
			}
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
				d_geometry_features.begin(); feature_iter != d_geometry_features.end(); ++feature_iter)
			{
				if (!(*feature_iter)->parent_ptr())
				{
					const GPlatesModel::FeatureCollectionHandle::iterator inserted =
							d_geometry_collection->add(*feature_iter);
					*feature_iter = *inserted;
				}
			}
			notification_guard.release_guard();
		}

		virtual void undo()
		{
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::reverse_iterator feature_iter =
				d_geometry_features.rbegin(); feature_iter != d_geometry_features.rend(); ++feature_iter)
			{
				if ((*feature_iter)->parent_ptr())
				{
					(*feature_iter)->remove_from_parent();
				}
			}
			if (d_rotation_feature->parent_ptr())
			{
				d_rotation_feature->remove_from_parent();
			}
			for (std::vector<TimeChange>::reverse_iterator change_iter = d_old_mor_changes.rbegin();
				change_iter != d_old_mor_changes.rend(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->before->clone());
				}
			}
			notification_guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<TimeChange> d_old_mor_changes;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_rotation_collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_rotation_feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_geometry_collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_geometry_features;
	};
}


GPlatesViewOperations::CreatePacificPlateOperation::CreatePacificPlateOperation(
		CreateOceanCrustOperation &mor_selection,
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_mor_selection(mor_selection),
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface()),
	d_seed_capture_armed(false)
{ }


void
GPlatesViewOperations::CreatePacificPlateOperation::arm_seed_capture()
{
	d_void_seed = boost::none;
	d_seed_capture_armed = true;
}


bool
GPlatesViewOperations::CreatePacificPlateOperation::capture_seed(
		const GPlatesMaths::PointOnSphere &point,
		bool is_on_earth,
		QString &message)
{
	if (!d_seed_capture_armed)
	{
		return false;
	}
	if (!is_on_earth)
	{
		message = QObject::tr("The Pacific-plate seed must be clicked on the globe or map surface.");
		return true;
	}
	d_void_seed = point;
	d_seed_capture_armed = false;
	const GPlatesMaths::LatLonPoint lat_lon = GPlatesMaths::make_lat_lon_point(point);
	message = QObject::tr(
			"Captured the local Pacific-plate void seed at %1 degrees latitude, %2 degrees longitude. Run Create Pacific Plate to preview it.")
				.arg(lat_lon.latitude(), 0, 'f', 3).arg(lat_lon.longitude(), 0, 'f', 3);
	return true;
}


void
GPlatesViewOperations::CreatePacificPlateOperation::clear_seed()
{
	d_seed_capture_armed = false;
	d_void_seed = boost::none;
}


GPlatesViewOperations::CreatePacificPlateOperation::Result
GPlatesViewOperations::CreatePacificPlateOperation::trigger(
		QWidget *parent_widget)
{
	std::vector<GPlatesModel::FeatureHandle::weak_ref> selected_mors;
	const std::vector<GPlatesModel::FeatureHandle::weak_ref> &shared_selection =
			d_mor_selection.selected_mors();
	for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator selection_iter =
		shared_selection.begin(); selection_iter != shared_selection.end(); ++selection_iter)
	{
		if (selection_iter->is_valid())
		{
			selected_mors.push_back(*selection_iter);
		}
	}
	if (selected_mors.size() != 3)
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("Shift-click exactly three connected half-stage MORs. The shared selection currently contains %1.")
						.arg(static_cast<unsigned int>(selected_mors.size())));
	}
	if (!d_void_seed)
	{
		arm_seed_capture();
		return Result(SELECTION_REQUIRED,
				QObject::tr("The three MORs are selected. Click once inside the local central void, then run Create Pacific Plate again."));
	}

	const double current_time = d_application_state.get_current_reconstruction_time();
	std::vector<Ridge> ridges;
	std::set<GPlatesModel::integer_plate_id_type> old_plate_ids;
	std::set< std::pair<GPlatesModel::integer_plate_id_type,
			GPlatesModel::integer_plate_id_type> > plate_pairs;
	QStringList pair_descriptions;
	for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator feature_iter =
		selected_mors.begin(); feature_iter != selected_mors.end(); ++feature_iter)
	{
		if (!CreateOceanCrustOperation::is_supported_mor(*feature_iter) || !(*feature_iter)->parent_ptr())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Every selected feature must be a loaded HalfStageRotationVersion3 MidOceanRidge."));
		}
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						*feature_iter, GPlatesModel::PropertyName::create_gpml("leftPlate"));
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						*feature_iter, GPlatesModel::PropertyName::create_gpml("rightPlate"));
		QString reason;
		const boost::optional<geometry_ptr_type> source_geometry =
				get_single_active_geometry(*feature_iter, current_time, reason);
		if (!left || !right || (*left)->get_value() == (*right)->get_value() || !source_geometry)
		{
			return Result(OPERATION_ERROR, source_geometry
					? QObject::tr("Every selected MOR needs two different left/right plate IDs.") : reason);
		}
		const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>((*source_geometry).get());
		if (!polyline)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Every selected MOR must have one active polyline geometry."));
		}
		const GPlatesModel::integer_plate_id_type left_id = (*left)->get_value();
		const GPlatesModel::integer_plate_id_type right_id = (*right)->get_value();
		Ridge ridge = { *feature_iter, left_id, right_id, *source_geometry };
		ridges.push_back(ridge);
		old_plate_ids.insert(left_id);
		old_plate_ids.insert(right_id);
		plate_pairs.insert(ordered_pair(left_id, right_id));
		pair_descriptions.append(QObject::tr("%1-%2").arg(left_id).arg(right_id));
	}
	if (old_plate_ids.size() != 3 || plate_pairs.size() != 3)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected MORs must describe the three unique pairs of exactly three old plates. Selected pairs: %1.")
						.arg(pair_descriptions.join(", ")));
	}

	typedef GPlatesAppLogic::FeatureCollectionFileState::file_reference file_reference;
	const std::vector<file_reference> loaded_files =
			d_application_state.get_feature_collection_file_state().get_loaded_files();
	std::vector<file_reference> rotation_files;
	for (std::vector<file_reference>::const_iterator file_iter = loaded_files.begin();
		file_iter != loaded_files.end(); ++file_iter)
	{
		const QString suffix = file_iter->get_file().get_file_info().get_qfileinfo().suffix().toLower();
		if (suffix == "rot" || suffix == "grot" ||
				collection_contains_rotations(file_iter->get_file().get_feature_collection()))
		{
			rotation_files.push_back(*file_iter);
		}
	}
	if (rotation_files.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Load a .rot/.grot file or another rotation-sequence collection before creating a new plate."));
	}

	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Create Pacific-Style Plate"));
	dialog.setModal(true);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(QObject::tr(
			"Use the three Shift-clicked MORs and the captured interior seed to isolate one local uncovered void. Preview a new OceanicCrust plate, three naturalized half-stage MORs, a normal identity rotation branch, and the end of the three superseded MORs as one undoable edit."), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	QFormLayout *form = new QFormLayout();
	form->addRow(QObject::tr("Selected old-plate pairs:"), new QLabel(pair_descriptions.join(", "), &dialog));
	const GPlatesMaths::LatLonPoint seed_lat_lon = GPlatesMaths::make_lat_lon_point(*d_void_seed);
	form->addRow(QObject::tr("Captured void seed:"), new QLabel(
			QObject::tr("%1 degrees, %2 degrees")
					.arg(seed_lat_lon.latitude(), 0, 'f', 3)
					.arg(seed_lat_lon.longitude(), 0, 'f', 3), &dialog));

	QDoubleSpinBox *older_time_spin = new QDoubleSpinBox(&dialog);
	older_time_spin->setDecimals(2);
	older_time_spin->setRange(current_time + 0.1, 10000.0);
	older_time_spin->setSingleStep(1.0);
	const boost::optional<double> project_older_bound =
			d_application_state.get_project_timestamp_schedule().default_older_bound(current_time);
	const double fallback_increment = d_view_state.get_animation_controller().time_increment();
	older_time_spin->setValue(project_older_bound ? *project_older_bound : current_time + fallback_increment);
	older_time_spin->setSuffix(QObject::tr(" Ma"));
	older_time_spin->setToolTip(QObject::tr(
			"Defaults to the next older Project Timestamp; otherwise uses the animation increment."));
	form->addRow(QObject::tr("Opening interval starts:"), older_time_spin);

	QSpinBox *new_plate_spin = new QSpinBox(&dialog);
	new_plate_spin->setRange(1, 99999999);
	new_plate_spin->setValue(RotationPlateCreation::next_unused_plate_id(d_application_state));
	form->addRow(QObject::tr("New central plate ID:"), new_plate_spin);

	QComboBox *parent_combo = new QComboBox(&dialog);
	for (std::set<GPlatesModel::integer_plate_id_type>::const_iterator plate_iter =
		old_plate_ids.begin(); plate_iter != old_plate_ids.end(); ++plate_iter)
	{
		parent_combo->addItem(QObject::tr("Plate %1").arg(*plate_iter), static_cast<int>(*plate_iter));
	}
	form->addRow(QObject::tr("Birth-time parent plate:"), parent_combo);

	QComboBox *rotation_combo = new QComboBox(&dialog);
	for (unsigned int file_index = 0; file_index < rotation_files.size(); ++file_index)
	{
		QString name = rotation_files[file_index].get_file().get_file_info().get_display_name(false);
		if (name.isEmpty())
		{
			name = QObject::tr("Untitled rotation collection %1").arg(file_index + 1);
		}
		rotation_combo->addItem(name, static_cast<int>(file_index));
		if (d_last_rotation_collection.is_valid() &&
				rotation_files[file_index].get_file().get_feature_collection() == d_last_rotation_collection)
		{
			rotation_combo->setCurrentIndex(static_cast<int>(file_index));
		}
	}
	form->addRow(QObject::tr("Rotation collection:"), rotation_combo);

	QDoubleSpinBox *local_radius_spin = new QDoubleSpinBox(&dialog);
	local_radius_spin->setDecimals(2);
	local_radius_spin->setRange(0.1, 90.0);
	local_radius_spin->setValue(30.0);
	local_radius_spin->setSuffix(QObject::tr(" degrees"));
	form->addRow(QObject::tr("Maximum local-void radius:"), local_radius_spin);

	QDoubleSpinBox *maximum_segment_spin = new QDoubleSpinBox(&dialog);
	maximum_segment_spin->setRange(1.0, 5000.0);
	maximum_segment_spin->setValue(100.0);
	maximum_segment_spin->setSuffix(QObject::tr(" km"));
	form->addRow(QObject::tr("Maximum MOR segment:"), maximum_segment_spin);
	QDoubleSpinBox *amplitude_spin = new QDoubleSpinBox(&dialog);
	amplitude_spin->setRange(0.0, 50.0);
	amplitude_spin->setValue(2.0);
	amplitude_spin->setSuffix(QObject::tr(" %"));
	form->addRow(QObject::tr("MOR squiggle amplitude:"), amplitude_spin);
	QDoubleSpinBox *wavelength_spin = new QDoubleSpinBox(&dialog);
	wavelength_spin->setRange(1.0, 10000.0);
	wavelength_spin->setValue(400.0);
	wavelength_spin->setSuffix(QObject::tr(" km"));
	form->addRow(QObject::tr("MOR squiggle wavelength:"), wavelength_spin);
	QSpinBox *smoothing_spin = new QSpinBox(&dialog);
	smoothing_spin->setRange(0, 20);
	smoothing_spin->setValue(2);
	form->addRow(QObject::tr("MOR smoothing passes:"), smoothing_spin);
	QSpinBox *random_seed_spin = new QSpinBox(&dialog);
	random_seed_spin->setRange(0, 2147483647);
	random_seed_spin->setValue(1);
	form->addRow(QObject::tr("Deterministic squiggle seed:"), random_seed_spin);
	layout->addLayout(form);

	GPlatesQtWidgets::ChooseFeatureCollectionWidget *collection_widget =
			new GPlatesQtWidgets::ChooseFeatureCollectionWidget(
					d_application_state.get_reconstruct_method_registry(),
					d_application_state.get_feature_collection_file_state(),
					d_application_state.get_feature_collection_file_io(),
					&dialog);
	collection_widget->setTitle(QObject::tr("Destination crust and MOR collection"));
	collection_widget->set_help_text(QObject::tr(
			"The new OceanicCrust polygon and its three MidOceanRidge features are written here. The rotation sequence is written to the rotation collection selected above."));
	collection_widget->initialise();
	if (d_last_output_collection.is_valid())
	{
		collection_widget->select_feature_collection(d_last_output_collection);
	}
	layout->addWidget(collection_widget);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Preview Plate Birth"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED,
				QObject::tr("Pacific-style plate creation cancelled; no data changed and the seed remains captured."));
	}

	boost::optional< std::pair<file_reference, bool> > collection_selection;
	try
	{
		collection_selection = collection_widget->get_file_reference();
	}
	catch (const GPlatesQtWidgets::ChooseFeatureCollectionWidget::NoFeatureCollectionSelectedException &)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("No destination crust/MOR collection was selected; no data changed."));
	}
	const GPlatesModel::FeatureCollectionHandle::weak_ref geometry_collection =
			collection_selection->first.get_file().get_feature_collection();
	const unsigned int rotation_file_index = static_cast<unsigned int>(rotation_combo->currentData().toInt());
	const GPlatesModel::FeatureCollectionHandle::weak_ref rotation_collection =
			rotation_files[rotation_file_index].get_file().get_feature_collection();
	if (!geometry_collection.is_valid() || !rotation_collection.is_valid())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("A selected destination collection is no longer loaded; no data changed."));
	}

	const double older_time = older_time_spin->value();
	if (older_time <= current_time + TIME_EPSILON)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The opening interval must start at a time older than the displayed birth time."));
	}
	const GPlatesModel::integer_plate_id_type new_plate = new_plate_spin->value();
	const GPlatesModel::integer_plate_id_type parent_plate = parent_combo->currentData().toInt();
	const RotationPlateCreation::Result rotation =
			RotationPlateCreation::prepare_identity_sequence(
					d_application_state, rotation_collection, new_plate, parent_plate,
					current_time, 0.0);
	if (!rotation.success || !rotation.feature)
	{
		return Result(OPERATION_ERROR, rotation.error);
	}

	try
	{
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
				tree_creator.get_reconstruction_tree(older_time);
		PacificPlateGeometry::polyline_seq_type reconstructed_ridges;
		PacificPlateGeometry::polyline_seq_type older_reconstructed_ridges;
		for (std::vector<Ridge>::const_iterator ridge_iter = ridges.begin();
			ridge_iter != ridges.end(); ++ridge_iter)
		{
			GPlatesAppLogic::ReconstructionFeatureProperties properties;
			properties.visit_feature(ridge_iter->feature);
			if (!properties.is_feature_defined_at_recon_time(current_time) ||
					!properties.is_feature_defined_at_recon_time(older_time))
			{
				throw std::runtime_error("Every selected MOR must be valid at both opening-interval bounds.");
			}
			const geometry_ptr_type reconstructed = GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
					ridge_iter->source_geometry,
					d_application_state.get_reconstruct_method_registry(),
					ridge_iter->feature, current_time, tree_creator,
					GPlatesAppLogic::ReconstructParams(), false);
			const GPlatesMaths::PolylineOnSphere *polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(reconstructed.get());
			if (!polyline)
			{
				throw std::runtime_error("A selected MOR could not be reconstructed as a current-time polyline.");
			}
			reconstructed_ridges.push_back(polyline->get_non_null_pointer());

			QString older_geometry_reason;
			const boost::optional<geometry_ptr_type> older_source_geometry =
					get_single_active_geometry(
							ridge_iter->feature, older_time, older_geometry_reason);
			if (!older_source_geometry)
			{
				throw std::runtime_error(older_geometry_reason.toStdString());
			}
			const geometry_ptr_type older_reconstructed =
					GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
							*older_source_geometry,
							d_application_state.get_reconstruct_method_registry(),
							ridge_iter->feature, older_time, tree_creator,
							GPlatesAppLogic::ReconstructParams(), false);
			const GPlatesMaths::PolylineOnSphere *older_polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(older_reconstructed.get());
			if (!older_polyline)
			{
				throw std::runtime_error("A selected MOR could not be reconstructed as an older-time polyline.");
			}
			older_reconstructed_ridges.push_back(older_polyline->get_non_null_pointer());
		}

		NaturalizeCoastlineGeometry::Parameters naturalize_parameters;
		naturalize_parameters.maximum_segment_length_km = maximum_segment_spin->value();
		naturalize_parameters.amplitude_percent = amplitude_spin->value();
		naturalize_parameters.wavelength_km = wavelength_spin->value();
		naturalize_parameters.smoothing_passes = smoothing_spin->value();
		naturalize_parameters.random_seed = static_cast<boost::uint32_t>(random_seed_spin->value());
		naturalize_parameters.planet_radius_km =
				d_application_state.get_planetary_parameters().effective_radius_kilometres();
		const OceanCrustBandBuilder::polygon_seq_type existing_crust =
				OceanCrustBandBuilder::get_existing_ocean_crust(d_application_state);
		typedef std::pair<GPlatesModel::integer_plate_id_type,
				OceanCrustBandBuilder::polygon_ptr_type> plate_piece_type;
		std::vector<plate_piece_type> ordinary_pieces;
		OceanCrustBandBuilder::polygon_seq_type occupied_crust(existing_crust);
		for (unsigned int ridge_index = 0; ridge_index < 3; ++ridge_index)
		{
			const GPlatesModel::integer_plate_id_type side_plates[2] =
					{ ridges[ridge_index].left_plate, ridges[ridge_index].right_plate };
			for (unsigned int side_index = 0; side_index < 2; ++side_index)
			{
				const OceanCrustBandBuilder::Result band =
						OceanCrustBandBuilder::build_side_band(
								*reconstructed_ridges[ridge_index],
								*older_reconstructed_ridges[ridge_index],
								side_plates[side_index], *older_tree, *current_tree,
								occupied_crust);
				if (!band.success)
				{
					throw std::runtime_error(QObject::tr(
							"Ordinary crust fill for Plate %1 failed before plate birth: %2")
								.arg(side_plates[side_index]).arg(band.error).toStdString());
				}
				for (OceanCrustBandBuilder::polygon_seq_type::const_iterator piece_iter =
					band.polygons.begin(); piece_iter != band.polygons.end(); ++piece_iter)
				{
					ordinary_pieces.push_back(std::make_pair(side_plates[side_index], *piece_iter));
					occupied_crust.push_back(*piece_iter);
				}
			}
		}
		const PacificPlateGeometry::Result geometry = PacificPlateGeometry::build_local_void(
				reconstructed_ridges, *d_void_seed, local_radius_spin->value(),
				naturalize_parameters, occupied_crust);
		if (!geometry.success || !geometry.central_crust)
		{
			throw std::runtime_error(geometry.error.toStdString());
		}

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection()
						.create_child_rendered_layer_and_transfer_ownership(
								RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polygon_on_sphere(
						*geometry.central_crust, GPlatesGui::Colour::get_aqua(), 3.0f, true,
						GPlatesGui::Colour(0.0f, 0.28f, 0.38f)));
		for (std::vector<plate_piece_type>::const_iterator piece_iter = ordinary_pieces.begin();
			piece_iter != ordinary_pieces.end(); ++piece_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polygon_on_sphere(
							piece_iter->second, GPlatesGui::Colour(0.72f, 0.72f, 0.72f),
							2.0f, true, GPlatesGui::Colour(0.18f, 0.18f, 0.18f)));
		}
		for (PacificPlateGeometry::polyline_seq_type::const_iterator ridge_iter =
			geometry.bounding_ridges.begin(); ridge_iter != geometry.bounding_ridges.end(); ++ridge_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							*ridge_iter, GPlatesGui::Colour::get_yellow(), 4.0f));
		}
		const QMessageBox::StandardButton confirmation = QMessageBox::question(
				parent_widget,
				QObject::tr("Confirm Pacific-Style Plate Birth"),
				QObject::tr(
						"Aqua is the one local uncovered component containing the clicked seed; yellow is the three-MOR naturalized boundary; grey is %5 ordinary side-crust piece(s) generated first through the shared band builder. Create Plate %1 at %2 Ma, identity-parented to Plate %3 through 0 Ma, after examining opening over %4-%2 Ma?\n\n%6 loaded OceanicCrust polygon(s) were subtracted before the ordinary fill, and both loaded and newly filled crust were subtracted from the central void%7. %8 squiggle vertices were inserted; the actual maximum MOR segment is %9 km. The three selected old MORs end at the birth time. Rotation, all crust, new MORs, and old-MOR valid-time edits commit atomically.")
							.arg(new_plate).arg(current_time, 0, 'f', 2).arg(parent_plate)
							.arg(older_time, 0, 'f', 2)
							.arg(static_cast<unsigned int>(ordinary_pieces.size()))
							.arg(static_cast<unsigned int>(existing_crust.size()))
							.arg(geometry.existing_overlap_removed
									? QObject::tr(" and overlap was removed")
									: QObject::tr("; none overlapped the local void"))
							.arg(geometry.inserted_vertex_count)
							.arg(geometry.actual_maximum_segment_length_km, 0, 'f', 2),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);
		preview_layer->clear_rendered_geometries();
		if (confirmation != QMessageBox::Yes)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Pacific-style plate preview rejected; no data changed and the seed remains captured."));
		}

		std::vector<TimeChange> old_mor_changes;
		for (std::vector<Ridge>::const_iterator ridge_iter = ridges.begin();
			ridge_iter != ridges.end(); ++ridge_iter)
		{
			old_mor_changes.push_back(end_feature_at_time(ridge_iter->feature, current_time));
		}

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> geometry_features;
		for (unsigned int piece_index = 0; piece_index < ordinary_pieces.size(); ++piece_index)
		{
			const OceanCrustBandBuilder::polygon_ptr_type stored_piece =
					OceanCrustBandBuilder::reverse_reconstruct_polygon(
							*ordinary_pieces[piece_index].second,
							ordinary_pieces[piece_index].first, *current_tree);
			geometry_features.push_back(OceanCrustBandBuilder::create_oceanic_crust_feature(
					QObject::tr("Pacific opening side crust %1-%2 Ma - Plate %3 (part %4)")
							.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2)
							.arg(ordinary_pieces[piece_index].first).arg(piece_index + 1),
					current_time, current_time, ordinary_pieces[piece_index].first,
					stored_piece));
		}
		const OceanCrustBandBuilder::polygon_ptr_type stored_crust =
				OceanCrustBandBuilder::reverse_reconstruct_polygon(
						**geometry.central_crust, parent_plate, *current_tree);
		geometry_features.push_back(OceanCrustBandBuilder::create_oceanic_crust_feature(
				QObject::tr("Pacific-style central oceanic crust - Plate %1 (%2-%3 Ma opening)")
						.arg(new_plate).arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2),
				current_time, current_time, new_plate, stored_crust));
		for (unsigned int edge_index = 0; edge_index < 3; ++edge_index)
		{
			const GPlatesModel::integer_plate_id_type neighbour =
					common_plate(ridges[edge_index], ridges[(edge_index + 1) % 3]);
			geometry_features.push_back(MORFeatureBuilder::create_half_stage_mor(
					QObject::tr("Pacific-style MOR - Plates %1/%2").arg(new_plate).arg(neighbour),
					current_time, new_plate, neighbour, geometry.bounding_ridges[edge_index]));
		}

		std::unique_ptr<QUndoCommand> command(new PacificPlateUndoCommand(
				d_feature_focus, d_model_interface, old_mor_changes,
				rotation_collection, *rotation.feature,
				geometry_collection, geometry_features));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		d_last_output_collection = geometry_collection;
		d_last_rotation_collection = rotation_collection;
		clear_seed();
		return Result(OPERATION_COMPLETED,
				QObject::tr("Created Pacific-style Plate %1 at %2 Ma with %3 ordinary side-crust piece(s), one central OceanicCrust polygon, three naturalized half-stage MORs, and an identity rotation sequence relative to Plate %4. The three superseded MORs end at the birth time; the complete edit is one undo step.")
						.arg(new_plate).arg(current_time, 0, 'f', 2)
						.arg(static_cast<unsigned int>(ordinary_pieces.size())).arg(parent_plate));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not create the Pacific-style plate: %1").arg(exception.what()));
	}
}
