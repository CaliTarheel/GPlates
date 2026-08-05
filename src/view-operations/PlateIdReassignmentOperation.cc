/* $Id$ */

/**
 * \file
 *
 * Copyright (C) 2026 CaliTarheel
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

#include "PlateIdReassignmentOperation.h"

#include <map>
#include <memory>
#include <set>
#include <vector>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QSpinBox>
#include <QStringList>
#include <QUndoCommand>
#include <QUndoStack>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/LayerProxyUtils.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructMethodInterface.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/GeometrySetter.h"
#include "feature-visitors/PropertyValueFinder.h"

#include "file-io/File.h"

#include "gui/FeatureFocus.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelInterface.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GpmlPlateId.h"
#include "property-values/GmlTimePeriod.h"

#include "view-operations/UndoRedo.h"


namespace
{
	const GPlatesModel::PropertyName &
	reconstruction_plate_id_property_name()
	{
		static const GPlatesModel::PropertyName property_name =
				GPlatesModel::PropertyName::create_gpml("reconstructionPlateId");
		return property_name;
	}


	class ReplaceFeaturePropertiesUndoCommand :
			public QUndoCommand
	{
	public:
		ReplaceFeaturePropertiesUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				GPlatesModel::FeatureHandle::weak_ref feature,
				GPlatesModel::FeatureHandle::iterator plate_property,
				GPlatesModel::FeatureHandle::iterator geometry_property,
				GPlatesModel::TopLevelProperty::non_null_ptr_type before_plate,
				GPlatesModel::TopLevelProperty::non_null_ptr_type after_plate,
				GPlatesModel::TopLevelProperty::non_null_ptr_type before_geometry,
				GPlatesModel::TopLevelProperty::non_null_ptr_type after_geometry) :
			d_model_interface(model_interface),
			d_feature(feature),
			d_plate_property(plate_property),
			d_geometry_property(geometry_property),
			d_before_plate(before_plate),
			d_after_plate(after_plate),
			d_before_geometry(before_geometry),
			d_after_geometry(after_geometry)
		{
			setText(QObject::tr("reassign focused feature without moving it"));
		}

		virtual void redo() { apply(true); }
		virtual void undo() { apply(false); }

	private:
		void apply(bool after)
		{
			if (!d_feature.is_valid() || !d_plate_property.is_still_valid() ||
					!d_geometry_property.is_still_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_feature->set(d_plate_property, (after ? d_after_plate : d_before_plate)->clone());
			d_feature->set(d_geometry_property, (after ? d_after_geometry : d_before_geometry)->clone());
			guard.release_guard();
		}

		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_feature;
		GPlatesModel::FeatureHandle::iterator d_plate_property;
		GPlatesModel::FeatureHandle::iterator d_geometry_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_before_plate;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_after_plate;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_before_geometry;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_after_geometry;
	};


	class AddFeatureUndoCommand :
			public QUndoCommand
	{
	public:
		AddFeatureUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				GPlatesModel::FeatureCollectionHandle::weak_ref collection,
				GPlatesModel::FeatureHandle::non_null_ptr_type feature,
				GPlatesGui::FeatureFocus &feature_focus) :
			d_model_interface(model_interface),
			d_collection(collection),
			d_feature(feature),
			d_feature_focus(feature_focus)
		{
			setText(QObject::tr("copy focused feature to another plate without moving it"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_collection_iterator = d_collection->add(d_feature);
			d_feature = *d_collection_iterator;
			guard.release_guard();
			d_feature_focus.set_focus(d_feature->reference());
		}

		virtual void undo()
		{
			if (!d_collection.is_valid() || !d_collection_iterator.is_still_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_feature = d_collection->remove(d_collection_iterator);
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_feature;
		GPlatesModel::FeatureCollectionHandle::iterator d_collection_iterator;
		GPlatesGui::FeatureFocus &d_feature_focus;
	};


	class RemoveFeatureUndoCommand :
			public QUndoCommand
	{
	public:
		RemoveFeatureUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				GPlatesModel::FeatureCollectionHandle::weak_ref collection,
				GPlatesModel::FeatureCollectionHandle::iterator collection_iterator) :
			d_model_interface(model_interface),
			d_collection(collection),
			d_collection_iterator(collection_iterator),
			d_feature(*collection_iterator)
		{
			setText(QObject::tr("delete feature in bulk Plate ID operation"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid() || !d_collection_iterator.is_still_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_feature = d_collection->remove(d_collection_iterator);
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			d_collection_iterator = d_collection->add(d_feature);
			d_feature = *d_collection_iterator;
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		GPlatesModel::FeatureCollectionHandle::iterator d_collection_iterator;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_feature;
	};


	class SetValidTimeUndoCommand :
			public QUndoCommand
	{
	public:
		SetValidTimeUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				GPlatesModel::FeatureHandle::weak_ref feature,
				GPlatesModel::FeatureHandle::iterator valid_time_property,
				boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> before,
				GPlatesModel::TopLevelProperty::non_null_ptr_type after) :
			d_model_interface(model_interface),
			d_feature(feature),
			d_valid_time_property(valid_time_property),
			d_before(before),
			d_after(after)
		{
			setText(QObject::tr("end feature valid time in bulk Plate ID operation"));
		}

		virtual void redo()
		{
			if (!d_feature.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_valid_time_property.is_still_valid())
			{
				d_feature->set(d_valid_time_property, d_after->clone());
			}
			else
			{
				d_valid_time_property = d_feature->add(d_after->clone());
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_feature.is_valid() || !d_valid_time_property.is_still_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_before)
			{
				d_feature->set(d_valid_time_property, (*d_before)->clone());
			}
			else
			{
				d_feature->remove(d_valid_time_property);
			}
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_feature;
		GPlatesModel::FeatureHandle::iterator d_valid_time_property;
		boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> d_before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_after;
	};


	const GPlatesModel::PropertyName &
	valid_time_property_name()
	{
		static const GPlatesModel::PropertyName property_name =
				GPlatesModel::PropertyName::create_gml("validTime");
		return property_name;
	}


	GPlatesModel::FeatureHandle::iterator
	find_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name)
	{
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
			property_iter != feature->end(); ++property_iter)
		{
			if ((*property_iter)->get_property_name() == property_name)
			{
				return property_iter;
			}
		}
		return GPlatesModel::FeatureHandle::iterator();
	}


	GPlatesModel::TopLevelProperty::non_null_ptr_type
	create_valid_time_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesPropertyValues::GeoTimeInstant &begin,
			const GPlatesPropertyValues::GeoTimeInstant &end,
			const boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> &template_property = boost::none)
	{
		const GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_type valid_time =
				GPlatesModel::ModelUtils::create_gml_time_period(begin, end);
		const boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> property =
				GPlatesModel::ModelUtils::create_top_level_property(
						valid_time_property_name(), valid_time, feature->feature_type());
		GPlatesModel::TopLevelProperty::non_null_ptr_type result = property
				? property.get()
				: GPlatesModel::TopLevelProperty::non_null_ptr_type(
						GPlatesModel::TopLevelPropertyInline::create(valid_time_property_name(), valid_time));
		if (template_property)
		{
			result->set_xml_attributes((*template_property)->get_xml_attributes());
		}
		return result;
	}


	struct BulkReport
	{
		std::map<QString, unsigned int> planned_by_collection_and_type;
		QStringList skipped_features;

		void add_planned(const QString &collection_name, const QString &feature_type)
		{
			++planned_by_collection_and_type[collection_name + QObject::tr(" — ") + feature_type];
		}

		void add_skipped(
				const GPlatesModel::FeatureHandle::weak_ref &feature,
				const QString &collection_name,
				const QString &reason)
		{
			skipped_features.append(QObject::tr("%1 — %2 — %3: %4")
					.arg(collection_name)
					.arg(feature->feature_type().get_name().qstring())
					.arg(feature->feature_id().get().qstring())
					.arg(reason));
		}

		unsigned int planned_count() const
		{
			unsigned int result = 0;
			for (std::map<QString, unsigned int>::const_iterator iter =
					planned_by_collection_and_type.begin();
					iter != planned_by_collection_and_type.end(); ++iter)
			{
				result += iter->second;
			}
			return result;
		}

		QString details() const
		{
			QStringList lines;
			lines.append(QObject::tr("Eligible features by collection and type:"));
			if (planned_by_collection_and_type.empty())
			{
				lines.append(QObject::tr("  (none)"));
			}
			for (std::map<QString, unsigned int>::const_iterator iter =
					planned_by_collection_and_type.begin();
					iter != planned_by_collection_and_type.end(); ++iter)
			{
				lines.append(QObject::tr("  %1: %2").arg(iter->first).arg(iter->second));
			}
			lines.append(QString());
			lines.append(QObject::tr("Skipped features:"));
			if (skipped_features.empty())
			{
				lines.append(QObject::tr("  (none)"));
			}
			else
			{
				for (QStringList::const_iterator iter = skipped_features.begin();
					iter != skipped_features.end(); ++iter)
				{
					lines.append(QObject::tr("  %1").arg(*iter));
				}
			}
			return lines.join('\n');
		}
	};


	void
	show_bulk_report(
			QWidget *parent,
			const QString &summary,
			const BulkReport &report)
	{
		QMessageBox message_box(QMessageBox::Information,
				QObject::tr("Bulk Plate ID Operations"), summary,
				QMessageBox::Ok, parent);
		message_box.setDetailedText(report.details());
		message_box.exec();
	}


	GPlatesModel::TopLevelProperty::non_null_ptr_type
	create_plate_id_property(
			const GPlatesModel::integer_plate_id_type plate_id)
	{
		const GPlatesPropertyValues::GpmlPlateId::non_null_ptr_type plate_id_value =
				GPlatesPropertyValues::GpmlPlateId::create(plate_id);
		const boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> property =
				GPlatesModel::ModelUtils::create_top_level_property(
						reconstruction_plate_id_property_name(), plate_id_value);
		if (property)
		{
			return property.get();
		}

		return GPlatesModel::TopLevelPropertyInline::create(
				reconstruction_plate_id_property_name(), plate_id_value);
	}


	struct PreparedReassignment
	{
		PreparedReassignment(
				GPlatesModel::FeatureHandle::iterator source_plate_property_,
				GPlatesModel::FeatureHandle::non_null_ptr_type feature_,
				GPlatesModel::FeatureHandle::iterator plate_property_,
				GPlatesModel::FeatureHandle::iterator geometry_property_) :
			source_plate_property(source_plate_property_),
			feature(feature_),
			plate_property(plate_property_),
			geometry_property(geometry_property_)
		{
		}

		GPlatesModel::FeatureHandle::iterator source_plate_property;
		GPlatesModel::FeatureHandle::non_null_ptr_type feature;
		GPlatesModel::FeatureHandle::iterator plate_property;
		GPlatesModel::FeatureHandle::iterator geometry_property;
	};


	boost::optional<PreparedReassignment>
	prepare_reassignment(
			GPlatesModel::FeatureHandle::weak_ref source_feature,
			GPlatesModel::FeatureHandle::iterator source_geometry_property,
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &reconstructed_geometry,
			GPlatesModel::integer_plate_id_type target_plate_id,
			double reconstruction_time,
			const GPlatesAppLogic::ReconstructMethodInterface::Context &reconstruct_method_context)
	{
		GPlatesModel::FeatureHandle::iterator source_plate_property;
		for (GPlatesModel::FeatureHandle::iterator property_iter = source_feature->begin();
				property_iter != source_feature->end(); ++property_iter)
		{
			if ((*property_iter)->get_property_name() == reconstruction_plate_id_property_name())
			{
				source_plate_property = property_iter;
				break;
			}
		}
		if (!source_plate_property.is_still_valid())
		{
			return boost::none;
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type prepared_feature =
				GPlatesModel::FeatureHandle::create(source_feature->feature_type());
		GPlatesModel::FeatureHandle::iterator prepared_plate_property;
		GPlatesModel::FeatureHandle::iterator prepared_geometry_property;
		for (GPlatesModel::FeatureHandle::iterator property_iter = source_feature->begin();
				property_iter != source_feature->end(); ++property_iter)
		{
			const GPlatesModel::FeatureHandle::iterator prepared_property =
					prepared_feature->add((*property_iter)->clone());
			if (property_iter == source_plate_property)
			{
				prepared_plate_property = prepared_property;
			}
			if (property_iter == source_geometry_property)
			{
				prepared_geometry_property = prepared_property;
			}
		}
		if (!prepared_plate_property.is_still_valid() || !prepared_geometry_property.is_still_valid())
		{
			return boost::none;
		}

		GPlatesModel::TopLevelProperty::non_null_ptr_type prepared_plate_property_value =
				create_plate_id_property(target_plate_id);
		prepared_plate_property_value->set_xml_attributes(
				(*source_plate_property)->get_xml_attributes());
		prepared_feature->set(prepared_plate_property, prepared_plate_property_value);
		const GPlatesAppLogic::ReconstructMethodRegistry reconstruct_method_registry;
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_geometry =
				GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						reconstructed_geometry,
						reconstruct_method_registry,
						prepared_feature->reference(),
						reconstruction_time,
						reconstruct_method_context,
						true);
		GPlatesModel::TopLevelProperty::non_null_ptr_type prepared_geometry =
				(*prepared_geometry_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter geometry_setter(present_day_geometry);
		geometry_setter.set_geometry(prepared_geometry.get());
		prepared_feature->set(prepared_geometry_property, prepared_geometry);

		return PreparedReassignment(
				source_plate_property,
				prepared_feature,
				prepared_plate_property,
				prepared_geometry_property);
	}
}


GPlatesViewOperations::PlateIdReassignmentOperation::PlateIdReassignmentOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state,
		QWidget *parent_widget) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_parent_widget(parent_widget)
{
}


void
GPlatesViewOperations::PlateIdReassignmentOperation::trigger()
{
	GPlatesGui::FeatureFocus &feature_focus = d_view_state.get_feature_focus();
	const GPlatesModel::FeatureHandle::weak_ref source_feature = feature_focus.focused_feature();
	const GPlatesModel::FeatureHandle::iterator source_geometry_property =
			feature_focus.associated_geometry_property();
	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> source_rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						feature_focus.associated_reconstruction_geometry());
	if (!source_feature.is_valid() || !source_geometry_property.is_still_valid() || !source_rfg)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("Select a reconstructed, non-topological feature first."));
		return;
	}
	GPlatesModel::FeatureCollectionHandle *source_collection_ptr = source_feature->parent_ptr();
	if (!source_collection_ptr)
	{
		QMessageBox::warning(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("The selected feature is not in a feature collection."));
		return;
	}
	const GPlatesAppLogic::Reconstruction &reconstruction =
			d_application_state.get_current_reconstruction();
	unsigned int active_geometry_property_count = 0;
	for (GPlatesModel::FeatureHandle::iterator property_iter = source_feature->begin();
			property_iter != source_feature->end(); ++property_iter)
	{
		if (GPlatesAppLogic::GeometryUtils::get_geometry_from_property(
				property_iter, reconstruction.get_reconstruction_time()))
		{
			++active_geometry_property_count;
		}
	}
	if (active_geometry_property_count != 1)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("This operation currently requires exactly one active geometry property. ") +
				QObject::tr("Features with multiple active geometries are left unchanged so none can jump."));
		return;
	}

	QDialog dialog(d_parent_widget);
	dialog.setWindowTitle(QObject::tr("Reassign Plate ID Without Jumping"));
	QFormLayout *layout = new QFormLayout(&dialog);
	QSpinBox *plate_id_spin = new QSpinBox(&dialog);
	plate_id_spin->setRange(0, 99999999);
	plate_id_spin->setValue((*source_rfg)->reconstruction_plate_id()
			? *(*source_rfg)->reconstruction_plate_id() : 0);
	QComboBox *mode_combo = new QComboBox(&dialog);
	mode_combo->addItem(QObject::tr("Copy feature"), 0);
	mode_combo->addItem(QObject::tr("Move feature"), 1);
	layout->addRow(QObject::tr("New Plate ID:"), plate_id_spin);
	layout->addRow(QObject::tr("Operation:"), mode_combo);
	QLabel *note = new QLabel(QObject::tr(
			"The geometry is reverse-reconstructed with the new Plate ID so it stays at exactly "
			"the same on-screen position at the current reconstruction time."), &dialog);
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
	if (mode_combo->currentData().toInt() == 1 &&
			(*source_rfg)->reconstruction_plate_id() &&
			plate_id_spin->value() == *(*source_rfg)->reconstruction_plate_id())
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("The focused feature already has Plate ID %1.").arg(plate_id_spin->value()));
		return;
	}

	GPlatesModel::FeatureHandle::iterator source_plate_property;
	for (GPlatesModel::FeatureHandle::iterator property_iter = source_feature->begin();
			property_iter != source_feature->end(); ++property_iter)
	{
		if ((*property_iter)->get_property_name() == reconstruction_plate_id_property_name())
		{
			source_plate_property = property_iter;
			break;
		}
	}
	if (!source_plate_property.is_still_valid())
	{
		QMessageBox::warning(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("The selected feature has no reconstruction Plate ID property."));
		return;
	}

	// Deep-clone the feature and remember the corresponding properties. This clone is
	// also a safe scratch feature for choosing the reconstruct method with the new ID.
	GPlatesModel::FeatureHandle::non_null_ptr_type prepared_feature =
			GPlatesModel::FeatureHandle::create(source_feature->feature_type());
	GPlatesModel::FeatureHandle::iterator prepared_plate_property;
	GPlatesModel::FeatureHandle::iterator prepared_geometry_property;
	for (GPlatesModel::FeatureHandle::iterator property_iter = source_feature->begin();
			property_iter != source_feature->end(); ++property_iter)
	{
		const GPlatesModel::FeatureHandle::iterator prepared_property =
				prepared_feature->add((*property_iter)->clone());
		if (property_iter == source_plate_property)
		{
			prepared_plate_property = prepared_property;
		}
		if (property_iter == source_geometry_property)
		{
			prepared_geometry_property = prepared_property;
		}
	}
	if (!prepared_plate_property.is_still_valid() || !prepared_geometry_property.is_still_valid())
	{
		QMessageBox::warning(d_parent_widget, QObject::tr("Reassign Plate ID"),
				QObject::tr("Could not prepare the selected feature's Plate ID and geometry."));
		return;
	}
	GPlatesModel::TopLevelProperty::non_null_ptr_type prepared_plate =
			create_plate_id_property(plate_id_spin->value());
	prepared_plate->set_xml_attributes((*source_plate_property)->get_xml_attributes());
	prepared_feature->set(prepared_plate_property, prepared_plate);

	std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> reconstruct_layer_outputs;
	GPlatesAppLogic::LayerProxyUtils::find_reconstruct_layer_outputs_of_feature_collection(
			reconstruct_layer_outputs,
			source_collection_ptr->reference(),
			d_application_state.get_reconstruct_graph());
	const GPlatesAppLogic::ReconstructMethodRegistry reconstruct_method_registry;
	const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_geometry =
			!reconstruct_layer_outputs.empty()
			? GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
					(*source_rfg)->reconstructed_geometry(),
					reconstruct_method_registry,
					prepared_feature->reference(),
					reconstruction.get_reconstruction_time(),
					reconstruct_layer_outputs.front()->get_reconstruct_method_context(),
					true)
			: GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
					(*source_rfg)->reconstructed_geometry(),
					reconstruct_method_registry,
					prepared_feature->reference(),
					reconstruction.get_reconstruction_time(),
					reconstruction.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator(),
					GPlatesAppLogic::ReconstructParams(),
					true);
	GPlatesModel::TopLevelProperty::non_null_ptr_type prepared_geometry =
			(*prepared_geometry_property)->clone();
	GPlatesFeatureVisitors::GeometrySetter geometry_setter(present_day_geometry);
	geometry_setter.set_geometry(prepared_geometry.get());
	prepared_feature->set(prepared_geometry_property, prepared_geometry);

	std::unique_ptr<QUndoCommand> command;
	if (mode_combo->currentData().toInt() == 0)
	{
		command.reset(new AddFeatureUndoCommand(
				d_application_state.get_model_interface(),
				source_collection_ptr->reference(),
				prepared_feature,
				feature_focus));
	}
	else
	{
		command.reset(new ReplaceFeaturePropertiesUndoCommand(
				d_application_state.get_model_interface(),
				source_feature,
				source_plate_property,
				source_geometry_property,
				(*source_plate_property)->clone(),
				(*prepared_plate_property)->clone(),
				(*source_geometry_property)->clone(),
				(*prepared_geometry_property)->clone()));
	}
	UndoRedo::instance().get_active_undo_stack().push(command.release());
}


void
GPlatesViewOperations::PlateIdReassignmentOperation::trigger_bulk()
{
	enum BulkMode
	{
		BULK_COPY,
		BULK_MOVE,
		BULK_END_VALID_TIME,
		BULK_DELETE_RECORDS
	};
	enum BulkScope
	{
		SCOPE_VISIBLE,
		SCOPE_SELECTED_COLLECTIONS,
		SCOPE_ALL_LOADED
	};
	struct LoadedCollection
	{
		LoadedCollection(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const QString &name_) :
			collection(collection_),
			name(name_)
		{
		}

		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		QString name;
	};
	struct Candidate
	{
		Candidate(
				const GPlatesModel::FeatureHandle::weak_ref &feature_,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const QString &collection_name_) :
			feature(feature_),
			collection(collection_),
			collection_name(collection_name_)
		{
		}

		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		QString collection_name;
	};
	struct CandidateGeometry
	{
		CandidateGeometry(
				const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type &geometry_,
				const GPlatesAppLogic::ReconstructMethodInterface::Context &reconstruct_context_) :
			geometry(geometry_),
			reconstruct_context(reconstruct_context_)
		{
		}

		GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type geometry;
		GPlatesAppLogic::ReconstructMethodInterface::Context reconstruct_context;
	};
	struct PlannedReassignment
	{
		PlannedReassignment(
				const Candidate &candidate_,
				const GPlatesModel::FeatureHandle::iterator &source_geometry_property_,
				const PreparedReassignment &prepared_) :
			candidate(candidate_),
			source_geometry_property(source_geometry_property_),
			prepared(prepared_)
		{
		}

		Candidate candidate;
		GPlatesModel::FeatureHandle::iterator source_geometry_property;
		PreparedReassignment prepared;
	};
	struct PlannedValidTime
	{
		PlannedValidTime(
				const Candidate &candidate_,
				const GPlatesModel::FeatureHandle::iterator &valid_time_property_,
				const boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> &before_,
				const GPlatesModel::TopLevelProperty::non_null_ptr_type &after_) :
			candidate(candidate_),
			valid_time_property(valid_time_property_),
			before(before_),
			after(after_)
		{
		}

		Candidate candidate;
		GPlatesModel::FeatureHandle::iterator valid_time_property;
		boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};

	std::vector<LoadedCollection> loaded_collections;
	std::map<const GPlatesModel::FeatureCollectionHandle *, QString> collection_names;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded_files =
			d_application_state.get_feature_collection_file_state().get_loaded_files();
	for (std::size_t file_index = 0; file_index < loaded_files.size(); ++file_index)
	{
		GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				loaded_files[file_index].get_file().get_feature_collection();
		if (!collection.is_valid())
		{
			continue;
		}
		QString name = loaded_files[file_index].get_file().get_file_info().get_display_name(false);
		if (name.isEmpty())
		{
			name = QObject::tr("Untitled collection %1").arg(file_index + 1);
		}
		loaded_collections.push_back(LoadedCollection(collection, name));
		collection_names[&*collection] = name;
	}

	QDialog dialog(d_parent_widget);
	dialog.setWindowTitle(QObject::tr("Bulk Plate ID Operations"));
	QFormLayout *layout = new QFormLayout(&dialog);
	QSpinBox *source_plate_spin = new QSpinBox(&dialog);
	QSpinBox *target_plate_spin = new QSpinBox(&dialog);
	source_plate_spin->setRange(0, 99999999);
	target_plate_spin->setRange(0, 99999999);
	QComboBox *mode_combo = new QComboBox(&dialog);
	mode_combo->addItem(QObject::tr("Copy to target Plate ID"), BULK_COPY);
	mode_combo->addItem(QObject::tr("Move to target Plate ID"), BULK_MOVE);
	mode_combo->addItem(QObject::tr("End valid time at current time"), BULK_END_VALID_TIME);
	mode_combo->addItem(QObject::tr("Delete feature records"), BULK_DELETE_RECORDS);
	QComboBox *scope_combo = new QComboBox(&dialog);
	scope_combo->addItem(QObject::tr("Visible reconstructed features"), SCOPE_VISIBLE);
	scope_combo->addItem(QObject::tr("Selected loaded collections"), SCOPE_SELECTED_COLLECTIONS);
	scope_combo->addItem(QObject::tr("All loaded collections"), SCOPE_ALL_LOADED);
	QListWidget *collection_list = new QListWidget(&dialog);
	collection_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	collection_list->setMaximumHeight(120);
	for (std::size_t collection_index = 0; collection_index < loaded_collections.size(); ++collection_index)
	{
		QListWidgetItem *item = new QListWidgetItem(
				loaded_collections[collection_index].name, collection_list);
		item->setData(Qt::UserRole, static_cast<unsigned long long>(collection_index));
		item->setSelected(true);
	}
	QCheckBox *copy_begin_at_current_time = new QCheckBox(
			QObject::tr("Set copied features' begin valid time to the current time"), &dialog);
	QCheckBox *dry_run = new QCheckBox(QObject::tr("Dry run only (make no changes)"), &dialog);
	dry_run->setChecked(true);
	layout->addRow(QObject::tr("Source Plate ID:"), source_plate_spin);
	layout->addRow(QObject::tr("Target Plate ID:"), target_plate_spin);
	layout->addRow(QObject::tr("Operation:"), mode_combo);
	layout->addRow(QObject::tr("Scope:"), scope_combo);
	layout->addRow(QObject::tr("Collections (for selected scope):"), collection_list);
	layout->addRow(copy_begin_at_current_time);
	layout->addRow(dry_run);
	QLabel *note = new QLabel(QObject::tr(
			"Only features on the source Plate ID that are valid at the current reconstruction "
			"time are eligible. Copy and move preserve each supported feature's position at that "
			"time. Expand the result details for counts and every skipped Feature ID. A committed "
			"batch is one undoable action."), &dialog);
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

	const BulkMode mode = static_cast<BulkMode>(mode_combo->currentData().toInt());
	const BulkScope scope = static_cast<BulkScope>(scope_combo->currentData().toInt());
	const GPlatesModel::integer_plate_id_type source_plate_id = source_plate_spin->value();
	const GPlatesModel::integer_plate_id_type target_plate_id = target_plate_spin->value();
	if ((mode == BULK_COPY || mode == BULK_MOVE) && source_plate_id == target_plate_id)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Bulk Plate ID Operations"),
				QObject::tr("Choose different source and target Plate IDs."));
		return;
	}

	std::set<std::size_t> chosen_collection_indices;
	if (scope == SCOPE_SELECTED_COLLECTIONS)
	{
		for (int item_index = 0; item_index < collection_list->count(); ++item_index)
		{
			const QListWidgetItem *item = collection_list->item(item_index);
			if (item->isSelected())
			{
				chosen_collection_indices.insert(
						static_cast<std::size_t>(item->data(Qt::UserRole).toULongLong()));
			}
		}
		if (chosen_collection_indices.empty())
		{
			QMessageBox::information(d_parent_widget, QObject::tr("Bulk Plate ID Operations"),
					QObject::tr("Select at least one loaded collection for the selected-collections scope."));
			return;
		}
	}
	else if (scope == SCOPE_ALL_LOADED)
	{
		for (std::size_t collection_index = 0;
			collection_index < loaded_collections.size(); ++collection_index)
		{
			chosen_collection_indices.insert(collection_index);
		}
	}

	BulkReport report;
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	std::vector<Candidate> candidates;
	std::map<const GPlatesModel::FeatureHandle *, std::size_t> candidate_indices;
	std::map<const GPlatesModel::FeatureHandle *, std::vector<CandidateGeometry> > candidate_geometries;
	const auto add_candidate_geometry = [&candidate_geometries](
			const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type &geometry,
			const GPlatesAppLogic::ReconstructMethodInterface::Context &reconstruct_context)
	{
		std::vector<CandidateGeometry> &geometries =
				candidate_geometries[geometry->feature_handle_ptr()];
		for (std::vector<CandidateGeometry>::const_iterator iter = geometries.begin();
			iter != geometries.end(); ++iter)
		{
			if (iter->geometry->property() == geometry->property())
			{
				return;
			}
		}
		geometries.push_back(CandidateGeometry(geometry, reconstruct_context));
	};

	const auto collection_name = [&collection_names](GPlatesModel::FeatureCollectionHandle *collection)
	{
		const std::map<const GPlatesModel::FeatureCollectionHandle *, QString>::const_iterator name_iter =
				collection_names.find(collection);
		return name_iter != collection_names.end()
				? name_iter->second
				: QObject::tr("Unlisted or unsaved collection");
	};
	const auto add_current_source_candidate = [&candidates, &candidate_indices, &report,
			source_plate_id, reconstruction_time](
				const GPlatesModel::FeatureHandle::weak_ref &feature,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const QString &name)
	{
		if (!feature.is_valid() || !collection.is_valid() ||
			candidate_indices.find(feature.operator->()) != candidate_indices.end())
		{
			return;
		}
		GPlatesAppLogic::ReconstructionFeatureProperties properties;
		properties.visit_feature(feature);
		if (!properties.get_recon_plate_id() ||
				*properties.get_recon_plate_id() != source_plate_id)
		{
			return;
		}
		if (!properties.is_feature_defined_at_recon_time(reconstruction_time))
		{
			report.add_skipped(feature, name,
					QObject::tr("not valid at the current reconstruction time"));
			return;
		}
		candidate_indices[feature.operator->()] = candidates.size();
		candidates.push_back(Candidate(feature, collection, name));
	};

	if (scope == SCOPE_VISIBLE)
	{
		const GPlatesPresentation::VisualLayers &visual_layers = d_view_state.get_visual_layers();
		for (size_t layer_index = 0; layer_index < visual_layers.size(); ++layer_index)
		{
			const boost::shared_ptr<const GPlatesPresentation::VisualLayer> visual_layer =
					visual_layers.visual_layer_at(layer_index).lock();
			if (!visual_layer || !visual_layer->is_visible() ||
					visual_layer->get_layer_type() !=
							static_cast<unsigned int>(GPlatesAppLogic::LayerTaskType::RECONSTRUCT))
			{
				continue;
			}
			const boost::optional<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_output =
					visual_layer->get_reconstruct_graph_layer().get_layer_output<
							GPlatesAppLogic::ReconstructLayerProxy>();
			if (!layer_output)
			{
				continue;
			}

		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*layer_output)->get_reconstructed_feature_geometries(geometries);
		for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
				geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
		{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
						!rfg.reconstruction_plate_id() ||
						*rfg.reconstruction_plate_id() != source_plate_id)
				{
					continue;
				}
				GPlatesModel::FeatureHandle::weak_ref feature = rfg.feature_handle_ptr()->reference();
				GPlatesModel::FeatureCollectionHandle *collection_ptr = feature->parent_ptr();
				if (!collection_ptr)
				{
					continue;
				}
				add_current_source_candidate(
						feature, collection_ptr->reference(), collection_name(collection_ptr));
				add_candidate_geometry(
						*geometry_iter, (*layer_output)->get_reconstruct_method_context());
			}
		}
	}
	else
	{
		for (std::set<std::size_t>::const_iterator collection_index_iter =
				chosen_collection_indices.begin();
				collection_index_iter != chosen_collection_indices.end(); ++collection_index_iter)
		{
			const LoadedCollection &loaded_collection = loaded_collections[*collection_index_iter];
			for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter =
					loaded_collection.collection->begin();
					feature_iter != loaded_collection.collection->end(); ++feature_iter)
			{
				add_current_source_candidate(
						(*feature_iter)->reference(), loaded_collection.collection,
						loaded_collection.name);
			}

			if (mode != BULK_COPY && mode != BULK_MOVE)
			{
				continue;
			}

			std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_outputs;
			GPlatesAppLogic::LayerProxyUtils::find_reconstruct_layer_outputs_of_feature_collection(
					layer_outputs, loaded_collection.collection,
					d_application_state.get_reconstruct_graph());
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			boost::optional<GPlatesAppLogic::ReconstructMethodInterface::Context> reconstruct_context;
			if (layer_outputs.empty())
			{
				const GPlatesAppLogic::ReconstructMethodRegistry reconstruct_method_registry;
				std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> collections(1,
						loaded_collection.collection);
				const GPlatesAppLogic::ReconstructionLayerProxy::non_null_ptr_type default_layer_output =
						d_application_state.get_current_reconstruction().get_default_reconstruction_layer_output();
				GPlatesAppLogic::ReconstructUtils::reconstruct(
						geometries,
						reconstruction_time,
						reconstruct_method_registry,
						collections,
						default_layer_output->get_reconstruction_tree_creator(),
						GPlatesAppLogic::ReconstructParams());
				reconstruct_context = GPlatesAppLogic::ReconstructMethodInterface::Context(
						GPlatesAppLogic::ReconstructParams(),
						default_layer_output->get_reconstruction_tree_creator());
			}
			else
			{
				layer_outputs.front()->get_reconstructed_feature_geometries(geometries);
				reconstruct_context = layer_outputs.front()->get_reconstruct_method_context();
			}
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (rfg.is_valid() && rfg.property().is_still_valid() &&
						candidate_indices.find(rfg.feature_handle_ptr()) != candidate_indices.end())
				{
					add_candidate_geometry(*geometry_iter, reconstruct_context.get());
				}
			}
		}
	}

	std::vector<PlannedReassignment> planned_reassignments;
	std::vector<PlannedValidTime> planned_valid_times;
	std::vector<Candidate> planned_deletions;
	for (std::vector<Candidate>::const_iterator candidate_iter = candidates.begin();
			candidate_iter != candidates.end(); ++candidate_iter)
	{
		const Candidate &candidate = *candidate_iter;
		const QString feature_type = candidate.feature->feature_type().get_name().qstring();
		if (mode == BULK_DELETE_RECORDS)
		{
			planned_deletions.push_back(candidate);
			report.add_planned(candidate.collection_name, feature_type);
			continue;
		}
		if (mode == BULK_END_VALID_TIME)
		{
			const GPlatesModel::FeatureHandle::iterator valid_time_property =
					find_property(candidate.feature, valid_time_property_name());
			boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> before;
			GPlatesPropertyValues::GeoTimeInstant begin =
					GPlatesPropertyValues::GeoTimeInstant::create_distant_past();
			if (valid_time_property.is_still_valid())
			{
				before = (*valid_time_property)->clone();
				const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type>
						valid_time = GPlatesFeatureVisitors::get_property_value<
								GPlatesPropertyValues::GmlTimePeriod>(candidate.feature,
								valid_time_property_name());
				if (!valid_time)
				{
					report.add_skipped(candidate.feature, candidate.collection_name,
							QObject::tr("validTime is not a supported constant gml:TimePeriod"));
					continue;
				}
				begin = (*valid_time)->begin()->get_time_position();
				if ((*valid_time)->end()->get_time_position().is_coincident_with(
						GPlatesPropertyValues::GeoTimeInstant(reconstruction_time)))
				{
					report.add_skipped(candidate.feature, candidate.collection_name,
							QObject::tr("valid time already ends at the current reconstruction time"));
					continue;
				}
			}
			const GPlatesModel::TopLevelProperty::non_null_ptr_type after =
					create_valid_time_property(candidate.feature, begin,
							GPlatesPropertyValues::GeoTimeInstant(reconstruction_time), before);
			planned_valid_times.push_back(
					PlannedValidTime(candidate, valid_time_property, before, after));
			report.add_planned(candidate.collection_name, feature_type);
			continue;
		}

		const std::map<const GPlatesModel::FeatureHandle *, std::vector<CandidateGeometry> >::const_iterator
				geometries_iter = candidate_geometries.find(candidate.feature.operator->());
		if (geometries_iter == candidate_geometries.end() || geometries_iter->second.empty())
		{
			report.add_skipped(candidate.feature, candidate.collection_name,
					QObject::tr("could not reconstruct an eligible geometry at the current time"));
			continue;
		}
		if (geometries_iter->second.size() != 1)
		{
			report.add_skipped(candidate.feature, candidate.collection_name,
					QObject::tr("has multiple active reconstructed geometries; no-jump target is ambiguous"));
			continue;
		}
		const CandidateGeometry &candidate_geometry = geometries_iter->second.front();
		const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = *candidate_geometry.geometry;
		const boost::optional<PreparedReassignment> prepared = prepare_reassignment(
				candidate.feature,
				rfg.property(),
				rfg.reconstructed_geometry(),
				target_plate_id,
				reconstruction_time,
				candidate_geometry.reconstruct_context);
		if (!prepared)
		{
			report.add_skipped(candidate.feature, candidate.collection_name,
					QObject::tr("Plate ID or geometry property could not be prepared safely"));
			continue;
		}
		if (mode == BULK_COPY && copy_begin_at_current_time->isChecked())
		{
			const GPlatesModel::FeatureHandle::weak_ref prepared_feature = prepared->feature->reference();
			const GPlatesModel::FeatureHandle::iterator valid_time_property =
					find_property(prepared_feature, valid_time_property_name());
			boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> template_property;
			GPlatesPropertyValues::GeoTimeInstant end =
					GPlatesPropertyValues::GeoTimeInstant::create_distant_future();
			if (valid_time_property.is_still_valid())
			{
				template_property = (*valid_time_property)->clone();
				const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type>
						valid_time = GPlatesFeatureVisitors::get_property_value<
								GPlatesPropertyValues::GmlTimePeriod>(prepared_feature,
								valid_time_property_name());
				if (!valid_time)
				{
					report.add_skipped(candidate.feature, candidate.collection_name,
							QObject::tr("copy begin-time option cannot update this validTime representation"));
					continue;
				}
				end = (*valid_time)->end()->get_time_position();
			}
			const GPlatesModel::TopLevelProperty::non_null_ptr_type copy_valid_time =
					create_valid_time_property(prepared_feature,
							GPlatesPropertyValues::GeoTimeInstant(reconstruction_time), end,
							template_property);
			if (valid_time_property.is_still_valid())
			{
				prepared_feature->set(valid_time_property, copy_valid_time);
			}
			else
			{
				prepared_feature->add(copy_valid_time);
			}
		}
		planned_reassignments.push_back(
				PlannedReassignment(candidate, rfg.property(), prepared.get()));
		report.add_planned(candidate.collection_name, feature_type);
	}

	if (dry_run->isChecked())
	{
		show_bulk_report(d_parent_widget,
				QObject::tr("Dry run: %1 feature(s) are eligible; %2 feature(s) would be skipped. No changes were made.")
					.arg(report.planned_count()).arg(report.skipped_features.size()),
				report);
		return;
	}
	if (report.planned_count() == 0)
	{
		show_bulk_report(d_parent_widget,
				QObject::tr("No eligible features were found on Plate ID %1. No changes were made.")
					.arg(source_plate_id), report);
		return;
	}

	QUndoStack &undo_stack = UndoRedo::instance().get_active_undo_stack();
	undo_stack.beginMacro(mode == BULK_COPY
			? QObject::tr("bulk copy Plate ID %1 to %2").arg(source_plate_id).arg(target_plate_id)
			: mode == BULK_MOVE
			? QObject::tr("bulk move Plate ID %1 to %2").arg(source_plate_id).arg(target_plate_id)
			: mode == BULK_END_VALID_TIME
			? QObject::tr("bulk end valid time for Plate ID %1").arg(source_plate_id)
			: QObject::tr("bulk delete records for Plate ID %1").arg(source_plate_id));
	GPlatesModel::NotificationGuard notification_guard(
			*d_application_state.get_model_interface().access_model());
	unsigned int changed_count = 0;
	for (std::vector<PlannedReassignment>::const_iterator iter =
			planned_reassignments.begin(); iter != planned_reassignments.end(); ++iter)
	{
		if (mode == BULK_COPY)
		{
			undo_stack.push(new AddFeatureUndoCommand(
					d_application_state.get_model_interface(),
					iter->candidate.collection,
					iter->prepared.feature,
					d_view_state.get_feature_focus()));
		}
		else
		{
			undo_stack.push(new ReplaceFeaturePropertiesUndoCommand(
					d_application_state.get_model_interface(),
					iter->candidate.feature,
					iter->prepared.source_plate_property,
					iter->source_geometry_property,
					(*iter->prepared.source_plate_property)->clone(),
					(*iter->prepared.plate_property)->clone(),
					(*iter->source_geometry_property)->clone(),
					(*iter->prepared.geometry_property)->clone()));
		}
		++changed_count;
	}
	for (std::vector<PlannedValidTime>::const_iterator iter =
			planned_valid_times.begin(); iter != planned_valid_times.end(); ++iter)
	{
		undo_stack.push(new SetValidTimeUndoCommand(
				d_application_state.get_model_interface(), iter->candidate.feature,
				iter->valid_time_property, iter->before, iter->after));
		++changed_count;
	}
	for (std::vector<Candidate>::const_iterator candidate_iter =
			planned_deletions.begin(); candidate_iter != planned_deletions.end(); ++candidate_iter)
	{
		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter =
				candidate_iter->collection->begin();
				feature_iter != candidate_iter->collection->end(); ++feature_iter)
		{
			if ((*feature_iter).get() == candidate_iter->feature.operator->())
			{
				undo_stack.push(new RemoveFeatureUndoCommand(
						d_application_state.get_model_interface(),
						candidate_iter->collection, feature_iter));
				++changed_count;
				break;
			}
		}
	}
	undo_stack.endMacro();
	notification_guard.release_guard();
	show_bulk_report(d_parent_widget,
			QObject::tr("Updated %1 feature(s); %2 feature(s) were skipped. Use Edit > Undo to restore the batch.")
				.arg(changed_count).arg(report.skipped_features.size()),
			report);
}
