/* $Id$ */

#include <algorithm>
#include <memory>
#include <set>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "PolygonBooleanOperation.h"

#include "PolygonBooleanGeometry.h"
#include "UndoRedo.h"
#include "VisibleGeometrySelection.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/Reconstruction.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/FeatureFocus.h"

#include "maths/PolygonOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"


namespace
{
	using GPlatesViewOperations::PolygonBooleanGeometry::polygon_ptr_type;
	using GPlatesViewOperations::PolygonBooleanGeometry::polygon_seq_type;
	using GPlatesViewOperations::VisibleGeometrySelection::Choice;
	using GPlatesViewOperations::VisibleGeometrySelection::choice_seq_type;

	struct RemovedFeature
	{
		RemovedFeature(
				const GPlatesModel::FeatureHandle::weak_ref &feature_,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_) :
			feature(feature_),
			collection(collection_)
		{  }

		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
	};

	typedef std::vector<RemovedFeature> removed_feature_seq_type;


	polygon_ptr_type
	reverse_reconstruct_polygon(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg)
	{
		if (!rfg.reconstruction_plate_id())
		{
			return polygon.get_non_null_pointer();
		}

		std::vector<GPlatesMaths::PointOnSphere> exterior;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
				polygon.exterior_ring_vertex_begin(); point_iter != polygon.exterior_ring_vertex_end(); ++point_iter)
		{
			exterior.push_back(GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
					*point_iter, *rfg.reconstruction_plate_id(), *rfg.get_reconstruction_tree(), true));
		}

		std::vector<std::vector<GPlatesMaths::PointOnSphere> > holes;
		for (unsigned int ring_index = 0; ring_index < polygon.number_of_interior_rings(); ++ring_index)
		{
			std::vector<GPlatesMaths::PointOnSphere> hole;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator point_iter =
					polygon.interior_ring_vertex_begin(ring_index);
					point_iter != polygon.interior_ring_vertex_end(ring_index); ++point_iter)
			{
				hole.push_back(GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
						*point_iter, *rfg.reconstruction_plate_id(), *rfg.get_reconstruction_tree(), true));
			}
			holes.push_back(hole);
		}
		return GPlatesMaths::PolygonOnSphere::create(exterior, holes, true);
	}


	class PolygonBooleanUndoCommand :
			public QUndoCommand
	{
	public:
		PolygonBooleanUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type &target,
				const polygon_seq_type &present_day_results,
				const removed_feature_seq_type &features_to_remove) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_target_feature(target->get_feature_ref()),
			d_target_property(target->property()),
			d_original_target_property((*target->property())->clone()),
			d_features_to_remove(features_to_remove),
			d_first_redo(true),
			d_target_was_removed(false)
		{
			setText(QObject::tr("polygon Boolean operation"));
			for (polygon_seq_type::const_iterator polygon_iter = present_day_results.begin();
					polygon_iter != present_day_results.end(); ++polygon_iter)
			{
				GPlatesModel::TopLevelProperty::non_null_ptr_type property = (*target->property())->clone();
				GPlatesFeatureVisitors::GeometrySetter geometry_setter(*polygon_iter);
				geometry_setter.set_geometry(property.get());
				d_result_properties.push_back(property);
			}
		}

		virtual void
		redo()
		{
			if (!d_target_feature.is_valid())
			{
				return;
			}
			GPlatesModel::FeatureCollectionHandle *target_collection = d_target_feature->parent_ptr();
			if (!target_collection && !d_target_collection.is_valid())
			{
				return;
			}

			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (target_collection)
			{
				d_target_collection = target_collection->reference();
			}

			if (d_first_redo)
			{
				create_result_features();
				d_first_redo = false;
			}

			if (d_result_properties.empty())
			{
				d_target_feature->remove_from_parent();
				d_target_was_removed = true;
			}
			else
			{
				if (!d_target_feature->parent_ptr())
				{
					d_target_collection->add(
							GPlatesModel::FeatureHandle::non_null_ptr_type(d_target_feature.handle_ptr()));
				}
				d_target_feature->set(d_target_property, d_result_properties.front());
				for (size_t feature_index = 0; feature_index < d_result_features.size(); ++feature_index)
				{
					d_target_collection->add(d_result_features[feature_index]);
				}
			}

			for (removed_feature_seq_type::iterator feature_iter = d_features_to_remove.begin();
					feature_iter != d_features_to_remove.end(); ++feature_iter)
			{
				if (feature_iter->feature.is_valid() && feature_iter->feature->parent_ptr())
				{
					feature_iter->feature->remove_from_parent();
				}
			}
			guard.release_guard();
		}

		virtual void
		undo()
		{
			if (!d_target_feature.is_valid() || !d_target_collection.is_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());

			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
					d_result_features.begin(); feature_iter != d_result_features.end(); ++feature_iter)
			{
				if ((*feature_iter)->parent_ptr())
				{
					(*feature_iter)->remove_from_parent();
				}
			}
			if (!d_target_feature->parent_ptr())
			{
				d_target_collection->add(
						GPlatesModel::FeatureHandle::non_null_ptr_type(d_target_feature.handle_ptr()));
			}
			d_target_feature->set(d_target_property, d_original_target_property);

			for (removed_feature_seq_type::iterator feature_iter = d_features_to_remove.begin();
					feature_iter != d_features_to_remove.end(); ++feature_iter)
			{
				if (feature_iter->feature.is_valid() && feature_iter->collection.is_valid() &&
						!feature_iter->feature->parent_ptr())
				{
					feature_iter->collection->add(
							GPlatesModel::FeatureHandle::non_null_ptr_type(feature_iter->feature.handle_ptr()));
				}
			}
			d_target_was_removed = false;
			guard.release_guard();
		}

	private:
		void
		create_result_features()
		{
			for (size_t result_index = 1; result_index < d_result_properties.size(); ++result_index)
			{
				GPlatesModel::FeatureHandle::non_null_ptr_type result_feature =
						GPlatesModel::FeatureHandle::create(d_target_feature->feature_type());
				GPlatesModel::FeatureHandle::iterator result_geometry_property;
				for (GPlatesModel::FeatureHandle::iterator property_iter = d_target_feature->begin();
						property_iter != d_target_feature->end(); ++property_iter)
				{
					GPlatesModel::FeatureHandle::iterator added_property =
							result_feature->add((*property_iter)->clone());
					if (property_iter == d_target_property)
					{
						result_geometry_property = added_property;
					}
				}
				result_feature->set(result_geometry_property, d_result_properties[result_index]);
				d_result_features.push_back(result_feature);
			}
		}

		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_target_feature;
		GPlatesModel::FeatureHandle::iterator d_target_property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type d_original_target_property;
		std::vector<GPlatesModel::TopLevelProperty::non_null_ptr_type> d_result_properties;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_result_features;
		removed_feature_seq_type d_features_to_remove;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_target_collection;
		bool d_first_redo;
		bool d_target_was_removed;
	};


	std::vector<int>
	selected_rows(
			const QListWidget &list)
	{
		std::vector<int> rows;
		const QList<QListWidgetItem *> items = list.selectedItems();
		for (QList<QListWidgetItem *>::const_iterator item_iter = items.begin(); item_iter != items.end(); ++item_iter)
		{
			rows.push_back(list.row(*item_iter));
		}
		std::sort(rows.begin(), rows.end());
		return rows;
	}
}


GPlatesViewOperations::PolygonBooleanOperation::PolygonBooleanOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::PolygonBooleanOperation::Result
GPlatesViewOperations::PolygonBooleanOperation::trigger(
		QWidget *parent)
{
	const choice_seq_type choices = VisibleGeometrySelection::get_choices(
			d_view_state, VisibleGeometrySelection::POLYGONS);
	if (choices.size() < 2)
	{
		return Result(
				OPERATION_ERROR,
				QObject::tr("Polygon Boolean Operations needs at least two visible reconstructed polygons."));
	}

	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Polygon Boolean Operations"));
	dialog.setModal(true);
	dialog.resize(1050, 560);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *instructions = new QLabel(
			QObject::tr(
					"Select one or more subject polygons on the left and the polygons to add, subtract, "
					"or compare on the right. Ctrl/Shift selects multiple rows. Geometry is evaluated "
					"on the sphere at %1 Ma.")
					.arg(d_application_state.get_current_reconstruction().get_reconstruction_time(), 0, 'f', 2),
			&dialog);
	instructions->setWordWrap(true);
	layout->addWidget(instructions);

	QFormLayout *operation_layout = new QFormLayout();
	QComboBox *operation_combo = new QComboBox(&dialog);
	operation_combo->addItem(QObject::tr("Add / Union"), PolygonBooleanGeometry::UNION);
	operation_combo->addItem(QObject::tr("Subtract"), PolygonBooleanGeometry::DIFFERENCE);
	operation_combo->addItem(QObject::tr("Intersection"), PolygonBooleanGeometry::INTERSECTION);
	operation_combo->addItem(QObject::tr("Symmetric Difference (XOR)"), PolygonBooleanGeometry::SYMMETRIC_DIFFERENCE);
	operation_layout->addRow(QObject::tr("Operation:"), operation_combo);
	layout->addLayout(operation_layout);

	QHBoxLayout *lists_layout = new QHBoxLayout();
	QGroupBox *subject_group = new QGroupBox(QObject::tr("Subjects (the geometry to change)"), &dialog);
	QGroupBox *modifier_group = new QGroupBox(QObject::tr("Modifiers (add/subtract/compare)"), &dialog);
	QVBoxLayout *subject_layout = new QVBoxLayout(subject_group);
	QVBoxLayout *modifier_layout = new QVBoxLayout(modifier_group);
	QListWidget *subject_list = new QListWidget(subject_group);
	QListWidget *modifier_list = new QListWidget(modifier_group);
	subject_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	modifier_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	for (choice_seq_type::const_iterator choice_iter = choices.begin(); choice_iter != choices.end(); ++choice_iter)
	{
		subject_list->addItem(choice_iter->label);
		modifier_list->addItem(choice_iter->label);
	}
	subject_layout->addWidget(subject_list);
	modifier_layout->addWidget(modifier_list);
	lists_layout->addWidget(subject_group);
	lists_layout->addWidget(modifier_group);
	layout->addLayout(lists_layout, 1);

	QCheckBox *consume_modifiers = new QCheckBox(
			QObject::tr("Remove modifier features after applying (useful when merging finished plate polygons)"),
			&dialog);
	consume_modifiers->setChecked(false);
	layout->addWidget(consume_modifiers);
	QLabel *result_note = new QLabel(
			QObject::tr(
					"The first subject becomes the result. Additional subject features are consumed. "
					"Disconnected results become cloned features. Everything is one undo step."),
			&dialog);
	result_note->setWordWrap(true);
	layout->addWidget(result_note);

	// Prefer the focused polygon as the first subject.
	if (d_feature_focus.associated_reconstruction_geometry())
	{
		for (size_t choice_index = 0; choice_index < choices.size(); ++choice_index)
		{
			if (choices[choice_index].geometry->get_feature_ref() == d_feature_focus.focused_feature())
			{
				subject_list->item(static_cast<int>(choice_index))->setSelected(true);
				break;
			}
		}
	}

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Apply"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);

	while (dialog.exec() == QDialog::Accepted)
	{
		const std::vector<int> subject_rows = selected_rows(*subject_list);
		const std::vector<int> modifier_rows = selected_rows(*modifier_list);
		if (subject_rows.empty() || modifier_rows.empty())
		{
			QMessageBox::warning(&dialog, QObject::tr("Polygon Boolean Operations"),
					QObject::tr("Select at least one subject and at least one modifier polygon."));
			continue;
		}

		std::set<int> subject_row_set(subject_rows.begin(), subject_rows.end());
		bool overlaps = false;
		for (std::vector<int>::const_iterator row_iter = modifier_rows.begin(); row_iter != modifier_rows.end(); ++row_iter)
		{
			if (subject_row_set.count(*row_iter))
			{
				overlaps = true;
				break;
			}
		}
		if (overlaps)
		{
			QMessageBox::warning(&dialog, QObject::tr("Polygon Boolean Operations"),
					QObject::tr("A polygon cannot be both a subject and a modifier in the same operation."));
			continue;
		}

		polygon_seq_type subjects;
		polygon_seq_type modifiers;
		for (std::vector<int>::const_iterator row_iter = subject_rows.begin(); row_iter != subject_rows.end(); ++row_iter)
		{
			const GPlatesMaths::PolygonOnSphere *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
					choices[*row_iter].geometry->reconstructed_geometry().get());
			if (polygon)
			{
				subjects.push_back(polygon->get_non_null_pointer());
			}
		}
		for (std::vector<int>::const_iterator row_iter = modifier_rows.begin(); row_iter != modifier_rows.end(); ++row_iter)
		{
			const GPlatesMaths::PolygonOnSphere *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
					choices[*row_iter].geometry->reconstructed_geometry().get());
			if (polygon)
			{
				modifiers.push_back(polygon->get_non_null_pointer());
			}
		}

		polygon_seq_type reconstructed_results;
		try
		{
			reconstructed_results = PolygonBooleanGeometry::apply(
					static_cast<PolygonBooleanGeometry::Operation>(operation_combo->currentData().toInt()),
					subjects,
					modifiers);
		}
		catch (const std::exception &exception)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The spherical Boolean operation failed: %1").arg(exception.what()));
		}

		const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type target =
				choices[subject_rows.front()].geometry;
		polygon_seq_type present_day_results;
		for (polygon_seq_type::const_iterator polygon_iter = reconstructed_results.begin();
				polygon_iter != reconstructed_results.end(); ++polygon_iter)
		{
			present_day_results.push_back(reverse_reconstruct_polygon(**polygon_iter, *target));
		}

		removed_feature_seq_type removed_features;
		std::set<const GPlatesModel::FeatureHandle *> removed_feature_pointers;
		for (size_t subject_index = 1; subject_index < subject_rows.size(); ++subject_index)
		{
			const GPlatesModel::FeatureHandle::weak_ref feature = choices[subject_rows[subject_index]].geometry->get_feature_ref();
			if (feature.handle_ptr() == target->get_feature_ref().handle_ptr())
			{
				return Result(
						OPERATION_ERROR,
						QObject::tr(
								"Two selected subject geometries belong to the same feature. Split them into "
								"separate features before consuming them in one Boolean operation."));
			}
			if (feature.is_valid() && feature->parent_ptr() && removed_feature_pointers.insert(feature.handle_ptr()).second)
			{
				removed_features.push_back(RemovedFeature(feature, feature->parent_ptr()->reference()));
			}
		}
		if (consume_modifiers->isChecked())
		{
			for (std::vector<int>::const_iterator row_iter = modifier_rows.begin(); row_iter != modifier_rows.end(); ++row_iter)
			{
				const GPlatesModel::FeatureHandle::weak_ref feature = choices[*row_iter].geometry->get_feature_ref();
				if (feature.handle_ptr() == target->get_feature_ref().handle_ptr())
				{
					return Result(
							OPERATION_ERROR,
							QObject::tr("A modifier geometry belongs to the result feature, so it cannot also be removed."));
				}
				if (feature.is_valid() && feature->parent_ptr() && removed_feature_pointers.insert(feature.handle_ptr()).second)
				{
					removed_features.push_back(RemovedFeature(feature, feature->parent_ptr()->reference()));
				}
			}
		}

		std::unique_ptr<QUndoCommand> command(new PolygonBooleanUndoCommand(
				d_feature_focus, d_model_interface, target, present_day_results, removed_features));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		return Result(
				OPERATION_COMPLETED,
				QObject::tr("Polygon operation complete: %1 result feature(s). Use Edit > Undo to restore the inputs.")
						.arg(present_day_results.size()));
	}

	return Result(OPERATION_CANCELLED, QObject::tr("Polygon Boolean Operations closed without changes."));
}
