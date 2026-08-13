/* $Id$ */

#include "CratonPlateIdLabels.h"

#include <set>
#include <vector>

#include <boost/optional.hpp>
#include <QFont>

#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"

#include "gui/Colour.h"

#include "maths/AngularDistance.h"
#include "maths/GeometryDistance.h"
#include "maths/PolygonMesh.h"
#include "maths/Vector3D.h"

#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"


namespace
{
	boost::optional<GPlatesMaths::PointOnSphere>
	find_interior_label_point(
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon)
	{
		boost::optional<GPlatesMaths::PointOnSphere> best_point;
		boost::optional<GPlatesMaths::AngularDistance> best_clearance;

		// The area-weighted centroid is ideal for ordinary shapes, but can fall outside
		// a strongly concave polygon.
		const GPlatesMaths::PointOnSphere interior_centroid(polygon->get_interior_centroid());
		if (polygon->is_point_in_polygon(interior_centroid))
		{
			best_point = interior_centroid;
			best_clearance = GPlatesMaths::minimum_distance(interior_centroid, *polygon, false);
		}

		// Triangle centres are guaranteed to be in the meshed interior. Choose the one
		// furthest from the outline so labels do not land in a narrow concavity.
		const boost::optional<GPlatesMaths::PolygonMesh::non_null_ptr_to_const_type> mesh =
				GPlatesMaths::PolygonMesh::create(polygon, 0.15);
		if (mesh)
		{
			const std::vector<GPlatesMaths::PolygonMesh::Vertex> &vertices = (*mesh)->get_vertices();
			const std::vector<GPlatesMaths::PolygonMesh::Triangle> &triangles = (*mesh)->get_triangles();
			for (std::vector<GPlatesMaths::PolygonMesh::Triangle>::const_iterator triangle_iter =
					triangles.begin(); triangle_iter != triangles.end(); ++triangle_iter)
			{
				GPlatesMaths::Vector3D centre_vector(0, 0, 0);
				for (unsigned int vertex_index = 0; vertex_index < 3; ++vertex_index)
				{
					centre_vector = centre_vector + GPlatesMaths::Vector3D(vertices[
							triangle_iter->get_mesh_vertex_index(vertex_index)].get_vertex());
				}
				if (centre_vector.magSqrd() <= 0)
				{
					continue;
				}
				const GPlatesMaths::PointOnSphere candidate(centre_vector.get_normalisation());
				if (!polygon->is_point_in_polygon(candidate))
				{
					continue;
				}
				const GPlatesMaths::AngularDistance clearance =
						GPlatesMaths::minimum_distance(candidate, *polygon, false);
				if (!best_clearance || clearance > *best_clearance)
				{
					best_point = candidate;
					best_clearance = clearance;
				}
			}
		}

		return best_point;
	}
}


GPlatesViewOperations::CratonPlateIdLabels::CratonPlateIdLabels(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_label_layer(view_state.get_rendered_geometry_collection().
			create_child_rendered_layer_and_transfer_ownership(
					RenderedGeometryCollection::RECONSTRUCTION_LAYER))
{
	QObject::connect(
			&d_application_state,
			SIGNAL(reconstructed(GPlatesAppLogic::ApplicationState &)),
			this,
			SLOT(handle_reconstructed(GPlatesAppLogic::ApplicationState &)));
	QObject::connect(
			&d_view_state.get_visual_layers(),
			SIGNAL(changed()),
			this,
			SLOT(refresh()));
	refresh();
}


void
GPlatesViewOperations::CratonPlateIdLabels::handle_reconstructed(
		GPlatesAppLogic::ApplicationState &)
{
	refresh();
}


void
GPlatesViewOperations::CratonPlateIdLabels::refresh()
{
	d_label_layer->set_active(true);
	d_label_layer->clear_rendered_geometries();

	static const GPlatesModel::FeatureType CRATON =
			GPlatesModel::FeatureType::create_gpml("Craton");
	std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
	QFont label_font;
	label_font.setBold(true);
	label_font.setPointSize(11);

	const GPlatesPresentation::VisualLayers &visual_layers = d_view_state.get_visual_layers();
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
			if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
					!rfg.get_feature_ref().is_valid() ||
					rfg.get_feature_ref()->feature_type() != CRATON ||
					!rfg.reconstruction_plate_id() ||
					!seen_properties.insert((*rfg.property()).get()).second)
			{
				continue;
			}
			const GPlatesMaths::PolygonOnSphere *polygon =
					dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
							rfg.reconstructed_geometry().get());
			if (!polygon)
			{
				continue;
			}
			const boost::optional<GPlatesMaths::PointOnSphere> label_point =
					find_interior_label_point(polygon->get_non_null_pointer());
			if (!label_point)
			{
				continue;
			}

			const QString label = QString::number(*rfg.reconstruction_plate_id());
			d_label_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_string(
							*label_point,
							label,
							GPlatesGui::Colour::get_white(),
							GPlatesGui::Colour::get_black(),
							-4 * label.size(),
							5,
							label_font));
		}
	}
}
