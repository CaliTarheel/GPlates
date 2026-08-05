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

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSpinBox>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "RotationFileEditorOperation.h"

#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/TRSUtils.h"

#include "file-io/File.h"

#include "maths/FiniteRotation.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlFiniteRotationSlerp.h"
#include "property-values/GpmlInterpolationFunction.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/StructuralType.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	enum EditMode
	{
		CONNECT_PLATE,
		DISCONNECT_PLATE,
		CREATE_PLATE,
		RETIRE_PLATE
	};

	enum TimeDirection
	{
		TOWARD_PRESENT,
		TOWARD_OLDER
	};

	struct RotationSequence
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator sampling_property;
		GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling;
		GPlatesModel::integer_plate_id_type moving_plate;
		GPlatesModel::integer_plate_id_type fixed_plate;
		double minimum_time;
		double maximum_time;
	};

	typedef std::vector<RotationSequence> sequence_seq_type;
	typedef std::vector<GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type> sample_seq_type;

	struct ExistingChange
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type original_property;
		boost::optional<GPlatesModel::TopLevelProperty::non_null_ptr_type> replacement_property;
	};

	typedef std::vector<ExistingChange> existing_change_seq_type;

	struct NewSequence
	{
		GPlatesModel::integer_plate_id_type moving_plate;
		GPlatesModel::integer_plate_id_type fixed_plate;
		GPlatesModel::TopLevelProperty::non_null_ptr_type sampling_property;
	};


	double
	sample_time(
			const GPlatesPropertyValues::GpmlTimeSample &sample)
	{
		return sample.valid_time()->get_time_position().value();
	}


	bool
	sample_less_than(
			const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type &lhs,
			const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type &rhs)
	{
		return sample_time(*lhs) < sample_time(*rhs);
	}


	GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type
	create_rotation_sample(
			double time,
			const GPlatesMaths::FiniteRotation &rotation,
			const QString &comment)
	{
		using namespace GPlatesPropertyValues;
		const StructuralType value_type = StructuralType::create_gpml("FiniteRotation");
		return GpmlTimeSample::create(
				GpmlFiniteRotation::create(rotation),
				GmlTimeInstant::create(GeoTimeInstant(time)),
				XsString::create(GPlatesUtils::make_icu_string_from_qstring(comment)),
				value_type);
	}


	void
	mark_retirement_boundary(
			const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type &sample)
	{
		QString comment;
		if (sample->description())
		{
			comment = GPlatesUtils::make_qstring_from_icu_string(
					sample->description().get()->get_value().get());
		}
		const QString marker = QObject::tr("GreaterPlates: retired toward present");
		if (!comment.contains(marker))
		{
			if (!comment.isEmpty())
			{
				comment.append(QObject::tr(" | "));
			}
			comment.append(marker);
		}
		sample->set_description(GPlatesPropertyValues::XsString::create(
				GPlatesUtils::make_icu_string_from_qstring(comment)));
	}


	GPlatesModel::TopLevelProperty::non_null_ptr_type
	create_sampling_property(
			const sample_seq_type &samples)
	{
		using namespace GPlatesPropertyValues;
		const StructuralType value_type = StructuralType::create_gpml("FiniteRotation");
		const GpmlIrregularSampling::non_null_ptr_type sampling = GpmlIrregularSampling::create(
				samples,
				GpmlInterpolationFunction::non_null_ptr_type(GpmlFiniteRotationSlerp::create(value_type)),
				value_type);
		return GPlatesModel::TopLevelPropertyInline::create(
				GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"), sampling);
	}


	sequence_seq_type
	collect_sequences(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		sequence_seq_type sequences;
		if (!collection.is_valid())
		{
			return sequences;
		}

		for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
				feature_iter != collection->end(); ++feature_iter)
		{
			GPlatesModel::FeatureHandle::weak_ref feature = (*feature_iter)->reference();
			GPlatesAppLogic::TRSUtils::TRSFinder finder;
			finder.visit_feature(feature);
			if (!finder.can_process_trs())
			{
				continue;
			}

			const GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling =
					*finder.irregular_sampling();
			if (sampling->time_samples().empty())
			{
				continue;
			}

			double minimum_time = 1e100;
			double maximum_time = -1e100;
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator sample_iter =
					sampling->time_samples().begin(); sample_iter != sampling->time_samples().end(); ++sample_iter)
			{
				if (sample_iter->is_disabled() ||
						!sample_iter->valid_time()->get_time_position().is_real())
				{
					continue;
				}
				const double time = sample_iter->valid_time()->get_time_position().value();
				minimum_time = std::min(minimum_time, time);
				maximum_time = std::max(maximum_time, time);
			}
			if (maximum_time < minimum_time)
			{
				continue;
			}

			RotationSequence sequence = {
				feature,
				*finder.irregular_sampling_property_iterator(),
				sampling,
				*finder.moving_ref_frame_plate_id(),
				*finder.fixed_ref_frame_plate_id(),
				minimum_time,
				maximum_time
			};
			sequences.push_back(sequence);
		}
		return sequences;
	}


	std::set<GPlatesModel::integer_plate_id_type>
	collect_plate_ids(
			const sequence_seq_type &sequences)
	{
		std::set<GPlatesModel::integer_plate_id_type> plate_ids;
		for (sequence_seq_type::const_iterator sequence_iter = sequences.begin();
				sequence_iter != sequences.end(); ++sequence_iter)
		{
			plate_ids.insert(sequence_iter->moving_plate);
			plate_ids.insert(sequence_iter->fixed_plate);
		}
		return plate_ids;
	}


	GPlatesMaths::FiniteRotation
	relative_rotation(
			const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
			double time,
			GPlatesModel::integer_plate_id_type moving_plate,
			GPlatesModel::integer_plate_id_type fixed_plate)
	{
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(time);
		const boost::optional<GPlatesMaths::FiniteRotation> moving_absolute =
				tree->get_composed_absolute_rotation_or_none(moving_plate);
		const boost::optional<GPlatesMaths::FiniteRotation> fixed_absolute =
				tree->get_composed_absolute_rotation_or_none(fixed_plate);
		if (!moving_absolute || !fixed_absolute)
		{
			throw std::runtime_error(
					QObject::tr("Plate %1 or parent %2 is absent from the rotation tree at %3 Ma.")
							.arg(moving_plate).arg(fixed_plate).arg(time, 0, 'f', 2).toStdString());
		}
		return GPlatesMaths::compose(
				GPlatesMaths::get_reverse(*fixed_absolute), *moving_absolute);
	}


	std::vector<double>
	build_sample_times(
			double current_time,
			double youngest_time,
			double oldest_time,
			TimeDirection direction,
			const sequence_seq_type &all_sequences)
	{
		const double begin_time = direction == TOWARD_PRESENT ? youngest_time : current_time;
		const double end_time = direction == TOWARD_PRESENT ? current_time : oldest_time;
		std::set<double> times;
		times.insert(begin_time);
		times.insert(end_time);
		for (double time = begin_time; time < end_time; time += 5.0)
		{
			times.insert(std::min(time, end_time));
		}
		for (sequence_seq_type::const_iterator sequence_iter = all_sequences.begin();
				sequence_iter != all_sequences.end(); ++sequence_iter)
		{
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator sample_iter =
					sequence_iter->sampling->time_samples().begin();
					sample_iter != sequence_iter->sampling->time_samples().end(); ++sample_iter)
			{
				if (!sample_iter->is_disabled() &&
						sample_iter->valid_time()->get_time_position().is_real())
				{
					const double time = sample_iter->valid_time()->get_time_position().value();
					if (time > begin_time && time < end_time)
					{
						times.insert(time);
					}
				}
			}
		}
		return std::vector<double>(times.begin(), times.end());
	}


	bool
	would_create_cycle(
			const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
			double current_time,
			GPlatesModel::integer_plate_id_type moving_plate,
			GPlatesModel::integer_plate_id_type proposed_parent)
	{
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(current_time);
		boost::optional<const GPlatesAppLogic::ReconstructionTree::Edge &> edge = tree->get_edge(proposed_parent);
		while (edge)
		{
			if (edge->get_fixed_plate() == moving_plate || edge->get_moving_plate() == moving_plate)
			{
				return true;
			}
			const GPlatesAppLogic::ReconstructionTree::Edge *parent = edge->get_parent_edge();
			if (!parent)
			{
				break;
			}
			edge = *parent;
		}
		return false;
	}


	class RotationEditUndoCommand :
			public QUndoCommand
	{
	public:
		RotationEditUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const existing_change_seq_type &existing_changes,
				const boost::optional<NewSequence> &new_sequence,
				const QString &description) :
			d_model_interface(model_interface),
			d_collection(collection),
			d_existing_changes(existing_changes),
			d_new_sequence(new_sequence),
			d_first_redo(true)
		{
			setText(description);
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (existing_change_seq_type::iterator change_iter = d_existing_changes.begin();
					change_iter != d_existing_changes.end(); ++change_iter)
			{
				if (!change_iter->feature.is_valid())
				{
					continue;
				}
				if (change_iter->replacement_property)
				{
					if (!change_iter->feature->parent_ptr())
					{
						change_iter->collection->add(GPlatesModel::FeatureHandle::non_null_ptr_type(
								change_iter->feature.handle_ptr()));
					}
					change_iter->feature->set(change_iter->property, *change_iter->replacement_property);
				}
				else if (change_iter->feature->parent_ptr())
				{
					change_iter->feature->remove_from_parent();
				}
			}

			if (d_new_sequence && d_first_redo)
			{
				GPlatesModel::FeatureHandle::weak_ref feature = GPlatesModel::FeatureHandle::create(
						d_collection, GPlatesModel::FeatureType::create_gpml("TotalReconstructionSequence"));
				feature->add(GPlatesModel::TopLevelPropertyInline::create(
						GPlatesModel::PropertyName::create_gpml("fixedReferenceFrame"),
						GPlatesPropertyValues::GpmlPlateId::create(d_new_sequence->fixed_plate)));
				feature->add(GPlatesModel::TopLevelPropertyInline::create(
						GPlatesModel::PropertyName::create_gpml("movingReferenceFrame"),
						GPlatesPropertyValues::GpmlPlateId::create(d_new_sequence->moving_plate)));
				feature->add(d_new_sequence->sampling_property);
				d_created_feature = GPlatesModel::FeatureHandle::non_null_ptr_type(feature.handle_ptr());
				d_first_redo = false;
			}
			else if (d_new_sequence && d_created_feature && !(*d_created_feature)->parent_ptr())
			{
				d_collection->add(*d_created_feature);
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_created_feature && (*d_created_feature)->parent_ptr())
			{
				(*d_created_feature)->remove_from_parent();
			}
			for (existing_change_seq_type::iterator change_iter = d_existing_changes.begin();
					change_iter != d_existing_changes.end(); ++change_iter)
			{
				if (!change_iter->feature.is_valid() || !change_iter->collection.is_valid())
				{
					continue;
				}
				if (!change_iter->feature->parent_ptr())
				{
					change_iter->collection->add(GPlatesModel::FeatureHandle::non_null_ptr_type(
							change_iter->feature.handle_ptr()));
				}
				change_iter->feature->set(change_iter->property, change_iter->original_property);
			}
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		existing_change_seq_type d_existing_changes;
		boost::optional<NewSequence> d_new_sequence;
		boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> d_created_feature;
		bool d_first_redo;
	};
}


GPlatesViewOperations::RotationFileEditorOperation::RotationFileEditorOperation(
		GPlatesAppLogic::ApplicationState &application_state) :
	d_application_state(application_state),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::RotationFileEditorOperation::Result
GPlatesViewOperations::RotationFileEditorOperation::trigger(
		QWidget *parent)
{
	GPlatesAppLogic::FeatureCollectionFileState &file_state =
			d_application_state.get_feature_collection_file_state();
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded_files =
			file_state.get_loaded_files();
	std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> rotation_files;
	for (size_t file_index = 0; file_index < loaded_files.size(); ++file_index)
	{
		const QString suffix = loaded_files[file_index].get_file().get_file_info()
				.get_qfileinfo().suffix().toLower();
		if (suffix == "rot" || suffix == "grot" ||
				!collect_sequences(loaded_files[file_index].get_file().get_feature_collection()).empty())
		{
			rotation_files.push_back(loaded_files[file_index]);
		}
	}
	if (rotation_files.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Load a .rot file or a feature collection containing rotation sequences first."));
	}

	const double current_time = d_application_state.get_current_reconstruction().get_reconstruction_time();
	const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
			d_application_state.get_current_reconstruction()
					.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
	const GPlatesModel::integer_plate_id_type anchor_plate = tree_creator.get_default_anchor_plate_id();

	QDialog dialog(parent);
	dialog.setWindowTitle(QObject::tr("Rotation File Editor"));
	dialog.setModal(true);
	dialog.resize(720, dialog.sizeHint().height());
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(
			QObject::tr(
					"Edit plate connections at %1 Ma. Re-parenting writes replacement total rotations "
					"that preserve the plate's existing absolute motion. Disconnect uses anchor plate %2. "
					"Retire truncates the selected plate's active rotation history toward the present.")
					.arg(current_time, 0, 'f', 2).arg(anchor_plate), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	QFormLayout *form = new QFormLayout();
	QComboBox *file_combo = new QComboBox(&dialog);
	for (size_t file_index = 0; file_index < rotation_files.size(); ++file_index)
	{
		QString name = rotation_files[file_index].get_file().get_file_info().get_display_name(false);
		if (name.isEmpty())
		{
			name = QObject::tr("Untitled feature collection %1").arg(file_index + 1);
		}
		file_combo->addItem(name, static_cast<int>(file_index));
	}
	QComboBox *mode_combo = new QComboBox(&dialog);
	mode_combo->addItem(QObject::tr("Connect / re-parent plate"), CONNECT_PLATE);
	mode_combo->addItem(QObject::tr("Disconnect plate (parent to anchor)"), DISCONNECT_PLATE);
	mode_combo->addItem(QObject::tr("Create a new plate"), CREATE_PLATE);
	mode_combo->addItem(QObject::tr("Retire plate toward the present"), RETIRE_PLATE);
	QSpinBox *moving_plate_spin = new QSpinBox(&dialog);
	QSpinBox *parent_plate_spin = new QSpinBox(&dialog);
	moving_plate_spin->setRange(0, 99999999);
	parent_plate_spin->setRange(0, 99999999);
	parent_plate_spin->setValue(anchor_plate);
	QComboBox *direction_combo = new QComboBox(&dialog);
	direction_combo->addItem(QObject::tr("From current time toward older times"), TOWARD_OLDER);
	direction_combo->addItem(QObject::tr("From current time toward the present"), TOWARD_PRESENT);
	form->addRow(QObject::tr("Rotation collection:"), file_combo);
	form->addRow(QObject::tr("Action:"), mode_combo);
	form->addRow(QObject::tr("Plate ID:"), moving_plate_spin);
	form->addRow(QObject::tr("Parent plate ID:"), parent_plate_spin);
	form->addRow(QObject::tr("Effective interval:"), direction_combo);
	layout->addLayout(form);

	QLabel *note = new QLabel(
			QObject::tr(
					"Connect and disconnect split any overlapping old parent sequences at the current "
					"time and add a new sequence sampled at existing pole times and at most 5 My apart. "
					"A new plate starts coincident with and attached to its selected parent. Retire keeps "
					"older samples, removes active history younger than the current time, and marks the "
					"boundary using an ordinary rotation-pole comment."), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Apply"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("Rotation File Editor closed without changes."));
	}

	const size_t selected_file_index = static_cast<size_t>(file_combo->currentData().toInt());
	const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
			rotation_files[selected_file_index].get_file().get_feature_collection();
	const sequence_seq_type sequences = collect_sequences(collection);
	const std::set<GPlatesModel::integer_plate_id_type> plate_ids = collect_plate_ids(sequences);
	std::set<GPlatesModel::integer_plate_id_type> all_loaded_plate_ids;
	sequence_seq_type all_loaded_sequences;
	for (size_t file_index = 0; file_index < rotation_files.size(); ++file_index)
	{
		const sequence_seq_type file_sequences = collect_sequences(
				rotation_files[file_index].get_file().get_feature_collection());
		const std::set<GPlatesModel::integer_plate_id_type> file_plate_ids = collect_plate_ids(file_sequences);
		all_loaded_plate_ids.insert(file_plate_ids.begin(), file_plate_ids.end());
		all_loaded_sequences.insert(
				all_loaded_sequences.end(), file_sequences.begin(), file_sequences.end());
	}
	const EditMode mode = static_cast<EditMode>(mode_combo->currentData().toInt());
	const TimeDirection direction = static_cast<TimeDirection>(direction_combo->currentData().toInt());
	const GPlatesModel::integer_plate_id_type moving_plate = moving_plate_spin->value();
	const GPlatesModel::integer_plate_id_type fixed_plate =
			mode == DISCONNECT_PLATE ? anchor_plate : parent_plate_spin->value();

	if (mode != RETIRE_PLATE && moving_plate == fixed_plate)
	{
		return Result(OPERATION_ERROR, QObject::tr("A plate cannot be its own parent."));
	}
	if (mode == CREATE_PLATE && all_loaded_plate_ids.count(moving_plate))
	{
		return Result(OPERATION_ERROR, QObject::tr("Plate %1 already exists in a loaded rotation collection.").arg(moving_plate));
	}
	if (mode != CREATE_PLATE && !plate_ids.count(moving_plate))
	{
		return Result(OPERATION_ERROR, QObject::tr("Plate %1 does not exist in the selected rotation collection.").arg(moving_plate));
	}
	if ((mode == RETIRE_PLATE || direction == TOWARD_PRESENT) && current_time <= 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("At 0 Ma there is no younger interval toward the present to edit or retire."));
	}

	double youngest_time = mode == CREATE_PLATE ? 0.0 : current_time;
	double oldest_time = mode == CREATE_PLATE ? current_time + 100.0 : current_time;
	for (sequence_seq_type::const_iterator sequence_iter = sequences.begin();
			sequence_iter != sequences.end(); ++sequence_iter)
	{
		if (mode == CREATE_PLATE || sequence_iter->moving_plate == moving_plate)
		{
			youngest_time = std::min(youngest_time, sequence_iter->minimum_time);
			oldest_time = std::max(oldest_time, sequence_iter->maximum_time);
		}
	}
	if (mode != CREATE_PLATE && mode != RETIRE_PLATE &&
			direction == TOWARD_OLDER && oldest_time <= current_time + 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Plate %1 has no rotation history older than %2 Ma in the selected collection.")
						.arg(moving_plate).arg(current_time, 0, 'f', 2));
	}
	if (mode != CREATE_PLATE && mode != RETIRE_PLATE &&
			direction == TOWARD_PRESENT && youngest_time >= current_time - 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Plate %1 has no rotation history younger than %2 Ma in the selected collection.")
						.arg(moving_plate).arg(current_time, 0, 'f', 2));
	}
	if (mode == RETIRE_PLATE && youngest_time >= current_time - 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Plate %1 has no active rotation history younger than %2 Ma to retire.")
						.arg(moving_plate).arg(current_time, 0, 'f', 2));
	}

	std::vector<double> new_sequence_times;
	if (mode != RETIRE_PLATE)
	{
		new_sequence_times = build_sample_times(
				current_time, youngest_time, oldest_time, direction, all_loaded_sequences);
	}
	std::vector<double> topology_check_times(new_sequence_times);
	if (mode == RETIRE_PLATE)
	{
		topology_check_times.push_back(current_time);
	}
	for (std::vector<double>::const_iterator time_iter = new_sequence_times.begin();
			time_iter != new_sequence_times.end() && time_iter + 1 != new_sequence_times.end(); ++time_iter)
	{
		topology_check_times.push_back((*time_iter + *(time_iter + 1)) * 0.5);
	}
	for (std::vector<double>::const_iterator time_iter = topology_check_times.begin();
			time_iter != topology_check_times.end(); ++time_iter)
	{
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type tree =
				tree_creator.get_reconstruction_tree(*time_iter);
		if (mode != RETIRE_PLATE && !tree->get_composed_absolute_rotation_or_none(fixed_plate))
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Parent plate %1 is absent from the rotation tree at %2 Ma.")
							.arg(fixed_plate).arg(*time_iter, 0, 'f', 2));
		}
		if (mode != CREATE_PLATE &&
				!tree->get_composed_absolute_rotation_or_none(moving_plate))
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Plate %1 is absent from the rotation tree at %2 Ma.")
							.arg(moving_plate).arg(*time_iter, 0, 'f', 2));
		}
		if (mode == CONNECT_PLATE &&
				would_create_cycle(tree_creator, *time_iter, moving_plate, fixed_plate))
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That parent choice would create a plate-circuit cycle at %1 Ma.")
							.arg(*time_iter, 0, 'f', 2));
		}
	}

	existing_change_seq_type changes;
	if (mode != CREATE_PLATE)
	{
		for (sequence_seq_type::const_iterator sequence_iter = sequences.begin();
				sequence_iter != sequences.end(); ++sequence_iter)
		{
			if (sequence_iter->moving_plate != moving_plate)
			{
				continue;
			}
			const bool overlaps = mode == RETIRE_PLATE
					? sequence_iter->minimum_time < current_time - 1e-9
					: direction == TOWARD_PRESENT
					? sequence_iter->minimum_time < current_time - 1e-9
					: sequence_iter->maximum_time > current_time + 1e-9;
			if (!overlaps)
			{
				continue;
			}

			sample_seq_type retained_samples;
			for (GPlatesModel::RevisionedVector<GPlatesPropertyValues::GpmlTimeSample>::const_iterator sample_iter =
					sequence_iter->sampling->time_samples().begin();
					sample_iter != sequence_iter->sampling->time_samples().end(); ++sample_iter)
			{
				const double time = sample_iter->valid_time()->get_time_position().value();
				const bool retain = mode == RETIRE_PLATE
						? time >= current_time - 1e-9
						: direction == TOWARD_PRESENT
						? time >= current_time - 1e-9
						: time <= current_time + 1e-9;
				if (retain)
				{
					const GPlatesPropertyValues::GpmlTimeSample::non_null_ptr_type retained_sample =
							sample_iter->clone();
					if (mode == RETIRE_PLATE && !retained_sample->is_disabled() &&
							std::fabs(time - current_time) < 1e-9)
					{
						mark_retirement_boundary(retained_sample);
					}
					retained_samples.push_back(retained_sample);
				}
			}

			// The old parent and new parent meet at an identical boundary rotation.
			bool has_boundary_sample = false;
			for (sample_seq_type::const_iterator sample_iter = retained_samples.begin();
					sample_iter != retained_samples.end(); ++sample_iter)
			{
				has_boundary_sample = has_boundary_sample ||
						(!(*sample_iter)->is_disabled() &&
						std::fabs(sample_time(**sample_iter) - current_time) < 1e-9);
			}
			if (!retained_samples.empty() && !has_boundary_sample)
			{
				retained_samples.push_back(create_rotation_sample(
						current_time,
						relative_rotation(tree_creator, current_time, moving_plate, sequence_iter->fixed_plate),
						mode == RETIRE_PLATE
								? QObject::tr("GreaterPlates: retired toward present")
								: QObject::tr("GreaterPlates: old parent boundary")));
			}
			std::sort(retained_samples.begin(), retained_samples.end(), sample_less_than);

			ExistingChange change = {
				sequence_iter->feature,
				sequence_iter->feature->parent_ptr()->reference(),
				sequence_iter->sampling_property,
				(*sequence_iter->sampling_property)->clone(),
				boost::none
			};
			if (!retained_samples.empty())
			{
				change.replacement_property = create_sampling_property(retained_samples);
			}
			changes.push_back(change);
		}
	}

	boost::optional<NewSequence> new_sequence;
	if (mode != RETIRE_PLATE)
	{
		sample_seq_type new_samples;
		if (mode == CREATE_PLATE)
		{
			const double other_end = direction == TOWARD_PRESENT ? 0.0 : oldest_time;
			new_samples.push_back(create_rotation_sample(
					std::min(current_time, other_end),
					GPlatesMaths::FiniteRotation::create_identity_rotation(),
					QObject::tr("GreaterPlates: new plate")));
			new_samples.push_back(create_rotation_sample(
					std::max(current_time, other_end),
					GPlatesMaths::FiniteRotation::create_identity_rotation(),
					QObject::tr("GreaterPlates: new plate")));
		}
		else
		{
			try
			{
				for (std::vector<double>::const_iterator time_iter = new_sequence_times.begin();
						time_iter != new_sequence_times.end(); ++time_iter)
				{
					new_samples.push_back(create_rotation_sample(
							*time_iter,
							relative_rotation(tree_creator, *time_iter, moving_plate, fixed_plate),
							mode == CONNECT_PLATE
									? QObject::tr("GreaterPlates: connect plate")
									: QObject::tr("GreaterPlates: disconnect plate")));
				}
			}
			catch (const std::exception &exception)
			{
				return Result(OPERATION_ERROR, QString::fromUtf8(exception.what()));
			}
		}
		std::sort(new_samples.begin(), new_samples.end(), sample_less_than);
		new_sequence = NewSequence{ moving_plate, fixed_plate, create_sampling_property(new_samples) };
	}

	if (mode == RETIRE_PLATE)
	{
		if (changes.empty())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("No rotation sequences for Plate %1 overlap the retirement time.")
							.arg(moving_plate));
		}
		const QMessageBox::StandardButton confirmation = QMessageBox::question(
				parent,
				QObject::tr("Retire Plate in Rotation History"),
				QObject::tr(
						"Retire Plate %1 at %2 Ma toward the present?\n\n"
						"This will update %3 rotation sequence(s), remove their active samples younger "
						"than %2 Ma, and mark the boundary with a standard pole comment. The operation "
						"is one undo step.")
						.arg(moving_plate).arg(current_time, 0, 'f', 2).arg(changes.size()),
				QMessageBox::Yes | QMessageBox::Cancel,
				QMessageBox::Cancel);
		if (confirmation != QMessageBox::Yes)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Plate retirement cancelled without changes."));
		}
	}

	std::unique_ptr<QUndoCommand> command(new RotationEditUndoCommand(
			d_model_interface,
			collection,
			changes,
			new_sequence,
			mode == CONNECT_PLATE ? QObject::tr("connect plate") :
					mode == DISCONNECT_PLATE ? QObject::tr("disconnect plate") :
					mode == RETIRE_PLATE ? QObject::tr("retire plate") : QObject::tr("create plate")));
	UndoRedo::instance().get_active_undo_stack().push(command.release());

	return Result(
			OPERATION_COMPLETED,
			mode == CREATE_PLATE
					? QObject::tr("Plate %1 created with parent %2 at %3 Ma. Use Edit > Undo to remove it.")
							.arg(moving_plate).arg(fixed_plate).arg(current_time, 0, 'f', 2)
					: mode == RETIRE_PLATE
					? QObject::tr("Plate %1 is retired toward the present at %2 Ma in %3 rotation sequence(s). Use Edit > Undo to restore its younger history.")
							.arg(moving_plate).arg(current_time, 0, 'f', 2).arg(changes.size())
					: QObject::tr("Plate %1 is now parented to plate %2 at %3 Ma without an absolute-motion jump. Use Edit > Undo to restore the old circuit.")
							.arg(moving_plate).arg(fixed_plate).arg(current_time, 0, 'f', 2));
}
