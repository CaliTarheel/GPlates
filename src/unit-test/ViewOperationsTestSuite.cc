/* $Id$ */

/**
 * \file 
 * $Revision$
 * $Date$
 * 
 * Copyright (C) 2010 The University of Sydney, Australia
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
#include <iostream>

#include <QDebug>

#include "unit-test/ViewOperationsTestSuite.h"
#include "unit-test/TestSuiteFilter.h"
#include "unit-test/AdvancePlateMotionGeometryTest.h"
#include "unit-test/BooleanPolygonGeometryTest.h"
#include "unit-test/CollisionGeometryTest.h"
#include "unit-test/PostCollisionRiftGeometryTest.h"
#include "unit-test/MantleEventGeometryTest.h"
#include "unit-test/InitialContinentGeometryTest.h"
#include "unit-test/InitialRiftGeometryTest.h"
#include "unit-test/InitialRotationFileTest.h"
#include "unit-test/InitialSubductionGeometryTest.h"
#include "unit-test/NaturalizeCoastlineTest.h"
#include "unit-test/OceanCrustBandBuilderTest.h"
#include "unit-test/SubductionEffectsGeometryTest.h"
#include "unit-test/TripleJunctionGeometryTest.h"

GPlatesUnitTest::ViewOperationsTestSuite::ViewOperationsTestSuite(
		unsigned level) : 
	GPlatesUnitTest::GPlatesTestSuite(
			"ViewOperationsTestSuite")
{
	init(level);
}

void 
GPlatesUnitTest::ViewOperationsTestSuite::construct_maps()
{
	ADD_TESTSUITE(AdvancePlateMotionGeometry);
	ADD_TESTSUITE(BooleanPolygonGeometry);
	ADD_TESTSUITE(CollisionGeometry);
	ADD_TESTSUITE(PostCollisionRiftGeometry);
	ADD_TESTSUITE(MantleEventGeometry);
	ADD_TESTSUITE(InitialContinentGeometry);
	ADD_TESTSUITE(InitialRiftGeometry);
	ADD_TESTSUITE(InitialRotationFile);
	ADD_TESTSUITE(InitialSubductionGeometry);
	ADD_TESTSUITE(NaturalizeCoastline);
	ADD_TESTSUITE(OceanCrustBandBuilder);
	ADD_TESTSUITE(SubductionEffectsGeometry);
	ADD_TESTSUITE(TripleJunctionGeometry);
}


