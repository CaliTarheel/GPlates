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

#include <memory>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QSettings>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include "app-logic/ApplicationState.h"
#include "app-logic/UserPreferences.h"

#include "gui/AnimationController.h"

#include "qt-widgets/ReconstructionTimeSpinBox.h"
#include "qt-widgets/TimeControlWidget.h"


namespace
{
	class TestTimeSpinBox :
			public GPlatesQtWidgets::ReconstructionTimeSpinBox
	{
	public:
		using GPlatesQtWidgets::ReconstructionTimeSpinBox::stepEnabled;
	};


	class TimeControlWidgetTest :
			public ::testing::Test
	{
	protected:
		void
		SetUp() override
		{
			ASSERT_TRUE(d_settings_directory.isValid());
			d_original_application_name = QCoreApplication::applicationName();
			d_original_settings_format = QSettings::defaultFormat();
			// UserPreferences writes via default-constructed QSettings. Isolate every integration
			// test in temporary INI files, including when CTest runs cases in parallel processes.
			const QSettings location(QSettings::IniFormat, QSettings::UserScope,
					"GPlatesTimeControlTests", "location");
			d_original_ini_path = QDir::cleanPath(
					QFileInfo(location.fileName()).absolutePath() + "/..");
			QSettings::setDefaultFormat(QSettings::IniFormat);
			QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_settings_directory.path());
			QCoreApplication::setApplicationName("time-control-tests");
			d_redirected_settings = true;

			d_application.reset(new GPlatesAppLogic::ApplicationState());
			d_application->get_user_preferences().set_value("view/animation/lock_view_time_to_range", false);
			d_controller.reset(new GPlatesGui::AnimationController(*d_application));
			d_controller->set_view_time(997.25);
			d_widget.reset(new GPlatesQtWidgets::TimeControlWidget(*d_controller, nullptr));
			d_spinbox = d_widget->findChild<GPlatesQtWidgets::ReconstructionTimeSpinBox *>(
					"spinbox_current_time");
			ASSERT_NE(d_spinbox, nullptr);
		}

		void
		TearDown() override
		{
			d_widget.reset();
			d_controller.reset();
			d_application.reset();
			if (d_redirected_settings)
			{
				QCoreApplication::setApplicationName(d_original_application_name);
				QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, d_original_ini_path);
				QSettings::setDefaultFormat(d_original_settings_format);
			}
		}

		QTemporaryDir d_settings_directory;
		QString d_original_application_name;
		QString d_original_ini_path;
		QSettings::Format d_original_settings_format = QSettings::NativeFormat;
		bool d_redirected_settings = false;
		std::unique_ptr<GPlatesAppLogic::ApplicationState> d_application;
		std::unique_ptr<GPlatesGui::AnimationController> d_controller;
		std::unique_ptr<GPlatesQtWidgets::TimeControlWidget> d_widget;
		GPlatesQtWidgets::ReconstructionTimeSpinBox *d_spinbox = nullptr;
	};
}


TEST(ReconstructionTimeSpinBoxTest, SnapsDirectionallyFromFractionalAndExactAges)
{
	TestTimeSpinBox spinbox;
	spinbox.setValue(997.25);
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 990);
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 980);
	spinbox.setValue(997.25);
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 1000);
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 1010);
	spinbox.setValue(1000.25);
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 1000);
	spinbox.setValue(999.99);
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 1000);
	spinbox.stepBy(0);
	EXPECT_DOUBLE_EQ(spinbox.value(), 1000);
}


TEST(ReconstructionTimeSpinBoxTest, MultipleStepsAndBoundsStayOnTheGrid)
{
	TestTimeSpinBox spinbox;
	spinbox.setValue(997.25);
	spinbox.stepBy(10);
	EXPECT_DOUBLE_EQ(spinbox.value(), 1090);
	spinbox.setValue(997.25);
	spinbox.stepBy(-10);
	EXPECT_DOUBLE_EQ(spinbox.value(), 900);
	spinbox.setValue(9989.25);
	spinbox.stepBy(10);
	EXPECT_DOUBLE_EQ(spinbox.value(), 9990);
	EXPECT_FALSE(spinbox.stepEnabled().testFlag(QAbstractSpinBox::StepUpEnabled));
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 9990);
	spinbox.setValue(9999);
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 9999); // No forward destination; never step backwards.
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 9990);
	spinbox.setValue(0.25);
	spinbox.stepBy(-10);
	EXPECT_DOUBLE_EQ(spinbox.value(), 0);
	EXPECT_FALSE(spinbox.stepEnabled().testFlag(QAbstractSpinBox::StepDownEnabled));
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 0);
}


TEST(ReconstructionTimeSpinBoxTest, NonGridLimitsAndReadOnlyDisableUnavailableSteps)
{
	TestTimeSpinBox spinbox;
	spinbox.setRange(3, 27);
	spinbox.setValue(25);
	EXPECT_FALSE(spinbox.stepEnabled().testFlag(QAbstractSpinBox::StepUpEnabled));
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 25);
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 20);
	spinbox.setValue(5);
	EXPECT_FALSE(spinbox.stepEnabled().testFlag(QAbstractSpinBox::StepDownEnabled));
	spinbox.stepDown();
	EXPECT_DOUBLE_EQ(spinbox.value(), 5);
	spinbox.stepUp();
	EXPECT_DOUBLE_EQ(spinbox.value(), 10);
	spinbox.setReadOnly(true);
	EXPECT_EQ(spinbox.stepEnabled(), QAbstractSpinBox::StepNone);
	spinbox.stepBy(1);
	EXPECT_DOUBLE_EQ(spinbox.value(), 10);
	spinbox.setReadOnly(false);
	spinbox.setRange(1, 9);
	EXPECT_EQ(spinbox.stepEnabled(), QAbstractSpinBox::StepNone);
	const double previous = spinbox.value();
	spinbox.stepBy(-10);
	spinbox.stepBy(10);
	EXPECT_DOUBLE_EQ(spinbox.value(), previous);
}


TEST(ReconstructionTimeSpinBoxTest, PendingTextKeyboardAndWheelUseTheSameSteps)
{
	TestTimeSpinBox spinbox;
	spinbox.setKeyboardTracking(false);
	spinbox.setValue(100);
	auto *editor = spinbox.findChild<QLineEdit *>();
	ASSERT_NE(editor, nullptr);
	editor->setText(spinbox.locale().toString(12.34, 'f', 2));
	spinbox.stepBy(1);
	EXPECT_DOUBLE_EQ(spinbox.value(), 20);
	spinbox.setValue(12.34);
	QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
	QApplication::sendEvent(&spinbox, &up);
	EXPECT_DOUBLE_EQ(spinbox.value(), 20);
	QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
	QApplication::sendEvent(&spinbox, &down);
	EXPECT_DOUBLE_EQ(spinbox.value(), 10);
	spinbox.setValue(0);
	editor->setText(spinbox.locale().toString(12.34, 'f', 2));
	EXPECT_TRUE(spinbox.stepEnabled().testFlag(QAbstractSpinBox::StepDownEnabled));
	QApplication::sendEvent(&spinbox, &down);
	EXPECT_DOUBLE_EQ(spinbox.value(), 10);
	QKeyEvent page_up(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier);
	QApplication::sendEvent(&spinbox, &page_up);
	EXPECT_DOUBLE_EQ(spinbox.value(), 110);
	spinbox.setValue(12.34);
	QWheelEvent wheel(QPointF(5, 5), QPointF(5, 5), QPoint(), QPoint(0, 120),
			Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
	QApplication::sendEvent(&spinbox, &wheel);
	EXPECT_DOUBLE_EQ(spinbox.value(), 20);
}


TEST_F(TimeControlWidgetTest, StepsCommitImmediatelyWithoutFinishingTextEditing)
{
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 997.25);
	int editing_finished_count = 0;
	QObject::connect(d_widget.get(), &GPlatesQtWidgets::TimeControlWidget::editing_finished,
			[&]() { ++editing_finished_count; });
	d_spinbox->stepDown();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 990);
	d_spinbox->stepUp();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 1000);
	EXPECT_EQ(editing_finished_count, 0);
}


TEST_F(TimeControlWidgetTest, TypedFractionsAndProgrammaticTimesRemainUnsnapped)
{
	auto *editor = d_spinbox->findChild<QLineEdit *>();
	ASSERT_NE(editor, nullptr);
	editor->setText(d_spinbox->locale().toString(12.34, 'f', 2));
	d_spinbox->interpretText();
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 12.34);
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 997.25);
	ASSERT_TRUE(QMetaObject::invokeMethod(d_spinbox, "editingFinished", Qt::DirectConnection));
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 12.34);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 12.34);
	d_controller->set_view_time(997.25);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 997.25);
	d_controller->set_time_increment(2.5);
	d_controller->step_forward();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 994.75);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 994.75);
	d_controller->step_back();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 997.25);
}


TEST_F(TimeControlWidgetTest, LockedRangeRejectsOffRangeGridStepsAndRestoresAcceptedAge)
{
	auto &preferences = d_application->get_user_preferences();
	preferences.set_value("view/animation/default_time_range_start", 27.0);
	preferences.set_value("view/animation/default_time_range_end", 3.0);
	preferences.set_value("view/animation/lock_view_time_to_range", true);
	d_controller->set_view_time(25.25);
	d_spinbox->stepUp();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 25.25);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 25.25); // Do not clamp the proposal to off-grid 27.
	d_spinbox->stepDown();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 20);
	d_controller->set_view_time(3.25);
	d_spinbox->stepDown();
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 3.25);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 3.25);
	d_spinbox->setValue(28.25);
	ASSERT_TRUE(QMetaObject::invokeMethod(d_spinbox, "editingFinished", Qt::DirectConnection));
	EXPECT_DOUBLE_EQ(d_controller->view_time(), 3.25);
	EXPECT_DOUBLE_EQ(d_spinbox->value(), 3.25);
}
