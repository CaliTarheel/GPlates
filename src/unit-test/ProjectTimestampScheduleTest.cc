/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#include "unit-test/ProjectTimestampScheduleTest.h"

#include <stdexcept>
#include <vector>

#include "app-logic/ProjectTimestampSchedule.h"


GPlatesUnitTest::ProjectTimestampScheduleTestSuite::ProjectTimestampScheduleTestSuite(
		unsigned depth) :
	GPlatesTestSuite("ProjectTimestampScheduleTestSuite")
{
	init(depth);
}


void
GPlatesUnitTest::ProjectTimestampScheduleTestSuite::construct_maps()
{
	boost::shared_ptr<ProjectTimestampScheduleTest> instance(
			new ProjectTimestampScheduleTest());
	ADD_TESTCASE(ProjectTimestampScheduleTest, test_divisible_schedule);
	ADD_TESTCASE(ProjectTimestampScheduleTest, test_non_divisible_schedule);
	ADD_TESTCASE(ProjectTimestampScheduleTest, test_single_timestamp);
	ADD_TESTCASE(ProjectTimestampScheduleTest, test_invalid_schedule);
}


void
GPlatesUnitTest::ProjectTimestampScheduleTest::test_divisible_schedule()
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 10.0);
	BOOST_REQUIRE_EQUAL(schedule.size(), 11);
	BOOST_CHECK_EQUAL(schedule.front(), 0.0);
	BOOST_CHECK_EQUAL(schedule.back(), 100.0);
}


void
GPlatesUnitTest::ProjectTimestampScheduleTest::test_non_divisible_schedule()
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 95.0, 10.0);
	BOOST_REQUIRE_EQUAL(schedule.size(), 11);
	BOOST_CHECK_EQUAL(schedule[schedule.size() - 2], 90.0);
	BOOST_CHECK_EQUAL(schedule.back(), 95.0);
}


void
GPlatesUnitTest::ProjectTimestampScheduleTest::test_single_timestamp()
{
	const std::vector<double> schedule =
			GPlatesAppLogic::ProjectTimestampSchedule::build(50.0, 50.0, 10.0);
	BOOST_REQUIRE_EQUAL(schedule.size(), 1);
	BOOST_CHECK_EQUAL(schedule.front(), 50.0);
}


void
GPlatesUnitTest::ProjectTimestampScheduleTest::test_invalid_schedule()
{
	BOOST_CHECK_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(100.0, 0.0, 10.0),
			std::invalid_argument);
	BOOST_CHECK_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 0.0),
			std::invalid_argument);
	BOOST_CHECK_THROW(
			GPlatesAppLogic::ProjectTimestampSchedule::build(0.0, 100.0, 1.0, 10),
			std::length_error);
}
