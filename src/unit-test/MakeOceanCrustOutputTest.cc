/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
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

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <boost/optional.hpp>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTimer>
#include <gtest/gtest.h>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerInputChannelName.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/UserPreferences.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/PropertyValueFinder.h"

#include "presentation/ViewState.h"

#include "property-values/Enumeration.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/MakeOceanCrustOperation.h"
#include "view-operations/UndoRedo.h"


namespace
{
	typedef GPlatesViewOperations::MakeOceanCrustOperation Operation;
	typedef GPlatesAppLogic::FeatureCollectionFileState::file_reference FileReference;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;


	void
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, name, value))
		{
			throw std::runtime_error("Cannot create output-picker test feature property.");
		}
	}


	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	std::string
	normalise_redo_revisions(
			const QByteArray &gpml)
	{
		// Redo restores deep property snapshots on the retained generated features, advancing
		// their revision IDs. Preserve identities and every property byte; only the top-level
		// revision immediately following each identity is ignored in the redo comparison.
		// Each caller separately checks the original target feature's revision remains unchanged.
		QString text = QString::fromUtf8(gpml);
		text.replace(QRegularExpression(
				"(<gpml:identity>[^<]+</gpml:identity>\\s*<gpml:revision>)[^<]+(</gpml:revision>)"),
				"\\1GENERATED-REVISION\\2");
		return text.toStdString();
	}


	class MakeOceanCrustOutputTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_directory.isValid());
			d_original_application_name = QCoreApplication::applicationName();
			d_original_settings_format = QSettings::defaultFormat();
			const QSettings location(QSettings::IniFormat, QSettings::UserScope,
					"GPlatesCrustOutputTests", "location");
			d_original_ini_path = QDir::cleanPath(
					QFileInfo(location.fileName()).absolutePath() + "/..");
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_directory.path());
			QCoreApplication::setApplicationName("crust-output-tests");
			d_redirected_settings = true;

			d_application.reset(new GPlatesAppLogic::ApplicationState());
			d_application->get_user_preferences().set_value("view/animation/lock_view_time_to_range", false);
			d_view_state.reset(new GPlatesPresentation::ViewState(*d_application));
			undo_stack().clear();
			load_rotations("source.rot");
			d_rotation_layer =
					d_application->get_reconstruct_graph().get_default_reconstruction_tree_layer();
			ASSERT_TRUE(d_rotation_layer.is_valid());

			d_ridge = make_ridge(801);
			d_source_file = create_collection("Source ridges", {*d_ridge});
			d_source = d_source_file->get_file().get_feature_collection();
			d_source_layer = reconstruct_layer(d_source);
			ASSERT_TRUE(d_source_layer.is_valid());
			connect_model(d_source_layer, d_rotation_layer);
			d_application->set_reconstruction_time(1000);
		}

		void
		TearDown() override
		{
			// Commands hold ApplicationState references, so destroy them before the model.
			undo_stack().clear();
			d_view_state.reset();
			d_application.reset();
			if (d_redirected_settings)
			{
				QCoreApplication::setApplicationName(d_original_application_name);
				QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_original_ini_path);
				QSettings::setDefaultFormat(d_original_settings_format);
			}
		}

		QUndoStack &
		undo_stack()
		{
			return GPlatesViewOperations::UndoRedo::instance().get_active_undo_stack();
		}

		void
		load_rotations(
				const QString &filename)
		{
			QFile file(d_directory.filePath(filename));
			ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Text));
			const QByteArray data(
					"101 0 0 90 3 0 ! junction A\n"
					"101 950 0 90 3 0 ! junction A\n"
					"101 1000 0 90 0 0 ! junction A\n"
					"202 0 0 210 3 0 ! junction B\n"
					"202 950 0 210 3 0 ! junction B\n"
					"202 1000 0 210 0 0 ! junction B\n"
					"303 0 0 330 3 0 ! junction C\n"
					"303 950 0 330 3 0 ! junction C\n"
					"303 1000 0 330 0 0 ! junction C\n"
					"201 0 90 0 0 0 ! left\n"
					"201 1000 90 0 20 0 ! left\n"
					"501 0 90 0 0 201 ! stationary relative to left\n"
					"501 1000 90 0 0 201 ! stationary relative to left\n"
					"801 0 90 0 0 201 ! right\n"
					"801 1000 90 0 40 201 ! right\n");
			ASSERT_EQ(file.write(data), data.size());
			file.close();
			d_application->get_feature_collection_file_io().load_file(file.fileName());
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type
		make_ridge(
				unsigned int right_plate,
				unsigned int left_plate = 201,
				double longitude = 20,
				const QString &name = "Test ridge",
				bool junction = false)
		{
			const auto feature = GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml("MidOceanRidge"));
			const auto ref = feature->reference();
			add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
			add_property(ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
					GPlatesPropertyValues::GpmlPlateId::create(left_plate));
			add_property(ref, GPlatesModel::PropertyName::create_gpml("rightPlate"),
					GPlatesPropertyValues::GpmlPlateId::create(right_plate));
			add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
					GPlatesPropertyValues::Enumeration::create(
							GPlatesPropertyValues::EnumerationType::create_gpml("ReconstructionMethodEnumeration"),
							"HalfStageRotationVersion3"));
			add_property(ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
					GPlatesModel::ModelUtils::create_gml_time_instant(
							GPlatesPropertyValues::GeoTimeInstant(1000)));
			const std::vector<GPlatesMaths::PointOnSphere> vertices = junction
					? std::vector<GPlatesMaths::PointOnSphere>{point(80, longitude), point(88, longitude)}
					: std::vector<GPlatesMaths::PointOnSphere>{point(-10, longitude), point(10, longitude)};
			add_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
					GPlatesAppLogic::GeometryUtils::create_geometry_property_value(
							GPlatesMaths::PolylineOnSphere::create(vertices)));
			return feature;
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type
		make_existing_feature()
		{
			const auto feature = GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml("UnclassifiedFeature"));
			add_property(feature->reference(), GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
							GPlatesUtils::make_icu_string_from_qstring("Keep this original feature")));
			add_property(feature->reference(),
					GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
					GPlatesPropertyValues::GpmlPlateId::create(201));
			add_property(feature->reference(),
					GPlatesModel::PropertyName::create_gpml("unclassifiedGeometry"),
					GPlatesAppLogic::GeometryUtils::create_geometry_property_value(
							point(5, 10).get_geometry_on_sphere()));
			return feature;
		}

		FileReference
		create_collection(
				const QString &label,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &features)
		{
			const auto collection = GPlatesModel::FeatureCollectionHandle::create();
			for (const auto &feature : features)
			{
				collection->add(feature);
			}
			return d_application->get_feature_collection_file_io().create_file(
					GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(QString(), label), collection),
					false);
		}

		GPlatesAppLogic::Layer
		reconstruct_layer(
				const Collection &collection)
		{
			for (auto layer : d_application->get_reconstruct_graph())
			{
				if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
				{
					continue;
				}
				for (const auto &input : layer.get_channel_inputs(
						GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTABLE_FEATURES))
				{
					const auto file = input.get_input_file();
					if (file && file->get_feature_collection() == collection)
					{
						return layer;
					}
				}
			}
			return GPlatesAppLogic::Layer();
		}

		void
		connect_model(
				GPlatesAppLogic::Layer layer,
				const GPlatesAppLogic::Layer &model)
		{
			layer.disconnect_channel_inputs(GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE);
			layer.connect_input_to_layer_output(model,
					GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE);
		}

		std::vector<GPlatesAppLogic::Layer>
		connected_models(
				const GPlatesAppLogic::Layer &layer)
		{
			std::vector<GPlatesAppLogic::Layer> result;
			for (const auto &input : layer.get_channel_inputs(
					GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE))
			{
				if (const auto model = input.get_input_layer())
				{
					result.push_back(*model);
				}
			}
			return result;
		}

		QByteArray
		serialise(
				const Collection &collection)
		{
			const QString path = d_directory.filePath("snapshot.gpml");
			const auto reference = GPlatesFileIO::File::create_file_reference(
					GPlatesFileIO::FileInfo(path), collection);
			d_application->get_feature_collection_file_format_registry().write_feature_collection(
					*reference);
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
			{
				throw std::runtime_error("Cannot read test GPML snapshot.");
			}
			return file.readAll();
		}


		void
		expect_generated_features_reconstructed(
				const Collection &collection,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &original)
		{
			// At the final sampled age every generated strip exists and every flowline is drawn.
			// Container membership alone is insufficient: undo deactivates removed features.
			const double original_time = d_application->get_current_reconstruction_time();
			d_application->set_reconstruction_time(990);
			const auto layer = reconstruct_layer(collection);
			ASSERT_TRUE(layer.is_valid());
			const auto output = layer.get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
			ASSERT_TRUE(output);
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*output)->get_reconstructed_feature_geometries(geometries);
			for (auto feature = collection->begin(); feature != collection->end(); ++feature)
			{
				if (&**feature == original.get())
				{
					continue;
				}
				const auto ref = (*feature)->reference();
				EXPECT_TRUE((*feature)->is_active());
				EXPECT_TRUE(ref.is_valid());
				bool reconstructed = false;
				for (const auto &geometry : geometries)
				{
					if (geometry->is_valid() && geometry->get_feature_ref() == ref)
					{
						reconstructed = true;
						break;
					}
				}
				EXPECT_TRUE(reconstructed) << "A restored feature is missing from its reconstructed layer.";
			}
			d_application->set_reconstruction_time(original_time);
		}

		Operation::Result
		run_dialog(
				const std::function<void(QDialog &)> &interact,
				Operation::Mode mode = Operation::FLOW_LINES)
		{
			bool visited = false;
			QTimer driver;
			QObject::connect(&driver, &QTimer::timeout, &driver, [&]()
			{
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || dialog->objectName() != "makeOceanCrustDialog")
				{
					return;
				}
				driver.stop();
				visited = true;
				try
				{
					interact(*dialog);
				}
				catch (const std::exception &exception)
				{
					ADD_FAILURE() << exception.what();
				}
				// A failed assertion inside interact must never strand a modal test window.
				if (dialog->isVisible())
				{
					dialog->reject();
				}
			});
			QTimer watchdog;
			watchdog.setSingleShot(true);
			QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [&]()
			{
				ADD_FAILURE() << "Output dialog did not complete within five seconds.";
				if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				{
					dialog->reject();
				}
			});
			driver.start(0);
			watchdog.start(5000);
			Operation operation(*d_application, *d_view_state);
			const auto result = operation.trigger(nullptr, mode);
			EXPECT_TRUE(visited) << result.message.toStdString();
			return result;
		}

		void
		choose_destination(
				QDialog &dialog,
				const QString &label)
		{
			auto *combo = dialog.findChild<QComboBox *>("outputCollectionCombo");
			ASSERT_NE(combo, nullptr);
			const int index = combo->findText(label, Qt::MatchStartsWith);
			ASSERT_GT(index, 0);
			combo->setCurrentIndex(index);
		}

		void
		click_create(
				QDialog &dialog)
		{
			auto *ridges = dialog.findChild<QListWidget *>("ridgeList");
			auto *duration = dialog.findChild<QDoubleSpinBox *>("futureDurationMyr");
			auto *interval = dialog.findChild<QDoubleSpinBox *>("stepIntervalMyr");
			auto *button = dialog.findChild<QPushButton *>("createFlowlinesButton");
			ASSERT_NE(ridges, nullptr);
			ASSERT_NE(duration, nullptr);
			ASSERT_NE(interval, nullptr);
			ASSERT_NE(button, nullptr);
			// The real dialog preselects only the focused feature; this headless fixture has none.
			int selected = 0;
			for (int index = 0; index < ridges->count(); ++index)
			{
				auto *ridge = ridges->item(index);
				if (ridge->flags().testFlag(Qt::ItemIsEnabled) &&
						ridge->flags().testFlag(Qt::ItemIsUserCheckable))
				{
					ridge->setCheckState(Qt::Checked);
					++selected;
				}
			}
			ASSERT_EQ(selected, 1);
			duration->setValue(10);
			interval->setValue(5);
			button->click();
		}

		std::size_t
		file_count()
		{
			return d_application->get_feature_collection_file_state().get_loaded_files().size();
		}

		QTemporaryDir d_directory;
		QString d_original_application_name;
		QString d_original_ini_path;
		QSettings::Format d_original_settings_format = QSettings::NativeFormat;
		bool d_redirected_settings = false;
		std::unique_ptr<GPlatesAppLogic::ApplicationState> d_application;
		std::unique_ptr<GPlatesPresentation::ViewState> d_view_state;
		boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> d_ridge;
		boost::optional<FileReference> d_source_file;
		Collection d_source;
		GPlatesAppLogic::Layer d_source_layer;
		GPlatesAppLogic::Layer d_rotation_layer;
	};
}


TEST_F(MakeOceanCrustOutputTest, AppendsToCompatibleTargetAndUndoRedoPreserveExistingData)
{
	const auto existing = make_existing_feature();
	const auto target = create_collection("Existing output", {existing});
	const auto collection = target.get_file().get_feature_collection();
	const auto layer = reconstruct_layer(collection);
	ASSERT_TRUE(layer.is_valid());
	connect_model(layer, d_rotation_layer);
	const auto original_target = serialise(collection);
	const auto existing_revision = existing->revision_id();
	const auto original_source = serialise(d_source);
	const auto models = connected_models(layer);
	const auto source_models = connected_models(d_source_layer);
	const auto files = file_count();
	const auto result = run_dialog([&](QDialog &dialog)
	{
		choose_destination(dialog, "Existing output");
		click_create(dialog);
	});
	ASSERT_EQ(result.outcome, Operation::FLOWLINES_CREATED) << result.message.toStdString();
	EXPECT_EQ(existing->revision_id(), existing_revision);
	EXPECT_EQ(collection->size(), 2u);
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(existing->parent_ptr(), collection.handle_ptr());
	EXPECT_EQ(connected_models(layer), models);
	EXPECT_EQ(connected_models(d_source_layer), source_models);
	EXPECT_EQ(serialise(d_source), original_source);
	ASSERT_EQ(undo_stack().count(), 1);
	const auto generated_target = serialise(collection);

	undo_stack().undo();
	EXPECT_EQ(serialise(collection), original_target);
	EXPECT_EQ(connected_models(layer), models);
	EXPECT_EQ(serialise(d_source), original_source);
	undo_stack().redo();
	ASSERT_EQ(collection->size(), 2u);
	expect_generated_features_reconstructed(collection, existing);
	EXPECT_EQ(existing->revision_id(), existing_revision);
	EXPECT_EQ(normalise_redo_revisions(serialise(collection)),
			normalise_redo_revisions(generated_target));
	EXPECT_EQ(connected_models(layer), models);
	EXPECT_EQ(connected_models(d_source_layer), source_models);
	EXPECT_EQ(serialise(d_source), original_source);
}


TEST_F(MakeOceanCrustOutputTest, EmptyUnsavedTargetIsSelectableWithoutCreatingAnotherCollection)
{
	const auto target = create_collection("Empty output", {});
	const auto collection = target.get_file().get_feature_collection();
	const auto files = file_count();
	const auto result = run_dialog([&](QDialog &dialog)
	{
		choose_destination(dialog, "Empty output");
		click_create(dialog);
	});
	ASSERT_EQ(result.outcome, Operation::FLOWLINES_CREATED) << result.message.toStdString();
	EXPECT_EQ(collection->size(), 1u);
	EXPECT_EQ(file_count(), files);
	const auto layer = reconstruct_layer(collection);
	ASSERT_TRUE(layer.is_valid());
	// An existing empty target retains the application's implicit default-model connection.
	EXPECT_TRUE(connected_models(layer).empty());
	EXPECT_EQ(d_application->get_reconstruct_graph().get_default_reconstruction_tree_layer(),
			d_rotation_layer);
	undo_stack().undo();
	EXPECT_EQ(collection->size(), 0u);
	EXPECT_EQ(file_count(), files);
}


TEST_F(MakeOceanCrustOutputTest, CrustModeAppendsPolygonsAndUndoRedoPreservesTheTarget)
{
	const auto existing = make_existing_feature();
	const auto target = create_collection("Crust output", {existing});
	const auto collection = target.get_file().get_feature_collection();
	const auto layer = reconstruct_layer(collection);
	ASSERT_TRUE(layer.is_valid());
	connect_model(layer, d_rotation_layer);
	const auto original_target = serialise(collection);
	const auto existing_revision = existing->revision_id();
	const auto original_source = serialise(d_source);
	const auto models = connected_models(layer);
	const auto files = file_count();
	const auto result = run_dialog([&](QDialog &dialog)
	{
		choose_destination(dialog, "Crust output");
		click_create(dialog);
	}, Operation::OCEAN_CRUST);
	ASSERT_EQ(result.outcome, Operation::CRUST_CREATED) << result.message.toStdString();
	EXPECT_EQ(existing->revision_id(), existing_revision);
	// Two sides, one ridge segment, two five-million-year intervals, plus the original.
	ASSERT_EQ(collection->size(), 5u);
	for (auto feature = collection->begin(); feature != collection->end(); ++feature)
	{
		if (&**feature == existing.get())
		{
			continue;
		}
		EXPECT_EQ((*feature)->feature_type(), GPlatesModel::FeatureType::create_gpml("OceanicCrust"));
		EXPECT_TRUE(GPlatesModel::get_property_value<GPlatesPropertyValues::GmlPolygon>(
				(*feature)->reference(), GPlatesModel::PropertyName::create_gpml("outlineOf")));
	}
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(serialise(d_source), original_source);
	EXPECT_EQ(connected_models(layer), models);
	const auto generated_target = serialise(collection);
	ASSERT_EQ(undo_stack().count(), 1);
	undo_stack().undo();
	EXPECT_EQ(serialise(collection), original_target);
	EXPECT_EQ(connected_models(layer), models);
	undo_stack().redo();
	ASSERT_EQ(collection->size(), 5u);
	expect_generated_features_reconstructed(collection, existing);
	EXPECT_EQ(existing->revision_id(), existing_revision);
	EXPECT_EQ(normalise_redo_revisions(serialise(collection)),
			normalise_redo_revisions(generated_target));
	EXPECT_EQ(connected_models(layer), models);
	EXPECT_EQ(serialise(d_source), original_source);
}


TEST_F(MakeOceanCrustOutputTest, CancelDoesNotCreateEmptyCollectionsOrChangeTheSource)
{
	const auto files = file_count();
	const auto source = serialise(d_source);
	const auto result = run_dialog([](QDialog &dialog)
	{
		dialog.reject();
	});
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(serialise(d_source), source);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(MakeOceanCrustOutputTest, InvalidRangeRemainsInDialogWithoutCreatingCollections)
{
	const auto files = file_count();
	const auto source = serialise(d_source);
	const auto result = run_dialog([](QDialog &dialog)
	{
		auto *duration = dialog.findChild<QDoubleSpinBox *>("futureDurationMyr");
		auto *button = dialog.findChild<QPushButton *>("createFlowlinesButton");
		auto *error = dialog.findChild<QLabel *>("flowlineValidationError");
		ASSERT_NE(duration, nullptr);
		ASSERT_NE(button, nullptr);
		ASSERT_NE(error, nullptr);
		duration->setValue(1001);
		button->click();
		EXPECT_TRUE(dialog.isVisible());
		EXPECT_FALSE(error->text().isEmpty());
	});
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(serialise(d_source), source);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(MakeOceanCrustOutputTest, FailedGenerationDoesNotLeaveANewEmptyCollection)
{
	// A distinct plate with no relative motion passes input checks but cannot generate flowlines.
	d_ridge.get()->remove_from_parent();
	d_ridge = make_ridge(501);
	d_source->add(*d_ridge);
	const auto files = file_count();
	const auto source = serialise(d_source);
	const auto result = run_dialog([&](QDialog &dialog)
	{
		click_create(dialog);
	});
	EXPECT_EQ(result.outcome, Operation::OPERATION_ERROR);
	EXPECT_FALSE(result.message.isEmpty());
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(serialise(d_source), source);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(MakeOceanCrustOutputTest, IncompatibleModelIsRefusedWithoutRewiringTheTarget)
{
	load_rotations("other.rot");
	const auto other_model =
			d_application->get_reconstruct_graph().get_default_reconstruction_tree_layer();
	ASSERT_TRUE(other_model.is_valid());
	ASSERT_NE(other_model, d_rotation_layer);
	const auto target = create_collection("Other model output", {make_existing_feature()});
	const auto collection = target.get_file().get_feature_collection();
	const auto layer = reconstruct_layer(collection);
	ASSERT_TRUE(layer.is_valid());
	connect_model(layer, other_model);
	const auto target_before = serialise(collection);
	const auto source_before = serialise(d_source);
	const auto models = connected_models(layer);
	const auto files = file_count();
	const auto result = run_dialog([&](QDialog &dialog)
	{
		choose_destination(dialog, "Other model output");
		click_create(dialog);
		auto *error = dialog.findChild<QLabel *>("flowlineValidationError");
		ASSERT_NE(error, nullptr);
		EXPECT_TRUE(error->text().contains("different rotation model", Qt::CaseInsensitive))
				<< error->text().toStdString();
		EXPECT_TRUE(dialog.isVisible());
	});
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(serialise(collection), target_before);
	EXPECT_EQ(serialise(d_source), source_before);
	EXPECT_EQ(connected_models(layer), models);
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(MakeOceanCrustOutputTest, UnloadedDestinationIsRejectedWithoutUsingAStaleFileReference)
{
	const auto target = create_collection("Unload this output", {make_existing_feature()});
	const auto files = file_count();
	const auto source = serialise(d_source);
	const auto result = run_dialog([&](QDialog &dialog)
	{
		choose_destination(dialog, "Unload this output");
		d_application->get_feature_collection_file_io().unload_file(target);
		click_create(dialog);
		auto *error = dialog.findChild<QLabel *>("flowlineValidationError");
		ASSERT_NE(error, nullptr);
		EXPECT_TRUE(error->text().contains("no longer loaded", Qt::CaseInsensitive))
				<< error->text().toStdString();
		EXPECT_TRUE(dialog.isVisible());
	});
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(file_count(), files - 1);
	EXPECT_EQ(serialise(d_source), source);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(MakeOceanCrustOutputTest, TripleJunctionPreviewCreatesNativeCrustWithAtomicUndoRedo)
{
	const auto ridge_file = create_collection("Three junction arms", {
			make_ridge(202, 101, 60, "Junction arm A/B", true),
			make_ridge(303, 202, 180, "Junction arm B/C", true),
			make_ridge(101, 303, -60, "Junction arm C/A", true)});
	const auto source = ridge_file.get_file().get_feature_collection();
	connect_model(reconstruct_layer(source), d_rotation_layer);
	const auto target = create_collection("Junction output", {});
	const auto collection = target.get_file().get_feature_collection();
	const auto originals = serialise(source);
	const auto original_other_ridge = serialise(d_source);
	const auto original_target = serialise(collection);
	const auto files = file_count();
	const auto result = run_dialog([&](QDialog &dialog)
	{
		auto *mode = dialog.findChild<QComboBox *>("crustModeCombo");
		auto *list = dialog.findChild<QListWidget *>("ridgeList");
		auto *duration = dialog.findChild<QDoubleSpinBox *>("futureDurationMyr");
		auto *interval = dialog.findChild<QDoubleSpinBox *>("stepIntervalMyr");
		auto *preview = dialog.findChild<QPushButton *>("previewTripleJunctionButton");
		auto *create = dialog.findChild<QPushButton *>("createFlowlinesButton");
		auto *reviewed = dialog.findChild<QCheckBox *>("tripleJunctionReviewed");
		auto *positions = dialog.findChild<QTableWidget *>("tripleJunctionSamples");
		auto *error = dialog.findChild<QLabel *>("flowlineValidationError");
		ASSERT_TRUE(mode && list && duration && interval && preview && create && reviewed && positions && error);
		EXPECT_DOUBLE_EQ(duration->value(), 50);
		EXPECT_DOUBLE_EQ(interval->value(), 10);
		mode->setCurrentIndex(1);
		unsigned int selected = 0;
		for (int index = 0; index < list->count(); ++index)
		{
			const bool choose = list->item(index)->text().startsWith("Junction arm");
			list->item(index)->setCheckState(choose ? Qt::Checked : Qt::Unchecked);
			selected += choose ? 1 : 0;
		}
		ASSERT_EQ(selected, 3u);
		choose_destination(dialog, "Junction output");
		preview->click();
		ASSERT_TRUE(reviewed->isEnabled()) << error->text().toStdString();
		ASSERT_EQ(positions->rowCount(), 6);
		EXPECT_EQ(positions->item(0, 0)->text(), "1000");
		EXPECT_EQ(positions->item(5, 0)->text(), "950");
		EXPECT_TRUE(dialog.findChild<QLabel *>("tripleJunctionSummary")->text().contains("150 polygons"));
		create->click();
		EXPECT_TRUE(dialog.isVisible());
		EXPECT_TRUE(error->text().contains("confirm"));
		reviewed->setChecked(true);
		duration->setValue(49);
		EXPECT_FALSE(reviewed->isEnabled());
		EXPECT_FALSE(reviewed->isChecked());
		EXPECT_EQ(positions->rowCount(), 0);
		duration->setValue(50);
		preview->click();
		ASSERT_TRUE(reviewed->isEnabled()) << error->text().toStdString();
		reviewed->setChecked(true);
		create->click();
	}, Operation::OCEAN_CRUST);
	ASSERT_EQ(result.outcome, Operation::CRUST_CREATED) << result.message.toStdString();
	ASSERT_EQ(collection->size(), 150u);
	EXPECT_EQ(file_count(), files);
	EXPECT_EQ(serialise(source), originals);
	EXPECT_EQ(serialise(d_source), original_other_ridge);
	ASSERT_EQ(undo_stack().count(), 1);
	const auto generated = normalise_redo_revisions(serialise(collection));
	for (unsigned int cycle = 0; cycle < 3; ++cycle)
	{
		undo_stack().undo();
		EXPECT_EQ(serialise(collection), original_target);
		undo_stack().redo();
		ASSERT_EQ(collection->size(), 150u);
		EXPECT_EQ(normalise_redo_revisions(serialise(collection)), generated);
		const auto output = reconstruct_layer(collection).
				get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
		ASSERT_TRUE(output);
		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*output)->get_reconstructed_feature_geometries(geometries);
		EXPECT_EQ(geometries.size(), 150u);
		EXPECT_EQ(serialise(source), originals);
		EXPECT_EQ(serialise(d_source), original_other_ridge);
	}
}
