/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The GPlates developers
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

#include <memory>
#include <stdexcept>
#include <vector>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <gtest/gtest.h>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/UserPreferences.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "maths/GeometryDistance.h"
#include "maths/GreatCircleArc.h"
#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"

#include "presentation/ViewState.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/GeometryBuilder.h"
#include "view-operations/RiftMakerOperation.h"
#include "view-operations/UndoRedo.h"


namespace
{
	typedef GPlatesViewOperations::RiftMakerOperation Operation;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;
	typedef std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> Geometries;


	std::string
	without_revisions(
			const QByteArray &gpml)
	{
		// Undo is itself a revision. Compare every persisted identity, property and geometry.
		QString text = QString::fromUtf8(gpml);
		text.replace(QRegularExpression("(<gpml:revision>)[^<]+(</gpml:revision>)"),
				"\\1REVISION\\2");
		return text.toStdString();
	}


	GPlatesMaths::PointOnSphere
	point(
			double latitude,
			double longitude)
	{
		return GPlatesMaths::make_point_on_sphere(GPlatesMaths::LatLonPoint(latitude, longitude));
	}


	void
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, name, value))
		{
			throw std::runtime_error("Cannot create Rift dialog test feature property.");
		}
	}


	class RiftMakerDialogTest :
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
					"GPlatesRiftDialogTests", "location");
			d_original_ini_path = QDir::cleanPath(
					QFileInfo(location.fileName()).absolutePath() + "/..");
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_directory.path());
			QCoreApplication::setApplicationName("rift-dialog-tests");
			d_redirected_settings = true;
			d_application.reset(new GPlatesAppLogic::ApplicationState());
			d_application->get_user_preferences().set_value("view/animation/lock_view_time_to_range", false);
			d_view_state.reset(new GPlatesPresentation::ViewState(*d_application));
			undo_stack().clear();

			QFile rotations(d_directory.filePath("source.rot"));
			ASSERT_TRUE(rotations.open(QIODevice::WriteOnly | QIODevice::Text));
			const QByteArray data("101 0 90 0 0 0 ! source plate\n"
					"101 800 90 0 0 0 ! source plate\n"
					"203 0 90 0 15 101 ! reassigned frame\n"
					"203 800 90 0 35 101 ! reassigned frame\n");
			ASSERT_EQ(rotations.write(data), data.size());
			rotations.close();
			auto &io = d_application->get_feature_collection_file_io();
			d_rotations = io.load_file(d_directory.filePath("source.rot"))
					.get_file().get_feature_collection();
			const auto crust = GPlatesModel::FeatureCollectionHandle::create();
			for (double south : {-10.0, 30.0})
			{
				const auto ring = GPlatesMaths::PolygonOnSphere::create(Points{
						point(south, -10), point(south, 10),
						point(south + 20, 10), point(south + 20, -10)});
				d_original_polygons.push_back(ring);
				const auto feature = make_feature("ContinentalCrust", "Target continent");
				add_property(feature->reference(), GPlatesModel::PropertyName::create_gpml("outlineOf"),
						GPlatesPropertyValues::GmlPolygon::create(ring));
				crust->add(feature);
			}
			write_collection(crust->reference(), d_directory.filePath("crust.gpml"));
			d_crust = io.load_file(d_directory.filePath("crust.gpml"))
					.get_file().get_feature_collection();
			const auto sketches = GPlatesModel::FeatureCollectionHandle::create();
			const auto sketch = make_feature("MidOceanRidge", "Full sketch");
			const auto line = GPlatesMaths::PolylineOnSphere::create(Points{
					point(-20, 0), point(0, 3), point(60, 0)});
			add_property(sketch->reference(), GPlatesModel::PropertyName::create_gpml("centerLineOf"),
					GPlatesAppLogic::GeometryUtils::create_geometry_property_value(line));
			sketches->add(sketch);
			write_collection(sketches->reference(), d_directory.filePath("sketch.gpml"));
			d_sketches = io.load_file(d_directory.filePath("sketch.gpml"))
					.get_file().get_feature_collection();
			d_application->set_anchored_plate_id(0);
			d_application->set_reconstruction_time(400);
		}

		void
		TearDown() override
		{
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

		GPlatesModel::FeatureHandle::non_null_ptr_type
		make_feature(
				const char *type,
				const QString &name)
		{
			const auto feature = GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml(type));
			const auto ref = feature->reference();
			add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
							GPlatesUtils::make_icu_string_from_qstring(name)));
			add_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
					GPlatesModel::ModelUtils::create_gml_time_period(
							GPlatesPropertyValues::GeoTimeInstant(800),
							GPlatesPropertyValues::GeoTimeInstant(0)));
			add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
					GPlatesPropertyValues::GpmlPlateId::create(101));
			return feature;
		}

		void
		write_collection(
				const Collection &collection,
				const QString &path)
		{
			const auto file = GPlatesFileIO::File::create_file_reference(
					GPlatesFileIO::FileInfo(path), collection);
			d_application->get_feature_collection_file_format_registry().write_feature_collection(*file);
		}

		QByteArray
		snapshot(
				const Collection &collection)
		{
			const QString path = d_directory.filePath("snapshot.gpml");
			write_collection(collection, path);
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
			{
				throw std::runtime_error("Cannot read Rift snapshot.");
			}
			return file.readAll();
		}

		Geometries
		generated_geometries()
		{
			std::vector<Collection> collections;
			for (const auto &file : d_application->get_feature_collection_file_state().get_loaded_files())
			{
				const auto collection = file.get_file().get_feature_collection();
				if (collection != d_rotations && collection != d_sketches)
				{
					collections.push_back(collection);
				}
			}
			Geometries result;
			GPlatesAppLogic::ReconstructUtils::reconstruct(result, 400, 0, collections, {d_rotations});
			return result;
		}

		Operation::Result
		run_dialog(
				bool apply,
				bool digitised = false,
				bool change_anchor = false,
				bool existing_destination = false)
		{
			bool visited = false;
			QTimer driver;
			QObject::connect(&driver, &QTimer::timeout, &driver, [&]()
			{
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || dialog->objectName() != "RiftMakerDialog")
				{
					return;
				}
				driver.stop();
				visited = true;
				const auto interact = [&]()
				{
					auto *sketch = dialog->findChild<QComboBox *>("riftSketchComboBox");
					auto *targets = dialog->findChild<QListWidget *>("riftPolygonsListWidget");
					auto *preview = dialog->findChild<QPushButton *>("riftPreviewButton");
					auto *table = dialog->findChild<QTableWidget *>("riftPiecesTable");
					auto *review = dialog->findChild<QCheckBox *>("riftAssignmentsReviewedCheckBox");
					auto *create = dialog->findChild<QPushButton *>("riftCreateButton");
					ASSERT_NE(sketch, nullptr);
					ASSERT_NE(targets, nullptr);
					ASSERT_NE(preview, nullptr);
					ASSERT_NE(table, nullptr);
					ASSERT_NE(review, nullptr);
					ASSERT_NE(create, nullptr);
					const int input = digitised ? sketch->findData(-1) : sketch->findText(
							"Full sketch", Qt::MatchContains);
					ASSERT_GE(input, 0);
					sketch->setCurrentIndex(input);
					ASSERT_EQ(targets->count(), 2);
					targets->selectAll();
					if (existing_destination)
					{
						auto *output = dialog->findChild<QComboBox *>("riftOutputCollectionComboBox");
						ASSERT_NE(output, nullptr);
						const int index = output->findText("crust.gpml", Qt::MatchContains);
						ASSERT_GE(index, 0);
						output->setCurrentIndex(index);
					}
					preview->click();
					ASSERT_EQ(table->rowCount(), 4);
					EXPECT_FALSE(create->isEnabled());
					for (int row = 0; row < 4; ++row)
					{
						auto *plate = dialog->findChild<QSpinBox *>(QString("riftPiecePlate%1").arg(row));
						ASSERT_NE(plate, nullptr);
						plate->setValue(row % 2 ? 203 : 101);
					}
					review->setChecked(true);
					ASSERT_TRUE(create->isEnabled());
					if (apply)
					{
						if (change_anchor)
						{
							d_application->set_anchored_plate_id(101);
						}
						create->click();
						if (change_anchor)
						{
							EXPECT_TRUE(dialog->isVisible());
							const auto *status = dialog->findChild<QLabel *>("riftStatusLabel");
							ASSERT_NE(status, nullptr);
							EXPECT_TRUE(status->text().contains("rotation model or anchor"))
									<< status->text().toStdString();
						}
					}
				};
				interact();
				if (dialog->isVisible())
				{
					dialog->reject();
				}
			});
			QTimer watchdog;
			watchdog.setSingleShot(true);
			QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [&]()
			{
				ADD_FAILURE() << "Rift dialog exceeded its test timeout.";
				if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				{
					dialog->reject();
				}
			});
			driver.start(0);
			watchdog.start(10000);
			Operation operation(*d_application, *d_view_state);
			const auto result = operation.trigger(nullptr);
			EXPECT_TRUE(visited) << result.message.toStdString();
			return result;
		}

		void
		check_saved_outputs()
		{
			std::vector<GPlatesFileIO::File::non_null_ptr_type> owners;
			std::vector<Collection> reloaded_collections;
			GPlatesFileIO::ReadErrorAccumulation errors;
			for (const auto &file : d_application->get_feature_collection_file_state().get_loaded_files())
			{
				const auto collection = file.get_file().get_feature_collection();
				if (collection == d_rotations || collection == d_sketches)
				{
					continue;
				}
				const QString path = d_directory.filePath(QString("result-%1.gpml").arg(owners.size()));
				write_collection(collection, path);
				const auto saved = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(path));
				d_application->get_feature_collection_file_format_registry()
						.read_feature_collection(saved->get_reference(), errors);
				owners.push_back(saved);
				reloaded_collections.push_back(saved->get_reference().get_feature_collection());
			}
			ASSERT_TRUE(errors.is_empty());
			Geometries reloaded;
			GPlatesAppLogic::ReconstructUtils::reconstruct(
					reloaded, 400, 0, reloaded_collections, {d_rotations});
			const auto original = generated_geometries();
			ASSERT_EQ(original.size(), 6u);
			ASSERT_EQ(reloaded.size(), original.size());
			for (size_t index = 0; index < original.size(); ++index)
			{
				EXPECT_EQ(reloaded[index]->get_feature_ref()->feature_id(),
						original[index]->get_feature_ref()->feature_id());
				Points expected, actual;
				GPlatesAppLogic::GeometryUtils::get_geometry_points(
						*original[index]->reconstructed_geometry(), expected);
				GPlatesAppLogic::GeometryUtils::get_geometry_points(
						*reloaded[index]->reconstructed_geometry(), actual);
				ASSERT_EQ(actual.size(), expected.size());
				for (size_t vertex = 0; vertex < expected.size(); ++vertex)
				{
					const auto a = actual[vertex].position_vector();
					const auto b = expected[vertex].position_vector();
					EXPECT_NEAR(a.x().dval(), b.x().dval(), 1e-12);
					EXPECT_NEAR(a.y().dval(), b.y().dval(), 1e-12);
					EXPECT_NEAR(a.z().dval(), b.z().dval(), 1e-12);
				}
			}
		}

		void
		check_cut_outputs()
		{
			unsigned int polygons = 0, ridges = 0;
			double area = 0;
			for (const auto &geometry : generated_geometries())
			{
				ASSERT_TRUE(geometry->is_valid());
				if (const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
						geometry->reconstructed_geometry().get()))
				{
					++polygons;
					area += polygon->get_area().dval();
				}
				if (const auto *line = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
						geometry->reconstructed_geometry().get()))
				{
					++ridges;
					for (auto segment = line->begin(); segment != line->end(); ++segment)
					{
						for (double fraction : {0.0, 0.5, 1.0})
						{
							const auto sample = segment->point_on_arc(fraction);
							bool inside = false;
							for (const auto &polygon : d_original_polygons)
							{
								inside = inside || polygon->is_point_in_polygon(sample) ||
										GPlatesMaths::minimum_distance(sample, *polygon, false)
												->get_cosine().dval() > 1.0 - 1e-14;
							}
							EXPECT_TRUE(inside) << "MOR extends beyond the selected polygons.";
						}
					}
				}
			}
			EXPECT_EQ(polygons, 4u);
			EXPECT_EQ(ridges, 2u); // No connecting ridge across the outside gap.
			double original_area = 0;
			for (const auto &polygon : d_original_polygons)
			{
				original_area += polygon->get_area().dval();
			}
			EXPECT_NEAR(area, original_area, 1e-11);
		}

		QTemporaryDir d_directory;
		QString d_original_application_name;
		QString d_original_ini_path;
		QSettings::Format d_original_settings_format = QSettings::NativeFormat;
		bool d_redirected_settings = false;
		std::unique_ptr<GPlatesAppLogic::ApplicationState> d_application;
		std::unique_ptr<GPlatesPresentation::ViewState> d_view_state;
		Collection d_rotations, d_crust, d_sketches;
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> d_original_polygons;
	};
}


TEST_F(RiftMakerDialogTest, MultiplePolygonsProduceTrimmedRidgesAndRepeatableUndo)
{
	const auto crust_before = snapshot(d_crust);
	const auto sketch_before = snapshot(d_sketches);
	const auto result = run_dialog(true);
	ASSERT_EQ(result.outcome, Operation::RIFT_CREATED) << result.message.toStdString();
	ASSERT_EQ(undo_stack().count(), 1);
	for (unsigned int cycle = 0; cycle < 3; ++cycle)
	{
		SCOPED_TRACE(cycle);
		check_cut_outputs();
		EXPECT_EQ(snapshot(d_sketches), sketch_before);
		undo_stack().undo();
		EXPECT_EQ(undo_stack().count(), 1);
		EXPECT_EQ(generated_geometries().size(), 2u);
		EXPECT_EQ(without_revisions(snapshot(d_crust)), without_revisions(crust_before));
		EXPECT_EQ(snapshot(d_sketches), sketch_before);
		undo_stack().redo();
	}
	check_cut_outputs();
	check_saved_outputs();
}


TEST_F(RiftMakerDialogTest, CancellingPreviewPreservesFeatures)
{
	const auto crust_before = snapshot(d_crust);
	const auto sketch_before = snapshot(d_sketches);
	const auto result = run_dialog(false);
	EXPECT_EQ(result.outcome, Operation::CANCELLED);
	EXPECT_EQ(snapshot(d_crust), crust_before);
	EXPECT_EQ(snapshot(d_sketches), sketch_before);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(RiftMakerDialogTest, DigitisedSketchSurvivesCreateUndoAndRedo)
{
	const auto line = GPlatesMaths::PolylineOnSphere::create(Points{
			point(-20, 0), point(0, 3), point(60, 0)});
	auto &builder = d_view_state->get_digitise_geometry_builder();
	builder.set_geometry(GPlatesMaths::GeometryType::POLYLINE,
			line->vertex_begin(), line->vertex_end());
	const auto before = builder.get_geometry_on_sphere();
	ASSERT_TRUE(before);
	const auto result = run_dialog(true, true);
	ASSERT_EQ(result.outcome, Operation::RIFT_CREATED) << result.message.toStdString();
	check_cut_outputs();
	EXPECT_TRUE(builder.get_geometry_on_sphere() == before);
	undo_stack().undo();
	EXPECT_TRUE(builder.get_geometry_on_sphere() == before);
	undo_stack().redo();
	EXPECT_TRUE(builder.get_geometry_on_sphere() == before);
	check_cut_outputs();
}


TEST_F(RiftMakerDialogTest, ChangedAnchorInvalidatesPreviewWithoutEditingFeatures)
{
	const auto crust_before = snapshot(d_crust);
	const auto sketch_before = snapshot(d_sketches);
	const auto result = run_dialog(true, false, true);
	EXPECT_EQ(result.outcome, Operation::CANCELLED);
	EXPECT_EQ(snapshot(d_crust), crust_before);
	EXPECT_EQ(snapshot(d_sketches), sketch_before);
	EXPECT_EQ(undo_stack().count(), 0);
}


TEST_F(RiftMakerDialogTest, ExistingDestinationKeepsUnrelatedCollectionsAndRestoresSource)
{
	const auto crust_before = snapshot(d_crust);
	const auto sketch_before = snapshot(d_sketches);
	const auto file_count = d_application->get_feature_collection_file_state().get_loaded_files().size();
	const auto result = run_dialog(true, false, false, true);
	ASSERT_EQ(result.outcome, Operation::RIFT_CREATED) << result.message.toStdString();
	check_cut_outputs();
	EXPECT_EQ(d_application->get_feature_collection_file_state().get_loaded_files().size(), file_count);
	EXPECT_EQ(snapshot(d_sketches), sketch_before);
	undo_stack().undo();
	EXPECT_EQ(without_revisions(snapshot(d_crust)), without_revisions(crust_before));
	EXPECT_EQ(snapshot(d_sketches), sketch_before);
	undo_stack().redo();
	check_cut_outputs();
	check_saved_outputs();
}
