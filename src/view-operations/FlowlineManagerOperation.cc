/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include "FlowlineManagerOperation.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

#include <boost/optional.hpp>
#include <boost/shared_ptr.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QStringList>
#include <QUndoCommand>
#include <QUndoStack>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/GeometrySetter.h"
#include "feature-visitors/PropertyValueFinder.h"

#include "gui/FeatureFocus.h"

#include "maths/MultiPointOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlArray.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/StructuralType.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/UndoRedo.h"


namespace
{
	const char *const RECIPE_PREFIX = "GreaterPlates flowline recipe; source=";

	enum ManagerAction
	{
		GENERATE_OR_REGENERATE,
		DELETE_GENERATED,
		SHOW_GENERATED_LAYER,
		HIDE_GENERATED_LAYER
	};

	enum SourceScope
	{
		FOCUSED_SOURCE,
		ALL_ELIGIBLE_IN_COLLECTION
	};

	struct PreparedSource
	{
		PreparedSource(
				const GPlatesModel::FeatureHandle::weak_ref &feature_,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &geometry_,
				GPlatesModel::integer_plate_id_type left_plate_,
				GPlatesModel::integer_plate_id_type right_plate_) :
			feature(feature_),
			collection(collection_),
			geometry(geometry_),
			left_plate(left_plate_),
			right_plate(right_plate_),
			feature_id(feature_->feature_id().get().qstring())
		{  }

		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry;
		GPlatesModel::integer_plate_id_type left_plate;
		GPlatesModel::integer_plate_id_type right_plate;
		QString feature_id;
	};

	struct FeatureRecord
	{
		FeatureRecord(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &feature_) :
			collection(collection_),
			feature(feature_)
		{  }

		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type feature;
	};

	typedef std::vector<FeatureRecord> feature_record_seq_type;


	QString
	description_string(
			const GPlatesModel::FeatureHandle::weak_ref &feature)
	{
		const GPlatesModel::PropertyName description_name =
				GPlatesModel::PropertyName::create_gml("description");
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
				property_iter != feature->end(); ++property_iter)
		{
			if ((*property_iter)->get_property_name() != description_name)
			{
				continue;
			}
			const boost::optional<GPlatesModel::PropertyValue::non_null_ptr_type> value =
					GPlatesModel::ModelUtils::get_property_value(**property_iter);
			if (!value)
			{
				continue;
			}
			const boost::optional<GPlatesPropertyValues::XsString::non_null_ptr_to_const_type> text =
					GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::XsString>(*value.get());
			if (text)
			{
				return GPlatesUtils::make_qstring_from_icu_string(text.get()->get_value().get());
			}
		}
		return QString();
	}


	boost::optional<QString>
	recipe_source_id(
			const GPlatesModel::FeatureHandle::weak_ref &feature)
	{
		const QString description = description_string(feature);
		const int prefix_index = description.indexOf(QString::fromLatin1(RECIPE_PREFIX));
		if (prefix_index < 0)
		{
			return boost::none;
		}
		const int value_start = prefix_index + static_cast<int>(std::strlen(RECIPE_PREFIX));
		const int value_end = description.indexOf(';', value_start);
		const QString value = description.mid(
				value_start,
				value_end < 0 ? -1 : value_end - value_start).trimmed();
		return value.isEmpty() ? boost::optional<QString>() : boost::optional<QString>(value);
	}


	boost::optional<double>
	recipe_number(
			const QString &description,
			const QString &key)
	{
		const QString marker = QString(" %1=").arg(key);
		const int marker_index = description.indexOf(marker);
		if (marker_index < 0)
		{
			return boost::none;
		}
		const int value_start = marker_index + marker.size();
		const int value_end = description.indexOf(';', value_start);
		bool ok = false;
		const double value = description.mid(
				value_start,
				value_end < 0 ? -1 : value_end - value_start).trimmed().toDouble(&ok);
		return ok ? boost::optional<double>(value) : boost::optional<double>();
	}


	GPlatesModel::FeatureHandle::weak_ref
	find_loaded_feature(
			GPlatesAppLogic::FeatureCollectionFileState &file_state,
			const QString &feature_id)
	{
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
				file_state.get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
				file_iter = files.begin(); file_iter != files.end(); ++file_iter)
		{
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					file_iter->get_file().get_feature_collection();
			for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
					feature_iter != collection->end(); ++feature_iter)
			{
				if ((*feature_iter)->feature_id().get().qstring() == feature_id)
				{
					return (*feature_iter)->reference();
				}
			}
		}
		return GPlatesModel::FeatureHandle::weak_ref();
	}


	boost::optional<PreparedSource>
	prepare_source(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			GPlatesAppLogic::ApplicationState &application_state,
			double reconstruction_time,
			bool swap_plates,
			QString &reason)
	{
		if (!feature.is_valid() || !feature->parent_ptr())
		{
			reason = QObject::tr("feature is no longer in a loaded collection");
			return boost::none;
		}
		if (feature->feature_type() == GPlatesModel::FeatureType::create_gpml("Flowline"))
		{
			reason = QObject::tr("feature is itself a flowline");
			return boost::none;
		}

		GPlatesAppLogic::ReconstructionFeatureProperties reconstruction_properties;
		reconstruction_properties.visit_feature(feature);
		if (!reconstruction_properties.is_feature_defined_at_recon_time(reconstruction_time))
		{
			reason = QObject::tr("feature is not valid at the current reconstruction time");
			return boost::none;
		}
		if (!reconstruction_properties.get_left_plate_id() ||
				!reconstruction_properties.get_right_plate_id())
		{
			reason = QObject::tr("missing leftPlate or rightPlate");
			return boost::none;
		}
		if (reconstruction_properties.get_reconstruction_method() &&
				!reconstruction_properties.get_reconstruction_method()->get().qstring().startsWith(
						"HalfStageRotation"))
		{
			reason = QObject::tr("reconstruction method is not half-stage rotation");
			return boost::none;
		}
		GPlatesModel::integer_plate_id_type left_plate =
				reconstruction_properties.get_left_plate_id().get();
		GPlatesModel::integer_plate_id_type right_plate =
				reconstruction_properties.get_right_plate_id().get();
		if (left_plate == right_plate)
		{
			reason = QObject::tr("leftPlate and rightPlate are identical");
			return boost::none;
		}
		if (swap_plates)
		{
			std::swap(left_plate, right_plate);
		}

		boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> source_geometry;
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
				property_iter != feature->end(); ++property_iter)
		{
			const boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type> geometry =
					GPlatesAppLogic::GeometryUtils::get_geometry_from_property(
							property_iter, reconstruction_time);
			if (!geometry)
			{
				continue;
			}
			if (source_geometry)
			{
				reason = QObject::tr("more than one active geometry property");
				return boost::none;
			}
			source_geometry = geometry;
		}
		if (!source_geometry)
		{
			reason = QObject::tr("no reconstructable active geometry");
			return boost::none;
		}

		try
		{
			const GPlatesAppLogic::Reconstruction &reconstruction =
					application_state.get_current_reconstruction();
			const GPlatesAppLogic::ReconstructMethodRegistry registry;
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type reconstructed_geometry =
					GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
							source_geometry.get(),
							registry,
							feature,
							reconstruction_time,
							reconstruction.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator(),
							GPlatesAppLogic::ReconstructParams(),
							false);
			return PreparedSource(
					feature,
					feature->parent_ptr()->reference(),
					reconstructed_geometry,
					left_plate,
					right_plate);
		}
		catch (const std::exception &exception)
		{
			reason = QObject::tr("reconstruction failed: %1").arg(QString::fromUtf8(exception.what()));
		}
		catch (...)
		{
			reason = QObject::tr("reconstruction failed");
		}
		return boost::none;
	}


	GPlatesModel::PropertyValue::non_null_ptr_type
	create_times_value(
			const std::vector<double> &times)
	{
		std::vector<GPlatesModel::PropertyValue::non_null_ptr_type> periods;
		for (std::vector<double>::const_iterator time_iter = times.begin();
				time_iter + 1 != times.end(); ++time_iter)
		{
			periods.push_back(GPlatesPropertyValues::GmlTimePeriod::create(
					GPlatesModel::ModelUtils::create_gml_time_instant(
							GPlatesPropertyValues::GeoTimeInstant(*(time_iter + 1))),
					GPlatesModel::ModelUtils::create_gml_time_instant(
							GPlatesPropertyValues::GeoTimeInstant(*time_iter))));
		}
		return GPlatesPropertyValues::GpmlArray::create(
				periods,
				GPlatesPropertyValues::StructuralType::create_gml("TimePeriod"));
	}


	GPlatesModel::FeatureHandle::iterator
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		const boost::optional<GPlatesModel::FeatureHandle::iterator> property =
				GPlatesModel::ModelUtils::add_property(feature, name, value);
		if (!property)
		{
			throw std::runtime_error("Could not create a required flowline property.");
		}
		return property.get();
	}


	GPlatesModel::FeatureHandle::non_null_ptr_type
	create_flowline(
			const PreparedSource &source,
			GPlatesAppLogic::ApplicationState &application_state,
			double reconstruction_time,
			double youngest,
			double oldest,
			double step)
	{
		const GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(
						GPlatesModel::FeatureType::create_gpml("Flowline"));
		const QString recipe = QString::fromLatin1(RECIPE_PREFIX) + source.feature_id +
				QObject::tr("; youngest=%1; oldest=%2; step=%3; left=%4; right=%5;")
						.arg(youngest, 0, 'f', 6)
						.arg(oldest, 0, 'f', 6)
						.arg(step, 0, 'f', 6)
						.arg(source.left_plate)
						.arg(source.right_plate);

		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(
								QObject::tr("Generated flowlines for %1").arg(source.feature_id))));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("description"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(recipe)));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesPropertyValues::GmlTimePeriod::create(
						GPlatesModel::ModelUtils::create_gml_time_instant(
								GPlatesPropertyValues::GeoTimeInstant(oldest)),
						GPlatesModel::ModelUtils::create_gml_time_instant(
								GPlatesPropertyValues::GeoTimeInstant(youngest))));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
				GPlatesPropertyValues::Enumeration::create(
						GPlatesPropertyValues::EnumerationType::create_gpml(
								"ReconstructionMethodEnumeration"),
						"HalfStageRotationVersion3"));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("leftPlate"),
				GPlatesPropertyValues::GpmlPlateId::create(source.left_plate));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("rightPlate"),
				GPlatesPropertyValues::GpmlPlateId::create(source.right_plate));
		add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("times"),
				create_times_value(GPlatesAppLogic::ProjectTimestampSchedule::build(
						youngest, oldest, step)));

		const GPlatesMaths::MultiPointOnSphere::non_null_ptr_to_const_type reconstruction_time_seeds =
				GPlatesAppLogic::GeometryUtils::convert_geometry_to_multi_point(*source.geometry, false);
		const boost::optional<GPlatesModel::PropertyValue::non_null_ptr_type> seed_value =
				GPlatesAppLogic::GeometryUtils::create_geometry_property_value(reconstruction_time_seeds);
		if (!seed_value)
		{
			throw std::runtime_error("Could not convert source vertices to flowline seed points.");
		}
		const GPlatesModel::FeatureHandle::iterator seed_property = add_property(
				feature->reference(),
				GPlatesModel::PropertyName::create_gpml("seedPoints"),
				seed_value.get());

		const GPlatesAppLogic::Reconstruction &reconstruction =
				application_state.get_current_reconstruction();
		const GPlatesAppLogic::ReconstructMethodRegistry registry;
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_seeds =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						reconstruction_time_seeds,
						registry,
						feature->reference(),
						reconstruction_time,
						reconstruction.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator(),
						GPlatesAppLogic::ReconstructParams(),
						true);
		GPlatesModel::TopLevelProperty::non_null_ptr_type present_day_seed_property =
				(*seed_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter geometry_setter(present_day_seeds);
		geometry_setter.set_geometry(present_day_seed_property.get());
		feature->set(seed_property, present_day_seed_property);
		return feature;
	}


	feature_record_seq_type
	find_generated(
			GPlatesAppLogic::FeatureCollectionFileState &file_state,
			const std::set<QString> &source_ids)
	{
		feature_record_seq_type records;
		const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
				file_state.get_loaded_files();
		for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
				file_iter = files.begin(); file_iter != files.end(); ++file_iter)
		{
			const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
					file_iter->get_file().get_feature_collection();
			for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
					feature_iter != collection->end(); ++feature_iter)
			{
				if ((*feature_iter)->feature_type() != GPlatesModel::FeatureType::create_gpml("Flowline"))
				{
					continue;
				}
				const boost::optional<QString> source_id = recipe_source_id((*feature_iter)->reference());
				if (source_id && source_ids.count(source_id.get()))
				{
					records.push_back(FeatureRecord(collection, *feature_iter));
				}
			}
		}
		return records;
	}


	class FlowlineManagerUndoCommand :
			public QUndoCommand
	{
	public:
		FlowlineManagerUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const feature_record_seq_type &removed,
				const feature_record_seq_type &added,
				const QString &description) :
			d_model_interface(model_interface),
			d_removed(removed),
			d_added(added)
		{
			setText(description);
		}

		virtual void redo()
		{
			apply(d_removed, false);
			apply(d_added, true);
		}

		virtual void undo()
		{
			apply(d_added, false);
			apply(d_removed, true);
		}

	private:
		void
		apply(
				feature_record_seq_type &records,
				bool add)
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (feature_record_seq_type::iterator record_iter = records.begin();
					record_iter != records.end(); ++record_iter)
			{
				if (!record_iter->collection.is_valid())
				{
					continue;
				}
				if (add && !record_iter->feature->parent_ptr())
				{
					record_iter->collection->add(record_iter->feature);
				}
				else if (!add && record_iter->feature->parent_ptr())
				{
					record_iter->feature->remove_from_parent();
				}
			}
			guard.release_guard();
		}

		GPlatesModel::ModelInterface d_model_interface;
		feature_record_seq_type d_removed;
		feature_record_seq_type d_added;
	};


	size_t
	set_collection_visibility(
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			bool visible)
	{
		size_t changed = 0;
		GPlatesPresentation::VisualLayers &layers = view_state.get_visual_layers();
		for (size_t layer_index = 0; layer_index < layers.size(); ++layer_index)
		{
			const boost::shared_ptr<GPlatesPresentation::VisualLayer> layer =
					layers.visual_layer_at(layer_index).lock();
			if (!layer)
			{
				continue;
			}
			const std::vector<GPlatesAppLogic::Layer::InputConnection> inputs =
					layer->get_reconstruct_graph_layer().get_all_inputs();
			bool uses_collection = false;
			for (std::vector<GPlatesAppLogic::Layer::InputConnection>::const_iterator
					input_iter = inputs.begin(); input_iter != inputs.end(); ++input_iter)
			{
				const boost::optional<GPlatesAppLogic::Layer::InputFile> input_file =
						input_iter->get_input_file();
				uses_collection = uses_collection ||
						(input_file && input_file->get_feature_collection().handle_ptr() == collection.handle_ptr());
			}
			if (uses_collection && layer->is_visible() != visible)
			{
				layer->set_visible(visible);
				++changed;
			}
		}
		return changed;
	}
}


GPlatesViewOperations::FlowlineManagerOperation::FlowlineManagerOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state,
		QWidget *parent_widget) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_parent_widget(parent_widget)
{  }


void
GPlatesViewOperations::FlowlineManagerOperation::trigger()
{
	GPlatesAppLogic::FeatureCollectionFileState &file_state =
			d_application_state.get_feature_collection_file_state();
	GPlatesModel::FeatureHandle::weak_ref source_feature =
			d_view_state.get_feature_focus().focused_feature();
	if (!source_feature.is_valid())
	{
		QMessageBox::information(d_parent_widget, tr("Flowline Manager"),
				tr("Focus a half-stage source feature or a generated flowline first."));
		return;
	}
	const QString focused_recipe_description = description_string(source_feature);
	const boost::optional<QString> focused_recipe_source = recipe_source_id(source_feature);
	if (focused_recipe_source)
	{
		source_feature = find_loaded_feature(file_state, focused_recipe_source.get());
		if (!source_feature.is_valid())
		{
			QMessageBox::warning(d_parent_widget, tr("Flowline Manager"),
					tr("The generated flowline's source Feature ID is no longer loaded: %1")
							.arg(focused_recipe_source.get()));
			return;
		}
	}
	if (!source_feature->parent_ptr())
	{
		QMessageBox::warning(d_parent_widget, tr("Flowline Manager"),
				tr("The source feature is not in a loaded feature collection."));
		return;
	}
	const GPlatesModel::FeatureCollectionHandle::weak_ref source_collection =
			source_feature->parent_ptr()->reference();

	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
			file_state.get_loaded_files();
	if (files.empty())
	{
		QMessageBox::information(d_parent_widget, tr("Flowline Manager"),
				tr("Load an output feature collection first."));
		return;
	}

	QDialog dialog(d_parent_widget);
	dialog.setWindowTitle(tr("Flowline Manager"));
	QFormLayout *layout = new QFormLayout(&dialog);
	QLabel *intro = new QLabel(tr(
			"Generate flowline seed points from source vertices without changing the source. "
			"Recipes store the source Feature ID so the output can be regenerated after rotation changes."), &dialog);
	intro->setWordWrap(true);
	layout->addRow(intro);

	QComboBox *action_combo = new QComboBox(&dialog);
	action_combo->addItem(tr("Generate / regenerate"), GENERATE_OR_REGENERATE);
	action_combo->addItem(tr("Delete generated flowlines"), DELETE_GENERATED);
	action_combo->addItem(tr("Show generated layer"), SHOW_GENERATED_LAYER);
	action_combo->addItem(tr("Hide generated layer"), HIDE_GENERATED_LAYER);
	QComboBox *scope_combo = new QComboBox(&dialog);
	scope_combo->addItem(tr("Focused source feature"), FOCUSED_SOURCE);
	scope_combo->addItem(tr("All eligible features in source collection"), ALL_ELIGIBLE_IN_COLLECTION);
	QComboBox *output_combo = new QComboBox(&dialog);
	for (size_t file_index = 0; file_index < files.size(); ++file_index)
	{
		output_combo->addItem(files[file_index].get_file().get_file_info().get_display_name(true));
		if (files[file_index].get_file().get_feature_collection().handle_ptr() == source_collection.handle_ptr())
		{
			output_combo->setCurrentIndex(static_cast<int>(file_index));
		}
	}

	const double current_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	QDoubleSpinBox *youngest_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *oldest_spin = new QDoubleSpinBox(&dialog);
	QDoubleSpinBox *step_spin = new QDoubleSpinBox(&dialog);
	for (QDoubleSpinBox *spin : std::vector<QDoubleSpinBox *>{ youngest_spin, oldest_spin, step_spin })
	{
		spin->setRange(0.0, 10000.0);
		spin->setDecimals(3);
	}
	youngest_spin->setValue(0.0);
	oldest_spin->setValue(std::max(100.0, current_time));
	step_spin->setMinimum(0.001);
	step_spin->setValue(5.0);
	if (focused_recipe_source)
	{
		const boost::optional<double> recipe_youngest = recipe_number(focused_recipe_description, "youngest");
		const boost::optional<double> recipe_oldest = recipe_number(focused_recipe_description, "oldest");
		const boost::optional<double> recipe_step = recipe_number(focused_recipe_description, "step");
		if (recipe_youngest) youngest_spin->setValue(recipe_youngest.get());
		if (recipe_oldest) oldest_spin->setValue(recipe_oldest.get());
		if (recipe_step) step_spin->setValue(recipe_step.get());
	}
	QCheckBox *replace_checkbox = new QCheckBox(tr("Replace existing recipes for each source"), &dialog);
	replace_checkbox->setChecked(true);
	QCheckBox *swap_checkbox = new QCheckBox(tr("Swap left/right Plate IDs"), &dialog);
	QCheckBox *dry_run_checkbox = new QCheckBox(tr("Dry run only"), &dialog);
	dry_run_checkbox->setChecked(true);

	layout->addRow(tr("Action:"), action_combo);
	layout->addRow(tr("Sources:"), scope_combo);
	layout->addRow(tr("Output collection / layer:"), output_combo);
	layout->addRow(tr("Youngest time (Ma):"), youngest_spin);
	layout->addRow(tr("Oldest time (Ma):"), oldest_spin);
	layout->addRow(tr("Step (My):"), step_spin);
	layout->addRow(replace_checkbox);
	layout->addRow(swap_checkbox);
	layout->addRow(dry_run_checkbox);
	QLabel *note = new QLabel(tr(
			"Generate and delete are one-step undo operations. Show/hide changes only the visual-layer state. "
			"Unsupported sources are reported by Feature ID and are never silently changed."), &dialog);
	note->setWordWrap(true);
	layout->addRow(note);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addRow(buttons);
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	const ManagerAction action = static_cast<ManagerAction>(action_combo->currentData().toInt());
	const GPlatesModel::FeatureCollectionHandle::weak_ref output_collection =
			files[output_combo->currentIndex()].get_file().get_feature_collection();
	if (action == SHOW_GENERATED_LAYER || action == HIDE_GENERATED_LAYER)
	{
		const bool visible = action == SHOW_GENERATED_LAYER;
		const size_t changed = set_collection_visibility(d_view_state, output_collection, visible);
		QMessageBox::information(d_parent_widget, tr("Flowline Manager"),
				tr("%1 %2 visual layer(s) connected to %3.")
						.arg(visible ? tr("Showed") : tr("Hid"))
						.arg(changed)
						.arg(output_combo->currentText()));
		return;
	}

	std::vector<GPlatesModel::FeatureHandle::weak_ref> source_features;
	if (scope_combo->currentData().toInt() == FOCUSED_SOURCE)
	{
		source_features.push_back(source_feature);
	}
	else
	{
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = source_collection->begin();
				feature_iter != source_collection->end(); ++feature_iter)
		{
			if ((*feature_iter)->feature_type() != GPlatesModel::FeatureType::create_gpml("Flowline"))
			{
				source_features.push_back((*feature_iter)->reference());
			}
		}
	}

	std::vector<PreparedSource> prepared_sources;
	QStringList skipped;
	std::set<QString> source_ids;
	std::set<QString> requested_source_ids;
	for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator source_iter =
			source_features.begin(); source_iter != source_features.end(); ++source_iter)
	{
		const QString feature_id = (*source_iter)->feature_id().get().qstring();
		requested_source_ids.insert(feature_id);
		QString reason;
		const boost::optional<PreparedSource> prepared = prepare_source(
				*source_iter,
				d_application_state,
				current_time,
				swap_checkbox->isChecked(),
				reason);
		if (prepared)
		{
			prepared_sources.push_back(prepared.get());
			source_ids.insert(feature_id);
		}
		else
		{
			skipped.append(tr("%1: %2").arg(feature_id, reason));
		}
	}

	feature_record_seq_type existing = find_generated(
			file_state,
			action == DELETE_GENERATED ? requested_source_ids : source_ids);
	if (action == DELETE_GENERATED)
	{
		const QString report = tr("Generated flowlines matched: %1\nSources considered: %2")
				.arg(existing.size()).arg(requested_source_ids.size());
		if (dry_run_checkbox->isChecked() || existing.empty())
		{
			QMessageBox::information(d_parent_widget, tr("Flowline Manager - Delete"), report);
			return;
		}
		std::unique_ptr<QUndoCommand> command(new FlowlineManagerUndoCommand(
				d_application_state.get_model_interface(),
				existing,
				feature_record_seq_type(),
				tr("delete generated flowlines")));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		QMessageBox::information(d_parent_widget, tr("Flowline Manager"),
				report + tr("\nDeleted in one undoable operation."));
		return;
	}

	if (youngest_spin->value() >= oldest_spin->value() - 1e-9)
	{
		QMessageBox::warning(d_parent_widget, tr("Flowline Manager"),
				tr("Oldest time must be greater than youngest time."));
		return;
	}
	QString report = tr("Eligible sources: %1\nSkipped sources: %2\nExisting generated features: %3\nPlanned generated features: %4")
			.arg(prepared_sources.size())
			.arg(skipped.size())
			.arg(existing.size())
			.arg(prepared_sources.size());
	if (!skipped.isEmpty())
	{
		report += tr("\n\nSkipped details:\n") + skipped.join("\n");
	}
	if (dry_run_checkbox->isChecked() || prepared_sources.empty())
	{
		QMessageBox::information(d_parent_widget, tr("Flowline Manager - Dry Run"), report);
		return;
	}

	feature_record_seq_type added;
	try
	{
		for (std::vector<PreparedSource>::const_iterator source_iter = prepared_sources.begin();
				source_iter != prepared_sources.end(); ++source_iter)
		{
			added.push_back(FeatureRecord(
					output_collection,
					create_flowline(
							*source_iter,
							d_application_state,
							current_time,
							youngest_spin->value(),
							oldest_spin->value(),
							step_spin->value())));
		}
	}
	catch (const std::exception &exception)
	{
		QMessageBox::critical(d_parent_widget, tr("Flowline Manager"),
				tr("No changes were made. Could not prepare the generated flowlines: %1")
						.arg(QString::fromUtf8(exception.what())));
		return;
	}
	if (!replace_checkbox->isChecked())
	{
		existing.clear();
	}
	std::unique_ptr<QUndoCommand> command(new FlowlineManagerUndoCommand(
			d_application_state.get_model_interface(),
			existing,
			added,
			tr("generate flowlines from saved recipes")));
	UndoRedo::instance().get_active_undo_stack().push(command.release());
	QMessageBox::information(d_parent_widget, tr("Flowline Manager"),
			report + tr("\nGenerated in one undoable operation."));
}
