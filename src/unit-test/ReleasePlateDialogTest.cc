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
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <gtest/gtest.h>

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/UserPreferences.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/File.h"
#include "file-io/FileInfo.h"

#include "maths/FiniteRotation.h"
#include "maths/LatLonPoint.h"
#include "maths/PointOnSphere.h"

#include "model/FeatureCollectionHandle.h"

#include "presentation/ViewState.h"

#include "view-operations/ReleasePlateOperation.h"
#include "view-operations/UndoRedo.h"


namespace
{
	typedef GPlatesViewOperations::ReleasePlateOperation Operation;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;


	void
	expect_same_rotation(
			const GPlatesMaths::FiniteRotation &actual,
			const GPlatesMaths::FiniteRotation &expected)
	{
		for (const auto &location : {GPlatesMaths::LatLonPoint(0, 0),
				GPlatesMaths::LatLonPoint(0, 90), GPlatesMaths::LatLonPoint(90, 0)})
		{
			const auto point = GPlatesMaths::make_point_on_sphere(location);
			const auto a = (actual * point).position_vector();
			const auto b = (expected * point).position_vector();
			EXPECT_NEAR(a.x().dval(), b.x().dval(), 1e-11);
			EXPECT_NEAR(a.y().dval(), b.y().dval(), 1e-11);
			EXPECT_NEAR(a.z().dval(), b.z().dval(), 1e-11);
		}
	}


	class ReleasePlateDialogTest :
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
					"GPlatesReleaseDialogTests", "location");
			d_original_ini_path = QDir::cleanPath(
					QFileInfo(location.fileName()).absolutePath() + "/..");
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_directory.path());
			QCoreApplication::setApplicationName("release-dialog-tests");
			d_redirected_settings = true;
			d_application.reset(new GPlatesAppLogic::ApplicationState());
			d_application->get_user_preferences().set_value("view/animation/lock_view_time_to_range", false);
			d_view_state.reset(new GPlatesPresentation::ViewState(*d_application));
			undo_stack().clear();

			QFile rotations(d_directory.filePath("source.rot"));
			ASSERT_TRUE(rotations.open(QIODevice::WriteOnly | QIODevice::Text));
			d_original_file = "011 0 90 0 30 000 ! parent\n"
					"011 800 90 0 80 000 ! parent\n"
					"234 0 0 0 15 011 ! relative child\n"
					"234 800 0 0 15 011 ! relative child\n"
					"345 0 0 90 3 234 ! descendant\n"
					"345 800 0 90 5 234 ! descendant\n";
			ASSERT_EQ(rotations.write(d_original_file), d_original_file.size());
			rotations.close();
			d_rotations = d_application->get_feature_collection_file_io()
					.load_file(d_directory.filePath("source.rot")).get_file().get_feature_collection();
			d_application->set_anchored_plate_id(11);
			d_application->set_reconstruction_time(375.5);
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

		GPlatesAppLogic::ReconstructionTreeCreator
		trees()
		{
			return GPlatesAppLogic::create_cached_reconstruction_tree_creator({d_rotations});
		}

		GPlatesMaths::FiniteRotation
		rotation(
				const GPlatesAppLogic::ReconstructionTreeCreator &creator,
				double time,
				unsigned int plate = 234,
				unsigned int anchor = 0)
		{
			const auto result = creator.get_reconstruction_tree(time, anchor)
					->get_composed_absolute_rotation_or_none(plate);
			if (!result)
			{
				throw std::runtime_error("Released test plate disappeared from reconstruction.");
			}
			return *result;
		}

		QByteArray
		snapshot()
		{
			const QString filename = d_directory.filePath("snapshot.gpml");
			const auto file = GPlatesFileIO::File::create_file_reference(
					GPlatesFileIO::FileInfo(filename), d_rotations);
			d_application->get_feature_collection_file_format_registry().write_feature_collection(*file);
			QFile input(filename);
			if (!input.open(QIODevice::ReadOnly))
			{
				throw std::runtime_error("Cannot read rotation snapshot.");
			}
			return input.readAll();
		}

		Operation::Result
		run_dialog(
				bool apply)
		{
			bool visited = false;
			QTimer driver;
			QObject::connect(&driver, &QTimer::timeout, &driver, [&]()
			{
				auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (!dialog || dialog->objectName() != "freePlateDialog")
				{
					return;
				}
				driver.stop();
				visited = true;
				const auto interact = [&]()
				{
					auto *plate = dialog->findChild<QSpinBox *>("freePlateId");
					auto *time = dialog->findChild<QDoubleSpinBox *>("freePlateTime");
					auto *review = dialog->findChild<QPushButton *>("freePlateReviewButton");
					auto *replace = dialog->findChild<QCheckBox *>("freePlateReplaceYounger");
					auto *button = dialog->findChild<QPushButton *>("freePlateApplyButton");
					ASSERT_NE(plate, nullptr);
					ASSERT_NE(time, nullptr);
					ASSERT_NE(review, nullptr);
					ASSERT_NE(replace, nullptr);
					ASSERT_NE(button, nullptr);
					EXPECT_DOUBLE_EQ(time->value(), 375.5);
					plate->setValue(234);
					review->click();
					EXPECT_FALSE(button->isEnabled());
					replace->setChecked(true);
					ASSERT_TRUE(button->isEnabled());
					// A previous review must not authorize a different plate or event age.
					time->setValue(400);
					EXPECT_FALSE(button->isEnabled());
					time->setValue(375.5);
					review->click();
					replace->setChecked(true);
					ASSERT_TRUE(button->isEnabled());
					if (apply)
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
				ADD_FAILURE() << "Free Plate dialog exceeded its test timeout.";
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
		expect_file_unchanged()
		{
			QFile original(d_directory.filePath("source.rot"));
			ASSERT_TRUE(original.open(QIODevice::ReadOnly | QIODevice::Text));
			EXPECT_EQ(original.readAll(), d_original_file);
		}

		QTemporaryDir d_directory;
		QString d_original_application_name;
		QString d_original_ini_path;
		QSettings::Format d_original_settings_format = QSettings::NativeFormat;
		bool d_redirected_settings = false;
		std::unique_ptr<GPlatesAppLogic::ApplicationState> d_application;
		std::unique_ptr<GPlatesPresentation::ViewState> d_view_state;
		Collection d_rotations;
		QByteArray d_original_file;
	};
}


TEST_F(ReleasePlateDialogTest, GeneralPlateAndFractionalAgePreservePoseAndUndoHistory)
{
	const auto before = trees();
	auto *earlier = new QUndoCommand("Earlier world-building edit");
	undo_stack().push(earlier);
	const auto result = run_dialog(true);
	ASSERT_EQ(result.outcome, Operation::OPERATION_COMPLETED) << result.message.toStdString();
	ASSERT_EQ(undo_stack().count(), 2);
	ASSERT_EQ(undo_stack().index(), 2);
	EXPECT_EQ(undo_stack().command(0), earlier);
	for (unsigned int cycle = 0; cycle < 3; ++cycle)
	{
		SCOPED_TRACE(cycle);
		const auto after = trees();
		for (double time : {0.0, 100.0, 375.5})
		{
			expect_same_rotation(rotation(after, time), rotation(before, 375.5));
			expect_same_rotation(rotation(after, time, 345, 234), rotation(before, time, 345, 234));
		}
		for (double time : {375.5, 500.0, 800.0})
		{
			expect_same_rotation(rotation(after, time), rotation(before, time));
		}
		undo_stack().undo();
		ASSERT_EQ(undo_stack().count(), 2);
		ASSERT_EQ(undo_stack().index(), 1);
		const auto undone = trees();
		for (double time : {0.0, 375.5, 800.0})
		{
			expect_same_rotation(rotation(undone, time), rotation(before, time));
		}
		undo_stack().redo();
		ASSERT_EQ(undo_stack().index(), 2);
	}
	expect_file_unchanged();
}


TEST_F(ReleasePlateDialogTest, CancellingReviewedChangePreservesModelAndDisk)
{
	const auto original = snapshot();
	const auto result = run_dialog(false);
	EXPECT_EQ(result.outcome, Operation::OPERATION_CANCELLED);
	EXPECT_EQ(snapshot(), original);
	EXPECT_EQ(undo_stack().count(), 0);
	expect_file_unchanged();
}
