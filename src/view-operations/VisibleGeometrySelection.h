/* $Id$ */

#ifndef GPLATES_VIEWOPERATIONS_VISIBLEGEOMETRYSELECTION_H
#define GPLATES_VIEWOPERATIONS_VISIBLEGEOMETRYSELECTION_H

#include <vector>

#include <QString>

#include "app-logic/ReconstructedFeatureGeometry.h"


namespace GPlatesPresentation
{
	class ViewState;
}

namespace GPlatesViewOperations
{
	namespace VisibleGeometrySelection
	{
		enum GeometryKind
		{
			POLYGONS,
			POLYLINES,
			POLYGONS_AND_POLYLINES
		};

		struct Choice
		{
			Choice(
					const QString &label_,
					const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type &geometry_) :
				label(label_),
				geometry(geometry_)
			{  }

			QString label;
			GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type geometry;
		};

		typedef std::vector<Choice> choice_seq_type;

		/**
		 * Returns one entry per editable geometry property from visible reconstruct
		 * layers. The returned geometries are all from the current reconstruction.
		 */
		choice_seq_type
		get_choices(
				const GPlatesPresentation::ViewState &view_state,
				GeometryKind geometry_kind);
	}
}

#endif // GPLATES_VIEWOPERATIONS_VISIBLEGEOMETRYSELECTION_H
