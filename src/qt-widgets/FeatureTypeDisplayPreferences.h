/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates.
 *
 * GPlates is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 2, as published by
 * the Free Software Foundation.
 */

#ifndef GPLATES_QTWIDGETS_FEATURETYPEDISPLAYPREFERENCES_H
#define GPLATES_QTWIDGETS_FEATURETYPEDISPLAYPREFERENCES_H

#include <QString>
#include <QStringList>


namespace GPlatesQtWidgets
{
	namespace FeatureTypeDisplayPreferences
	{
		/**
		 * A QStringList of qualified feature type names hidden from feature type choosers.
		 *
		 * Storing the exceptions instead of the visible types means new GPGIM feature
		 * types are visible by default when a newer version of GPlates is installed.
		 */
		inline
		QString
		hidden_feature_types_key()
		{
			return QString("feature_type_display/hidden_feature_types");
		}


		/**
		 * Feature types used by the Artifexia worldbuilding project.
		 *
		 * These names were inventoried from the feature collections loaded by
		 * Artifexia's arty.gproj. Flowline is included even though its collection
		 * was empty when the project was saved, matching the project's documented
		 * create-then-delete flowline workflow.
		 */
		inline
		QStringList
		artifexia_feature_types()
		{
			return QStringList()
					<< "gpml:ClosedPlateBoundary"
					<< "gpml:ContinentalCrust"
					<< "gpml:ContinentalRift"
					<< "gpml:Craton"
					<< "gpml:Flowline"
					<< "gpml:HotSpot"
					<< "gpml:IslandArc"
					<< "gpml:LargeIgneousProvince"
					<< "gpml:MidOceanRidge"
					<< "gpml:MotionPath"
					<< "gpml:OceanicCrust"
					<< "gpml:OrogenicBelt"
					<< "gpml:Raster"
					<< "gpml:SubductionZone"
					<< "gpml:TerraneBoundary"
					<< "gpml:TopologicalClosedPlateBoundary"
					<< "gpml:Transform"
					<< "gpml:UnclassifiedFeature";
		}
	}
}

#endif  // GPLATES_QTWIDGETS_FEATURETYPEDISPLAYPREFERENCES_H
