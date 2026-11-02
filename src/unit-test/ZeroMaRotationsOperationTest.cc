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

#include <vector>
#include <QFile>
#include <QTemporaryDir>
#include <QUndoStack>
#include <gtest/gtest.h>

#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/TRSUtils.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "model/FeatureHandle.h"
#include "model/FeatureType.h"
#include "model/FeatureVisitor.h"
#include "model/Metadata.h"
#include "model/Model.h"
#include "model/PropertyName.h"
#include "model/PropertyValueFinder.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlFiniteRotationSlerp.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/ZeroMaRotationsOperation.h"

namespace
{
	typedef GPlatesViewOperations::ZeroMaRotationsOperation Operation;
	typedef GPlatesPropertyValues::GpmlTimeSample Sample;
	typedef GPlatesPropertyValues::GpmlFiniteRotation Rotation;
	typedef GPlatesPropertyValues::GpmlIrregularSampling Sampling;
	typedef std::vector<Sample::non_null_ptr_type> Samples;


	Sample::non_null_ptr_type
	pole(
			double time,
			double angle,
			bool disabled = false,
			const char *comment = "pole comment")
	{
		return Sample::create(
				angle == 0 ? Rotation::create_zero_rotation() :
						Rotation::create(std::make_pair(30.0, 60.0), angle),
				GPlatesPropertyValues::GmlTimeInstant::create(
						GPlatesPropertyValues::GeoTimeInstant(time)),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(QString::fromUtf8(comment))),
				Rotation::STRUCTURAL_TYPE, disabled);
	}


	Sampling::non_null_ptr_type
	sampling(
			const GPlatesModel::FeatureHandle::weak_ref &feature)
	{
		GPlatesAppLogic::TRSUtils::TRSFinder finder;
		finder.visit_feature(feature);
		return *finder.irregular_sampling();
	}


	std::vector<Sample::non_null_ptr_to_const_type>
	zeros(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			bool disabled = false)
	{
		std::vector<Sample::non_null_ptr_to_const_type> result;
		const auto poles = sampling(feature);
		for (Sample::non_null_ptr_type sample : poles->time_samples())
		{
			const auto &time = sample->valid_time()->get_time_position();
			if (time.is_real() && time.value() == 0 && sample->is_disabled() == disabled)
			{
				result.push_back(sample);
			}
		}
		return result;
	}


	const Rotation &
	rotation(
			const Sample &sample)
	{
		return dynamic_cast<const Rotation &>(*sample.value());
	}


	QString
	description(
			const Sample &sample)
	{
		return sample.description() ?
				GPlatesUtils::make_qstring_from_icu_string((*sample.description())->get_value().get()) :
				QString();
	}


	class ChangeAttachedZero :
			public GPlatesModel::FeatureVisitor
	{
		void
		visit_gpml_irregular_sampling(
				Sampling &poles) override
		{
			for (Sample::non_null_ptr_type sample : poles.time_samples())
			{
				if (sample->valid_time()->get_time_position().value() == 0)
				{
					sample->set_value(pole(0, 89)->value());
				}
			}
		}
	};


	class ZeroMaRotationsOperationTest :
			public ::testing::Test
	{
	protected:
		ZeroMaRotationsOperationTest() :
			d_collection(GPlatesModel::FeatureCollectionHandle::create(d_model->root()))
		{  }

		GPlatesModel::FeatureHandle::weak_ref
		sequence(
				int moving,
				int fixed,
				const Samples &samples,
				bool disabled = false,
				bool wrapped = false)
		{
			const auto feature = GPlatesModel::FeatureHandle::create(d_collection,
					GPlatesModel::FeatureType::create_gpml("TotalReconstructionSequence"));
			for (const auto &id : { std::make_pair("movingReferenceFrame", moving),
					std::make_pair("fixedReferenceFrame", fixed) })
			{
				feature->add(GPlatesModel::TopLevelPropertyInline::create(
						GPlatesModel::PropertyName::create_gpml(id.first),
						GPlatesPropertyValues::GpmlPlateId::create(id.second)));
			}
			const auto poles = Sampling::create(samples,
					GPlatesPropertyValues::GpmlInterpolationFunction::non_null_ptr_type(
							GPlatesPropertyValues::GpmlFiniteRotationSlerp::create(Rotation::STRUCTURAL_TYPE)),
					Rotation::STRUCTURAL_TYPE);
			if (disabled)
			{
				poles->set_disabled(true);
			}
			GPlatesModel::PropertyValue::non_null_ptr_type value = poles;
			if (wrapped)
			{
				value = GPlatesPropertyValues::GpmlConstantValue::create(
						value, Sampling::STRUCTURAL_TYPE,
						GPlatesUtils::make_icu_string_from_qstring("wrapper description"));
			}
			feature->add(GPlatesModel::TopLevelPropertyInline::create(
					GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"), value));
			return feature;
		}

		GPlatesModel::ModelInterface d_model;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		Operation::Summary d_summary;
	};
}


TEST_F(ZeroMaRotationsOperationTest, copies_youngest_positive_time_without_mutating_prepared_model)
{
	const auto source = pole(5, 32);
	const auto feature = sequence(101, 0,
			{ pole(-2, 70), pole(0, 0), pole(1, 80, true), source, pole(20, 65) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	ASSERT_EQ(zeros(feature).size(), 1u);
	EXPECT_TRUE(rotation(*zeros(feature).front()).is_zero_rotation());
	command->redo();
	EXPECT_EQ(rotation(*zeros(feature).front()).get_finite_rotation(),
			rotation(*source).get_finite_rotation());
	EXPECT_EQ(sampling(feature)->time_samples().size(), 5u);
	EXPECT_EQ(d_summary.updated_plates, 1u);
	EXPECT_EQ(d_summary.replaced_zero_samples, 1u);
}


TEST_F(ZeroMaRotationsOperationTest, missing_zero_is_added_even_for_a_tiny_positive_time)
{
	const auto source = pole(1e-10, 12, false, "youngest comment");
	const auto feature = sequence(101, 0, { source, pole(20, 50) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	ASSERT_EQ(zeros(feature).size(), 1u);
	EXPECT_EQ(rotation(*zeros(feature).front()).get_finite_rotation(),
			rotation(*source).get_finite_rotation());
	EXPECT_EQ(description(*zeros(feature).front()), "youngest comment");
	EXPECT_EQ(d_summary.added_zero_samples, 1u);
}


TEST_F(ZeroMaRotationsOperationTest, skips_disabled_data_and_copies_identity)
{
	const auto enabled = sequence(101, 0,
			{ pole(0, 40), pole(0, 70, true), pole(1, 60, true), pole(5, 0), pole(20, 30) });
	const auto disabled = sequence(202, 0, { pole(0, 30), pole(5, 60) }, true);
	const auto before = sampling(disabled);
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	ASSERT_EQ(zeros(enabled).size(), 1u);
	EXPECT_TRUE(rotation(*zeros(enabled).front()).is_zero_rotation());
	EXPECT_EQ(zeros(enabled, true).size(), 1u);
	EXPECT_TRUE(*sampling(disabled) == *before);
	EXPECT_EQ(d_summary.updated_plates, 1u);
}


TEST_F(ZeroMaRotationsOperationTest, no_positive_pole_is_reported_without_changing_anything)
{
	const auto feature = sequence(101, 0, { pole(-10, 30), pole(0, 20), pole(5, 40, true) });
	const auto before = sampling(feature);
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	EXPECT_FALSE(command);
	EXPECT_EQ(d_summary.updated_plates, 0u);
	ASSERT_EQ(d_summary.skipped_plates.size(), 1);
	EXPECT_TRUE(d_summary.skipped_plates.front().contains("101"));
	EXPECT_TRUE(*sampling(feature) == *before);
}


TEST_F(ZeroMaRotationsOperationTest, ordinary_crossover_uses_the_present_day_branch)
{
	const auto older = sequence(101, 202, { pole(10, 80), pole(100, 90) });
	const auto older_before = sampling(older);
	const auto source = pole(10, 25);
	const auto present = sequence(101, 0, { pole(0, 0), source });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	EXPECT_EQ(rotation(*zeros(present).front()).get_finite_rotation(),
			rotation(*source).get_finite_rotation());
	EXPECT_TRUE(*sampling(older) == *older_before);
	EXPECT_TRUE(d_summary.skipped_plates.empty());
}


TEST_F(ZeroMaRotationsOperationTest, conflicting_youngest_sources_are_skipped)
{
	const auto first = sequence(101, 0, { pole(5, 20), pole(20, 40) });
	const auto second = sequence(101, 202, { pole(5, 30), pole(30, 70) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	EXPECT_FALSE(command);
	EXPECT_EQ(d_summary.skipped_plates.size(), 1);
	EXPECT_TRUE(zeros(first).empty());
	EXPECT_TRUE(zeros(second).empty());
}


TEST_F(ZeroMaRotationsOperationTest, competing_sequence_spanning_negative_ages_is_left_unchanged)
{
	const auto future = sequence(101, 202, { pole(-10, 10), pole(0, 20), pole(100, 50) });
	const auto source = sequence(101, 0, { pole(5, 30), pole(20, 60) });
	const auto before = sampling(future);
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	EXPECT_FALSE(command);
	EXPECT_EQ(d_summary.skipped_plates.size(), 1);
	EXPECT_TRUE(*sampling(future) == *before);
	EXPECT_TRUE(zeros(source).empty());
}


TEST_F(ZeroMaRotationsOperationTest, duplicate_zero_rows_collapse_but_disabled_rows_survive)
{
	const auto feature = sequence(101, 0,
			{ pole(0, 0), pole(0, 10), pole(0, 90, true), pole(5, 20), pole(5, 20) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	EXPECT_EQ(zeros(feature).size(), 1u);
	EXPECT_EQ(zeros(feature, true).size(), 1u);
	EXPECT_EQ(d_summary.replaced_zero_samples, 2u);
}


TEST_F(ZeroMaRotationsOperationTest, new_reference_frame_is_used_at_zero_and_just_above_zero)
{
	sequence(1, 0, { pole(0, 30), pole(100, 30) });
	sequence(2, 0, { pole(0, 60), pole(100, 60) });
	const auto old_sequence = sequence(101, 1, { pole(0, 0), pole(100, 70) });
	const auto source = pole(5, 20);
	const auto new_sequence = sequence(101, 2, { source, pole(20, 40) });
	const auto before_tree =
			GPlatesAppLogic::create_cached_reconstruction_tree_creator({ d_collection })
			.get_reconstruction_tree(0);
	const auto before_rotation = before_tree->get_composed_absolute_rotation(101);
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	EXPECT_TRUE(zeros(old_sequence).empty());
	ASSERT_EQ(zeros(new_sequence).size(), 1u);
	const auto creator = GPlatesAppLogic::create_cached_reconstruction_tree_creator({ d_collection });
	const auto zero_tree = creator.get_reconstruction_tree(0);
	const auto nearby_tree = creator.get_reconstruction_tree(0.001);
	ASSERT_TRUE(zero_tree->get_edge(101));
	EXPECT_EQ(zero_tree->get_edge(101)->get_fixed_plate(), 2u);
	EXPECT_EQ(zero_tree->get_composed_absolute_rotation(101),
			nearby_tree->get_composed_absolute_rotation(101));
	EXPECT_NE(zero_tree->get_composed_absolute_rotation(101), before_rotation);
}


TEST_F(ZeroMaRotationsOperationTest, preserves_wrappers_interpolation_comments_and_metadata)
{
	const auto source = pole(5, 25, false, "source description");
	GPlatesModel::MetadataContainer metadata;
	metadata.push_back(GPlatesModel::Metadata::shared_ptr_type(
			new GPlatesModel::Metadata("AUTH", "test author")));
	dynamic_cast<Rotation &>(*source->value()).set_metadata(metadata);
	const auto feature = sequence(101, 0,
			{ pole(0, 0, false, "keep present description"), source, pole(20, 60) }, false, true);
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	const auto result = sampling(feature);
	ASSERT_TRUE(result->interpolation_function());
	EXPECT_TRUE(dynamic_cast<const GPlatesPropertyValues::GpmlFiniteRotationSlerp *>(
			result->interpolation_function().get().get()));
	const auto wrapper = GPlatesModel::get_property_value<GPlatesPropertyValues::GpmlConstantValue>(
			feature, GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"));
	ASSERT_TRUE(wrapper);
	EXPECT_TRUE((*wrapper)->get_description());
	ASSERT_EQ(zeros(feature).size(), 1u);
	EXPECT_EQ(description(*zeros(feature).front()), "keep present description");
	const auto zero_samples = zeros(feature);
	const auto &copied_metadata = rotation(*zero_samples.front()).get_metadata();
	ASSERT_EQ(copied_metadata.size(), 1u);
	EXPECT_EQ(copied_metadata.front()->get_content(), "test author");
	EXPECT_EQ(description(*source), "source description");
}


TEST_F(ZeroMaRotationsOperationTest, undo_redo_is_one_atomic_command_with_detached_snapshots)
{
	const auto first = sequence(101, 0, { pole(0, 0), pole(5, 20) });
	const auto second = sequence(202, 0, { pole(10, 50), pole(20, 70) });
	const auto first_before = sampling(first);
	const auto second_before = sampling(second);
	QUndoStack stack;
	stack.push(Operation::create_undo_command(d_model, d_collection, d_summary).release());
	ASSERT_EQ(stack.count(), 1);
	const auto first_after = sampling(first);
	const auto second_after = sampling(second);
	ChangeAttachedZero later_edit;
	later_edit.visit_feature(first);
	stack.undo();
	EXPECT_TRUE(*sampling(first) == *first_before);
	EXPECT_TRUE(*sampling(second) == *second_before);
	stack.redo();
	EXPECT_TRUE(*sampling(first) == *first_after);
	EXPECT_TRUE(*sampling(second) == *second_after);
	stack.undo();
	EXPECT_TRUE(*sampling(first) == *first_before);
}


TEST_F(ZeroMaRotationsOperationTest, new_parent_cycle_is_skipped_without_losing_other_plates)
{
	const auto first = sequence(101, 202, { pole(5, 20), pole(20, 40) });
	const auto second = sequence(202, 101, { pole(10, 30), pole(30, 60) });
	const auto independent = sequence(303, 0, { pole(5, 45), pole(10, 50) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	EXPECT_EQ(d_summary.updated_plates, 1u);
	EXPECT_EQ(d_summary.skipped_plates.size(), 2);
	command->redo();
	EXPECT_TRUE(zeros(first).empty());
	EXPECT_TRUE(zeros(second).empty());
	EXPECT_EQ(zeros(independent).size(), 1u);
}


TEST_F(ZeroMaRotationsOperationTest, checks_unchanged_present_day_links_for_cycles)
{
	const auto moving = sequence(101, 202, { pole(5, 20), pole(20, 40) });
	const auto unchanged = sequence(202, 101, { pole(0, 30) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	EXPECT_FALSE(command);
	EXPECT_EQ(d_summary.updated_plates, 0u);
	EXPECT_EQ(d_summary.skipped_plates.size(), 2);
	EXPECT_TRUE(zeros(moving).empty());
	EXPECT_EQ(zeros(unchanged).size(), 1u);
}


TEST_F(ZeroMaRotationsOperationTest, a_root_self_reference_is_not_a_parent_cycle)
{
	sequence(0, 0, { pole(0, 0), pole(100, 0) });
	const auto moving = sequence(101, 0, { pole(5, 20), pole(20, 40) });
	auto command = Operation::create_undo_command(d_model, d_collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	EXPECT_EQ(d_summary.updated_plates, 1u);
	EXPECT_EQ(zeros(moving).size(), 1u);
}


TEST_F(ZeroMaRotationsOperationTest, edited_rotation_file_round_trips_through_normal_save)
{
	QTemporaryDir directory;
	ASSERT_TRUE(directory.isValid());
	const QString filename = directory.filePath("rotations.rot");
	QFile input(filename);
	ASSERT_TRUE(input.open(QIODevice::WriteOnly));
	const QByteArray original =
			"101 0 90 0 0 000 ! present comment\n"
			"101 5 60 30 25 000 ! source comment\n"
			"101 20 60 30 50 000 ! older comment\n";
	ASSERT_EQ(input.write(original), original.size());
	input.close();
	GPlatesFileIO::FeatureCollectionFileFormat::Registry registry;
	const auto file = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(filename));
	GPlatesFileIO::ReadErrorAccumulation read_errors;
	registry.read_feature_collection(file->get_reference(), read_errors);
	const auto collection = file->get_reference().get_feature_collection();
	auto command = Operation::create_undo_command(d_model, collection, d_summary);
	ASSERT_TRUE(command);
	command->redo();
	ASSERT_TRUE(input.open(QIODevice::ReadOnly));
	EXPECT_EQ(input.readAll(), original); // Applying is a model edit, not an implicit save.
	input.close();
	registry.write_feature_collection(file->get_reference());
	const auto reloaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(filename));
	registry.read_feature_collection(reloaded->get_reference(), read_errors);
	const auto result = reloaded->get_reference().get_feature_collection();
	ASSERT_EQ(result->size(), 1u);
	const auto feature = (*result->begin())->reference();
	ASSERT_EQ(zeros(feature).size(), 1u);
	EXPECT_EQ(rotation(*zeros(feature).front()).get_finite_rotation(),
			rotation(*pole(5, 25)).get_finite_rotation());
	EXPECT_TRUE(description(*zeros(feature).front()).contains("present comment"));
	command->undo();
	registry.write_feature_collection(file->get_reference());
	const auto undone = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(filename));
	registry.read_feature_collection(undone->get_reference(), read_errors);
	EXPECT_TRUE(rotation(*zeros((*undone->get_reference().get_feature_collection()->begin())
			->reference()).front()).is_zero_rotation());
}
