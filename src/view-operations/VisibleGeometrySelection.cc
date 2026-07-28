/* $Id$ */

#include <algorithm>
#include <set>
#include <vector>

#include "VisibleGeometrySelection.h"

#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructLayerProxy.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/QualifiedXmlName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"


namespace
{
	bool
	choice_label_less_than(
			const GPlatesViewOperations::VisibleGeometrySelection::Choice &lhs,
			const GPlatesViewOperations::VisibleGeometrySelection::Choice &rhs)
	{
		return lhs.label.localeAwareCompare(rhs.label) < 0;
	}
}


GPlatesViewOperations::VisibleGeometrySelection::choice_seq_type
GPlatesViewOperations::VisibleGeometrySelection::get_choices(
		const GPlatesPresentation::ViewState &view_state,
		GeometryKind geometry_kind)
{
	choice_seq_type choices;
	std::set<const GPlatesModel::TopLevelProperty *> seen_properties;

	const GPlatesPresentation::VisualLayers &visual_layers = view_state.get_visual_layers();
	for (size_t layer_index = 0; layer_index < visual_layers.size(); ++layer_index)
	{
		const boost::shared_ptr<const GPlatesPresentation::VisualLayer> visual_layer =
				visual_layers.visual_layer_at(layer_index).lock();
		if (!visual_layer || !visual_layer->is_visible() ||
				visual_layer->get_layer_type() !=
						static_cast<unsigned int>(GPlatesAppLogic::LayerTaskType::RECONSTRUCT))
		{
			continue;
		}

		const boost::optional<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_output =
				visual_layer->get_reconstruct_graph_layer().get_layer_output<
						GPlatesAppLogic::ReconstructLayerProxy>();
		if (!layer_output)
		{
			continue;
		}

		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*layer_output)->get_reconstructed_feature_geometries(geometries);
		for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
				geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
		{
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
			if (!rfg.is_valid() || !rfg.property().is_still_valid())
			{
				continue;
			}

			const bool is_polygon =
					dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
			const bool is_polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(rfg.reconstructed_geometry().get());
			if ((geometry_kind == POLYGONS && !is_polygon) ||
					(geometry_kind == POLYLINES && !is_polyline) ||
					(geometry_kind == POLYGONS_AND_POLYLINES && !is_polygon && !is_polyline))
			{
				continue;
			}

			const GPlatesModel::TopLevelProperty *property = (*rfg.property()).get();
			if (!seen_properties.insert(property).second)
			{
				continue;
			}

			const GPlatesModel::FeatureHandle *feature = rfg.feature_handle_ptr();
			QString label = GPlatesModel::convert_qualified_xml_name_to_qstring(feature->feature_type());
			if (rfg.reconstruction_plate_id())
			{
				label += QString(" | plate %1").arg(*rfg.reconstruction_plate_id());
			}
			label += QString(" | %1").arg(feature->feature_id().get().qstring());
			choices.push_back(Choice(label, *geometry_iter));
		}
	}

	std::sort(choices.begin(), choices.end(), choice_label_less_than);
	return choices;
}
