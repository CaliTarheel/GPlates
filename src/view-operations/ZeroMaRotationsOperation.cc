/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
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
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <vector>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ZeroMaRotationsOperation.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/TRSUtils.h"

#include "file-io/File.h"

#include "model/FeatureHandle.h"
#include "model/FeatureVisitor.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlTimeSample.h"

namespace
{
	typedef GPlatesPropertyValues::GpmlTimeSample Sample;
	typedef std::vector<Sample::non_null_ptr_type> Samples;
	typedef GPlatesPropertyValues::GpmlFiniteRotation Rotation;

	struct Sequence
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling;
		GPlatesModel::integer_plate_id_type fixed_plate;
		bool has_zero;
		bool crosses_zero;
	};

	struct Candidate
	{
		size_t sequence;
		const Sample *sample;
	};

	struct Change
	{
		GPlatesModel::integer_plate_id_type moving_plate;
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};

	struct PlannedPlate
	{
		GPlatesModel::integer_plate_id_type fixed_plate;
		bool added_zero;
		unsigned int replaced_zeros;
	};

	typedef std::map<GPlatesModel::integer_plate_id_type,
			std::set<GPlatesModel::integer_plate_id_type>> ParentLinks;


	bool
	reaches_cycle(
			GPlatesModel::integer_plate_id_type plate,
			const ParentLinks &parents,
			std::set<GPlatesModel::integer_plate_id_type> &path,
			std::set<GPlatesModel::integer_plate_id_type> &complete)
	{
		if (path.count(plate))
		{
			return true;
		}
		if (complete.count(plate))
		{
			return false;
		}
		path.insert(plate);
		const auto links = parents.find(plate);
		if (links != parents.end())
		{
			for (const auto parent : links->second)
			{
				if (reaches_cycle(parent, parents, path, complete))
				{
					return true;
				}
			}
		}
		path.erase(plate);
		complete.insert(plate);
		return false;
	}


	bool
	finite_time(
			const Sample &sample)
	{
		const auto &time = sample.valid_time()->get_time_position();
		return time.is_real() && std::isfinite(time.value());
	}


	bool
	enabled_zero(
			const Sample &sample)
	{
		return !sample.is_disabled() && finite_time(sample) &&
				sample.valid_time()->get_time_position().value() == 0.0;
	}


	// Editing a clone through a visitor preserves wrappers, interpolation and XML attributes.
	class ReplaceSamplesVisitor :
			public GPlatesModel::FeatureVisitor
	{
	public:
		explicit
		ReplaceSamplesVisitor(
				const Samples &samples) :
			d_samples(samples)
		{  }

		void
		visit_gpml_constant_value(
				GPlatesPropertyValues::GpmlConstantValue &value) override
		{
			value.value()->accept_visitor(*this);
		}

		void
		visit_gpml_irregular_sampling(
				GPlatesPropertyValues::GpmlIrregularSampling &sampling) override
		{
			sampling.time_samples().assign(d_samples.begin(), d_samples.end());
		}

	private:
		const Samples &d_samples;
	};


	class ZeroMaUndoCommand :
			public QUndoCommand
	{
	public:
		ZeroMaUndoCommand(
				GPlatesModel::ModelInterface model,
				const std::vector<Change> &changes) :
			d_model(model), d_changes(changes)
		{
			setText(QObject::tr("copy youngest rotations to 0 Ma"));
		}

		void
		redo() override
		{
			apply(true);
		}

		void
		undo() override
		{
			apply(false);
		}

	private:
		void
		apply(
				bool after)
		{
			for (const auto &change : d_changes)
			{
				if (!change.feature.is_valid() || !change.property.is_still_valid())
				{
					return;
				}
			}
			GPlatesModel::NotificationGuard guard(*d_model.access_model());
			for (const auto &change : d_changes)
			{
				// FeatureHandle stores this pointer, so never attach an undo snapshot directly.
				change.feature->set(change.property, (after ? change.after : change.before)->clone());
			}
		}

		GPlatesModel::ModelInterface d_model;
		std::vector<Change> d_changes;
	};
}


std::unique_ptr<QUndoCommand>
GPlatesViewOperations::ZeroMaRotationsOperation::create_undo_command(
		GPlatesModel::ModelInterface model_interface,
		const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
		Summary &summary)
{
	summary = Summary();
	if (!collection.is_valid())
	{
		return {};
	}
	std::vector<Sequence> sequences;
	std::map<GPlatesModel::integer_plate_id_type, std::vector<size_t>> plates;
	for (auto feature_iter = collection->begin(); feature_iter != collection->end(); ++feature_iter)
	{
		const auto feature = (*feature_iter)->reference();
		GPlatesAppLogic::TRSUtils::TRSFinder finder;
		finder.visit_feature(feature);
		if (!finder.can_process_trs() || (*finder.irregular_sampling())->is_disabled() ||
				*finder.moving_ref_frame_plate_id() == 999)
		{
			continue;
		}
		const auto sampling = *finder.irregular_sampling();
		bool has_zero = false;
		bool has_negative = false;
		bool has_positive = false;
		for (const auto &sample : sampling->time_samples())
		{
			has_zero = has_zero || enabled_zero(*sample);
			if (!sample->is_disabled() && finite_time(*sample))
			{
				const double time = sample->valid_time()->get_time_position().value();
				has_negative = has_negative || time < 0;
				has_positive = has_positive || time > 0;
			}
		}
		plates[*finder.moving_ref_frame_plate_id()].push_back(sequences.size());
		sequences.push_back({ feature, *finder.irregular_sampling_property_iterator(),
				sampling, *finder.fixed_ref_frame_plate_id(), has_zero, has_negative && has_positive });
	}

	std::vector<Change> changes;
	std::map<GPlatesModel::integer_plate_id_type, PlannedPlate> plans;
	ParentLinks existing_parents;
	for (const auto &plate : plates)
	{
		double youngest = std::numeric_limits<double>::infinity();
		std::vector<Candidate> candidates;
		const Sample *existing_zero = nullptr;
		for (size_t sequence_index : plate.second)
		{
			if ((sequences[sequence_index].has_zero || sequences[sequence_index].crosses_zero) &&
					sequences[sequence_index].fixed_plate != plate.first)
			{
				existing_parents[plate.first].insert(sequences[sequence_index].fixed_plate);
			}
			for (const auto &sample : sequences[sequence_index].sampling->time_samples())
			{
				if (!existing_zero && enabled_zero(*sample))
				{
					existing_zero = &*sample;
				}
				if (sample->is_disabled() || !finite_time(*sample))
				{
					continue;
				}
				const double time = sample->valid_time()->get_time_position().value();
				if (time <= 0 || time > youngest)
				{
					continue;
				}
				if (time < youngest)
				{
					candidates.clear();
					youngest = time;
				}
				candidates.push_back({ sequence_index, &*sample });
			}
		}
		if (candidates.empty())
		{
			summary.skipped_plates.append(QObject::tr("Plate %1: no enabled positive-time pole.")
					.arg(plate.first));
			continue;
		}

		// At a crossover, the present branch is the one already reaching 0 Ma.
		std::vector<Candidate> present_candidates;
		for (const auto &candidate : candidates)
		{
			if (sequences[candidate.sequence].has_zero)
			{
				present_candidates.push_back(candidate);
			}
		}
		if (!present_candidates.empty())
		{
			candidates.swap(present_candidates);
		}
		const Candidate source = candidates.front();
		const auto &source_sequence = sequences[source.sequence];
		const Rotation *rotation = dynamic_cast<const Rotation *>(source.sample->value().get());
		bool ambiguous = !rotation || source_sequence.fixed_plate == plate.first;
		for (const auto &candidate : candidates)
		{
			const Rotation *other = dynamic_cast<const Rotation *>(candidate.sample->value().get());
			if (!other || !rotation ||
					sequences[candidate.sequence].fixed_plate != source_sequence.fixed_plate ||
					!(other->get_finite_rotation() == rotation->get_finite_rotation()))
			{
				ambiguous = true;
			}
		}
		if (ambiguous)
		{
			summary.skipped_plates.append(QObject::tr(
					"Plate %1: ambiguous or invalid youngest pole at %2 Ma.")
					.arg(plate.first).arg(youngest));
			continue;
		}
		const bool competing_future_sequence = std::any_of(
				plate.second.begin(), plate.second.end(), [&](size_t index)
				{
					return index != source.sequence && sequences[index].crosses_zero;
				});
		if (competing_future_sequence)
		{
			summary.skipped_plates.append(QObject::tr(
					"Plate %1: another sequence spans 0 Ma through negative ages.").arg(plate.first));
			continue;
		}

		// Preserve an existing present-day description. A new sample inherits the source's.
		const auto zero = (existing_zero ? existing_zero : source.sample)->clone();
		zero->set_valid_time(GPlatesPropertyValues::GmlTimeInstant::create(
				GPlatesPropertyValues::GeoTimeInstant(0.0)));
		zero->set_value(rotation->clone());
		zero->set_disabled(false);
		unsigned int replaced_zeros = 0;
		for (size_t sequence_index : plate.second)
		{
			const auto &sequence = sequences[sequence_index];
			if (sequence_index != source.sequence && !sequence.has_zero)
			{
				continue;
			}
			Samples replacement;
			for (const auto &sample : sequence.sampling->time_samples())
			{
				if (enabled_zero(*sample))
				{
					++replaced_zeros;
				}
				else
				{
					replacement.push_back(sample->clone());
				}
			}
			if (sequence_index == source.sequence)
			{
				const auto position = std::find_if(replacement.begin(), replacement.end(),
						[](const Sample::non_null_ptr_type &sample)
						{
							return finite_time(*sample) &&
									sample->valid_time()->get_time_position().value() >= 0;
						});
				replacement.insert(position, zero);
			}
			const auto before = (*sequence.property)->clone();
			const auto after = before->clone();
			ReplaceSamplesVisitor visitor(replacement);
			after->accept_visitor(visitor);
			changes.push_back({ plate.first, sequence.feature, sequence.property, before, after });
		}
		plans.emplace(plate.first, PlannedPlate{
				source_sequence.fixed_plate, !existing_zero, replaced_zeros });
	}

	// Independently selected source ages can form a circuit that never existed at any one age.
	// Include unchanged 0 Ma links; restoring a skipped plate can expose another such circuit.
	std::set<GPlatesModel::integer_plate_id_type> rejected;
	while (true)
	{
		ParentLinks parents = existing_parents;
		for (const auto &plan : plans)
		{
			if (!rejected.count(plan.first))
			{
				parents[plan.first] = { plan.second.fixed_plate };
			}
		}
		std::set<GPlatesModel::integer_plate_id_type> newly_rejected;
		for (const auto &plan : plans)
		{
			std::set<GPlatesModel::integer_plate_id_type> path, complete;
			if (!rejected.count(plan.first) && reaches_cycle(plan.first, parents, path, complete))
			{
				newly_rejected.insert(plan.first);
			}
		}
		if (newly_rejected.empty())
		{
			break;
		}
		rejected.insert(newly_rejected.begin(), newly_rejected.end());
	}
	for (const auto &plan : plans)
	{
		if (rejected.count(plan.first))
		{
			summary.skipped_plates.append(QObject::tr(
					"Plate %1: its proposed 0 Ma reference circuit contains a cycle.").arg(plan.first));
		}
		else
		{
			++summary.updated_plates;
			summary.added_zero_samples += plan.second.added_zero ? 1 : 0;
			summary.replaced_zero_samples += plan.second.replaced_zeros;
		}
	}
	changes.erase(std::remove_if(changes.begin(), changes.end(), [&rejected](const Change &change)
	{
		return rejected.count(change.moving_plate) != 0;
	}), changes.end());
	if (changes.empty())
	{
		return {};
	}
	return std::unique_ptr<QUndoCommand>(new ZeroMaUndoCommand(model_interface, changes));
}


GPlatesViewOperations::ZeroMaRotationsOperation::ZeroMaRotationsOperation(
		GPlatesAppLogic::ApplicationState &application_state) :
	d_application_state(application_state)
{  }


GPlatesViewOperations::ZeroMaRotationsOperation::Result
GPlatesViewOperations::ZeroMaRotationsOperation::trigger(
		QWidget *parent)
{
	const auto loaded_files =
			d_application_state.get_feature_collection_file_state().get_loaded_files();
	std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files;
	for (const auto &file : loaded_files)
	{
		const QString suffix = file.get_file().get_file_info().get_qfileinfo().suffix().toLower();
		if (suffix == "rot" || suffix == "grot")
		{
			files.push_back(file);
		}
	}
	if (files.empty())
	{
		return Result(OPERATION_ERROR, QObject::tr("Load a .rot or .grot rotation file first."));
	}
	QDialog dialog(parent);
	dialog.setObjectName("zeroMaRotationsDialog");
	dialog.setWindowTitle(QObject::tr("0MY — Copy youngest rotations to 0 Ma"));
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *description = new QLabel(QObject::tr(
			"For each moving plate, copy its youngest enabled pole at a positive age "
			"(closest to 0 Ma) to 0 Ma, "
			"including its reference plate. Existing 0 Ma poles are replaced; missing ones are added. "
			"Other ages and disabled poles stay unchanged. Save the selected file afterward."), &dialog);
	description->setWordWrap(true);
	layout->addWidget(description);
	QComboBox *file_combo = new QComboBox(&dialog);
	file_combo->setObjectName("zeroMaRotationFileCombo");
	for (const auto &file : files)
	{
		const QString path = file.get_file().get_file_info().get_qfileinfo().absoluteFilePath();
		file_combo->addItem(path);
		file_combo->setItemData(file_combo->count() - 1, path, Qt::ToolTipRole);
	}
	layout->addWidget(file_combo);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Apply"));
	buttons->button(QDialogButtonBox::Ok)->setObjectName("zeroMaRotationsApplyButton");
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	dialog.resize(560, dialog.sizeHint().height());
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("No rotations changed."));
	}
	Summary summary;
	auto command = create_undo_command(d_application_state.get_model_interface(),
			files[file_combo->currentIndex()].get_file().get_feature_collection(), summary);
	if (command)
	{
		UndoRedo::instance().get_active_undo_stack().push(command.release());
	}
	QString message = QObject::tr(
			"Updated %1 plate(s) at 0 Ma: %2 new sample(s), %3 existing sample(s) replaced. "
			"Skipped %4 plate(s). Use Edit > Undo to restore the changes; save the file to keep them.")
			.arg(summary.updated_plates).arg(summary.added_zero_samples)
			.arg(summary.replaced_zero_samples).arg(summary.skipped_plates.size());
	if (!summary.skipped_plates.empty())
	{
		message += "\n\n" + summary.skipped_plates.join("\n");
	}
	return Result(OPERATION_COMPLETED, message);
}
