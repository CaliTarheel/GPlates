/* $Id$ */

/**
 * \file
 * Review-and-commit workflow for a selected continent's Voronoi rift network.
 */

#include "ProposeInitialRiftsOperation.h"

#include <climits>
#include <cmath>
#include <functional>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

#include <boost/optional.hpp>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizePolicy>
#include <QSplitter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "InitialRiftGeometry.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructGraph.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"
#include "maths/UnitVector3D.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/Earth.h"
#include "utils/UnicodeStringUtils.h"


namespace
{
	namespace RiftGeometry = GPlatesViewOperations::InitialRiftGeometry;

	enum EdgeRole
	{
		EDGE_IGNORED,
		EDGE_MID_OCEAN_RIDGE,
		EDGE_FAILED_RIFT
	};

	struct RiftSource
	{
		RiftSource(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent_,
				const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons_,
				const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id_) :
			continent(continent_),
			cratons(cratons_),
			plate_id(plate_id_)
		{  }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type continent;
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> cratons;
		boost::optional<GPlatesModel::integer_plate_id_type> plate_id;
	};

	std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>
	gather_active_cratons(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesMaths::PolygonOnSphere &selected_continent)
	{
		static const GPlatesModel::FeatureType CRATON =
				GPlatesModel::FeatureType::create_gpml("Craton");
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> cratons;
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_outputs;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layer_outputs);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layer_outputs.begin(); layer_iter != layer_outputs.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() ||
						!rfg.get_feature_ref().is_valid() || rfg.get_feature_ref()->feature_type() != CRATON)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
				const GPlatesModel::TopLevelProperty *property = (*rfg.property()).get();
				bool belongs_to_selected_continent = false;
				if (polygon)
				{
					for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
							polygon->exterior_ring_vertex_begin();
						vertex_iter != polygon->exterior_ring_vertex_end(); ++vertex_iter)
					{
						if (selected_continent.is_point_in_polygon(*vertex_iter))
						{
							belongs_to_selected_continent = true;
							break;
						}
					}
				}
				if (polygon && belongs_to_selected_continent && seen_properties.insert(property).second)
				{
					cratons.push_back(GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type(polygon));
				}
			}
		}
		return cratons;
	}

	RiftSource
	gather_selected_rift_source(
			GPlatesGui::FeatureFocus &feature_focus,
			GPlatesAppLogic::ApplicationState &application_state)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		if (!feature_focus.is_valid())
		{
			throw std::runtime_error(
					"Select a ContinentalCrust polygon on the globe or in the feature table, then run this tool again.");
		}

		const GPlatesModel::FeatureHandle::weak_ref selected_feature = feature_focus.focused_feature();
		if (selected_feature->feature_type() != CONTINENTAL_CRUST)
		{
			throw std::runtime_error(
					"The selected feature is not ContinentalCrust. Select the continental-crust polygon to be divided.");
		}
		if (!feature_focus.associated_reconstruction_geometry())
		{
			throw std::runtime_error(
					"The selected ContinentalCrust feature has no visible reconstructed geometry at this time.");
		}

		const GPlatesAppLogic::ReconstructedFeatureGeometry *reconstructed_feature_geometry =
				dynamic_cast<const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						feature_focus.associated_reconstruction_geometry().get());
		if (!reconstructed_feature_geometry)
		{
			throw std::runtime_error(
					"The selected ContinentalCrust geometry is topology-resolved; select its source polygon instead.");
		}
		const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
						reconstructed_feature_geometry->reconstructed_geometry().get());
		if (!polygon)
		{
			throw std::runtime_error("The selected ContinentalCrust geometry is not a polygon.");
		}

		return RiftSource(
				GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type(polygon),
				gather_active_cratons(application_state, *polygon),
				reconstructed_feature_geometry->reconstruction_plate_id());
	}

	void
	add_required_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &property_value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, property_name, property_value))
		{
			throw std::runtime_error(
					QString("Unable to add required GPML property '%1'.")
					.arg(property_name.get_name().qstring())
					.toStdString());
		}
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type
	create_line_feature(
			const GPlatesModel::FeatureType &feature_type,
			const QString &name,
			const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id,
			double start_time,
			const boost::optional<double> &end_time,
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &polyline)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type feature =
				GPlatesModel::FeatureHandle::create(feature_type);
		const GPlatesModel::FeatureHandle::weak_ref feature_ref = feature->reference();

		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gml("validTime"),
				GPlatesModel::ModelUtils::create_gml_time_period(
						GPlatesPropertyValues::GeoTimeInstant(start_time),
						end_time
								? GPlatesPropertyValues::GeoTimeInstant(*end_time)
								: GPlatesPropertyValues::GeoTimeInstant::create_distant_future()));
		if (plate_id)
		{
			add_required_property(
					feature_ref,
					GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
					GPlatesPropertyValues::GpmlPlateId::create(*plate_id));
		}
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
				GPlatesModel::ModelUtils::create_gml_time_instant(
						GPlatesPropertyValues::GeoTimeInstant(start_time)));
		add_required_property(
				feature_ref,
				GPlatesModel::PropertyName::create_gpml("centerLineOf"),
				GPlatesAppLogic::GeometryUtils::create_polyline_geometry_property_value(polyline));
		return feature;
	}

	struct FeatureGroup
	{
		FeatureGroup(
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection_,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &features_) :
			collection(collection_),
			features(features_)
		{  }

		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
		std::vector<GPlatesModel::FeatureCollectionHandle::iterator> iterators;
	};

	class CommitVoronoiRiftsUndoCommand :
			public QUndoCommand
	{
	public:
		CommitVoronoiRiftsUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const std::vector<FeatureGroup> &groups) :
			d_model_interface(model_interface),
			d_groups(groups)
		{
			setText(QObject::tr("commit reviewed Voronoi rift edges"));
		}

		virtual void redo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::vector<FeatureGroup>::iterator group_iter = d_groups.begin();
				group_iter != d_groups.end(); ++group_iter)
			{
				if (!group_iter->collection.is_valid())
				{
					continue;
				}
				group_iter->iterators.clear();
				for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
						group_iter->features.begin();
					feature_iter != group_iter->features.end(); ++feature_iter)
				{
					const GPlatesModel::FeatureCollectionHandle::iterator inserted =
							group_iter->collection->add(*feature_iter);
					group_iter->iterators.push_back(inserted);
					*feature_iter = *inserted;
				}
			}
			guard.release_guard();
		}

		virtual void undo()
		{
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			for (std::size_t group_reverse = d_groups.size(); group_reverse > 0; --group_reverse)
			{
				FeatureGroup &group = d_groups[group_reverse - 1];
				if (!group.collection.is_valid())
				{
					continue;
				}
				for (std::size_t feature_reverse = group.iterators.size(); feature_reverse > 0; --feature_reverse)
				{
					const std::size_t index = feature_reverse - 1;
					if (group.iterators[index].is_still_valid())
					{
						group.features[index] = group.collection->remove(group.iterators[index]);
					}
				}
				group.iterators.clear();
			}
			guard.release_guard();
		}

	private:
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<FeatureGroup> d_groups;
	};

	bool
	edges_share_node(
			const RiftGeometry::Edge &lhs,
			const RiftGeometry::Edge &rhs)
	{
		return lhs.start_node == rhs.start_node || lhs.start_node == rhs.end_node ||
				lhs.end_node == rhs.start_node || lhs.end_node == rhs.end_node;
	}

	bool
	failed_edge_is_eligible(
			unsigned int edge_index,
			const RiftGeometry::Result &network,
			const std::vector<EdgeRole> &roles)
	{
		const RiftGeometry::Edge &failed_edge = network.edges[edge_index];
		if (failed_edge.touches_coast)
		{
			return true;
		}
		for (unsigned int candidate = 0; candidate < roles.size(); ++candidate)
		{
			if (roles[candidate] == EDGE_MID_OCEAN_RIDGE &&
				edges_share_node(failed_edge, network.edges[candidate]))
			{
				return true;
			}
		}
		return false;
	}

	GPlatesMaths::PointOnSphere
	spherical_midpoint(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end)
	{
		const double x = start.position_vector().x().dval() + end.position_vector().x().dval();
		const double y = start.position_vector().y().dval() + end.position_vector().y().dval();
		const double z = start.position_vector().z().dval() + end.position_vector().z().dval();
		const double length = std::sqrt(x * x + y * y + z * z);
		return GPlatesMaths::PointOnSphere(
				GPlatesMaths::UnitVector3D(x / length, y / length, z / length));
	}

	bool
	point_is_in_rift_planning_area(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesMaths::PolygonOnSphere &continent,
			const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons)
	{
		if (!continent.is_point_in_polygon(point))
		{
			return false;
		}
		for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
				craton_iter = cratons.begin(); craton_iter != cratons.end(); ++craton_iter)
		{
			if ((*craton_iter)->is_point_in_polygon(point))
			{
				return false;
			}
		}
		return true;
	}

	bool
	failed_edge_is_inside_planning_area(
			const RiftGeometry::Edge &edge,
			const GPlatesMaths::PolygonOnSphere &continent,
			const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons)
	{
		GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = edge.polyline->vertex_begin();
		GPlatesMaths::PointOnSphere previous = *vertex_iter;
		if (!point_is_in_rift_planning_area(previous, continent, cratons))
		{
			return false;
		}
		for (++vertex_iter; vertex_iter != edge.polyline->vertex_end(); ++vertex_iter)
		{
			if (!point_is_in_rift_planning_area(*vertex_iter, continent, cratons) ||
					!point_is_in_rift_planning_area(spherical_midpoint(previous, *vertex_iter), continent, cratons))
			{
				return false;
			}
			previous = *vertex_iter;
		}
		return true;
	}

	unsigned int
	count_mor_components(
			const RiftGeometry::Result &network,
			const std::vector<EdgeRole> &roles)
	{
		std::vector<bool> visited(roles.size(), false);
		unsigned int components = 0;
		for (unsigned int start = 0; start < roles.size(); ++start)
		{
			if (roles[start] != EDGE_MID_OCEAN_RIDGE || visited[start])
			{
				continue;
			}
			++components;
			std::vector<unsigned int> pending(1, start);
			visited[start] = true;
			while (!pending.empty())
			{
				const unsigned int current = pending.back();
				pending.pop_back();
				for (unsigned int candidate = 0; candidate < roles.size(); ++candidate)
				{
					if (!visited[candidate] && roles[candidate] == EDGE_MID_OCEAN_RIDGE &&
						edges_share_node(network.edges[current], network.edges[candidate]))
					{
						visited[candidate] = true;
						pending.push_back(candidate);
					}
				}
			}
		}
		return components;
	}

	std::set<int>
	selected_rows(
			const QTableWidget &table)
	{
		std::set<int> rows;
		const QList<QTableWidgetItem *> items = table.selectedItems();
		for (QList<QTableWidgetItem *>::const_iterator item_iter = items.begin();
			item_iter != items.end(); ++item_iter)
		{
			rows.insert((*item_iter)->row());
		}
		return rows;
	}

	QString
	role_name(
			EdgeRole role)
	{
		switch (role)
		{
		case EDGE_MID_OCEAN_RIDGE:
			return QObject::tr("MOR");
		case EDGE_FAILED_RIFT:
			return QObject::tr("Failed Rift");
		default:
			return QObject::tr("Ignore");
		}
	}

	struct PreviewVector
	{
		PreviewVector() : x(0), y(0), z(0) { }
		PreviewVector(double x_, double y_, double z_) : x(x_), y(y_), z(z_) { }
		double x;
		double y;
		double z;
	};

	PreviewVector
	preview_vector(
			const GPlatesMaths::PointOnSphere &point)
	{
		return PreviewVector(
				point.position_vector().x().dval(),
				point.position_vector().y().dval(),
				point.position_vector().z().dval());
	}

	PreviewVector operator+(const PreviewVector &lhs, const PreviewVector &rhs)
	{
		return PreviewVector(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z);
	}

	PreviewVector operator*(const PreviewVector &value, double scale)
	{
		return PreviewVector(value.x * scale, value.y * scale, value.z * scale);
	}

	double preview_dot(const PreviewVector &lhs, const PreviewVector &rhs)
	{
		return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
	}

	PreviewVector
	preview_cross(const PreviewVector &lhs, const PreviewVector &rhs)
	{
		return PreviewVector(
				lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.x * rhs.y - lhs.y * rhs.x);
	}

	PreviewVector
	preview_normalise(const PreviewVector &value)
	{
		const double length = std::sqrt(preview_dot(value, value));
		return value * (1.0 / length);
	}

	class RiftNetworkPreviewWidget :
			public QWidget
	{
	public:
		typedef std::function<void (int, Qt::KeyboardModifiers)> edge_clicked_handler_type;

		RiftNetworkPreviewWidget(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &continent,
				const std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> &cratons,
				QWidget *parent) :
			QWidget(parent),
			d_continent(continent),
			d_cratons(cratons),
			d_network(NULL),
			d_roles(NULL),
			d_scale(1.0),
			d_have_transform(false)
		{
			PreviewVector centre;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
					continent->exterior_ring_vertex_begin();
				vertex_iter != continent->exterior_ring_vertex_end(); ++vertex_iter)
			{
				centre = centre + preview_vector(*vertex_iter);
			}
			d_centre = preview_normalise(centre);
			const PreviewVector reference = std::fabs(d_centre.z) < 0.9
					? PreviewVector(0, 0, 1) : PreviewVector(1, 0, 0);
			d_east = preview_normalise(preview_cross(reference, d_centre));
			d_north = preview_normalise(preview_cross(d_centre, d_east));
			setMinimumHeight(280);
			setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
			setToolTip(QObject::tr(
					"Click an edge to select its table row. Ctrl-click toggles additional edges."));
		}

		void
		set_state(
				const RiftGeometry::Result *network,
				const std::vector<EdgeRole> *roles,
				const std::set<int> &selected)
		{
			d_network = network;
			d_roles = roles;
			d_selected = selected;
			update();
		}

		void
		set_edge_clicked_handler(
				const edge_clicked_handler_type &handler)
		{
			d_edge_clicked_handler = handler;
		}

	protected:
		virtual void
		paintEvent(
				QPaintEvent *)
		{
			QPainter painter(this);
			painter.setRenderHint(QPainter::Antialiasing, true);
			painter.fillRect(rect(), QColor(18, 24, 31));
			prepare_transform();

			painter.setPen(QPen(QColor(118, 144, 111), 2.0));
			painter.setBrush(QColor(61, 78, 61));
			painter.drawPath(polygon_path(*d_continent));
			painter.setPen(QPen(QColor(225, 170, 74), 1.7));
			painter.setBrush(QColor(151, 94, 35, 210));
			for (std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type>::const_iterator
					craton_iter = d_cratons.begin(); craton_iter != d_cratons.end(); ++craton_iter)
			{
				painter.drawPath(polygon_path(**craton_iter));
			}

			if (!d_network || !d_roles)
			{
				painter.setPen(Qt::white);
				painter.drawText(rect(), Qt::AlignCenter, QObject::tr("Generating Voronoi network..."));
				return;
			}

			for (unsigned int edge_index = 0; edge_index < d_network->edges.size(); ++edge_index)
			{
				QColor colour(76, 201, 240);
				double width = 2.0;
				if ((*d_roles)[edge_index] == EDGE_MID_OCEAN_RIDGE)
				{
					colour = QColor(199, 112, 255);
					width = 4.0;
				}
				else if ((*d_roles)[edge_index] == EDGE_FAILED_RIFT)
				{
					colour = failed_edge_is_eligible(edge_index, *d_network, *d_roles) &&
							failed_edge_is_inside_planning_area(
									d_network->edges[edge_index], *d_continent, d_cratons)
							? QColor(235, 235, 235) : QColor(255, 76, 76);
					width = 3.2;
				}
				if (d_selected.find(static_cast<int>(edge_index)) != d_selected.end())
				{
					colour = QColor(255, 232, 76);
					width = 5.0;
				}
				painter.setPen(QPen(colour, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
				painter.setBrush(Qt::NoBrush);
				painter.drawPath(polyline_path(*d_network->edges[edge_index].polyline));
			}

			painter.setPen(QColor(215, 222, 228));
			painter.drawText(QRect(10, 7, width() - 20, 22), Qt::AlignLeft,
					QObject::tr("Blue: undecided   Purple: MOR   White: failed rift   Gold: craton exclusion"));
		}

		virtual void
		mousePressEvent(
				QMouseEvent *event)
		{
			if (!d_network || !d_have_transform || !d_edge_clicked_handler)
			{
				return;
			}
			const QPointF click(event->pos());
			double closest_distance = 11.0;
			int closest_edge = -1;
			for (unsigned int edge_index = 0; edge_index < d_network->edges.size(); ++edge_index)
			{
				GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter =
						d_network->edges[edge_index].polyline->vertex_begin();
				QPointF previous = to_view(project(*vertex_iter));
				for (++vertex_iter; vertex_iter != d_network->edges[edge_index].polyline->vertex_end(); ++vertex_iter)
				{
					const QPointF current = to_view(project(*vertex_iter));
					const double distance = point_segment_distance(click, previous, current);
					if (distance < closest_distance)
					{
						closest_distance = distance;
						closest_edge = static_cast<int>(edge_index);
					}
					previous = current;
				}
			}
			if (closest_edge >= 0)
			{
				d_edge_clicked_handler(closest_edge, event->modifiers());
			}
		}

	private:
		QPointF
		project(
				const GPlatesMaths::PointOnSphere &point) const
		{
			const PreviewVector vector = preview_vector(point);
			const double denominator = std::max(1e-10, 1.0 + preview_dot(d_centre, vector));
			const double scale = std::sqrt(2.0 / denominator);
			return QPointF(scale * preview_dot(d_east, vector), scale * preview_dot(d_north, vector));
		}

		void
		prepare_transform()
		{
			double minimum_x = 1e10;
			double maximum_x = -1e10;
			double minimum_y = 1e10;
			double maximum_y = -1e10;
			for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
					d_continent->exterior_ring_vertex_begin();
				vertex_iter != d_continent->exterior_ring_vertex_end(); ++vertex_iter)
			{
				const QPointF point = project(*vertex_iter);
				minimum_x = std::min(minimum_x, point.x());
				maximum_x = std::max(maximum_x, point.x());
				minimum_y = std::min(minimum_y, point.y());
				maximum_y = std::max(maximum_y, point.y());
			}
			d_world_centre = QPointF(0.5 * (minimum_x + maximum_x), 0.5 * (minimum_y + maximum_y));
			d_view_centre = rect().center();
			const double available_width = std::max(1, width() - 36);
			const double available_height = std::max(1, height() - 54);
			d_scale = std::min(
					available_width / std::max(1e-8, maximum_x - minimum_x),
					available_height / std::max(1e-8, maximum_y - minimum_y));
			d_view_centre.setY(d_view_centre.y() + 10.0);
			d_have_transform = true;
		}

		QPointF
		to_view(
				const QPointF &world) const
		{
			return QPointF(
					d_view_centre.x() + (world.x() - d_world_centre.x()) * d_scale,
					d_view_centre.y() - (world.y() - d_world_centre.y()) * d_scale);
		}

		QPainterPath
		polygon_path(
				const GPlatesMaths::PolygonOnSphere &polygon) const
		{
			QPainterPath path;
			GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
					polygon.exterior_ring_vertex_begin();
			if (vertex_iter == polygon.exterior_ring_vertex_end())
			{
				return path;
			}
			path.moveTo(to_view(project(*vertex_iter)));
			for (++vertex_iter; vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
			{
				path.lineTo(to_view(project(*vertex_iter)));
			}
			path.closeSubpath();
			return path;
		}

		QPainterPath
		polyline_path(
				const GPlatesMaths::PolylineOnSphere &polyline) const
		{
			QPainterPath path;
			GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = polyline.vertex_begin();
			if (vertex_iter == polyline.vertex_end())
			{
				return path;
			}
			path.moveTo(to_view(project(*vertex_iter)));
			for (++vertex_iter; vertex_iter != polyline.vertex_end(); ++vertex_iter)
			{
				path.lineTo(to_view(project(*vertex_iter)));
			}
			return path;
		}

		static double
		point_segment_distance(
				const QPointF &point,
				const QPointF &start,
				const QPointF &end)
		{
			const QPointF segment = end - start;
			const double length_squared = segment.x() * segment.x() + segment.y() * segment.y();
			if (length_squared <= 1e-12)
			{
				return std::sqrt(QPointF::dotProduct(point - start, point - start));
			}
			const QPointF relative = point - start;
			const double interpolation = std::max(0.0, std::min(1.0,
					QPointF::dotProduct(relative, segment) / length_squared));
			const QPointF difference = point - (start + segment * interpolation);
			return std::sqrt(QPointF::dotProduct(difference, difference));
		}

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type d_continent;
		std::vector<GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type> d_cratons;
		const RiftGeometry::Result *d_network;
		const std::vector<EdgeRole> *d_roles;
		std::set<int> d_selected;
		edge_clicked_handler_type d_edge_clicked_handler;
		PreviewVector d_centre;
		PreviewVector d_east;
		PreviewVector d_north;
		QPointF d_world_centre;
		QPointF d_view_centre;
		double d_scale;
		bool d_have_transform;
	};

	boost::optional<GPlatesAppLogic::Layer>
	find_reconstruct_layer_for_collection(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		GPlatesAppLogic::ReconstructGraph &reconstruct_graph = application_state.get_reconstruct_graph();
		for (GPlatesAppLogic::ReconstructGraph::iterator layer_iter = reconstruct_graph.begin();
			layer_iter != reconstruct_graph.end(); ++layer_iter)
		{
			GPlatesAppLogic::Layer layer = *layer_iter;
			if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}
			const std::vector<GPlatesAppLogic::Layer::InputConnection> inputs =
					layer.get_channel_inputs(layer.get_main_input_feature_collection_channel());
			for (std::vector<GPlatesAppLogic::Layer::InputConnection>::const_iterator input_iter = inputs.begin();
				input_iter != inputs.end(); ++input_iter)
			{
				const boost::optional<GPlatesAppLogic::Layer::InputFile> input_file = input_iter->get_input_file();
				if (input_file && input_file->get_feature_collection() == collection)
				{
					return layer;
				}
			}
		}
		return boost::none;
	}

	void
	name_output_layer(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			const QString &name)
	{
		const boost::optional<GPlatesAppLogic::Layer> layer =
				find_reconstruct_layer_for_collection(application_state, collection);
		if (!layer)
		{
			return;
		}
		boost::shared_ptr<GPlatesPresentation::VisualLayer> visual_layer =
				view_state.get_visual_layers().get_visual_layer(*layer).lock();
		if (visual_layer)
		{
			visual_layer->set_custom_name(name);
			visual_layer->set_visible(true);
		}
	}
}


GPlatesViewOperations::ProposeInitialRiftsOperation::ProposeInitialRiftsOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_rendered_geometry_collection(view_state.get_rendered_geometry_collection()),
	d_model_interface(application_state.get_model_interface()),
	d_review_in_progress(false)
{  }


GPlatesViewOperations::ProposeInitialRiftsOperation::Result
GPlatesViewOperations::ProposeInitialRiftsOperation::trigger(
		QWidget *parent_widget)
{
	if (d_review_in_progress)
	{
		return Result(OPERATION_CANCELLED, QObject::tr("The Voronoi rift review window is already open."));
	}
	d_review_in_progress = true;
	try
	{
		const RiftSource source = gather_selected_rift_source(
				d_view_state.get_feature_focus(), d_application_state);
		const double start_time = d_application_state.get_current_reconstruction().get_reconstruction_time();

		QDialog dialog(parent_widget);
		dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Select Voronoi Rift Edges"));
		dialog.setModal(false);
		dialog.setWindowModality(Qt::NonModal);
		dialog.resize(1100, 760);
		QVBoxLayout *dialog_layout = new QVBoxLayout(&dialog);
		QLabel *introduction = new QLabel(
				QObject::tr(
						"A random Voronoi network is clipped to the selected ContinentalCrust polygon at %1 Ma. "
						"Any complete edge that enters one of the %2 active Craton polygons is removed. "
						"Select one or more rows, then classify those edges as provisional mid-ocean ridge, failed rift, or ignored. "
						"You can also click edges directly in the preview; Ctrl-click toggles additional edges. "
						"Purple edges are MORs, white edges are failed rifts, and yellow edges are the rows currently selected. "
						"Failed rifts must touch a selected MOR node or reach the coastline.")
				.arg(start_time, 0, 'f', 1)
				.arg(source.cratons.size()),
				&dialog);
		introduction->setWordWrap(true);
		dialog_layout->addWidget(introduction);

		QGroupBox *generation_group = new QGroupBox(QObject::tr("Voronoi generation"), &dialog);
		QFormLayout *generation_form = new QFormLayout(generation_group);
		QDoubleSpinBox *cell_spacing = new QDoubleSpinBox(generation_group);
		cell_spacing->setRange(500.0, 4000.0);
		cell_spacing->setDecimals(0);
		cell_spacing->setSingleStep(100.0);
		cell_spacing->setValue(1800.0);
		cell_spacing->setSuffix(QObject::tr(" km"));
		cell_spacing->setToolTip(QObject::tr(
				"Controls the approximate distance between random Voronoi sites. Smaller values create more, shorter selectable edges."));
		generation_form->addRow(QObject::tr("Cell spacing factor:"), cell_spacing);
		QDoubleSpinBox *minimum_edge_factor = new QDoubleSpinBox(generation_group);
		minimum_edge_factor->setRange(0.05, 0.50);
		minimum_edge_factor->setDecimals(2);
		minimum_edge_factor->setSingleStep(0.02);
		minimum_edge_factor->setValue(0.16);
		minimum_edge_factor->setToolTip(QObject::tr(
				"Drops clipped fragments shorter than this fraction of the cell spacing."));
		generation_form->addRow(QObject::tr("Minimum edge factor:"), minimum_edge_factor);
		QDoubleSpinBox *maximum_segment_length = new QDoubleSpinBox(generation_group);
		maximum_segment_length->setRange(50.0, 300.0);
		maximum_segment_length->setDecimals(0);
		maximum_segment_length->setSingleStep(10.0);
		maximum_segment_length->setValue(130.0);
		maximum_segment_length->setSuffix(QObject::tr(" km"));
		generation_form->addRow(QObject::tr("Maximum render segment:"), maximum_segment_length);
		QSpinBox *seed = new QSpinBox(generation_group);
		seed->setRange(0, INT_MAX);
		seed->setValue(static_cast<int>(std::random_device()() & INT_MAX));
		generation_form->addRow(QObject::tr("Seed:"), seed);
		dialog_layout->addWidget(generation_group);

		QTableWidget *edge_table = new QTableWidget(&dialog);
		edge_table->setColumnCount(4);
		edge_table->setHorizontalHeaderLabels(QStringList()
				<< QObject::tr("Edge") << QObject::tr("Length")
				<< QObject::tr("Location") << QObject::tr("Decision"));
		edge_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
		edge_table->setSelectionBehavior(QAbstractItemView::SelectRows);
		edge_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
		edge_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
		edge_table->setSortingEnabled(false);
		RiftNetworkPreviewWidget *network_preview = new RiftNetworkPreviewWidget(
				source.continent, source.cratons, &dialog);
		QSplitter *review_splitter = new QSplitter(Qt::Horizontal, &dialog);
		review_splitter->addWidget(network_preview);
		review_splitter->addWidget(edge_table);
		review_splitter->setStretchFactor(0, 3);
		review_splitter->setStretchFactor(1, 2);
		dialog_layout->addWidget(review_splitter, 1);

		QHBoxLayout *classification_layout = new QHBoxLayout;
		QPushButton *mark_mor = new QPushButton(QObject::tr("Mark Selected as MOR"), &dialog);
		QPushButton *mark_failed = new QPushButton(QObject::tr("Mark Selected as Failed Rift"), &dialog);
		QPushButton *clear_decision = new QPushButton(QObject::tr("Clear Selected"), &dialog);
		QCheckBox *show_sites = new QCheckBox(QObject::tr("Show Voronoi sites"), &dialog);
		classification_layout->addWidget(mark_mor);
		classification_layout->addWidget(mark_failed);
		classification_layout->addWidget(clear_decision);
		classification_layout->addStretch();
		classification_layout->addWidget(show_sites);
		dialog_layout->addLayout(classification_layout);

		QLabel *status = new QLabel(&dialog);
		status->setWordWrap(true);
		dialog_layout->addWidget(status);

		QDialogButtonBox *buttons = new QDialogButtonBox(
				QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
				Qt::Horizontal,
				&dialog);
		QPushButton *regenerate = buttons->addButton(
				QObject::tr("Regenerate Network"), QDialogButtonBox::ActionRole);
		QPushButton *commit = buttons->button(QDialogButtonBox::Ok);
		commit->setText(QObject::tr("Commit Edge Decisions"));
		commit->setEnabled(false);
		dialog_layout->addWidget(buttons);

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
						RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		boost::optional<RiftGeometry::Result> network;
		std::vector<EdgeRole> roles;

			auto update_review = [&]()
			{
			preview_layer->clear_rendered_geometries();
			if (!network)
			{
				network_preview->set_state(NULL, NULL, std::set<int>());
				commit->setEnabled(false);
				return;
			}
			const std::set<int> highlighted_rows = selected_rows(*edge_table);
			network_preview->set_state(&*network, &roles, highlighted_rows);
				const GPlatesGui::Colour mor_colour(85.0f / 255.0f, 0.0f, 127.0f / 255.0f);
				unsigned int mor_count = 0;
				std::vector<unsigned int> mor_edge_indices;
				unsigned int failed_count = 0;
			unsigned int invalid_failed_count = 0;
			for (unsigned int edge_index = 0; edge_index < network->edges.size(); ++edge_index)
			{
				GPlatesGui::Colour colour = GPlatesGui::Colour::get_aqua();
				float width = 2.2f;
					if (roles[edge_index] == EDGE_MID_OCEAN_RIDGE)
					{
						colour = mor_colour;
						width = 4.0f;
						++mor_count;
						mor_edge_indices.push_back(edge_index);
				}
				else if (roles[edge_index] == EDGE_FAILED_RIFT)
				{
					colour = GPlatesGui::Colour::get_silver();
					width = 3.0f;
					++failed_count;
					if (!failed_edge_is_eligible(edge_index, *network, roles) ||
							!failed_edge_is_inside_planning_area(
									network->edges[edge_index], *source.continent, source.cratons))
					{
						colour = GPlatesGui::Colour::get_red();
						++invalid_failed_count;
					}
				}
				if (highlighted_rows.find(static_cast<int>(edge_index)) != highlighted_rows.end())
				{
					colour = GPlatesGui::Colour::get_yellow();
					width = 5.0f;
				}
				preview_layer->add_rendered_geometry(
						RenderedGeometryFactory::create_rendered_polyline_on_sphere(
								network->edges[edge_index].polyline, colour, width));
			}
			if (show_sites->isChecked())
			{
				for (std::vector<GPlatesMaths::PointOnSphere>::const_iterator site_iter =
						network->sites.begin(); site_iter != network->sites.end(); ++site_iter)
				{
					preview_layer->add_rendered_geometry(
							RenderedGeometryFactory::create_rendered_point_on_sphere(
									*site_iter, GPlatesGui::Colour::get_aqua(), 4.0f));
				}
			}

				const unsigned int mor_components = count_mor_components(*network, roles);
				const unsigned int joined_mor_path_count = static_cast<unsigned int>(
						RiftGeometry::join_edge_paths(*network, mor_edge_indices).size());
				if (invalid_failed_count)
				{
					status->setText(QObject::tr(
							"%1 MOR edge(s) in %2 connected group(s) will save as %3 joined polyline(s); %4 failed-rift edge(s). "
							"%5 failed edge(s) are red because they are disconnected, leave continental crust, or enter a craton. "
							"%6 craton-crossing candidate edge(s) were culled.")
						.arg(mor_count).arg(mor_components).arg(joined_mor_path_count)
						.arg(failed_count).arg(invalid_failed_count)
						.arg(network->metrics.craton_intersection_count));
			}
			else
			{
				status->setText(QObject::tr(
						"%1 sites produced %2 clipped edges averaging %3 km after culling %4 craton-crossing candidate edge(s). "
							"Decisions: %5 MOR edge(s) in %6 connected group(s) will save as %7 joined polyline(s); %8 failed-rift edge(s). "
							"Plate grouping and half-stage rotations are intentionally deferred.")
					.arg(network->metrics.site_count)
					.arg(network->metrics.edge_count)
					.arg(network->metrics.average_edge_length_km, 0, 'f', 0)
					.arg(network->metrics.craton_intersection_count)
						.arg(mor_count).arg(mor_components).arg(joined_mor_path_count).arg(failed_count));
			}
			commit->setEnabled(mor_count > 0 && invalid_failed_count == 0);
		};

		auto update_role_cells = [&]()
		{
			for (unsigned int row = 0; row < roles.size(); ++row)
			{
				QTableWidgetItem *role_item = edge_table->item(row, 3);
				if (role_item)
				{
					role_item->setText(role_name(roles[row]));
				}
			}
		};

		auto classify_selected = [&](EdgeRole role)
		{
			const std::set<int> rows = selected_rows(*edge_table);
			for (std::set<int>::const_iterator row_iter = rows.begin(); row_iter != rows.end(); ++row_iter)
			{
				if (*row_iter >= 0 && *row_iter < static_cast<int>(roles.size()))
				{
					roles[*row_iter] = role;
				}
			}
			update_role_cells();
			update_review();
		};

		auto regenerate_network = [&]()
		{
			preview_layer->clear_rendered_geometries();
			commit->setEnabled(false);
			try
			{
				RiftGeometry::Parameters parameters;
				parameters.random_seed = static_cast<boost::uint32_t>(seed->value());
				parameters.target_cell_spacing_km = cell_spacing->value();
				parameters.minimum_edge_length_factor = minimum_edge_factor->value();
				parameters.maximum_segment_length_km = maximum_segment_length->value();
				parameters.planet_radius_km = GPlatesUtils::Earth::MEAN_RADIUS_KMS;
				network = RiftGeometry::generate(source.continent, source.cratons, parameters);
				roles.assign(network->edges.size(), EDGE_IGNORED);
				edge_table->setRowCount(static_cast<int>(network->edges.size()));
				for (unsigned int row = 0; row < network->edges.size(); ++row)
				{
					const RiftGeometry::Edge &edge = network->edges[row];
					edge_table->setItem(row, 0,
							new QTableWidgetItem(QObject::tr("Edge %1").arg(row + 1, 3, 10, QChar('0'))));
					edge_table->setItem(row, 1,
							new QTableWidgetItem(QObject::tr("%1 km").arg(edge.length_km, 0, 'f', 0)));
					edge_table->setItem(row, 2,
							new QTableWidgetItem(edge.touches_coast ? QObject::tr("Coast-connected") : QObject::tr("Interior")));
					edge_table->setItem(row, 3,
							new QTableWidgetItem(role_name(EDGE_IGNORED)));
				}
				if (!network->edges.empty())
				{
					edge_table->selectRow(0);
				}
				update_review();
			}
			catch (const std::exception &exception)
			{
				network = boost::none;
				roles.clear();
				edge_table->setRowCount(0);
				status->setText(QObject::tr("Voronoi generation failed: %1").arg(exception.what()));
			}
		};

		QObject::connect(edge_table, &QTableWidget::itemSelectionChanged, [&]() { update_review(); });
		network_preview->set_edge_clicked_handler(
				[&](int edge_index, Qt::KeyboardModifiers modifiers)
				{
					const QModelIndex index = edge_table->model()->index(edge_index, 0);
					QItemSelectionModel::SelectionFlags flags = QItemSelectionModel::Rows;
					flags |= (modifiers & Qt::ControlModifier)
							? QItemSelectionModel::Toggle : QItemSelectionModel::ClearAndSelect;
					edge_table->selectionModel()->select(index, flags);
					edge_table->setCurrentCell(edge_index, 0, QItemSelectionModel::NoUpdate);
					edge_table->scrollToItem(edge_table->item(edge_index, 0));
				});
		QObject::connect(mark_mor, &QPushButton::clicked, [&]() { classify_selected(EDGE_MID_OCEAN_RIDGE); });
		QObject::connect(mark_failed, &QPushButton::clicked, [&]() { classify_selected(EDGE_FAILED_RIFT); });
		QObject::connect(clear_decision, &QPushButton::clicked, [&]() { classify_selected(EDGE_IGNORED); });
		QObject::connect(show_sites, &QCheckBox::toggled, [&](bool) { update_review(); });
		QObject::connect(regenerate, &QPushButton::clicked, [&]() { regenerate_network(); });
		QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

		regenerate_network();
		QEventLoop review_loop;
		QObject::connect(&dialog, &QDialog::finished, &review_loop, &QEventLoop::quit);
		dialog.show();
		review_loop.exec();
		preview_layer->clear_rendered_geometries();

		if (dialog.result() != QDialog::Accepted || !network)
		{
			d_review_in_progress = false;
			return Result(OPERATION_CANCELLED, QObject::tr("Voronoi rift review closed; no data changed."));
		}

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> mor_features;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> failed_features;
		std::vector<unsigned int> mor_edge_indices;
		for (unsigned int edge_index = 0; edge_index < roles.size(); ++edge_index)
		{
			if (roles[edge_index] == EDGE_MID_OCEAN_RIDGE)
			{
				mor_edge_indices.push_back(edge_index);
			}
			else if (roles[edge_index] == EDGE_FAILED_RIFT)
			{
				failed_features.push_back(create_line_feature(
						GPlatesModel::FeatureType::create_gpml("ContinentalRift"),
						QObject::tr("Failed Rift Edge %1 (%2 Ma)")
						.arg(edge_index + 1, 3, 10, QChar('0'))
						.arg(start_time, 0, 'f', 1),
						source.plate_id,
						start_time,
						boost::none,
						network->edges[edge_index].polyline));
			}
		}
		const std::vector<GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type> joined_mor_paths =
				RiftGeometry::join_edge_paths(*network, mor_edge_indices);
		for (unsigned int path_index = 0; path_index < joined_mor_paths.size(); ++path_index)
		{
			mor_features.push_back(create_line_feature(
					GPlatesModel::FeatureType::create_gpml("MidOceanRidge"),
					QObject::tr("Provisional MOR Route %1 (%2 Ma)")
					.arg(path_index + 1, 3, 10, QChar('0'))
					.arg(start_time, 0, 'f', 1),
					boost::none,
					start_time,
					start_time,
					joined_mor_paths[path_index]));
		}

		std::vector<FeatureGroup> groups;
		GPlatesAppLogic::FeatureCollectionFileState::file_reference mor_output_file =
				create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("Provisional Mid-Ocean Ridges"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref mor_collection =
				mor_output_file.get_file().get_feature_collection();
		groups.push_back(FeatureGroup(mor_collection, mor_features));

		GPlatesModel::FeatureCollectionHandle::weak_ref failed_collection;
		if (!failed_features.empty())
		{
			GPlatesAppLogic::FeatureCollectionFileState::file_reference failed_output_file =
					create_named_empty_feature_collection(
							d_application_state.get_feature_collection_file_io(),
							QObject::tr("Failed Rifts"));
			failed_collection = failed_output_file.get_file().get_feature_collection();
			groups.push_back(FeatureGroup(failed_collection, failed_features));
		}

		std::unique_ptr<QUndoCommand> command(new CommitVoronoiRiftsUndoCommand(
				d_model_interface, groups));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		name_output_layer(d_application_state, d_view_state, mor_collection,
				QObject::tr("Provisional Mid-Ocean Ridges"));
		if (failed_collection.is_valid())
		{
			name_output_layer(d_application_state, d_view_state, failed_collection,
					QObject::tr("Failed Rifts"));
		}

		d_review_in_progress = false;
		return Result(OPERATION_COMPLETED,
				QObject::tr(
						"Joined %1 selected MOR edge(s) into %2 provisional MOR route polyline(s), and committed %3 failed-rift edge(s) at %4 Ma in separate feature collections. "
						"No .rot file or half-stage plate IDs were invented; those remain for the plate-grouping step. Undo removes the complete decision.")
				.arg(mor_edge_indices.size())
				.arg(mor_features.size())
				.arg(failed_features.size())
				.arg(start_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		d_review_in_progress = false;
		return Result(OPERATION_ERROR,
				QObject::tr("Could not build the Voronoi rift network: %1").arg(exception.what()));
	}
}
