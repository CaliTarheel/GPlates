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
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
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
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/UserPreferences.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"
#include "file-io/ReadErrorAccumulation.h"

#include "gui/FeatureFocus.h"

#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"

#include "presentation/ViewState.h"

#include "property-values/Enumeration.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/SplitPlateOperation.h"
#include "view-operations/UndoRedo.h"


namespace
{
	typedef GPlatesViewOperations::SplitPlateOperation Operation;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;
	typedef std::vector<GPlatesMaths::PointOnSphere> Points;
	typedef std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> Geometries;


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
			throw std::runtime_error("Cannot create Split dialog test property.");
		}
	}


	std::string
	without_revisions(
			const QByteArray &gpml)
	{
		// Undo/redo changes the edited feature's revision, but never its identity or data.
		QString text = QString::fromUtf8(gpml);
		text.replace(QRegularExpression("(<gpml:revision>)[^<]+(</gpml:revision>)"),
				"\\1REVISION\\2");
		return text.toStdString();
	}


	QString
	feature_xml(
			const QByteArray &gpml,
			const QString &identity)
	{
		const QRegularExpression member(
				"<gml:featureMember>\\s*<[^>]+>\\s*<gpml:identity>" +
				QRegularExpression::escape(identity) +
				"</gpml:identity>.*?</gml:featureMember>",
				QRegularExpression::DotMatchesEverythingOption);
		const auto match = member.match(QString::fromUtf8(gpml));
		if (!match.hasMatch())
		{
			throw std::runtime_error("Cannot find unchanged feature in Split test GPML.");
		}
		return match.captured();
	}


	void
	expect_gpml_round_trip(
			const QByteArray &actual,
			const QByteArray &expected)
	{
		QString actual_xml = QString::fromUtf8(actual);
		QString expected_xml = QString::fromUtf8(expected);
		const QRegularExpression coordinates("(<gml:posList\\b[^>]*>)([^<]*)(</gml:posList>)");
		auto actual_lists = coordinates.globalMatch(actual_xml);
		auto expected_lists = coordinates.globalMatch(expected_xml);
		unsigned int ring_index = 0;
		while (expected_lists.hasNext())
		{
			SCOPED_TRACE(ring_index++);
			ASSERT_TRUE(actual_lists.hasNext());
			const auto actual_values = actual_lists.next().captured(2).simplified().split(' ');
			const auto expected_values = expected_lists.next().captured(2).simplified().split(' ');
			ASSERT_EQ(actual_values.size(), expected_values.size());
			ASSERT_GE(expected_values.size(), 6);
			ASSERT_EQ(expected_values.size() % 2, 0);
			for (int index = 0; index < expected_values.size(); index += 2)
			{
				SCOPED_TRACE(index / 2);
				bool actual_latitude_valid, actual_longitude_valid;
				bool expected_latitude_valid, expected_longitude_valid;
				const double actual_latitude = actual_values[index].toDouble(&actual_latitude_valid);
				const double actual_longitude = actual_values[index + 1].toDouble(&actual_longitude_valid);
				const double expected_latitude = expected_values[index].toDouble(&expected_latitude_valid);
				const double expected_longitude =
						expected_values[index + 1].toDouble(&expected_longitude_valid);
				ASSERT_TRUE(actual_latitude_valid && actual_longitude_valid);
				ASSERT_TRUE(expected_latitude_valid && expected_longitude_valid);
				const auto actual_point = point(actual_latitude, actual_longitude).position_vector();
				const auto expected_point = point(expected_latitude, expected_longitude).position_vector();
				// GPML stores latitude/longitude; reading normalises unit vectors and can change
				// the last decimal digit when written again. Check every vertex, including holes
				// and ring closure, to a sub-micrometre equivalent on an Earth-sized sphere.
				EXPECT_NEAR(actual_point.x().dval(), expected_point.x().dval(), 1e-13);
				EXPECT_NEAR(actual_point.y().dval(), expected_point.y().dval(), 1e-13);
				EXPECT_NEAR(actual_point.z().dval(), expected_point.z().dval(), 1e-13);
			}
		}
		ASSERT_GT(ring_index, 0u);
		EXPECT_FALSE(actual_lists.hasNext());
		// All identities, revisions, metadata, geometry wrappers and attributes remain exact.
		// Only coordinate text already compared numerically above is replaced here.
		actual_xml.replace(coordinates, "\\1COORDINATES\\3");
		expected_xml.replace(coordinates, "\\1COORDINATES\\3");
		EXPECT_EQ(actual_xml.toStdString(), expected_xml.toStdString());
	}


	class SplitPlateDialogTest :
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
					"GPlatesSplitDialogTests", "location");
			d_original_ini_path = QDir::cleanPath(
					QFileInfo(location.fileName()).absolutePath() + "/..");
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_directory.path());
			QCoreApplication::setApplicationName("split-dialog-tests");
			d_redirected_settings = true;
			d_application.reset(new GPlatesAppLogic::ApplicationState());
			d_application->get_user_preferences().set_value("view/animation/lock_view_time_to_range", false);
			d_view_state.reset(new GPlatesPresentation::ViewState(*d_application));
			undo_stack().clear();
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

		void
		write_collection(
				const Collection &collection,
				const QString &path)
		{
			const auto reference = GPlatesFileIO::File::create_file_reference(
					GPlatesFileIO::FileInfo(path), collection);
			d_application->get_feature_collection_file_format_registry().write_feature_collection(
					*reference);
		}

		QByteArray
		serialise(
				const Collection &collection)
		{
			const QString path = d_directory.filePath("snapshot.gpml");
			write_collection(collection, path);
			QFile file(path);
			if (!file.open(QIODevice::ReadOnly))
			{
				throw std::runtime_error("Cannot read Split test GPML snapshot.");
			}
			return file.readAll();
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type
		make_feature(
				const char *type,
				const QString &name,
				double begin = 1000,
				double end = 0)
		{
			const auto feature = GPlatesModel::FeatureHandle::create(
					GPlatesModel::FeatureType::create_gpml(type));
			add_property(feature->reference(), GPlatesModel::PropertyName::create_gml("name"),
					GPlatesPropertyValues::XsString::create(
							GPlatesUtils::make_icu_string_from_qstring(name)));
			add_property(feature->reference(), GPlatesModel::PropertyName::create_gml("validTime"),
					GPlatesModel::ModelUtils::create_gml_time_period(
							GPlatesPropertyValues::GeoTimeInstant(begin),
							GPlatesPropertyValues::GeoTimeInstant(end)));
			return feature;
		}

		void
		load_synthetic()
		{
			QFile rotations(d_directory.filePath("source.rot"));
			ASSERT_TRUE(rotations.open(QIODevice::WriteOnly | QIODevice::Text));
			const QByteArray data("100 0 90 0 0 0 ! fixed parent\n"
					"100 1100 90 0 0 0 ! fixed parent\n"
					"300 0 90 0 0 100 ! first neighbour\n"
					"300 1100 90 0 0 100 ! first neighbour\n"
					"700 0 90 0 0 100 ! second neighbour\n"
					"700 1100 90 0 0 100 ! second neighbour\n");
			ASSERT_EQ(rotations.write(data), data.size());
			rotations.close();
			const auto crust = GPlatesModel::FeatureCollectionHandle::create();
			const auto ring = GPlatesMaths::PolygonOnSphere::create(Points{
					point(-10, -10), point(-10, 10), point(10, 10), point(10, -10)});
			for (unsigned int index = 0; index < 3; ++index)
			{
				const auto feature = make_feature("ContinentalCrust", "Test continent",
						index == 0 ? 1100 : 1000, index == 0 ? 1001 : 0);
				add_property(feature->reference(),
						GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
						GPlatesPropertyValues::GpmlPlateId::create(index == 2 ? 300 : 100));
				add_property(feature->reference(), GPlatesModel::PropertyName::create_gpml("outlineOf"),
						GPlatesPropertyValues::GmlPolygon::create(ring));
				crust->add(feature);
			}
			const auto ridges = GPlatesModel::FeatureCollectionHandle::create();
			for (unsigned int index = 0; index < 3; ++index)
			{
				const auto feature = make_feature("MidOceanRidge", "Test ridge");
				const auto ref = feature->reference();
				add_property(ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
						GPlatesPropertyValues::GpmlPlateId::create(100));
				add_property(ref, GPlatesModel::PropertyName::create_gpml("rightPlate"),
						GPlatesPropertyValues::GpmlPlateId::create(index == 0 ? 300 : 700));
				add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
						GPlatesPropertyValues::Enumeration::create(
								GPlatesPropertyValues::EnumerationType::create_gpml(
										"ReconstructionMethodEnumeration"), "HalfStageRotationVersion3"));
				add_property(ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
						GPlatesModel::ModelUtils::create_gml_time_instant(
								GPlatesPropertyValues::GeoTimeInstant(1000)));
				const Points vertices = index == 0
						? Points{point(-20, -10), point(-10, -10), point(10, -10), point(20, -10)}
						: Points{point(-20, 0), point(0, 5), point(10, 10), point(20, 15)};
				add_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
						GPlatesAppLogic::GeometryUtils::create_geometry_property_value(
								GPlatesMaths::PolylineOnSphere::create(vertices)));
				ridges->add(feature);
			}
			write_collection(crust->reference(), d_directory.filePath("polygons.gpml"));
			write_collection(ridges->reference(), d_directory.filePath("cutters.gpml"));
			load_inputs(QDir(d_directory.path()), "source.rot");
		}

		void
		load_inputs(
				const QDir &directory,
				const QString &rotation_filename)
		{
			auto &io = d_application->get_feature_collection_file_io();
			d_rotations = io.load_file(directory.filePath(rotation_filename))
					.get_file().get_feature_collection();
			d_crust = io.load_file(directory.filePath("polygons.gpml"))
					.get_file().get_feature_collection();
			d_ridges = io.load_file(directory.filePath("cutters.gpml"))
					.get_file().get_feature_collection();
			d_application->set_anchored_plate_id(0);
			d_application->set_reconstruction_time(1000);
		}

		Geometries
		reconstructed_crust()
		{
			Geometries geometries;
			for (const auto &layer : d_application->get_reconstruct_graph())
			{
				if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
				{
					continue;
				}
				for (const auto &input : layer.get_channel_inputs(
						GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTABLE_FEATURES))
				{
					const auto file = input.get_input_file();
					if (file && file->get_feature_collection() == d_crust)
					{
						const auto proxy = layer.get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
						if (proxy)
						{
							(*proxy)->get_reconstructed_feature_geometries(geometries);
						}
					}
				}
			}
			return geometries;
		}

		Operation::Result
		run_dialog(
				bool accept)
		{
			bool visited = false;
			QTimer driver;
			QObject::connect(&driver, &QTimer::timeout, &driver, [&]()
			{
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || dialog->objectName() != "SplitPlateDialog")
				{
					return;
				}
				driver.stop();
				visited = true;
				// Keep assertion returns inside this lambda so rejection below always executes.
				const auto interact = [&]()
				{
					auto *polygons = dialog->findChild<QComboBox *>("splitPolygonComboBox");
					auto *ridges = dialog->findChild<QListWidget *>("splitPolylinesListWidget");
					auto *button = dialog->findChild<QPushButton *>("splitApplyButton");
					ASSERT_NE(polygons, nullptr);
					ASSERT_NE(ridges, nullptr);
					ASSERT_NE(button, nullptr);
					EXPECT_EQ(polygons->count(), 2); // The older plate-100 feature is inactive.
					ASSERT_EQ(ridges->count(), 3);
					int selected = -1;
					for (int index = 0; index < polygons->count(); ++index)
					{
						if (polygons->itemText(index).contains("| Plate 100 |"))
						{
							EXPECT_EQ(selected, -1);
							selected = index;
						}
					}
					ASSERT_GE(selected, 0);
					polygons->setCurrentIndex(selected);
					EXPECT_FALSE(button->isEnabled());
					ridges->selectAll();
					ASSERT_TRUE(button->isEnabled());
					if (accept)
					{
						button->click();
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
				ADD_FAILURE() << "Split dialog did not complete within five seconds.";
				if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget()))
				{
					dialog->reject();
				}
			});
			driver.start(0);
			watchdog.start(5000);
			Operation operation(d_view_state->get_feature_focus(), *d_application);
			const auto result = operation.trigger(nullptr, *d_view_state);
			EXPECT_TRUE(visited) << result.message.toStdString();
			return result;
		}

		void
		verify_split_and_redo()
		{
			ASSERT_EQ(d_crust->size(), 3u);
			const auto original = serialise(d_crust);
			const auto original_ridges = serialise(d_ridges);
			const auto before = reconstructed_crust();
			ASSERT_EQ(before.size(), 2u);
			double original_area = 0;
			QString selected_id;
			for (const auto &geometry : before)
			{
				const auto &plate = geometry->reconstruction_plate_id();
				if (plate && *plate == 100u)
				{
					const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
							geometry->reconstructed_geometry().get());
					ASSERT_NE(polygon, nullptr);
					original_area = polygon->get_area().dval();
					selected_id = geometry->get_feature_ref()->feature_id().get().qstring();
					d_view_state->get_feature_focus().set_focus(
							geometry->get_feature_ref(), geometry->get_non_null_pointer_to_const());
				}
			}
			ASSERT_FALSE(selected_id.isEmpty());
			auto &focus = d_view_state->get_feature_focus();
			ASSERT_TRUE(focus.is_valid());
			ASSERT_EQ(undo_stack().count(), 0);
			unsigned int focus_clears = 0;
			QObject focus_observer;
			QObject::connect(&focus, &GPlatesGui::FeatureFocus::focus_changed, &focus_observer,
					[&](GPlatesGui::FeatureFocus &updated_focus)
					{
						if (!updated_focus.is_valid())
						{
							++focus_clears;
							// Changing focus clears the active geometry tool's undo stack. It
							// must happen before push, never from an executing undo command.
							EXPECT_EQ(undo_stack().count(), 0);
						}
					});
			std::map<QString, QString> unchanged_revisions;
			std::map<QString, QString> unchanged_xml;
			for (auto feature = d_crust->begin(); feature != d_crust->end(); ++feature)
			{
				const QString id = (*feature)->feature_id().get().qstring();
				if (id != selected_id)
				{
					unchanged_revisions[id] = (*feature)->revision_id().get().qstring();
					unchanged_xml[id] = feature_xml(original, id);
				}
			}
			const auto result = run_dialog(true);
			ASSERT_EQ(result.outcome, Operation::SPLIT_COMPLETED) << result.message.toStdString();
			ASSERT_EQ(undo_stack().count(), 1);
			EXPECT_EQ(undo_stack().index(), 1);
			EXPECT_EQ(focus_clears, 1u);
			EXPECT_FALSE(focus.is_valid());
			const auto split = serialise(d_crust);
			const auto verify_pieces = [&]()
			{
				ASSERT_EQ(d_crust->size(), 4u);
				const auto geometries = reconstructed_crust();
				ASSERT_EQ(geometries.size(), 3u);
				double area = 0;
				unsigned int pieces = 0;
				for (const auto &geometry : geometries)
				{
					ASSERT_TRUE(geometry->is_valid());
					ASSERT_TRUE(geometry->get_feature_ref().is_valid());
					EXPECT_TRUE(geometry->get_feature_ref()->is_active());
					const auto &plate = geometry->reconstruction_plate_id();
					if (plate && *plate == 100u)
					{
						const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
								geometry->reconstructed_geometry().get());
						ASSERT_NE(polygon, nullptr);
						EXPECT_GT(polygon->get_area().dval(), 0);
						area += polygon->get_area().dval();
						++pieces;
					}
				}
				EXPECT_EQ(pieces, 2u);
				EXPECT_NEAR(area, original_area, 1e-10);
				const auto current = serialise(d_crust);
				for (const auto &unchanged : unchanged_xml)
				{
					EXPECT_EQ(feature_xml(current, unchanged.first), unchanged.second);
				}
				for (auto feature = d_crust->begin(); feature != d_crust->end(); ++feature)
				{
					const auto expected = unchanged_revisions.find((*feature)->feature_id().get().qstring());
					if (expected != unchanged_revisions.end())
					{
						EXPECT_EQ((*feature)->revision_id().get().qstring(), expected->second);
					}
				}
				EXPECT_EQ(serialise(d_ridges), original_ridges);
			};
			ASSERT_NO_FATAL_FAILURE(verify_pieces());
			for (unsigned int cycle = 0; cycle < 2; ++cycle)
			{
				SCOPED_TRACE(cycle);
				ASSERT_EQ(undo_stack().count(), 1);
				ASSERT_EQ(undo_stack().index(), 1);
				undo_stack().undo();
				ASSERT_EQ(undo_stack().count(), 1);
				ASSERT_EQ(undo_stack().index(), 0);
				EXPECT_EQ(focus_clears, 1u);
				ASSERT_EQ(d_crust->size(), 3u);
				EXPECT_EQ(without_revisions(serialise(d_crust)), without_revisions(original));
				EXPECT_EQ(serialise(d_ridges), original_ridges);
				undo_stack().redo();
				ASSERT_EQ(undo_stack().count(), 1);
				ASSERT_EQ(undo_stack().index(), 1);
				EXPECT_EQ(focus_clears, 1u);
				ASSERT_NO_FATAL_FAILURE(verify_pieces());
				EXPECT_EQ(without_revisions(serialise(d_crust)), without_revisions(split));
			}
			const QString saved_path = d_directory.filePath("split-output.gpml");
			write_collection(d_crust, saved_path);
			const auto loaded = GPlatesFileIO::File::create_file(GPlatesFileIO::FileInfo(saved_path));
			GPlatesFileIO::ReadErrorAccumulation errors;
			d_application->get_feature_collection_file_format_registry().read_feature_collection(
					loaded->get_reference(), errors);
			ASSERT_TRUE(errors.is_empty());
			const auto saved_collection = loaded->get_reference().get_feature_collection();
			ASSERT_EQ(saved_collection->size(), 4u);
			ASSERT_NO_FATAL_FAILURE(expect_gpml_round_trip(
					serialise(saved_collection), serialise(d_crust)));
			Geometries reloaded_geometries;
			GPlatesAppLogic::ReconstructUtils::reconstruct(
					reloaded_geometries, 1000, 0, {saved_collection}, {d_rotations});
			ASSERT_EQ(reloaded_geometries.size(), 3u);
			double reloaded_area = 0;
			unsigned int reloaded_pieces = 0;
			for (const auto &geometry : reloaded_geometries)
			{
				ASSERT_TRUE(geometry->is_valid());
				const auto &plate = geometry->reconstruction_plate_id();
				if (plate && *plate == 100u)
				{
					const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
							geometry->reconstructed_geometry().get());
					ASSERT_NE(polygon, nullptr);
					reloaded_area += polygon->get_area().dval();
					++reloaded_pieces;
				}
			}
			EXPECT_EQ(reloaded_pieces, 2u);
			EXPECT_NEAR(reloaded_area, original_area, 1e-10);
		}

		QTemporaryDir d_directory;
		QString d_original_application_name;
		QString d_original_ini_path;
		QSettings::Format d_original_settings_format = QSettings::NativeFormat;
		bool d_redirected_settings = false;
		std::unique_ptr<GPlatesAppLogic::ApplicationState> d_application;
		std::unique_ptr<GPlatesPresentation::ViewState> d_view_state;
		Collection d_crust;
		Collection d_ridges;
		Collection d_rotations;
	};
}


TEST_F(SplitPlateDialogTest, PersistedBoundaryVertexAndDuplicateCuttersSplitAndRedo)
{
	ASSERT_NO_FATAL_FAILURE(load_synthetic());
	ASSERT_NO_FATAL_FAILURE(verify_split_and_redo());
}


TEST_F(SplitPlateDialogTest, CancellingPreservesBothFilesAndUndoStack)
{
	ASSERT_NO_FATAL_FAILURE(load_synthetic());
	const auto crust = serialise(d_crust);
	const auto ridges = serialise(d_ridges);
	const auto result = run_dialog(false);
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(undo_stack().count(), 0);
	EXPECT_EQ(serialise(d_crust), crust);
	EXPECT_EQ(serialise(d_ridges), ridges);
}


TEST_F(SplitPlateDialogTest, UnfocusedSplitPreservesEarlierUndoHistory)
{
	ASSERT_NO_FATAL_FAILURE(load_synthetic());
	ASSERT_FALSE(d_view_state->get_feature_focus().is_valid());
	const auto original = serialise(d_crust);
	const auto ridges = serialise(d_ridges);
	auto *previous_command = new QUndoCommand("Previous World Building operation");
	undo_stack().push(previous_command);
	ASSERT_EQ(undo_stack().count(), 1);
	ASSERT_EQ(undo_stack().index(), 1);
	const auto result = run_dialog(true);
	ASSERT_EQ(result.outcome, Operation::SPLIT_COMPLETED) << result.message.toStdString();
	ASSERT_EQ(undo_stack().count(), 2);
	ASSERT_EQ(undo_stack().index(), 2);
	EXPECT_EQ(undo_stack().command(0), previous_command);
	ASSERT_EQ(d_crust->size(), 4u);
	const auto split = serialise(d_crust);
	undo_stack().undo();
	ASSERT_EQ(undo_stack().count(), 2);
	ASSERT_EQ(undo_stack().index(), 1);
	EXPECT_EQ(undo_stack().command(0), previous_command);
	ASSERT_EQ(d_crust->size(), 3u);
	EXPECT_EQ(without_revisions(serialise(d_crust)), without_revisions(original));
	EXPECT_EQ(serialise(d_ridges), ridges);
	undo_stack().redo();
	ASSERT_EQ(undo_stack().count(), 2);
	ASSERT_EQ(undo_stack().index(), 2);
	EXPECT_EQ(undo_stack().command(0), previous_command);
	ASSERT_EQ(d_crust->size(), 4u);
	EXPECT_EQ(without_revisions(serialise(d_crust)), without_revisions(split));
	EXPECT_EQ(serialise(d_ridges), ridges);
	const auto geometries = reconstructed_crust();
	ASSERT_EQ(geometries.size(), 3u);
	for (const auto &geometry : geometries)
	{
		ASSERT_TRUE(geometry->is_valid());
		ASSERT_TRUE(geometry->get_feature_ref().is_valid());
		EXPECT_TRUE(geometry->get_feature_ref()->is_active());
	}
}


TEST_F(SplitPlateDialogTest, OptionalPrivateDataset)
{
	// Private repro files are never part of the test corpus. CI runs the synthetic equivalent.
	const QString input = QString::fromLocal8Bit(qgetenv("GPLATES_SPLIT_REPRO_DIR"));
	if (input.isEmpty())
	{
		GTEST_SKIP() << "Set GPLATES_SPLIT_REPRO_DIR to opt in to a private Split reproduction.";
	}
	const QDir directory(input);
	QString rotation = QString::fromLocal8Bit(qgetenv("GPLATES_SPLIT_REPRO_ROTATION"));
	if (rotation.isEmpty())
	{
		rotation = "rotations.rot";
	}
	ASSERT_EQ(QFileInfo(rotation).fileName(), rotation);
	const std::vector<std::pair<QString, QString>> input_names = {
		{rotation, "rotations.rot"},
		{"polygons.gpml", "polygons.gpml"},
		{"cutters.gpml", "cutters.gpml"}
	};
	std::map<QString, QByteArray> original_files;
	for (const auto &names : input_names)
	{
		const QString path = directory.filePath(names.first);
		QFile file(path);
		ASSERT_TRUE(file.open(QIODevice::ReadOnly)) << path.toStdString();
		original_files[path] = file.readAll();
		file.close();
		ASSERT_TRUE(QFile::copy(path, d_directory.filePath(names.second)));
	}
	ASSERT_NO_FATAL_FAILURE(load_inputs(QDir(d_directory.path()), "rotations.rot"));
	ASSERT_NO_FATAL_FAILURE(verify_split_and_redo());
	for (const auto &original : original_files)
	{
		QFile file(original.first);
		ASSERT_TRUE(file.open(QIODevice::ReadOnly));
		EXPECT_EQ(file.readAll(), original.second);
	}
	if (HasFailure())
	{
		return;
	}
	const QString output = QString::fromLocal8Bit(qgetenv("GPLATES_SPLIT_REPRO_OUTPUT"));
	if (!output.isEmpty())
	{
		// A deliberately requested, new output is the only persistent write. Never overwrite inputs.
		ASSERT_FALSE(QFileInfo::exists(output));
		ASSERT_TRUE(QFileInfo(QFileInfo(output).absolutePath()).isDir());
		write_collection(d_crust, output);
		RecordProperty("split_output", output.toStdString());
	}
}
