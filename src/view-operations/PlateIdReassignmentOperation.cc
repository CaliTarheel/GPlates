/* $Id$ */

#include "PlateIdReassignmentOperation.h"

#include <memory>
#include <set>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QUndoCommand>
#include <QUndoStack>

#include "app-logic/ApplicationState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/LayerProxyUtils.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructMethodInterface.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/GeometrySetter.h"

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

		prepared_feature->set(prepared_plate_property, create_plate_id_property(target_plate_id));
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
	prepared_feature->set(prepared_plate_property, create_plate_id_property(plate_id_spin->value()));

	std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> reconstruct_layer_outputs;
	GPlatesAppLogic::LayerProxyUtils::find_reconstruct_layer_outputs_of_feature_collection(
			reconstruct_layer_outputs,
			source_collection_ptr->reference(),
			d_application_state.get_reconstruct_graph());
	const GPlatesAppLogic::Reconstruction &reconstruction =
			d_application_state.get_current_reconstruction();
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
		BULK_DELETE
	};

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
	mode_combo->addItem(QObject::tr("Delete"), BULK_DELETE);
	layout->addRow(QObject::tr("Source Plate ID:"), source_plate_spin);
	layout->addRow(QObject::tr("Target Plate ID:"), target_plate_spin);
	layout->addRow(QObject::tr("Operation:"), mode_combo);
	QLabel *note = new QLabel(QObject::tr(
			"Applies to visible reconstructed features on the source Plate ID. Copy and move "
			"preserve each feature's position at the current reconstruction time. The entire "
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
	const GPlatesModel::integer_plate_id_type source_plate_id = source_plate_spin->value();
	const GPlatesModel::integer_plate_id_type target_plate_id = target_plate_spin->value();
	if (mode != BULK_DELETE && source_plate_id == target_plate_id)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Bulk Plate ID Operations"),
				QObject::tr("Choose different source and target Plate IDs."));
		return;
	}

	QUndoStack &undo_stack = UndoRedo::instance().get_active_undo_stack();
	undo_stack.beginMacro(mode == BULK_COPY
			? QObject::tr("bulk copy Plate ID %1 to %2").arg(source_plate_id).arg(target_plate_id)
			: mode == BULK_MOVE
			? QObject::tr("bulk move Plate ID %1 to %2").arg(source_plate_id).arg(target_plate_id)
			: QObject::tr("bulk delete Plate ID %1").arg(source_plate_id));
	GPlatesModel::NotificationGuard notification_guard(
			*d_application_state.get_model_interface().access_model());

	unsigned int changed_count = 0;
	std::set<const GPlatesModel::FeatureHandle *> seen_features;
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
					*rfg.reconstruction_plate_id() != source_plate_id ||
					!seen_features.insert(rfg.feature_handle_ptr()).second)
			{
				continue;
			}

			GPlatesModel::FeatureHandle::weak_ref source_feature =
					rfg.feature_handle_ptr()->reference();
			GPlatesModel::FeatureCollectionHandle *collection_ptr = source_feature->parent_ptr();
			if (!collection_ptr)
			{
				continue;
			}

			if (mode == BULK_DELETE)
			{
				for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection_ptr->begin();
						feature_iter != collection_ptr->end(); ++feature_iter)
				{
					if ((*feature_iter).get() == rfg.feature_handle_ptr())
					{
						undo_stack.push(new RemoveFeatureUndoCommand(
								d_application_state.get_model_interface(),
								collection_ptr->reference(),
								feature_iter));
						++changed_count;
						break;
					}
				}
				continue;
			}

			const boost::optional<PreparedReassignment> prepared = prepare_reassignment(
					source_feature,
					rfg.property(),
					rfg.reconstructed_geometry(),
					target_plate_id,
					d_application_state.get_current_reconstruction().get_reconstruction_time(),
					(*layer_output)->get_reconstruct_method_context());
			if (!prepared)
			{
				continue;
			}

			if (mode == BULK_COPY)
			{
				undo_stack.push(new AddFeatureUndoCommand(
						d_application_state.get_model_interface(),
						collection_ptr->reference(),
						prepared->feature,
						d_view_state.get_feature_focus()));
			}
			else
			{
				undo_stack.push(new ReplaceFeaturePropertiesUndoCommand(
						d_application_state.get_model_interface(),
						source_feature,
						prepared->source_plate_property,
						rfg.property(),
						(*prepared->source_plate_property)->clone(),
						(*prepared->plate_property)->clone(),
						(*rfg.property())->clone(),
						(*prepared->geometry_property)->clone()));
			}
			++changed_count;
		}
	}

	undo_stack.endMacro();
	notification_guard.release_guard();
	if (changed_count == 0)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Bulk Plate ID Operations"),
				QObject::tr("No visible reconstructed features were found on Plate ID %1.")
						.arg(source_plate_id));
	}
	else
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Bulk Plate ID Operations"),
				QObject::tr("Updated %1 visible feature(s). Use Edit > Undo to restore the batch.")
						.arg(changed_count));
	}
}
