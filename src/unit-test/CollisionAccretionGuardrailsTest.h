/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_UNIT_TEST_COLLISIONACCRETIONGUARDRAILSTEST_H
#define GPLATES_UNIT_TEST_COLLISIONACCRETIONGUARDRAILSTEST_H
#include <boost/test/unit_test.hpp>
#include "unit-test/GPlatesTestSuite.h"
namespace GPlatesUnitTest
{
	class CollisionAccretionGuardrailsTest { public: void test_explicit_survivor_and_timestamp(); };
	class CollisionAccretionGuardrailsTestSuite : public GPlatesTestSuite
	{
	public: CollisionAccretionGuardrailsTestSuite(unsigned depth);
	protected: void construct_maps();
	};
}
#endif
