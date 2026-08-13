/* $Id$ */

#ifndef GPLATES_UNIT_TEST_COLLISIONGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_COLLISIONGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class CollisionGeometryTest
	{
	public:
		void test_contact_corridor();
		void test_contact_margin_deformation();
		void test_collision_classification();
		void test_himalayan_width();
	};

	class CollisionGeometryTestSuite : public GPlatesTestSuite
	{
	public:
		explicit CollisionGeometryTestSuite(unsigned level);
	protected:
		virtual void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_COLLISIONGEOMETRYTEST_H
