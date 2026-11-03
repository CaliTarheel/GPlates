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
#include <stdexcept>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

#include "ReleasePlateOperation.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructedFlowline.h"
#include "app-logic/TRSUtils.h"

#include "file-io/File.h"

#include "gui/FeatureFocus.h"

#include "maths/FiniteRotation.h"
#include "maths/PointOnSphere.h"
#include "maths/Vector3D.h"

#include "model/FeatureVisitor.h"
#include "model/NotificationGuard.h"
#include "model/PropertyValueFinder.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"

#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

namespace
{
	typedef GPlatesModel::integer_plate_id_type Plate;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;
	typedef GPlatesPropertyValues::GpmlTimeSample Sample;
	typedef GPlatesPropertyValues::GpmlFiniteRotation Pole;
	typedef std::vector<Sample::non_null_ptr_type> Samples;
	typedef GPlatesMaths::FiniteRotation Rotation;
	typedef GPlatesAppLogic::ReconstructionTreeCreator TreeCreator;
	typedef GPlatesViewOperations::ReleasePlateOperation Operation;

	struct Sequence
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		Collection collection;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesPropertyValues::GpmlIrregularSampling::non_null_ptr_type sampling;
		Plate moving;
		Plate fixed;
		Samples enabled;
		QString error;
	};

	struct Change
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		Collection collection;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};


	void
	fail(
			const QString &message)
	{
		throw std::runtime_error(message.toUtf8().constData());
	}


	double
	age(
			const Sample &sample)
	{
		return sample.valid_time()->get_time_position().value();
	}


	const Rotation &
	rotation(
			const Sample &sample)
	{
		return dynamic_cast<const Pole &>(*sample.value()).get_finite_rotation();
	}


	bool
	same_rotation(
			const Rotation &first,
			const Rotation &second,
			double tolerance = 1e-10)
	{
		for (const auto &axis : {GPlatesMaths::UnitVector3D::xBasis(),
				GPlatesMaths::UnitVector3D::yBasis(), GPlatesMaths::UnitVector3D::zBasis()})
		{
			const auto difference = GPlatesMaths::Vector3D(first * axis) -
					GPlatesMaths::Vector3D(second * axis);
			if (difference.magSqrd().dval() > tolerance * tolerance)
			{
				return false;
			}
		}
		return true;
	}


	std::vector<Sequence>
	sequences_in(
			const std::vector<Collection> &collections)
	{
		std::vector<Sequence> result;
		for (const auto &collection : collections)
		{
			if (!collection.is_valid())
			{
				fail(QObject::tr("A rotation collection is no longer loaded."));
			}
			for (const auto &handle : *collection)
			{
				GPlatesAppLogic::TRSUtils::TRSFinder finder;
				finder.visit_feature(handle->reference());
				if (!finder.can_process_trs() || (*finder.irregular_sampling())->is_disabled() ||
						*finder.moving_ref_frame_plate_id() == 999)
				{
					continue;
				}
				Sequence sequence{handle->reference(), collection,
						*finder.irregular_sampling_property_iterator(), *finder.irregular_sampling(),
						*finder.moving_ref_frame_plate_id(), *finder.fixed_ref_frame_plate_id(), {}, {}};
				for (Sample::non_null_ptr_type sample : sequence.sampling->time_samples())
				{
					if (sample->is_disabled())
					{
						continue;
					}
					const auto &instant = sample->valid_time()->get_time_position();
					if (!instant.is_real() || !std::isfinite(instant.value()) ||
							instant.value() < 0 || !dynamic_cast<const Pole *>(sample->value().get()))
					{
						sequence.error = QObject::tr("Plate %1 has a non-finite, negative-age or "
								"invalid enabled rotation sample.").arg(sequence.moving);
						continue;
					}
					sequence.enabled.push_back(sample);
				}
				std::sort(sequence.enabled.begin(), sequence.enabled.end(),
						[](const Sample::non_null_ptr_type &a, const Sample::non_null_ptr_type &b)
						{
							return age(*a) < age(*b);
						});
				for (size_t index = 1; index < sequence.enabled.size(); ++index)
				{
					if (age(*sequence.enabled[index - 1]) == age(*sequence.enabled[index]))
					{
						sequence.error = QObject::tr("Plate %1 has duplicate enabled ages in a "
								"rotation sequence. Resolve those rows first.").arg(sequence.moving);
					}
				}
				result.push_back(sequence);
			}
		}
		return result;
	}


	size_t
	older_sequence(
			const std::vector<Sequence> &sequences,
			Plate plate,
			double time)
	{
		boost::optional<size_t> chosen;
		for (size_t index = 0; index < sequences.size(); ++index)
		{
			const auto &sequence = sequences[index];
			if (sequence.moving != plate)
			{
				continue;
			}
			if (!sequence.error.isEmpty())
			{
				fail(sequence.error);
			}
			if (sequence.enabled.size() >= 2 && age(*sequence.enabled.front()) <= time &&
					age(*sequence.enabled.back()) > time)
			{
				if (chosen)
				{
					fail(QObject::tr("Plate %1 has overlapping older rotation sequences at %2 Ma. "
							"Resolve the competing sequences before freeing it.").arg(plate).arg(time));
				}
				chosen = index;
			}
		}
		if (!chosen)
		{
			fail(QObject::tr("Plate %1 has no unambiguous rotation sequence spanning %2 Ma "
					"into the older past.").arg(plate).arg(time));
		}
		return *chosen;
	}


	Rotation
	interpolated_pole(
			const Sequence &sequence,
			double time)
	{
		for (size_t index = 0; index < sequence.enabled.size(); ++index)
		{
			if (age(*sequence.enabled[index]) == time)
			{
				return rotation(*sequence.enabled[index]);
			}
			if (age(*sequence.enabled[index]) > time && index > 0)
			{
				const auto &young = rotation(*sequence.enabled[index - 1]);
				const auto &old = rotation(*sequence.enabled[index]);
				return GPlatesMaths::interpolate(young, old, age(*sequence.enabled[index - 1]),
						age(*sequence.enabled[index]), time,
						young.axis_hint() ? young.axis_hint() : old.axis_hint());
			}
		}
		fail(QObject::tr("Cannot interpolate the release boundary."));
		return Rotation::create_identity_rotation();
	}


	Sample::non_null_ptr_type
	boundary_sample(
			const Sample &source,
			double time,
			const Rotation &value)
	{
		const auto result = source.clone();
		result->set_valid_time(GPlatesPropertyValues::GmlTimeInstant::create(
				GPlatesPropertyValues::GeoTimeInstant(time)));
		const auto pole = dynamic_cast<const Pole &>(*source.value()).clone();
		pole->set_finite_rotation(value);
		result->set_value(pole);
		result->set_disabled(false);
		return result;
	}


	class ReplaceSampling :
			public GPlatesModel::FeatureVisitor
	{
	public:
		ReplaceSampling(
				const Samples &samples,
				bool disabled) :
			d_samples(samples), d_disabled(disabled)
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
			sampling.set_disabled(d_disabled);
		}

	private:
		const Samples &d_samples;
		bool d_disabled;
	};


	GPlatesModel::TopLevelProperty::non_null_ptr_type
	replacement(
			const Sequence &sequence,
			const Samples &samples,
			bool disabled = false)
	{
		const auto property = (*sequence.property)->clone();
		ReplaceSampling visitor(samples, disabled);
		property->accept_visitor(visitor);
		return property;
	}


	GPlatesModel::FeatureHandle::non_null_ptr_type
	deep_clone(
			const GPlatesModel::FeatureHandle &feature)
	{
		const auto copy = GPlatesModel::FeatureHandle::create(feature.feature_type());
		for (const auto &property : feature)
		{
			copy->add(property->clone());
		}
		return copy;
	}


	class ReleaseUndoCommand :
			public QUndoCommand
	{
	public:
		ReleaseUndoCommand(
				GPlatesModel::ModelInterface model,
				const std::vector<Change> &changes,
				const Collection &destination,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &created,
				Plate plate,
				double time) :
			d_model(model), d_changes(changes), d_destination(destination), d_created(created)
		{
			setText(QObject::tr("free plate %1 at %2 Ma").arg(plate).arg(time));
			for (auto property = created->begin(); property != created->end(); ++property)
			{
				d_created_properties.push_back({property, (*property)->clone()});
			}
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
			if (!d_destination.is_valid())
			{
				return;
			}
			if (d_created->parent_ptr() &&
					d_created->parent_ptr() != d_destination.handle_ptr())
			{
				return;
			}
			for (const auto &change : d_changes)
			{
				if (!change.collection.is_valid() || !change.feature.is_valid() ||
						!change.property.is_still_valid() ||
						change.feature->parent_ptr() != change.collection.handle_ptr())
				{
					return;
				}
			}
			GPlatesModel::NotificationGuard guard(*d_model.access_model());
			for (const auto &change : d_changes)
			{
				change.feature->set(change.property, (after ? change.after : change.before)->clone());
			}
			if (after)
			{
				// remove_from_parent retains the parent pointer and deactivates the feature.
				// Reattach AND reactivate on redo; parent_ptr alone cannot establish membership.
				bool attached = false;
				for (const auto &feature : *d_destination)
				{
					attached = attached || feature.get() == d_created.get();
				}
				if (!attached)
				{
					d_destination->add(d_created);
				}
				d_created->set_active(true);
				for (auto property = d_created->begin(); property != d_created->end(); ++property)
				{
					const auto saved = std::find_if(d_created_properties.begin(),
							d_created_properties.end(), [&](const auto &entry)
							{
								return entry.first == property;
							});
					if (saved == d_created_properties.end())
					{
						d_created->remove(property);
					}
				}
				for (auto &property : d_created_properties)
				{
					if (property.first.is_still_valid())
					{
						d_created->set(property.first, property.second->clone());
					}
					else
					{
						property.first = d_created->add(property.second->clone());
					}
				}
			}
			else if (d_created->is_active())
			{
				d_created->remove_from_parent();
			}
		}

		GPlatesModel::ModelInterface d_model;
		std::vector<Change> d_changes;
		Collection d_destination;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_created;
		std::vector<std::pair<GPlatesModel::FeatureHandle::iterator,
				GPlatesModel::TopLevelProperty::non_null_ptr_type>> d_created_properties;
	};


	std::vector<GPlatesMaths::PointOnSphere>
	geometry_points(
			const GPlatesAppLogic::ReconstructedFeatureGeometry &geometry)
	{
		std::vector<GPlatesMaths::PointOnSphere> points;
		GPlatesAppLogic::GeometryUtils::get_geometry_points(*geometry.reconstructed_geometry(), points);
		const auto *flowline = dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(&geometry);
		if (flowline)
		{
			GPlatesAppLogic::GeometryUtils::get_geometry_points(*flowline->left_flowline_points(), points);
			GPlatesAppLogic::GeometryUtils::get_geometry_points(*flowline->right_flowline_points(), points);
		}
		return points;
	}


	void
	preflight_geometries(
			const std::vector<GPlatesModel::FeatureHandle::weak_ref> &features,
			const TreeCreator &before,
			const TreeCreator &after,
			double time)
	{
		GPlatesAppLogic::ReconstructMethodRegistry registry;
		std::set<const GPlatesModel::FeatureHandle *> visited;
		for (const auto &feature : features)
		{
			if (!feature.is_valid() || !visited.insert(feature.handle_ptr()).second)
			{
				continue;
			}
			const auto collection = GPlatesModel::FeatureCollectionHandle::create();
			collection->add(deep_clone(*feature));
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> old, proposed;
			GPlatesAppLogic::ReconstructUtils::reconstruct(old, time, registry,
					{collection->reference()}, before);
			GPlatesAppLogic::ReconstructUtils::reconstruct(proposed, time, registry,
					{collection->reference()}, after);
			bool changed = old.size() != proposed.size();
			for (size_t index = 0; !changed && index < old.size(); ++index)
			{
				const auto old_points = geometry_points(*old[index]);
				const auto new_points = geometry_points(*proposed[index]);
				changed = old_points.size() != new_points.size();
				for (size_t point = 0; !changed && point < old_points.size(); ++point)
				{
					const auto difference = GPlatesMaths::Vector3D(old_points[point].position_vector()) -
							GPlatesMaths::Vector3D(new_points[point].position_vector());
					changed = difference.magSqrd().dval() > 1e-18;
				}
			}
			if (changed)
			{
				const auto name = GPlatesModel::get_property_value<GPlatesPropertyValues::XsString>(
						feature, GPlatesModel::PropertyName::create_gml("name"));
				const QString identity = feature->feature_id().get().qstring();
				const QString label = name ? (*name)->get_value().get().qstring() +
						" (" + identity + ")" : identity;
				fail(QObject::tr("Feature %1 would move at %2 Ma. Its reconstruction depends on "
						"the younger rotation history (for example a half-stage ridge or flowline). "
						"Review that feature's import time and motion before freeing this plate; "
						"no rotations have been changed.")
						.arg(label).arg(time));
			}
		}
	}
}


std::unique_ptr<QUndoCommand>
GPlatesViewOperations::ReleasePlateOperation::create_undo_command(
		GPlatesModel::ModelInterface model,
		const std::vector<Collection> &rotations,
		Plate plate,
		double time,
		Summary &summary,
		const std::vector<GPlatesModel::FeatureHandle::weak_ref> &features,
		bool extend_to_distant_past)
{
	summary = Summary();
	try
	{
		if (plate == 0 || plate == 999 || !std::isfinite(time) || time <= 0)
		{
			fail(QObject::tr("Choose a moving plate other than 0 or 999 and a finite age above 0 Ma."));
		}
		const auto sequences = sequences_in(rotations);
		const auto before = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
				rotations, extend_to_distant_past, 0, 128);
		const auto event_tree = before.get_reconstruction_tree(time, 0);
		const auto hold = event_tree->get_composed_absolute_rotation_or_none(plate);
		if (!hold)
		{
			fail(QObject::tr("Plate %1 is not connected to Plate 0 at %2 Ma.").arg(plate).arg(time));
		}
		std::vector<size_t> chain;
		std::set<Plate> ancestors;
		Plate current = plate;
		while (current != 0)
		{
			if (!ancestors.insert(current).second)
			{
				fail(QObject::tr("The older plate circuit contains a cycle."));
			}
			const size_t index = older_sequence(sequences, current, time);
			chain.push_back(index);
			current = sequences[index].fixed;
		}
		const auto &source = sequences[chain.front()];
		summary.previous_parent = source.fixed;
		if (source.fixed == 0)
		{
			fail(QObject::tr("Plate %1 already moves independently relative to Plate 0 at this age.")
					.arg(plate));
		}
		Rotation composed = Rotation::create_identity_rotation();
		for (auto index = chain.rbegin(); index != chain.rend(); ++index)
		{
			composed = GPlatesMaths::compose(composed, interpolated_pole(sequences[*index], time));
		}
		if (!same_rotation(composed, *hold))
		{
			fail(QObject::tr("The older and current plate circuits disagree at the release age. "
					"Synchronise the existing crossover first."));
		}

		std::vector<Change> changes;
		for (size_t index = 0; index < sequences.size(); ++index)
		{
			const auto &sequence = sequences[index];
			const bool releasing = sequence.moving == plate;
			const bool pinning = std::find(chain.begin() + 1, chain.end(), index) != chain.end();
			if (!releasing && !pinning)
			{
				continue;
			}
			if (!sequence.error.isEmpty())
			{
				fail(sequence.error);
			}
			Samples samples;
			bool changed = false;
			bool has_boundary = false;
			unsigned int enabled = 0;
			for (Sample::non_null_ptr_type sample : sequence.sampling->time_samples())
			{
				const auto copy = sample->clone();
				if (!sample->is_disabled())
				{
					has_boundary = has_boundary || age(*sample) == time;
					if (releasing && age(*sample) < time)
					{
						copy->set_disabled(true);
						++summary.younger_samples;
						changed = true;
					}
					else
					{
						++enabled;
					}
				}
				samples.push_back(copy);
			}
			const bool add_boundary = !has_boundary &&
					(pinning || (index == chain.front()));
			if (add_boundary)
			{
				samples.push_back(boundary_sample(*sequence.enabled.front(), time,
						interpolated_pole(sequence, time)));
				++enabled;
				changed = true;
				summary.ancestor_pins += pinning ? 1 : 0;
			}
			if (!changed)
			{
				continue;
			}
			// Keep disabled records and all source metadata. The graph ignores disabled rows.
			// A remaining one-pole sequence cannot reconstruct and is explicitly disabled.
			std::stable_sort(samples.begin(), samples.end(),
					[](const Sample::non_null_ptr_type &a, const Sample::non_null_ptr_type &b)
					{
						return a->valid_time()->get_time_position().is_strictly_later_than(
								b->valid_time()->get_time_position());
					});
			changes.push_back({sequence.feature, sequence.collection, sequence.property,
					(*sequence.property)->clone(), replacement(sequence, samples, enabled < 2)});
			if (std::find(summary.changed_collections.begin(), summary.changed_collections.end(),
					sequence.collection) == summary.changed_collections.end())
			{
				summary.changed_collections.push_back(sequence.collection);
			}
		}

		const auto created = deep_clone(*source.feature);
		GPlatesAppLogic::TRSUtils::TRSFinder created_finder;
		created_finder.visit_feature(created->reference());
		created->set(*created_finder.fixed_ref_frame_property_iterator(),
				GPlatesModel::TopLevelPropertyInline::create(
						GPlatesModel::PropertyName::create_gpml("fixedReferenceFrame"),
						GPlatesPropertyValues::GpmlPlateId::create(0)));
		created->set(*created_finder.irregular_sampling_property_iterator(), replacement(source,
				{boundary_sample(*source.enabled.front(), 0, *hold),
						boundary_sample(*source.enabled.front(), time, *hold)}));
		if (std::find(summary.changed_collections.begin(), summary.changed_collections.end(),
				source.collection) == summary.changed_collections.end())
		{
			summary.changed_collections.push_back(source.collection);
		}

		// Rebuild a detached graph in exactly the input order. Never temporarily mutate live data.
		std::vector<GPlatesModel::FeatureCollectionHandle::non_null_ptr_type> preview_owners;
		std::vector<Collection> preview_collections;
		for (const auto &collection : rotations)
		{
			const auto preview = GPlatesModel::FeatureCollectionHandle::create();
			preview_owners.push_back(preview);
			preview_collections.push_back(preview->reference());
			for (const auto &feature : *collection)
			{
				const auto copy = deep_clone(*feature);
				for (const auto &change : changes)
				{
					if (change.feature.handle_ptr() == feature.get())
					{
						GPlatesAppLogic::TRSUtils::TRSFinder finder;
						finder.visit_feature(copy->reference());
						copy->set(*finder.irregular_sampling_property_iterator(), change.after->clone());
					}
				}
				preview->add(copy);
			}
			if (collection == source.collection)
			{
				preview->add(deep_clone(*created));
			}
		}
		const auto after = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
				preview_collections, extend_to_distant_past, 0, 128);
		std::set<double> check_times{time};
		std::set<Plate> check_plates{plate};
		for (const auto &sequence : sequences)
		{
			check_plates.insert(sequence.moving);
			for (const auto &sample : sequence.enabled)
			{
				if (age(*sample) >= time)
				{
					check_times.insert(age(*sample));
				}
			}
		}
		const std::vector<double> boundaries(check_times.begin(), check_times.end());
		for (size_t index = 1; index < boundaries.size(); ++index)
		{
			check_times.insert(boundaries[index - 1] +
					(boundaries[index] - boundaries[index - 1]) * 0.5);
		}
		for (double check_time : check_times)
		{
			const auto old = before.get_reconstruction_tree(check_time, 0);
			const auto proposed = after.get_reconstruction_tree(check_time, 0);
			for (Plate check_plate : check_plates)
			{
				const auto a = old->get_composed_absolute_rotation_or_none(check_plate);
				const auto b = proposed->get_composed_absolute_rotation_or_none(check_plate);
				if (static_cast<bool>(a) != static_cast<bool>(b) || (a && !same_rotation(*a, *b)))
				{
					fail(QObject::tr("The proposed release changes plate %1 at %2 Ma in the "
							"preserved history. Resolve overlapping rotation circuits first.")
							.arg(check_plate).arg(check_time));
				}
			}
		}
		for (double young : {0.0, time * 0.5, time})
		{
			const auto result = after.get_reconstruction_tree(young, 0)
					->get_composed_absolute_rotation_or_none(plate);
			if (!result || !same_rotation(*result, *hold))
			{
				fail(QObject::tr("Competing rotation circuits prevent the younger Plate-0 hold."));
			}
		}
		preflight_geometries(features, before, after, time);
		summary.review = QObject::tr(
				"Free plate %1 from plate %2 at %3 Ma.\n"
				"Its position will be held relative to Plate 0 from %3 Ma through 0 Ma.\n"
				"%4 younger enabled pole(s) will be retained as disabled records; their motion "
				"will be replaced.\n%5 ancestor boundary pole(s) will be added to protect older motion.\n"
				"Older rotations and descendant relative rotations are preserved.\n"
				"%6 rotation collection(s) will change. Save them normally afterward.\n\n"
				"A moving display anchor can still move the plate on screen. Rotation text files "
				"round saved poles, so a small numerical difference can remain after reload.")
				.arg(plate).arg(source.fixed).arg(time).arg(summary.younger_samples)
				.arg(summary.ancestor_pins).arg(summary.changed_collections.size());
		return std::unique_ptr<QUndoCommand>(new ReleaseUndoCommand(
				model, changes, source.collection, created, plate, time));
	}
	catch (const std::exception &error)
	{
		summary.error = QString::fromUtf8(error.what());
		return {};
	}
}


GPlatesViewOperations::ReleasePlateOperation::ReleasePlateOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state), d_view_state(view_state)
{  }


GPlatesViewOperations::ReleasePlateOperation::Result
GPlatesViewOperations::ReleasePlateOperation::trigger(
		QWidget *parent)
{
	std::vector<GPlatesAppLogic::ReconstructionLayerProxy::non_null_ptr_type> models;
	d_application_state.get_current_reconstruction()
			.get_active_layer_outputs<GPlatesAppLogic::ReconstructionLayerProxy>(models);
	if (models.empty())
	{
		return Result(OPERATION_ERROR, QObject::tr("Load a rotation model before freeing a plate."));
	}
	QDialog dialog(parent);
	dialog.setObjectName("freePlateDialog");
	dialog.setWindowTitle(QObject::tr("Free Plate"));
	auto *layout = new QVBoxLayout(&dialog);
	auto *explanation = new QLabel(QObject::tr(
			"Release a plate from its parent at the chosen age. Its current position at that age "
			"becomes an independent Plate-0 rotation, held through 0 Ma until you edit it."), &dialog);
	explanation->setWordWrap(true);
	layout->addWidget(explanation);
	auto *form = new QFormLayout();
	auto *model_combo = new QComboBox(&dialog);
	model_combo->setObjectName("freePlateModelCombo");
	for (const auto &model : models)
	{
		QStringList paths;
		for (const auto &collection : model->get_current_reconstruction_feature_collections())
		{
			for (const auto &file :
					d_application_state.get_feature_collection_file_state().get_loaded_files())
			{
				if (file.get_file().get_feature_collection() == collection)
				{
					const auto &info = file.get_file().get_file_info().get_qfileinfo();
					paths.push_back(info.filePath().isEmpty() ? QObject::tr("Unsaved rotations") :
							info.absoluteFilePath());
				}
			}
		}
		model_combo->addItem(paths.isEmpty() ? QObject::tr("Rotation model") : paths.join("; "));
		model_combo->setItemData(model_combo->count() - 1, paths.join("\n"), Qt::ToolTipRole);
	}
	auto *plate_spin = new QSpinBox(&dialog);
	plate_spin->setObjectName("freePlateId");
	plate_spin->setRange(1, std::numeric_limits<int>::max());
	auto *time_spin = new QDoubleSpinBox(&dialog);
	time_spin->setObjectName("freePlateTime");
	time_spin->setDecimals(6);
	time_spin->setRange(0.000001, 1000000000.0);
	time_spin->setValue(d_application_state.get_current_reconstruction_time());
	const auto focused = d_view_state.get_feature_focus().associated_reconstruction_geometry();
	if (focused)
	{
		const auto rfg = GPlatesAppLogic::ReconstructionGeometryUtils::
				get_reconstruction_geometry_derived_type<
						GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type>(focused);
		if (rfg)
		{
			if ((*rfg)->reconstruction_plate_id())
			{
				plate_spin->setValue(*(*rfg)->reconstruction_plate_id());
			}
			for (size_t index = 0; index < models.size(); ++index)
			{
				if (models[index]->get_reconstruction_tree()->get_reconstruction_graph() ==
						(*rfg)->get_reconstruction_tree()->get_reconstruction_graph())
				{
					model_combo->setCurrentIndex(static_cast<int>(index));
				}
			}
		}
	}
	form->addRow(QObject::tr("Rotation model:"), model_combo);
	form->addRow(QObject::tr("Moving plate:"), plate_spin);
	form->addRow(QObject::tr("Release age (Ma):"), time_spin);
	layout->addLayout(form);
	auto *review_button = new QPushButton(QObject::tr("Review changes"), &dialog);
	review_button->setObjectName("freePlateReviewButton");
	layout->addWidget(review_button);
	auto *review = new QTextEdit(&dialog);
	review->setObjectName("freePlateReview");
	review->setReadOnly(true);
	layout->addWidget(review);
	auto *replace_younger = new QCheckBox(QObject::tr(
			"Replace the reviewed younger motion with this Plate-0 hold"), &dialog);
	replace_younger->setObjectName("freePlateReplaceYounger");
	layout->addWidget(replace_younger);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, &dialog);
	auto *apply = buttons->button(QDialogButtonBox::Apply);
	apply->setText(QObject::tr("Free Plate"));
	apply->setObjectName("freePlateApplyButton");
	apply->setEnabled(false);
	layout->addWidget(buttons);
	bool reviewed = false;
	QString current_signature;
	QString reviewed_signature;
	Summary summary;
	std::unique_ptr<QUndoCommand> command;
	const auto prepare = [&]()
	{
		const auto model = models[model_combo->currentIndex()];
		std::vector<GPlatesModel::FeatureHandle::weak_ref> features;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		d_application_state.get_current_reconstruction()
				.get_active_layer_outputs<GPlatesAppLogic::ReconstructLayerProxy>(layers);
		for (const auto &layer : layers)
		{
			if (layer->get_current_reconstruction_layer_proxy()->get_reconstruction_tree()
					->get_reconstruction_graph() == model->get_reconstruction_tree()->get_reconstruction_graph())
			{
				layer->get_current_reconstructable_features(features);
			}
		}
		QStringList revisions;
		for (const auto &collection : model->get_current_reconstruction_feature_collections())
		{
			if (collection.is_valid())
			{
				for (const auto &feature : *collection)
				{
					revisions.push_back(feature->feature_id().get().qstring() + ":" +
							feature->revision_id().get().qstring());
				}
			}
		}
		for (const auto &feature : features)
		{
			if (feature.is_valid())
			{
				revisions.push_back(feature->feature_id().get().qstring() + ":" +
						feature->revision_id().get().qstring());
			}
		}
		current_signature = revisions.join(";");
		const bool extend_past = model->get_current_reconstruction_params()
				.get_extend_total_reconstruction_poles_to_distant_past();
		command = create_undo_command(d_application_state.get_model_interface(),
				model->get_current_reconstruction_feature_collections(), plate_spin->value(),
				time_spin->value(), summary, features, extend_past);
		for (const auto &collection : summary.changed_collections)
		{
			bool loaded = false;
			for (const auto &file :
					d_application_state.get_feature_collection_file_state().get_loaded_files())
			{
				if (file.get_file().get_feature_collection() == collection)
				{
					loaded = true;
					const auto &info = file.get_file().get_file_info().get_qfileinfo();
					if (!info.filePath().isEmpty() && (!info.isFile() || !info.isWritable()))
					{
						summary.error = QObject::tr("Rotation file is missing or read-only: %1")
								.arg(info.absoluteFilePath());
						command.reset();
					}
				}
			}
			if (!loaded)
			{
				summary.error = QObject::tr("A rotation collection was unloaded. Review again.");
				command.reset();
			}
		}
		review->setPlainText(command ? summary.review : summary.error);
		return static_cast<bool>(command);
	};
	const auto invalidate = [&]()
	{
		reviewed = false;
		command.reset();
		replace_younger->setChecked(false);
		apply->setEnabled(false);
		review->clear();
	};
	QObject::connect(model_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
			&dialog, invalidate);
	QObject::connect(plate_spin, QOverload<int>::of(&QSpinBox::valueChanged), &dialog, invalidate);
	QObject::connect(time_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			&dialog, invalidate);
	QObject::connect(review_button, &QPushButton::clicked, &dialog, [&]()
	{
		replace_younger->setChecked(false);
		reviewed = prepare();
		reviewed_signature = current_signature;
		apply->setEnabled(reviewed && summary.younger_samples == 0);
	});
	QObject::connect(replace_younger, &QCheckBox::toggled, &dialog, [&](bool checked)
	{
		apply->setEnabled(reviewed && (checked || summary.younger_samples == 0));
	});
	QObject::connect(apply, &QPushButton::clicked, &dialog, [&]()
	{
		if (reviewed && prepare() && current_signature == reviewed_signature &&
				(replace_younger->isChecked() || summary.younger_samples == 0))
		{
			dialog.accept();
		}
		else
		{
			if (current_signature != reviewed_signature)
			{
				review->setPlainText(QObject::tr(
						"The model or its geometry changed since Review. Review the changes again."));
			}
			reviewed = false;
			replace_younger->setChecked(false);
			apply->setEnabled(false);
		}
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	dialog.resize(680, 530);
	if (dialog.exec() != QDialog::Accepted || !command)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("No rotations changed."));
	}
	// Focus notifications can clear the global undo stack. Never emit them inside a command.
	if (d_view_state.get_feature_focus().focused_feature().is_valid() ||
			d_view_state.get_feature_focus().associated_reconstruction_geometry())
	{
		d_view_state.get_feature_focus().unset_focus();
	}
	UndoRedo::instance().get_active_undo_stack().push(command.release());
	return Result(OPERATION_COMPLETED, summary.review);
}
