/* $Id$ */

/**
 * \file
 * Implements the Worldbuilding Pasta "Make Rift" operation.
 */

#include "MakeRiftOperation.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QStringList>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "MORFeatureBuilder.h"
#include "SplitPlateOperation.h"
#include "UndoRedo.h"
#include "WorldbuildingFeatureCollectionUtils.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/GeometryIntersect.h"
#include "maths/Vector3D.h"

#include "model/FeatureCollectionHandle.h"
#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/EnumerationType.h"
#include "property-values/GeoTimeInstant.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"


namespace
{
	struct CratonInfo
	{
		CratonInfo(
				const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon_,
				const boost::optional<GPlatesModel::integer_plate_id_type> &plate_id_) :
			polygon(polygon_),
			plate_id(plate_id_)
		{ }

		GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type polygon;
		boost::optional<GPlatesModel::integer_plate_id_type> plate_id;
	};

	typedef std::vector<CratonInfo> craton_seq_type;
	typedef std::vector<GPlatesModel::integer_plate_id_type> plate_id_seq_type;

	craton_seq_type
	gather_relevant_cratons(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesMaths::PolygonOnSphere &continent)
	{
		static const GPlatesModel::FeatureType CRATON = GPlatesModel::FeatureType::create_gpml("Craton");
		craton_seq_type cratons;
		std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layers);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layers.begin(); layer_iter != layers.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg = **geometry_iter;
				if (!rfg.is_valid() || !rfg.property().is_still_valid() || !rfg.get_feature_ref().is_valid() ||
						rfg.get_feature_ref()->feature_type() != CRATON)
				{
					continue;
				}
				const GPlatesMaths::PolygonOnSphere *polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(rfg.reconstructed_geometry().get());
				if (!polygon || !continent.is_point_in_polygon(*polygon->exterior_ring_vertex_begin()))
				{
					continue;
				}
				const GPlatesModel::TopLevelProperty *property = (*rfg.property()).get();
				if (seen_properties.insert(property).second)
				{
					cratons.push_back(CratonInfo(
							polygon->get_non_null_pointer(), rfg.reconstruction_plate_id()));
				}
			}
		}
		return cratons;
	}

	bool
	cutter_intersects_cratons(
			const GPlatesMaths::PolylineOnSphere &cutter,
			const craton_seq_type &cratons)
	{
		for (craton_seq_type::const_iterator craton_iter = cratons.begin();
			craton_iter != cratons.end(); ++craton_iter)
		{
			for (GPlatesMaths::PolylineOnSphere::vertex_const_iterator vertex_iter = cutter.vertex_begin();
				vertex_iter != cutter.vertex_end(); ++vertex_iter)
			{
				if (craton_iter->polygon->is_point_in_polygon(*vertex_iter))
				{
					return true;
				}
			}
			GPlatesMaths::GeometryIntersect::Graph intersections;
			if (GPlatesMaths::GeometryIntersect::intersect(
					intersections, *craton_iter->polygon, cutter, true) &&
					!intersections.unordered_intersections.empty())
			{
				return true;
			}
		}
		return false;
	}

	plate_id_seq_type
	craton_plate_ids_in_polygon(
			const GPlatesMaths::PolygonOnSphere &child_polygon,
			const craton_seq_type &cratons)
	{
		plate_id_seq_type plate_ids;
		for (craton_seq_type::const_iterator craton_iter = cratons.begin();
			craton_iter != cratons.end(); ++craton_iter)
		{
			if (craton_iter->plate_id && child_polygon.is_point_in_polygon(
					GPlatesMaths::PointOnSphere(craton_iter->polygon->get_interior_centroid())))
			{
				plate_ids.push_back(*craton_iter->plate_id);
			}
		}
		std::sort(plate_ids.begin(), plate_ids.end());
		plate_ids.erase(std::unique(plate_ids.begin(), plate_ids.end()), plate_ids.end());
		return plate_ids;
	}

	QString
	format_plate_ids(
			const plate_id_seq_type &plate_ids)
	{
		QStringList formatted_ids;
		for (plate_id_seq_type::const_iterator plate_id_iter = plate_ids.begin();
			plate_id_iter != plate_ids.end(); ++plate_id_iter)
		{
			formatted_ids.push_back(QString::number(*plate_id_iter));
		}
		return formatted_ids.join(QObject::tr(", "));
	}

	GPlatesModel::integer_plate_id_type
	recommended_plate_id(
			const plate_id_seq_type &craton_plate_ids,
			GPlatesModel::integer_plate_id_type fallback_plate_id,
			GPlatesModel::integer_plate_id_type source_plate_id)
	{
		if (std::find(craton_plate_ids.begin(), craton_plate_ids.end(), source_plate_id) !=
				craton_plate_ids.end())
		{
			return source_plate_id;
		}
		return craton_plate_ids.empty() ? fallback_plate_id : craton_plate_ids.front();
	}

	QComboBox *
	create_plate_id_combo(
			const plate_id_seq_type &craton_plate_ids,
			GPlatesModel::integer_plate_id_type recommended_id,
			QWidget *parent)
	{
		QComboBox *combo = new QComboBox(parent);
		combo->setEditable(true);
		combo->setInsertPolicy(QComboBox::NoInsert);
		combo->setDuplicatesEnabled(false);
		for (plate_id_seq_type::const_iterator plate_id_iter = craton_plate_ids.begin();
			plate_id_iter != craton_plate_ids.end(); ++plate_id_iter)
		{
			combo->addItem(QString::number(*plate_id_iter));
		}
		if (craton_plate_ids.empty())
		{
			combo->addItem(QString::number(recommended_id));
		}
		combo->setCurrentText(QString::number(recommended_id));
		combo->lineEdit()->setValidator(new QIntValidator(0, 99999999, combo));
		combo->setToolTip(QObject::tr(
				"Choose a listed craton plate ID, or type a new plate ID from 0 to 99999999."));
		return combo;
	}

	QLabel *
	create_craton_guidance(
			const QString &side,
			const plate_id_seq_type &craton_plate_ids,
			GPlatesModel::integer_plate_id_type recommended_id,
			QWidget *parent)
	{
		QString guidance;
		if (craton_plate_ids.empty())
		{
			guidance = QObject::tr(
					"No numbered craton was detected in the %1 child. Plate %2 is suggested; you can type another ID.")
					.arg(side).arg(recommended_id);
		}
		else if (craton_plate_ids.size() == 1)
		{
			guidance = QObject::tr(
					"The %1 child contains Craton %2, so Plate %2 is recommended. You can still type another ID.")
					.arg(side).arg(craton_plate_ids.front());
		}
		else
		{
			guidance = QObject::tr(
					"The %1 child contains Cratons %2. Choose the craton number that should represent this child plate, or type another ID. Recommended: %3.")
					.arg(side, format_plate_ids(craton_plate_ids)).arg(recommended_id);
		}
		QLabel *label = new QLabel(guidance, parent);
		label->setWordWrap(true);
		return label;
	}

	class MakeRiftDialog :
			public QDialog
	{
	public:
		MakeRiftDialog(
				GPlatesModel::integer_plate_id_type source_plate_id,
				GPlatesModel::integer_plate_id_type suggested_right_plate_id,
				const plate_id_seq_type &left_craton_plate_ids,
				const plate_id_seq_type &right_craton_plate_ids,
				QWidget *parent) :
			QDialog(parent)
		{
			const GPlatesModel::integer_plate_id_type left_recommended_id = recommended_plate_id(
					left_craton_plate_ids, source_plate_id, source_plate_id);
			GPlatesModel::integer_plate_id_type right_recommended_id = recommended_plate_id(
					right_craton_plate_ids, suggested_right_plate_id, source_plate_id);
			if (right_recommended_id == left_recommended_id)
			{
				const plate_id_seq_type::const_iterator distinct_craton_id = std::find_if(
						right_craton_plate_ids.begin(), right_craton_plate_ids.end(),
						[left_recommended_id](GPlatesModel::integer_plate_id_type plate_id)
						{
							return plate_id != left_recommended_id;
						});
				right_recommended_id = distinct_craton_id == right_craton_plate_ids.end()
						? suggested_right_plate_id : *distinct_craton_id;
			}
			setWindowTitle(QObject::tr("Worldbuilding Pasta - Make Rift"));
			QVBoxLayout *layout = new QVBoxLayout(this);
			QLabel *instructions = new QLabel(QObject::tr(
					"Names and plate IDs are assigned relative to the direction in which you drew the cutter. "
					"Each plate-ID list comes from the numbered cratons actually inside that child. "
					"Selecting an ID assigns the child continent and MOR side; it does not renumber the craton features. "
					"The actual MOR will use a HalfStageRotationVersion3 reconstruction between these plates."), this);
			instructions->setWordWrap(true);
			layout->addWidget(instructions);
			QFormLayout *form = new QFormLayout;
			d_left_name = new QLineEdit(QObject::tr("Left Rift Continent"), this);
			d_right_name = new QLineEdit(QObject::tr("Right Rift Continent"), this);
			d_left_plate = create_plate_id_combo(
					left_craton_plate_ids, left_recommended_id, this);
			d_right_plate = create_plate_id_combo(
					right_craton_plate_ids, right_recommended_id, this);
			form->addRow(QObject::tr("Left-side name:"), d_left_name);
			form->addRow(QObject::tr("Left plate ID:"), d_left_plate);
			form->addRow(QString(), create_craton_guidance(
					QObject::tr("left"), left_craton_plate_ids, left_recommended_id, this));
			form->addRow(QObject::tr("Right-side name:"), d_right_name);
			form->addRow(QObject::tr("Right plate ID:"), d_right_plate);
			form->addRow(QString(), create_craton_guidance(
					QObject::tr("right"), right_craton_plate_ids, right_recommended_id, this));
			layout->addLayout(form);
			QDialogButtonBox *buttons = new QDialogButtonBox(
					QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
			QObject::connect(buttons, &QDialogButtonBox::accepted, this, [this]()
			{
				if (left_name().isEmpty() || right_name().isEmpty())
				{
					QMessageBox::warning(this, QObject::tr("Names Required"),
							QObject::tr("Give both child continents a name before continuing."));
					return;
				}
				if (!plate_ids_are_valid())
				{
					QMessageBox::warning(this, QObject::tr("Plate IDs Required"),
							QObject::tr("Enter a whole-number plate ID from 0 to 99999999 for each child."));
					return;
				}
				if (left_plate() == right_plate())
				{
					QMessageBox::warning(this, QObject::tr("Distinct Plate IDs Required"),
							QObject::tr("Choose different plate IDs for the left and right child continents."));
					return;
				}
				accept();
			});
			QObject::connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
			layout->addWidget(buttons);
		}

		QString left_name() const { return d_left_name->text().trimmed(); }
		QString right_name() const { return d_right_name->text().trimmed(); }
		GPlatesModel::integer_plate_id_type left_plate() const
		{
			return static_cast<GPlatesModel::integer_plate_id_type>(d_left_plate->currentText().toUInt());
		}
		GPlatesModel::integer_plate_id_type right_plate() const
		{
			return static_cast<GPlatesModel::integer_plate_id_type>(d_right_plate->currentText().toUInt());
		}
		bool plate_ids_are_valid() const
		{
			bool left_is_valid = false;
			bool right_is_valid = false;
			d_left_plate->currentText().toUInt(&left_is_valid);
			d_right_plate->currentText().toUInt(&right_is_valid);
			return left_is_valid && right_is_valid && !d_left_plate->currentText().isEmpty() &&
					!d_right_plate->currentText().isEmpty();
		}
	private:
		QLineEdit *d_left_name;
		QLineEdit *d_right_name;
		QComboBox *d_left_plate;
		QComboBox *d_right_plate;
	};

	void
	set_required_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &property_name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::set_property(feature, property_name, value))
		{
			throw std::runtime_error(QString("Unable to set required property '%1'.")
					.arg(property_name.get_name().qstring()).toStdString());
		}
	}

	GPlatesModel::FeatureHandle::non_null_ptr_type
	clone_continent_child(
			const GPlatesModel::FeatureHandle::weak_ref &source,
			const GPlatesModel::FeatureHandle::iterator &source_geometry_property,
			const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type &polygon,
			const QString &name,
			GPlatesModel::integer_plate_id_type plate_id)
	{
		GPlatesModel::FeatureHandle::non_null_ptr_type child =
				GPlatesModel::FeatureHandle::create(source->feature_type());
		GPlatesModel::FeatureHandle::iterator child_geometry_property;
		for (GPlatesModel::FeatureHandle::iterator property_iter = source->begin();
			property_iter != source->end(); ++property_iter)
		{
			const GPlatesModel::FeatureHandle::iterator child_property = child->add((*property_iter)->clone());
			if (property_iter == source_geometry_property)
			{
				child_geometry_property = child_property;
			}
		}
		GPlatesModel::TopLevelProperty::non_null_ptr_type geometry_property =
				(*child_geometry_property)->clone();
		GPlatesFeatureVisitors::GeometrySetter geometry_setter(polygon);
		geometry_setter.set_geometry(geometry_property.get());
		child->set(child_geometry_property, geometry_property);
		set_required_property(
				child->reference(), GPlatesModel::PropertyName::create_gml("name"),
				GPlatesPropertyValues::XsString::create(
						GPlatesUtils::make_icu_string_from_qstring(name)));
		set_required_property(
				child->reference(), GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
				GPlatesPropertyValues::GpmlPlateId::create(plate_id));
		return child;
	}

	class MakeRiftUndoCommand :
			public QUndoCommand
	{
	public:
		MakeRiftUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const GPlatesModel::FeatureHandle::weak_ref &source_feature,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &left_feature,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &right_feature,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &mor_collection,
				const GPlatesModel::FeatureHandle::non_null_ptr_type &mor_feature) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_source_feature(source_feature),
			d_left_feature(left_feature),
			d_right_feature(right_feature),
			d_mor_collection(mor_collection),
			d_mor_feature(mor_feature),
			d_first_redo(true)
		{
			setText(QObject::tr("make continental rift"));
		}

		virtual void redo()
		{
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_first_redo)
			{
				GPlatesModel::FeatureCollectionHandle *collection = d_source_feature->parent_ptr();
				if (!collection)
				{
					return;
				}
				d_continent_collection = collection->reference();
				for (GPlatesModel::FeatureCollectionHandle::iterator feature_iter = collection->begin();
					feature_iter != collection->end(); ++feature_iter)
				{
					if ((*feature_iter)->reference() == d_source_feature)
					{
						d_original_feature = collection->remove(feature_iter);
						break;
					}
				}
				d_first_redo = false;
			}
			else if (d_original_iterator && d_original_iterator->is_still_valid())
			{
				d_original_feature = d_continent_collection->remove(*d_original_iterator);
				d_original_iterator = boost::none;
			}
			if (!d_original_feature || !d_continent_collection.is_valid() || !d_mor_collection.is_valid())
			{
				return;
			}
			d_left_iterator = d_continent_collection->add(d_left_feature);
			d_right_iterator = d_continent_collection->add(d_right_feature);
			d_mor_iterator = d_mor_collection->add(d_mor_feature);
			guard.release_guard();
		}

		virtual void undo()
		{
			if (!d_original_feature || !d_left_iterator || !d_right_iterator || !d_mor_iterator)
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesModel::NotificationGuard guard(*d_model_interface.access_model());
			if (d_mor_iterator->is_still_valid())
			{
				d_mor_feature = d_mor_collection->remove(*d_mor_iterator);
			}
			if (d_right_iterator->is_still_valid())
			{
				d_right_feature = d_continent_collection->remove(*d_right_iterator);
			}
			if (d_left_iterator->is_still_valid())
			{
				d_left_feature = d_continent_collection->remove(*d_left_iterator);
			}
			d_original_iterator = d_continent_collection->add(*d_original_feature);
			d_left_iterator = boost::none;
			d_right_iterator = boost::none;
			d_mor_iterator = boost::none;
			guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		GPlatesModel::FeatureHandle::weak_ref d_source_feature;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_left_feature;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_right_feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_mor_collection;
		GPlatesModel::FeatureHandle::non_null_ptr_type d_mor_feature;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_continent_collection;
		boost::optional<GPlatesModel::FeatureHandle::non_null_ptr_type> d_original_feature;
		boost::optional<GPlatesModel::FeatureCollectionHandle::iterator> d_original_iterator;
		boost::optional<GPlatesModel::FeatureCollectionHandle::iterator> d_left_iterator;
		boost::optional<GPlatesModel::FeatureCollectionHandle::iterator> d_right_iterator;
		boost::optional<GPlatesModel::FeatureCollectionHandle::iterator> d_mor_iterator;
		bool d_first_redo;
	};

	double
	polygon_side(
			const GPlatesMaths::PolygonOnSphere &polygon,
			const GPlatesMaths::PointOnSphere &rift_start,
			const GPlatesMaths::PointOnSphere &rift_end)
	{
		const GPlatesMaths::Vector3D normal = GPlatesMaths::cross(
				rift_start.position_vector(), rift_end.position_vector());
		double side = 0;
		for (GPlatesMaths::PolygonOnSphere::ring_vertex_const_iterator vertex_iter =
				polygon.exterior_ring_vertex_begin();
			vertex_iter != polygon.exterior_ring_vertex_end(); ++vertex_iter)
		{
			side += GPlatesMaths::dot(normal, vertex_iter->position_vector()).dval();
		}
		return side;
	}

	GPlatesModel::integer_plate_id_type
	suggest_right_plate_id(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesModel::integer_plate_id_type source_plate_id)
	{
		std::set<GPlatesModel::integer_plate_id_type> used;
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layers;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layers);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layers.begin(); layer_iter != layers.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					geometry_iter = geometries.begin(); geometry_iter != geometries.end(); ++geometry_iter)
			{
				if ((*geometry_iter)->reconstruction_plate_id())
				{
					used.insert(*(*geometry_iter)->reconstruction_plate_id());
				}
			}
		}
		GPlatesModel::integer_plate_id_type candidate = source_plate_id + 1;
		while (used.find(candidate) != used.end())
		{
			++candidate;
		}
		return candidate;
	}

	boost::optional<GPlatesAppLogic::Layer>
	find_reconstruct_layer_for_collection(
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
	{
		GPlatesAppLogic::ReconstructGraph &graph = application_state.get_reconstruct_graph();
		for (GPlatesAppLogic::ReconstructGraph::iterator layer_iter = graph.begin();
			layer_iter != graph.end(); ++layer_iter)
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
				const boost::optional<GPlatesAppLogic::Layer::InputFile> file = input_iter->get_input_file();
				if (file && file->get_feature_collection() == collection)
				{
					return layer;
				}
			}
		}
		return boost::none;
	}

	void
	name_mor_layer(
			GPlatesAppLogic::ApplicationState &application_state,
			GPlatesPresentation::ViewState &view_state,
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection)
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
			visual_layer->set_custom_name(QObject::tr("Active Mid-Ocean Ridges"));
			visual_layer->set_visible(true);
		}
	}
}


GPlatesViewOperations::MakeRiftOperation::MakeRiftOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface()),
	d_selection_mode(NOT_SELECTING)
{  }


GPlatesViewOperations::MakeRiftOperation::Result
GPlatesViewOperations::MakeRiftOperation::arm_continent_selection()
{
	// Clear the current focus before arming so clicking the same feature again still
	// produces a focus_changed signal.
	d_selection_mode = NOT_SELECTING;
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_CONTINENT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select a ContinentalCrust polygon in the globe or map view."));
}


GPlatesViewOperations::MakeRiftOperation::Result
GPlatesViewOperations::MakeRiftOperation::arm_rift_selection()
{
	d_selection_mode = NOT_SELECTING;
	d_feature_focus.unset_focus();
	d_selection_mode = SELECTING_RIFT;
	return Result(SELECTION_ARMED,
			QObject::tr("Select the polyline that should become the rift. It does not need to be a provisional MOR."));
}


GPlatesViewOperations::MakeRiftOperation::Result
GPlatesViewOperations::MakeRiftOperation::capture_armed_selection()
{
	if (d_selection_mode == NOT_SELECTING)
	{
		return Result(OPERATION_CANCELLED, QString());
	}
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That item has no selectable reconstructed feature geometry; selection is still armed."));
	}

	const boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> rfg =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!rfg)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Make Rift requires editable reconstructed feature geometry; selection is still armed."));
	}
	const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry = (*rfg)->reconstructed_geometry();
	const GPlatesMaths::PolygonOnSphere *polygon =
			dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(geometry.get());
	const GPlatesMaths::PolylineOnSphere *polyline =
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(geometry.get());
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();

	if (d_selection_mode == SELECTING_CONTINENT)
	{
		static const GPlatesModel::FeatureType CONTINENTAL_CRUST =
				GPlatesModel::FeatureType::create_gpml("ContinentalCrust");
		if (!polygon || d_feature_focus.focused_feature()->feature_type() != CONTINENTAL_CRUST)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("That is not a ContinentalCrust polygon; continent selection is still armed."));
		}
		if (!(*rfg)->property().is_still_valid())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected continent has no editable geometry property; selection is still armed."));
		}
		d_captured_continent = CapturedContinent(
				d_feature_focus.focused_feature(),
				(*rfg)->property(),
				(*rfg)->get_non_null_pointer_to_const(),
				polygon->get_non_null_pointer(),
				reconstruction_time);
		d_captured_rift = boost::none;
		d_selection_mode = NOT_SELECTING;
		return Result(CONTINENT_CAPTURED,
				QObject::tr("Continent captured. Arm Select Rift, then click the cutter polyline."));
	}

	if (!polyline)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("That is not a polyline; rift selection is still armed."));
	}
	d_captured_rift = CapturedRift(
			d_feature_focus.focused_feature(),
			(*rfg)->get_non_null_pointer_to_const(),
			polyline->get_non_null_pointer(),
			reconstruction_time);
	d_selection_mode = NOT_SELECTING;
	return Result(RIFT_CAPTURED,
			QObject::tr("Rift polyline captured. Press Cut / Make Rift when both selections are ready."));
}


bool
GPlatesViewOperations::MakeRiftOperation::can_cut() const
{
	return d_captured_continent && d_captured_rift &&
			d_captured_continent->feature.is_valid() &&
			d_captured_continent->geometry_property.is_still_valid() &&
			d_captured_rift->feature.is_valid();
}


QString
GPlatesViewOperations::MakeRiftOperation::continent_status() const
{
	if (!d_captured_continent || !d_captured_continent->feature.is_valid())
	{
		return QObject::tr("Continent: not selected");
	}
	const boost::optional<GPlatesModel::integer_plate_id_type> plate_id =
			d_captured_continent->reconstructed_feature_geometry->reconstruction_plate_id();
	return plate_id
			? QObject::tr("Continent: Plate %1 at %2 Ma")
					.arg(*plate_id).arg(d_captured_continent->reconstruction_time, 0, 'f', 1)
			: QObject::tr("Continent: selected at %1 Ma")
					.arg(d_captured_continent->reconstruction_time, 0, 'f', 1);
}


QString
GPlatesViewOperations::MakeRiftOperation::rift_status() const
{
	if (!d_captured_rift || !d_captured_rift->feature.is_valid())
	{
		return QObject::tr("Rift: not selected");
	}
	return QObject::tr("Rift: %1 vertices at %2 Ma")
			.arg(d_captured_rift->polyline->number_of_vertices())
			.arg(d_captured_rift->reconstruction_time, 0, 'f', 1);
}


GPlatesViewOperations::MakeRiftOperation::Result
GPlatesViewOperations::MakeRiftOperation::cut(
		QWidget *parent_widget)
{
	if (!d_captured_continent || !d_captured_rift)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select both a continent and a rift polyline before cutting."));
	}
	if (!d_captured_continent->feature.is_valid() ||
			!d_captured_continent->geometry_property.is_still_valid())
	{
		d_captured_continent = boost::none;
		return Result(OPERATION_ERROR,
				QObject::tr("The captured continent is no longer editable; select it again."));
	}
	if (!d_captured_rift->feature.is_valid())
	{
		d_captured_rift = boost::none;
		return Result(OPERATION_ERROR,
				QObject::tr("The captured rift no longer exists; select it again."));
	}
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (std::fabs(reconstruction_time - d_captured_continent->reconstruction_time) > 1e-9 ||
			std::fabs(reconstruction_time - d_captured_rift->reconstruction_time) > 1e-9)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The reconstruction time changed after a selection. Re-select both features at the current time."));
	}

	try
	{
		const GPlatesModel::integer_plate_id_type source_plate_id =
				d_captured_continent->reconstructed_feature_geometry->reconstruction_plate_id().get_value_or(0);
		QString error_message;
		const craton_seq_type cratons = gather_relevant_cratons(
				d_application_state, *d_captured_continent->polygon);
		if (cutter_intersects_cratons(*d_captured_rift->polyline, cratons))
		{
			return Result(OPERATION_ERROR,
					QObject::tr("The selected rift enters or touches a craton. Choose or draw a route around all cratons."));
		}

		const boost::optional<SplitPlateGeometry::Result> split = SplitPlateGeometry::split_polygon(
				error_message,
				*d_captured_continent->polygon,
				*d_captured_rift->polyline,
				*d_captured_continent->reconstructed_feature_geometry);
		if (!split)
		{
			return Result(OPERATION_ERROR, error_message);
		}

		GPlatesMaths::PolylineOnSphere::vertex_const_iterator rift_vertex = split->rift_polyline->vertex_begin();
		const GPlatesMaths::PointOnSphere rift_start = *rift_vertex;
		const GPlatesMaths::PointOnSphere rift_end = *(++rift_vertex);
		const bool polygon1_is_left = polygon_side(
				*split->reconstructed_polygon1, rift_start, rift_end) >=
				polygon_side(*split->reconstructed_polygon2, rift_start, rift_end);
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type left_polygon =
				polygon1_is_left ? split->polygon1 : split->polygon2;
		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type right_polygon =
				polygon1_is_left ? split->polygon2 : split->polygon1;
		const plate_id_seq_type left_craton_plate_ids = craton_plate_ids_in_polygon(
				*left_polygon, cratons);
		const plate_id_seq_type right_craton_plate_ids = craton_plate_ids_in_polygon(
				*right_polygon, cratons);

		MakeRiftDialog dialog(
				source_plate_id,
				suggest_right_plate_id(d_application_state, source_plate_id),
				left_craton_plate_ids,
				right_craton_plate_ids,
				parent_widget);
		if (dialog.exec() != QDialog::Accepted)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Make Rift cancelled; both selections remain captured."));
		}
		if (dialog.left_name().isEmpty() || dialog.right_name().isEmpty())
		{
			return Result(OPERATION_ERROR, QObject::tr("Both child continents need names."));
		}
		if (!dialog.plate_ids_are_valid())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Both child plate IDs must be whole numbers from 0 to 99999999."));
		}
		if (dialog.left_plate() == dialog.right_plate())
		{
			return Result(OPERATION_ERROR, QObject::tr("The left and right child continents need distinct plate IDs."));
		}

		const GPlatesModel::FeatureHandle::non_null_ptr_type left_feature = clone_continent_child(
				d_captured_continent->feature, d_captured_continent->geometry_property,
				left_polygon, dialog.left_name(), dialog.left_plate());
		const GPlatesModel::FeatureHandle::non_null_ptr_type right_feature = clone_continent_child(
				d_captured_continent->feature, d_captured_continent->geometry_property,
				right_polygon, dialog.right_name(), dialog.right_plate());
		const GPlatesModel::FeatureHandle::non_null_ptr_type mor_feature = MORFeatureBuilder::create_half_stage_mor(
				QObject::tr("%1 - %2 Rift").arg(dialog.left_name(), dialog.right_name()),
				reconstruction_time, dialog.left_plate(), dialog.right_plate(), split->rift_polyline);

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection().create_child_rendered_layer_and_transfer_ownership(
						RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		preview_layer->add_rendered_geometry(
				RenderedGeometryFactory::create_rendered_polyline_on_sphere(
						split->rift_polyline, GPlatesGui::Colour::get_yellow(), 5.0f));
		const QMessageBox::StandardButton confirmation = QMessageBox::question(
				parent_widget,
				QObject::tr("Confirm Rift"),
				QObject::tr(
						"The yellow preview is the clipped portion of the selected %1-vertex rift.\n\n"
						"Split the continent into '%2' (Plate %3) and '%4' (Plate %5), and create the half-stage MOR?")
						.arg(d_captured_rift->polyline->number_of_vertices())
						.arg(dialog.left_name()).arg(dialog.left_plate())
						.arg(dialog.right_name()).arg(dialog.right_plate()),
				QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes);
		preview_layer->clear_rendered_geometries();
		if (confirmation != QMessageBox::Yes)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("Rift rejected; no data changed and both selections remain captured."));
		}

		GPlatesAppLogic::FeatureCollectionFileState::file_reference mor_file =
				create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("Active Mid-Ocean Ridges"));
		const GPlatesModel::FeatureCollectionHandle::weak_ref mor_collection =
				mor_file.get_file().get_feature_collection();
		std::unique_ptr<QUndoCommand> command(new MakeRiftUndoCommand(
				d_feature_focus, d_model_interface, d_captured_continent->feature,
				left_feature, right_feature, mor_collection, mor_feature));
		reset();
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		name_mor_layer(d_application_state, d_view_state, mor_collection);
		return Result(RIFT_COMPLETED,
				QObject::tr(
						"Created '%1' (Plate %2), '%3' (Plate %4), and one HalfStageRotationVersion3 MOR at %5 Ma. "
						"The MOR is structurally ready for left/right rotations; no .rot poles were invented.")
						.arg(dialog.left_name()).arg(dialog.left_plate())
						.arg(dialog.right_name()).arg(dialog.right_plate())
						.arg(reconstruction_time, 0, 'f', 1));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR, QObject::tr("Could not make the rift: %1").arg(exception.what()));
	}
}


void
GPlatesViewOperations::MakeRiftOperation::reset()
{
	d_selection_mode = NOT_SELECTING;
	d_captured_continent = boost::none;
	d_captured_rift = boost::none;
}
