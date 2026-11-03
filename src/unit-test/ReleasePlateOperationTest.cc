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
#include <limits>
#include <stdexcept>
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

#include "maths/FiniteRotation.h"
#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureVisitor.h"
#include "model/Metadata.h"
#include "model/Model.h"
#include "model/ModelUtils.h"
#include "model/TopLevelPropertyInline.h"

#include "property-values/Enumeration.h"
#include "property-values/GmlLineString.h"
#include "property-values/GmlTimeInstant.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlFiniteRotation.h"
#include "property-values/GpmlFiniteRotationSlerp.h"
#include "property-values/GpmlIrregularSampling.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/GpmlTimeSample.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/ReleasePlateOperation.h"

namespace
{
	typedef GPlatesViewOperations::ReleasePlateOperation Operation;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;
	typedef GPlatesPropertyValues::GpmlTimeSample Sample;
	typedef GPlatesPropertyValues::GpmlFiniteRotation Pole;
	typedef GPlatesPropertyValues::GpmlIrregularSampling Sampling;
	typedef GPlatesMaths::FiniteRotation Rotation;
	typedef GPlatesAppLogic::ReconstructionTreeCreator Trees;


	Sample::non_null_ptr_type
	pole(
			double time,
			double latitude,
			double longitude,
			double angle,
			bool disabled = false,
			const char *comment = "original pole comment")
	{
		return Sample::create(angle == 0 ? Pole::create_zero_rotation() :
				Pole::create(std::make_pair(longitude, latitude), angle),
				GPlatesPropertyValues::GmlTimeInstant::create(
						GPlatesPropertyValues::GeoTimeInstant(time)),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(comment)),
				Pole::STRUCTURAL_TYPE, disabled);
	}


	void
	expect_rotation(
			const Rotation &actual,
			const Rotation &expected,
			double tolerance = 1e-11)
	{
		for (const auto &location : {GPlatesMaths::LatLonPoint(0, 0),
				GPlatesMaths::LatLonPoint(0, 90), GPlatesMaths::LatLonPoint(90, 0)})
		{
			const auto point = GPlatesMaths::make_point_on_sphere(location);
			const auto a = (actual * point).position_vector();
			const auto b = (expected * point).position_vector();
			EXPECT_NEAR(a.x().dval(), b.x().dval(), tolerance);
			EXPECT_NEAR(a.y().dval(), b.y().dval(), tolerance);
			EXPECT_NEAR(a.z().dval(), b.z().dval(), tolerance);
		}
	}


	Rotation
	total(
			const Trees &trees,
			double time,
			unsigned int plate = 123,
			unsigned int anchor = 0)
	{
		const auto value = trees.get_reconstruction_tree(time, anchor)
				->get_composed_absolute_rotation_or_none(plate);
		if (!value)
		{
			throw std::runtime_error("Test plate is missing from the reconstruction tree.");
		}
		return *value;
	}


	Sampling::non_null_ptr_type
	sampling(
			const GPlatesModel::FeatureHandle::weak_ref &feature)
	{
		GPlatesAppLogic::TRSUtils::TRSFinder finder;
		finder.visit_feature(feature);
		return *finder.irregular_sampling();
	}


	class ReleasePlateOperationTest :
			public ::testing::Test
	{
	protected:
		ReleasePlateOperationTest() :
			d_collection(GPlatesModel::FeatureCollectionHandle::create(d_model->root()))
		{  }

		GPlatesModel::FeatureHandle::weak_ref
		sequence(
				unsigned int moving,
				unsigned int fixed,
				const std::vector<Sample::non_null_ptr_type> &samples,
				bool wrapped = false)
		{
			const auto feature = GPlatesModel::FeatureHandle::create(d_collection,
					GPlatesModel::FeatureType::create_gpml("TotalReconstructionSequence"));
			for (const auto &id : {std::make_pair("movingReferenceFrame", moving),
					std::make_pair("fixedReferenceFrame", fixed)})
			{
				feature->add(GPlatesModel::TopLevelPropertyInline::create(
						GPlatesModel::PropertyName::create_gpml(id.first),
						GPlatesPropertyValues::GpmlPlateId::create(id.second)));
			}
			const auto poles = Sampling::create(samples,
					GPlatesPropertyValues::GpmlInterpolationFunction::non_null_ptr_type(
							GPlatesPropertyValues::GpmlFiniteRotationSlerp::create(Pole::STRUCTURAL_TYPE)),
					Pole::STRUCTURAL_TYPE);
			GPlatesModel::PropertyValue::non_null_ptr_type value = poles;
			if (wrapped)
			{
				value = GPlatesPropertyValues::GpmlConstantValue::create(value,
						Sampling::STRUCTURAL_TYPE,
						GPlatesUtils::make_icu_string_from_qstring("preserve wrapper description"));
			}
			feature->add(GPlatesModel::TopLevelPropertyInline::create(
					GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"), value));
			feature->add(GPlatesModel::TopLevelPropertyInline::create(
					GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
							GPlatesUtils::make_icu_string_from_qstring("keep sequence metadata"))));
			return feature;
		}

		void
		model()
		{
			sequence(11, 0, {pole(0, 90, 0, 5), pole(1000, 90, 0, 75)});
			sequence(77, 11, {pole(0, 0, 90, 8), pole(1000, 0, 90, 18)});
			sequence(123, 77, {pole(0, 0, 0, -15), pole(1000, 0, 0, 35)});
			sequence(222, 123, {pole(0, 30, 20, 3), pole(1000, 30, 20, 12)});
		}

		Trees
		trees()
		{
			return GPlatesAppLogic::create_cached_reconstruction_tree_creator({d_collection});
		}

		GPlatesModel::FeatureHandle::weak_ref
		feature(
				unsigned int moving,
				unsigned int fixed)
		{
			for (const auto &handle : *d_collection)
			{
				GPlatesAppLogic::TRSUtils::TRSFinder finder;
				finder.visit_feature(handle->reference());
				if (finder.can_process_trs() && *finder.moving_ref_frame_plate_id() == moving &&
						*finder.fixed_ref_frame_plate_id() == fixed)
				{
					return handle->reference();
				}
			}
			return {};
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type
		ridge(
				double import_time)
		{
			const auto ridge = GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml("MidOceanRidge"));
			const auto add = [&](const GPlatesModel::PropertyName &name,
					const GPlatesModel::PropertyValue::non_null_ptr_type &value)
			{
				ridge->add(GPlatesModel::TopLevelPropertyInline::create(name, value));
			};
			add(GPlatesModel::PropertyName::create_gpml("centerLineOf"),
					GPlatesPropertyValues::GmlLineString::create(GPlatesMaths::PolylineOnSphere::create(
							std::vector<GPlatesMaths::PointOnSphere>{
									GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(0, 0)),
									GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(10, 0))})));
			add(GPlatesModel::PropertyName::create_gpml("leftPlate"),
					GPlatesPropertyValues::GpmlPlateId::create(77));
			add(GPlatesModel::PropertyName::create_gpml("rightPlate"),
					GPlatesPropertyValues::GpmlPlateId::create(123));
			add(GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
					GPlatesPropertyValues::Enumeration::create(
							GPlatesPropertyValues::EnumerationType::create_gpml(
									"ReconstructionMethodEnumeration"),
							GPlatesPropertyValues::EnumerationContent("HalfStageRotationVersion3")));
			add(GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
					GPlatesPropertyValues::GmlTimeInstant::create(
							GPlatesPropertyValues::GeoTimeInstant(import_time)));
			return ridge;
		}

		GPlatesModel::ModelInterface d_model;
		Collection d_collection;
		Operation::Summary d_summary;
	};
}


TEST_F(ReleasePlateOperationTest, GeneralPlateAndAgeKeepOlderMotionAndDescendantFrames)
{
	model();
	const auto before = trees();
	const auto source = feature(123, 77);
	const auto original = sampling(source)->clone();
	const auto descendant = feature(222, 123);
	const auto descendant_revision = descendant->revision_id();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	EXPECT_EQ(d_collection->size(), 4u);
	EXPECT_TRUE(*sampling(source) == *original);
	EXPECT_EQ(d_summary.ancestor_pins, 2u);
	EXPECT_EQ(d_summary.previous_parent, 77u);
	command->redo();
	const auto after = trees();
	for (unsigned int anchor : {0u, 11u, 77u, 222u})
	{
		SCOPED_TRACE(anchor);
		for (unsigned int plate : {11u, 77u, 123u, 222u})
		{
			for (double time : {900.0, 900.000001, 950.0, 1000.0})
			{
				expect_rotation(total(after, time, plate, anchor), total(before, time, plate, anchor));
			}
		}
	}
	for (double time : {0.0, 0.5, 1.0, 250.0, 899.999999, 900.0})
	{
		expect_rotation(total(after, time), total(before, 900));
		expect_rotation(total(after, time, 222, 123), total(before, time, 222, 123));
	}
	EXPECT_EQ(descendant->revision_id(), descendant_revision);
}


TEST_F(ReleasePlateOperationTest, FractionalReleaseAndAncestorsPinnedAgainstLaterYoungerEdits)
{
	model();
	const auto before = trees();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 456.25, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	for (const auto &parent : {feature(11, 0), feature(77, 11)})
	{
		GPlatesAppLogic::TRSUtils::TRSFinder finder;
		finder.visit_feature(parent);
		const auto property = (*(*finder.irregular_sampling_property_iterator()))->clone();
		class InsertYoungerPole : public GPlatesModel::FeatureVisitor
		{
			void
			visit_gpml_irregular_sampling(
					Sampling &value) override
			{
				auto &samples = value.time_samples();
				std::vector<Sample::non_null_ptr_type> replacement{samples.begin(), samples.end()};
				replacement.push_back(pole(400, -20, 40, 60));
				std::sort(replacement.begin(), replacement.end(), [](const auto &a, const auto &b)
				{
					return a->valid_time()->get_time_position().value() <
							b->valid_time()->get_time_position().value();
				});
				samples.assign(replacement.begin(), replacement.end());
			}
		} visitor;
		property->accept_visitor(visitor);
		parent->set(*finder.irregular_sampling_property_iterator(), property);
	}
	const auto edited = trees();
	for (double time : {456.25, 456.250001, 700.0, 1000.0})
	{
		expect_rotation(total(edited, time), total(before, time));
	}
	expect_rotation(total(edited, 0), total(before, 456.25));
}


TEST_F(ReleasePlateOperationTest, PreservesDisabledRowsCommentsAndMetadata)
{
	sequence(77, 0, {pole(0, 90, 0, 10), pole(1000, 90, 0, 70)});
	const auto disabled = pole(123, 20, 30, 40, true, "disabled source comment");
	const auto source = sequence(123, 77,
			{pole(0, 0, 0, 5, false, "younger comment"), disabled,
					pole(500, 0, 0, 15, false, "middle comment"),
					pole(1000, 0, 0, 30, false, "older comment")});
	const auto source_id = source->feature_id();
	GPlatesAppLogic::TRSUtils::TRSFinder finder;
	finder.visit_feature(source);
	const auto before_property = (*(*finder.irregular_sampling_property_iterator()))->clone();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	EXPECT_EQ(d_summary.younger_samples, 2u);
	command->redo();
	bool retained_disabled = false;
	bool retained_younger = false;
	// TRSFinder returns a detached clone; keep it alive while iterating its samples.
	const auto changed_sampling = sampling(source);
	for (Sample::non_null_ptr_type sample : changed_sampling->time_samples())
	{
		if (sample->valid_time()->get_time_position().value() == 123)
		{
			retained_disabled = sample->is_disabled() && *sample == *disabled;
		}
		if (sample->valid_time()->get_time_position().value() == 0)
		{
			retained_younger = sample->is_disabled() && sample->description() &&
					(*sample->description())->get_value().get().qstring() == "younger comment";
		}
	}
	EXPECT_TRUE(retained_disabled);
	EXPECT_TRUE(retained_younger);
	EXPECT_EQ(source->feature_id(), source_id);
	command->undo();
	EXPECT_TRUE(*(*(*finder.irregular_sampling_property_iterator())) == *before_property);

	// TRSFinder understands this wrapper, but the native reconstruction graph does not.
	// Free Plate must refuse that disconnected input without stripping its metadata.
	const auto wrapped = GPlatesModel::TopLevelPropertyInline::create(
			GPlatesModel::PropertyName::create_gpml("totalReconstructionPole"),
			GPlatesPropertyValues::GpmlConstantValue::create(sampling(source)->clone(),
					Sampling::STRUCTURAL_TYPE,
					GPlatesUtils::make_icu_string_from_qstring("preserve wrapper description")));
	source->set(*finder.irregular_sampling_property_iterator(), wrapped);
	const auto wrapped_snapshot = wrapped->clone();
	EXPECT_FALSE(Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary));
	EXPECT_TRUE(d_summary.error.contains("not connected")) << d_summary.error.toStdString();
	EXPECT_TRUE(*(*(*finder.irregular_sampling_property_iterator())) == *wrapped_snapshot);
}


TEST_F(ReleasePlateOperationTest, RepeatedUndoRedoRestoresActiveRootAndImmutableSnapshots)
{
	model();
	const auto before = trees();
	const auto source = feature(123, 77);
	const auto original = sampling(source)->clone();
	QUndoStack stack;
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	stack.push(command.release());
	const auto created = feature(123, 0);
	ASSERT_TRUE(created.is_valid());
	const auto retained = GPlatesModel::FeatureHandle::non_null_ptr_type(created.handle_ptr());
	for (int cycle = 0; cycle < 3; ++cycle)
	{
		SCOPED_TRACE(cycle);
		ASSERT_EQ(stack.count(), 1);
		ASSERT_EQ(stack.index(), 1);
		ASSERT_EQ(d_collection->size(), 5u);
		ASSERT_TRUE(retained->is_active());
		ASSERT_TRUE(retained->reference().is_valid());
		expect_rotation(total(trees(), 0), total(before, 900));
		stack.undo();
		ASSERT_EQ(d_collection->size(), 4u);
		EXPECT_FALSE(retained->is_active());
		EXPECT_TRUE(*sampling(source) == *original);
		expect_rotation(total(trees(), 0), total(before, 0));
		stack.redo();
	}
}


TEST_F(ReleasePlateOperationTest, RefusesAmbiguousMissingIndependentAndInvalidInputs)
{
	model();
	for (const auto &input : {std::make_pair(0u, 900.0), std::make_pair(999u, 900.0),
			std::make_pair(123u, 0.0), std::make_pair(123u, -1.0),
			std::make_pair(123u, std::numeric_limits<double>::infinity()),
			std::make_pair(444u, 900.0), std::make_pair(11u, 900.0),
			std::make_pair(123u, 1000.0)})
	{
		SCOPED_TRACE(input.first);
		EXPECT_FALSE(Operation::create_undo_command(d_model, {d_collection},
				input.first, input.second, d_summary));
		EXPECT_FALSE(d_summary.error.isEmpty());
		EXPECT_EQ(d_collection->size(), 4u);
	}
	sequence(123, 11, {pole(0, 10, 20, 5), pole(1000, 10, 20, 15)});
	EXPECT_FALSE(Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary));
	EXPECT_TRUE(d_summary.error.contains("overlapping"));
}


TEST_F(ReleasePlateOperationTest, RestoresDeletedGeneratedPropertiesOnRedo)
{
	model();
	const auto before = trees();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	const auto generated = feature(123, 0);
	ASSERT_TRUE(generated.is_valid());
	const auto retained = GPlatesModel::FeatureHandle::non_null_ptr_type(generated.handle_ptr());
	GPlatesAppLogic::TRSUtils::TRSFinder finder;
	finder.visit_feature(generated);
	generated->remove(*finder.irregular_sampling_property_iterator());
	command->undo();
	command->redo();
	ASSERT_TRUE(retained->is_active());
	ASSERT_TRUE(retained->reference().is_valid());
	expect_rotation(total(trees(), 0), total(before, 900));
}


TEST_F(ReleasePlateOperationTest, RefusesToModifyFeaturesMovedToAnotherCollection)
{
	model();
	const auto source = feature(123, 77);
	const auto original = sampling(source)->clone();
	const auto parent_before = sampling(feature(77, 11))->clone();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	const auto other = GPlatesModel::FeatureCollectionHandle::create(d_model->root());
	const auto moved = GPlatesModel::FeatureHandle::non_null_ptr_type(source.handle_ptr());
	source->remove_from_parent();
	other->add(moved);
	moved->set_active(true);
	command->redo();
	EXPECT_EQ(d_collection->size(), 3u);
	EXPECT_EQ(other->size(), 1u);
	EXPECT_TRUE(*sampling(moved->reference()) == *original);
	EXPECT_TRUE(*sampling(feature(77, 11)) == *parent_before);
}


TEST_F(ReleasePlateOperationTest, IdentityChildBecomesItsParentsNonIdentityAbsolutePole)
{
	sequence(321, 0, {pole(0, 45, 30, 10), pole(400, 45, 30, 50)});
	sequence(42, 321, {pole(0, 90, 0, 0), pole(400, 90, 0, 0)});
	const auto before = trees();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 42, 123, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	for (double time : {0.0, 50.0, 123.0})
	{
		expect_rotation(total(trees(), time, 42), total(before, 123, 321));
	}
	expect_rotation(total(trees(), 250, 42), total(before, 250, 42));
}


TEST_F(ReleasePlateOperationTest, ExistingMatchedCrossoverPreservesTheOlderBranch)
{
	sequence(77, 0, {pole(0, 90, 0, 10), pole(1000, 90, 0, 10)});
	sequence(88, 0, {pole(0, 90, 0, 20), pole(1000, 90, 0, 20)});
	sequence(123, 88, {pole(0, 90, 0, 5), pole(900, 90, 0, 5)});
	sequence(123, 77, {pole(900, 90, 0, 15), pole(1000, 90, 0, 25)});
	const auto before = trees();
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	EXPECT_EQ(d_summary.previous_parent, 77u);
	expect_rotation(total(trees(), 0), total(before, 900));
	expect_rotation(total(trees(), 950), total(before, 950));
}


TEST_F(ReleasePlateOperationTest, HalfStageHistoryJumpRefusesTransactionAndNamesFeature)
{
	model();
	const auto source = feature(123, 77);
	const auto original = sampling(source)->clone();
	const auto mor = ridge(0);
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary,
			{mor->reference()});
	EXPECT_FALSE(command);
	EXPECT_TRUE(d_summary.error.contains(mor->feature_id().get().qstring()))
			<< d_summary.error.toStdString();
	EXPECT_TRUE(d_summary.error.contains("would move")) << d_summary.error.toStdString();
	EXPECT_EQ(d_collection->size(), 4u);
	EXPECT_TRUE(*sampling(source) == *original);
}


TEST_F(ReleasePlateOperationTest, HalfStageImportedInPreservedHistoryRemainsUsable)
{
	model();
	const auto mor = ridge(1000);
	auto command = Operation::create_undo_command(d_model, {d_collection}, 123, 900, d_summary,
			{mor->reference()});
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	EXPECT_TRUE(feature(123, 0).is_valid());
}


TEST_F(ReleasePlateOperationTest, NormalRotationSaveReloadHasOnlyTextPrecisionError)
{
	QTemporaryDir directory;
	ASSERT_TRUE(directory.isValid());
	const QString path = directory.filePath("rotations.rot");
	QFile input(path);
	ASSERT_TRUE(input.open(QIODevice::WriteOnly));
	const QByteArray original = "077 0 90 0 5 000 ! parent\n"
			"077 1000 90 0 75 000 ! parent\n"
			"123 0 0 0 -15 077 ! younger comment\n"
			"123 1000 0 0 35 077 ! older comment\n";
	ASSERT_EQ(input.write(original), original.size());
	input.close();
	GPlatesFileIO::FeatureCollectionFileFormat::Registry registry;
	const auto file = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	GPlatesFileIO::ReadErrorAccumulation errors;
	registry.read_feature_collection(file->get_reference(), errors);
	const auto collection = file->get_reference().get_feature_collection();
	const auto before = GPlatesAppLogic::create_cached_reconstruction_tree_creator({collection});
	auto command = Operation::create_undo_command(d_model, {collection}, 123, 900, d_summary);
	ASSERT_TRUE(command) << d_summary.error.toStdString();
	command->redo();
	ASSERT_TRUE(input.open(QIODevice::ReadOnly));
	EXPECT_EQ(input.readAll(), original);
	input.close();
	registry.write_feature_collection(file->get_reference());
	const auto reloaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
	registry.read_feature_collection(reloaded->get_reference(), errors);
	const auto after = GPlatesAppLogic::create_cached_reconstruction_tree_creator(
			{reloaded->get_reference().get_feature_collection()});
	// Normal .rot saving currently rounds Euler poles to four decimal places. Measure
	// continuity at that precision; do not pretend text round-trips are bit-exact.
	for (double time : {0.0, 450.0, 899.999, 900.0})
	{
		expect_rotation(total(after, time), total(before, 900), 4e-6);
	}
	for (double time : {900.001, 950.0, 1000.0})
	{
		expect_rotation(total(after, time), total(before, time), 4e-6);
	}
	ASSERT_TRUE(input.open(QIODevice::ReadOnly));
	const auto saved = input.readAll();
	EXPECT_TRUE(saved.contains("younger comment"));
	EXPECT_TRUE(saved.contains("older comment"));
}
