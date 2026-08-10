/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "app-logic/ProjectDocumentRegistry.h"
#include "app-logic/ProjectTimestampSchedule.h"


TEST(ProjectTimestampScheduleTest, divisible_schedule)
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 10.0);
	ASSERT_EQ(schedule.size(), 11u);
	EXPECT_EQ(schedule.front(), 0.0);
	EXPECT_EQ(schedule.back(), 100.0);
}


TEST(ProjectTimestampScheduleTest, non_divisible_schedule)
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 95.0, 10.0);
	ASSERT_EQ(schedule.size(), 11u);
	EXPECT_EQ(schedule[schedule.size() - 2], 90.0);
	EXPECT_EQ(schedule.back(), 95.0);
}


TEST(ProjectTimestampScheduleTest, single_timestamp)
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(50.0, 50.0, 10.0);
	ASSERT_EQ(schedule.size(), 1u);
	EXPECT_EQ(schedule.front(), 50.0);
}


TEST(ProjectTimestampScheduleTest, invalid_schedule)
{
	EXPECT_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(100.0, 0.0, 10.0),
			std::invalid_argument);
	EXPECT_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 0.0),
			std::invalid_argument);
	EXPECT_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 1.0, 10),
			std::length_error);
}


TEST(ProjectTimestampScheduleTest, project_document_schedule)
{
	QTemporaryDir temporary_directory;
	ASSERT_TRUE(temporary_directory.isValid());
	const QString document_path = QDir(temporary_directory.path()).filePath("PROJECT.md");
	QFile document(document_path);
	ASSERT_TRUE(document.open(QIODevice::WriteOnly));
	// A whole, well-formed document. schedule_survives_other_bad_fields() covers the schedule
	// arriving intact when the rest of the document is missing or wrong.
	const QByteArray markdown(
			"---\n"
			"gplates:\n"
			"  planet:\n"
			"    radius_m: 6371000\n"
			"  reconstruction:\n"
			"    required_timestamps_ma: \"1000, 950, 900, 850, 800, 750, 700, 650, 600, 560, 520, 480, 440, 400, 370, 340, 310, 280, 250, 225, 200, 180, 160, 140, 120, 100, 80, 60, 40, 20, 10, 0\"\n"
			"---\n");
	ASSERT_EQ(document.write(markdown), static_cast<qint64>(markdown.size()));
	document.close();

	GPlatesAppLogic::ProjectDocumentRegistry registry;
	GPlatesAppLogic::ProjectTimestampSchedule schedule(registry);
	EXPECT_EQ(
			schedule.source(),
			GPlatesAppLogic::ProjectTimestampSchedule::NO_PROJECT_TIMESTAMPS);
	registry.add_document(document_path);
	EXPECT_EQ(
			schedule.source(),
			GPlatesAppLogic::ProjectTimestampSchedule::PROJECT_MARKDOWN);
	ASSERT_EQ(schedule.timestamps_older_to_younger().size(), 32u);
	ASSERT_TRUE(schedule.next_older_timestamp(850.0));
	EXPECT_EQ(schedule.next_older_timestamp(850.0).get(), 900.0);
	ASSERT_TRUE(schedule.next_younger_timestamp(850.0));
	EXPECT_EQ(schedule.next_younger_timestamp(850.0).get(), 800.0);
	ASSERT_TRUE(schedule.next_older_timestamp(875.0));
	EXPECT_EQ(schedule.next_older_timestamp(875.0).get(), 900.0);
	ASSERT_TRUE(schedule.next_younger_timestamp(875.0));
	EXPECT_EQ(schedule.next_younger_timestamp(875.0).get(), 850.0);
	EXPECT_FALSE(schedule.next_older_timestamp(1000.0));
	EXPECT_FALSE(schedule.next_younger_timestamp(0.0));
}


TEST(ProjectTimestampScheduleTest, schedule_survives_other_bad_fields)
{
	// A schedule does not depend on anything else in the document being present or usable. This
	// document has no planet section at all, and a second has a radius that is nonsense; both
	// still navigate. The schedule is the user's data and is not collateral damage from a mistake
	// in an unrelated field.
	QTemporaryDir temporary_directory;
	ASSERT_TRUE(temporary_directory.isValid());

	const QString no_planet_path = QDir(temporary_directory.path()).filePath("PROJECT.md");
	QFile no_planet(no_planet_path);
	ASSERT_TRUE(no_planet.open(QIODevice::WriteOnly));
	const QByteArray no_planet_markdown(
			"---\n"
			"gplates:\n"
			"  reconstruction:\n"
			"    required_timestamps_ma: \"1000, 500, 0\"\n"
			"---\n");
	ASSERT_EQ(no_planet.write(no_planet_markdown), static_cast<qint64>(no_planet_markdown.size()));
	no_planet.close();

	GPlatesAppLogic::ProjectDocumentRegistry registry;
	GPlatesAppLogic::ProjectTimestampSchedule schedule(registry);
	registry.add_document(no_planet_path);
	EXPECT_EQ(
			schedule.source(),
			GPlatesAppLogic::ProjectTimestampSchedule::PROJECT_MARKDOWN);
	ASSERT_EQ(schedule.timestamps_older_to_younger().size(), 3u);
	ASSERT_TRUE(schedule.next_older_timestamp(500.0));
	EXPECT_EQ(schedule.next_older_timestamp(500.0).get(), 1000.0);

	const QString bad_radius_path = QDir(temporary_directory.path()).filePath("BAD-RADIUS.md");
	QFile bad_radius(bad_radius_path);
	ASSERT_TRUE(bad_radius.open(QIODevice::WriteOnly));
	const QByteArray bad_radius_markdown(
			"---\n"
			"gplates:\n"
			"  planet:\n"
			"    radius_m: 0\n"
			"  reconstruction:\n"
			"    required_timestamps_ma: \"800, 400, 0\"\n"
			"---\n");
	ASSERT_EQ(bad_radius.write(bad_radius_markdown), static_cast<qint64>(bad_radius_markdown.size()));
	bad_radius.close();

	const int bad_radius_index = registry.add_document(bad_radius_path);
	ASSERT_TRUE(registry.set_primary_document(bad_radius_index));
	EXPECT_EQ(
			schedule.source(),
			GPlatesAppLogic::ProjectTimestampSchedule::PROJECT_MARKDOWN);
	ASSERT_EQ(schedule.timestamps_older_to_younger().size(), 3u);
	ASSERT_TRUE(schedule.next_younger_timestamp(400.0));
	EXPECT_EQ(schedule.next_younger_timestamp(400.0).get(), 0.0);

	// A schedule that is itself unreadable is still reported, which AnimationController shows in
	// the status bar before falling back to the ordinary frame step, so Alt doing nothing is
	// explained rather than silent.
	const QString bad_schedule_path = QDir(temporary_directory.path()).filePath("BAD-SCHEDULE.md");
	QFile bad_schedule(bad_schedule_path);
	ASSERT_TRUE(bad_schedule.open(QIODevice::WriteOnly));
	const QByteArray bad_schedule_markdown(
			"---\n"
			"gplates:\n"
			"  reconstruction:\n"
			"    required_timestamps_ma: \"100, 50, 50, 0\"\n"
			"---\n");
	ASSERT_EQ(bad_schedule.write(bad_schedule_markdown), static_cast<qint64>(bad_schedule_markdown.size()));
	bad_schedule.close();

	const int bad_schedule_index = registry.add_document(bad_schedule_path);
	ASSERT_TRUE(registry.set_primary_document(bad_schedule_index));
	EXPECT_EQ(
			schedule.source(),
			GPlatesAppLogic::ProjectTimestampSchedule::INVALID_PROJECT_METADATA);
	EXPECT_FALSE(schedule.diagnostic().isEmpty());
	EXPECT_TRUE(schedule.timestamps_older_to_younger().empty());
}
