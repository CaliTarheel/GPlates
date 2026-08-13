/* $Id$ */

#ifndef GPLATES_UNIT_TEST_INITIALRIFTGEOMETRYTEST_H
#define GPLATES_UNIT_TEST_INITIALRIFTGEOMETRYTEST_H

#include "unit-test/GPlatesTestSuite.h"


namespace GPlatesUnitTest
{
	class InitialRiftGeometryTest
	{
	public:
		void test_deterministic_network();
		void test_network_is_clipped_to_continent();
		void test_network_excludes_cratons();
		void test_voronoi_network_branches();
		void test_selected_edges_join_into_one_polyline();
		void test_spacing_controls_network_scale();
		void test_artifexia_segment_quality();
	};

	class InitialRiftGeometryTestSuite :
			public GPlatesTestSuite
	{
	public:
		InitialRiftGeometryTestSuite(unsigned depth);

	protected:
		void construct_maps();
	};
}

#endif // GPLATES_UNIT_TEST_INITIALRIFTGEOMETRYTEST_H
