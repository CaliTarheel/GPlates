/* $Id$ */

/**
 * \file
 * Implements the RRR mode of the shared Worldbuilding Pasta MOR workflow.
 */

#include "CreateTripleJunctionCrustOperation.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QStringList>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "CreateOceanCrustOperation.h"
#include "OceanCrustBandBuilder.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "SubductionCutterGeometry.h"
#include "TripleJunctionGeometry.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/PlanetaryParameters.h"
#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/FeatureCollectionFileState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/ProjectTimestampSchedule.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionLayerProxy.h"
#include "app-logic/ReconstructionTree.h"
#include "app-logic/ReconstructionTreeCreator.h"
#include "app-logic/ReconstructMethodRegistry.h"
#include "app-logic/ReconstructParams.h"
#include "app-logic/ReconstructUtils.h"

#include "feature-visitors/GeometrySetter.h"
#include "feature-visitors/PropertyValueFinder.h"

#include "gui/AnimationController.h"
#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/GeometryOnSphere.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureCollectionHandle.h"
#include "model/FeatureHandle.h"
#include "model/NotificationGuard.h"
#include "model/PropertyName.h"
#include "model/TopLevelProperty.h"

#include "presentation/ViewState.h"

#include "property-values/GpmlPlateId.h"

#include "qt-widgets/ChooseFeatureCollectionWidget.h"


namespace
{
	const double TIME_EPSILON = 1e-9;

	typedef GPlatesViewOperations::OceanCrustBandBuilder::polygon_seq_type polygon_seq_type;
	typedef GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry_ptr_type;

	struct ActiveGeometry
	{
		ActiveGeometry(
				const geometry_ptr_type &geometry_,
				const GPlatesModel::FeatureHandle::iterator &property_) :
			geometry(geometry_), property(property_) { }

		geometry_ptr_type geometry;
		GPlatesModel::FeatureHandle::iterator property;
	};

	struct Ridge
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::integer_plate_id_type left_plate;
		GPlatesModel::integer_plate_id_type right_plate;
		GPlatesModel::FeatureHandle::iterator geometry_property;
		geometry_ptr_type source_geometry;
	};

	struct RidgeGeometryChange
	{
		RidgeGeometryChange(
				const GPlatesModel::FeatureHandle::weak_ref &feature_,
				const GPlatesModel::FeatureHandle::iterator &property_,
				const GPlatesModel::TopLevelProperty::non_null_ptr_type &before_,
				const GPlatesModel::TopLevelProperty::non_null_ptr_type &after_) :
			feature(feature_), property(property_), before(before_), after(after_) { }

		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesModel::TopLevelProperty::non_null_ptr_type before;
		GPlatesModel::TopLevelProperty::non_null_ptr_type after;
	};

	boost::optional<ActiveGeometry>
	get_single_active_geometry(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			double reconstruction_time,
			QString &reason)
	{
		boost::optional<ActiveGeometry> active_geometry;
		for (GPlatesModel::FeatureHandle::iterator property_iter = feature->begin();
			property_iter != feature->end(); ++property_iter)
		{
			const boost::optional<geometry_ptr_type> candidate =
					GPlatesAppLogic::GeometryUtils::get_geometry_from_property(
							property_iter, reconstruction_time);
			if (!candidate)
			{
				continue;
			}
			if (active_geometry)
			{
				reason = QObject::tr("A selected MOR has more than one active geometry property.");
				return boost::none;
			}
			active_geometry = ActiveGeometry(*candidate, property_iter);
		}
		if (!active_geometry)
		{
			reason = QObject::tr("A selected MOR has no active geometry at %1 Ma.")
					.arg(reconstruction_time, 0, 'f', 2);
		}
		return active_geometry;
	}

	std::pair<GPlatesModel::integer_plate_id_type, GPlatesModel::integer_plate_id_type>
	ordered_pair(
			GPlatesModel::integer_plate_id_type first,
			GPlatesModel::integer_plate_id_type second)
	{
		return first < second ? std::make_pair(first, second) : std::make_pair(second, first);
	}

	polygon_seq_type
	union_plate_components(
			const polygon_seq_type &pieces)
	{
		if (pieces.size() < 2)
		{
			return pieces;
		}
		polygon_seq_type operands(pieces.begin() + 1, pieces.end());
		const GPlatesViewOperations::SubductionCutterGeometry::BooleanResult merged =
				GPlatesViewOperations::SubductionCutterGeometry::apply_polygon_boolean(
						*pieces.front(), operands,
						GPlatesViewOperations::SubductionCutterGeometry::POLYGON_UNION);
		if (!merged.success)
		{
			throw std::runtime_error(QObject::tr("Could not merge one plate's RRR crust pieces: %1")
					.arg(merged.error).toStdString());
		}
		return merged.polygons;
	}

	class TripleJunctionCrustUndoCommand : public QUndoCommand
	{
	public:
		TripleJunctionCrustUndoCommand(
				GPlatesGui::FeatureFocus &feature_focus,
				GPlatesModel::ModelInterface model_interface,
				const std::vector<RidgeGeometryChange> &ridge_changes,
				const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
				const std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> &crust_features) :
			d_feature_focus(feature_focus),
			d_model_interface(model_interface),
			d_ridge_changes(ridge_changes),
			d_collection(collection),
			d_crust_features(crust_features)
		{
			setText(QObject::tr("extend RRR junction and create ocean crust"));
		}

		virtual void redo()
		{
			if (!d_collection.is_valid())
			{
				return;
			}
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<RidgeGeometryChange>::iterator change_iter = d_ridge_changes.begin();
				change_iter != d_ridge_changes.end(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->after->clone());
				}
			}
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::iterator feature_iter =
				d_crust_features.begin(); feature_iter != d_crust_features.end(); ++feature_iter)
			{
				if (!(*feature_iter)->parent_ptr())
				{
					const GPlatesModel::FeatureCollectionHandle::iterator inserted =
							d_collection->add(*feature_iter);
					*feature_iter = *inserted;
				}
			}
			notification_guard.release_guard();
		}

		virtual void undo()
		{
			d_feature_focus.unset_focus();
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type>::reverse_iterator feature_iter =
				d_crust_features.rbegin(); feature_iter != d_crust_features.rend(); ++feature_iter)
			{
				if ((*feature_iter)->parent_ptr())
				{
					(*feature_iter)->remove_from_parent();
				}
			}
			for (std::vector<RidgeGeometryChange>::reverse_iterator change_iter =
				d_ridge_changes.rbegin(); change_iter != d_ridge_changes.rend(); ++change_iter)
			{
				if (change_iter->feature.is_valid() && change_iter->property.is_still_valid())
				{
					change_iter->feature->set(change_iter->property, change_iter->before->clone());
				}
			}
			notification_guard.release_guard();
		}

	private:
		GPlatesGui::FeatureFocus &d_feature_focus;
		GPlatesModel::ModelInterface d_model_interface;
		std::vector<RidgeGeometryChange> d_ridge_changes;
		GPlatesModel::FeatureCollectionHandle::weak_ref d_collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> d_crust_features;
	};
}


GPlatesViewOperations::CreateTripleJunctionCrustOperation::CreateTripleJunctionCrustOperation(
		CreateOceanCrustOperation &mor_selection,
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_mor_selection(mor_selection),
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_view_state(view_state),
	d_model_interface(application_state.get_model_interface())
{ }


GPlatesViewOperations::CreateTripleJunctionCrustOperation::Result
GPlatesViewOperations::CreateTripleJunctionCrustOperation::trigger(
		QWidget *parent_widget)
{
	std::vector<GPlatesModel::FeatureHandle::weak_ref> selected_mors;
	const std::vector<GPlatesModel::FeatureHandle::weak_ref> &shared_selection =
			d_mor_selection.selected_mors();
	for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator selection_iter =
		shared_selection.begin(); selection_iter != shared_selection.end(); ++selection_iter)
	{
		if (selection_iter->is_valid())
		{
			selected_mors.push_back(*selection_iter);
		}
	}
	if (selected_mors.size() != 3)
	{
		return Result(SELECTION_REQUIRED,
				QObject::tr("Shift-click exactly three connected HalfStageRotationVersion3 MidOceanRidge features. The shared MOR selection currently contains %1.")
						.arg(static_cast<unsigned int>(selected_mors.size())));
	}

	const double current_time = d_application_state.get_current_reconstruction_time();
	std::vector<Ridge> ridges;
	std::set<GPlatesModel::integer_plate_id_type> plate_ids;
	std::set< std::pair<GPlatesModel::integer_plate_id_type,
			GPlatesModel::integer_plate_id_type> > plate_pairs;
	QStringList pair_descriptions;
	QStringList ridge_descriptions;
	for (std::vector<GPlatesModel::FeatureHandle::weak_ref>::const_iterator feature_iter =
		selected_mors.begin(); feature_iter != selected_mors.end(); ++feature_iter)
	{
		if (!CreateOceanCrustOperation::is_supported_mor(*feature_iter) ||
				!(*feature_iter)->parent_ptr())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Every selected feature must be a loaded HalfStageRotationVersion3 MidOceanRidge."));
		}
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> left =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						*feature_iter, GPlatesModel::PropertyName::create_gpml("leftPlate"));
		const boost::optional<GPlatesPropertyValues::GpmlPlateId::non_null_ptr_to_const_type> right =
				GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GpmlPlateId>(
						*feature_iter, GPlatesModel::PropertyName::create_gpml("rightPlate"));
		if (!left || !right || (*left)->get_value() == (*right)->get_value())
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Every selected MOR needs two different left/right plate IDs."));
		}
		QString geometry_reason;
		const boost::optional<ActiveGeometry> active_geometry =
				get_single_active_geometry(*feature_iter, current_time, geometry_reason);
		if (!active_geometry)
		{
			return Result(OPERATION_ERROR, geometry_reason);
		}
		const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(active_geometry->geometry.get());
		if (!polyline)
		{
			return Result(OPERATION_ERROR,
					QObject::tr("Every selected MOR must have one active polyline geometry."));
		}

		const GPlatesModel::integer_plate_id_type left_id = (*left)->get_value();
		const GPlatesModel::integer_plate_id_type right_id = (*right)->get_value();
		Ridge ridge = { *feature_iter, left_id, right_id,
				active_geometry->property, active_geometry->geometry };
		ridges.push_back(ridge);
		plate_ids.insert(left_id);
		plate_ids.insert(right_id);
		plate_pairs.insert(ordered_pair(left_id, right_id));
		pair_descriptions.append(QObject::tr("%1-%2").arg(left_id).arg(right_id));
		ridge_descriptions.append(QObject::tr("MOR %1: left Plate %2 / right Plate %3")
				.arg((*feature_iter)->feature_id().get().qstring())
				.arg(left_id).arg(right_id));
	}
	if (plate_ids.size() != 3 || plate_pairs.size() != 3)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The three MORs must describe the three unique plate pairs of one RRR junction. Selected pairs: %1.")
						.arg(pair_descriptions.join(", ")));
	}
	QStringList plate_id_descriptions;
	for (std::set<GPlatesModel::integer_plate_id_type>::const_iterator plate_iter = plate_ids.begin();
			plate_iter != plate_ids.end(); ++plate_iter)
	{
		plate_id_descriptions.append(QString::number(*plate_iter));
	}

	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Worldbuilding Pasta - Generate RRR Triple-Junction Crust"));
	dialog.setModal(true);
	QVBoxLayout *layout = new QVBoxLayout(&dialog);
	QLabel *intro = new QLabel(QObject::tr(
			"Resolve the closest endpoints of the three Shift-clicked MORs, extend all three to one junction, and use the existing ocean-crust band builder for each adjoining plate. The MOR extensions and new crust commit as one undoable operation."),
			&dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	QFormLayout *form = new QFormLayout();
	QLabel *selected_graph_label = new QLabel(ridge_descriptions.join("\n"), &dialog);
	selected_graph_label->setWordWrap(true);
	form->addRow(QObject::tr("Selected MOR graph:"), selected_graph_label);
	form->addRow(QObject::tr("Young ridge edge:"),
			new QLabel(QObject::tr("%1 Ma").arg(current_time, 0, 'f', 2), &dialog));
	QDoubleSpinBox *older_time_spin = new QDoubleSpinBox(&dialog);
	older_time_spin->setDecimals(2);
	older_time_spin->setSingleStep(1.0);
	older_time_spin->setRange(current_time + 0.1, 10000.0);
	const boost::optional<double> project_older_bound =
			d_application_state.get_project_timestamp_schedule().default_older_bound(current_time);
	const double fallback_increment = d_view_state.get_animation_controller().time_increment();
	older_time_spin->setValue(project_older_bound ? *project_older_bound : current_time + fallback_increment);
	older_time_spin->setSuffix(QObject::tr(" Ma"));
	older_time_spin->setToolTip(QObject::tr(
			"Defaults to the next older Project Timestamp; otherwise uses the animation increment."));
	form->addRow(QObject::tr("Older ridge edge:"), older_time_spin);
	form->addRow(QObject::tr("Default source:"), new QLabel(project_older_bound
			? QObject::tr("Next older Project Timestamp (%1 Ma)").arg(*project_older_bound, 0, 'f', 2)
			: QObject::tr("Animation increment fallback (%1 My)").arg(fallback_increment, 0, 'f', 2),
			&dialog));
	QDoubleSpinBox *extension_spin = new QDoubleSpinBox(&dialog);
	extension_spin->setDecimals(3);
	extension_spin->setRange(0.0, 10.0);
	extension_spin->setSingleStep(0.1);
	extension_spin->setValue(1.0);
	extension_spin->setSuffix(QObject::tr(" degrees"));
	extension_spin->setToolTip(QObject::tr(
			"Reject the operation rather than connecting endpoints farther apart than this limit."));
	form->addRow(QObject::tr("Maximum endpoint extension:"), extension_spin);
	layout->addLayout(form);

	GPlatesQtWidgets::ChooseFeatureCollectionWidget *collection_widget =
			new GPlatesQtWidgets::ChooseFeatureCollectionWidget(
					d_application_state.get_reconstruct_method_registry(),
					d_application_state.get_feature_collection_file_state(),
					d_application_state.get_feature_collection_file_io(),
					&dialog);
	collection_widget->setTitle(QObject::tr("Destination ocean-crust collection"));
	collection_widget->set_help_text(QObject::tr(
			"The same collection receives all three plates' OceanicCrust components."));
	collection_widget->initialise();
	if (d_last_output_collection.is_valid())
	{
		collection_widget->select_feature_collection(d_last_output_collection);
	}
	layout->addWidget(collection_widget);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Preview RRR Fill"));
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED,
				QObject::tr("RRR triple-junction generation cancelled; no data changed."));
	}

	boost::optional< std::pair<
			GPlatesAppLogic::FeatureCollectionFileState::file_reference, bool> > collection_selection;
	try
	{
		collection_selection = collection_widget->get_file_reference();
	}
	catch (const GPlatesQtWidgets::ChooseFeatureCollectionWidget::NoFeatureCollectionSelectedException &)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("No destination feature collection was selected; no data changed."));
	}
	const GPlatesModel::FeatureCollectionHandle::weak_ref output_collection =
			collection_selection->first.get_file().get_feature_collection();
	if (!output_collection.is_valid())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected destination collection is no longer loaded; no data changed."));
	}

	const double older_time = older_time_spin->value();
	if (older_time <= current_time + TIME_EPSILON)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The older ridge edge must be older than the displayed time."));
	}

	try
	{
		const GPlatesAppLogic::ReconstructionTreeCreator tree_creator =
				d_application_state.get_current_reconstruction()
						.get_default_reconstruction_layer_output()->get_reconstruction_tree_creator();
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type current_tree =
				tree_creator.get_reconstruction_tree(current_time);
		const GPlatesAppLogic::ReconstructionTree::non_null_ptr_to_const_type older_tree =
				tree_creator.get_reconstruction_tree(older_time);

		TripleJunctionGeometry::polyline_seq_type current_ridge_lines;
		for (std::vector<Ridge>::const_iterator ridge_iter = ridges.begin();
			ridge_iter != ridges.end(); ++ridge_iter)
		{
			GPlatesAppLogic::ReconstructionFeatureProperties reconstruction_properties;
			reconstruction_properties.visit_feature(ridge_iter->feature);
			if (!reconstruction_properties.is_feature_defined_at_recon_time(current_time) ||
					!reconstruction_properties.is_feature_defined_at_recon_time(older_time))
			{
				throw std::runtime_error(
						"Every selected MOR must be valid at both interval bounds.");
			}
			const geometry_ptr_type reconstructed =
					GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
							ridge_iter->source_geometry,
							d_application_state.get_reconstruct_method_registry(),
							ridge_iter->feature, current_time, tree_creator,
							GPlatesAppLogic::ReconstructParams(), false);
			const GPlatesMaths::PolylineOnSphere *polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(reconstructed.get());
			if (!polyline)
			{
				throw std::runtime_error(
						"A selected MOR could not be reconstructed as a current-time polyline.");
			}
			current_ridge_lines.push_back(polyline->get_non_null_pointer());
		}

		const TripleJunctionGeometry::Result resolved =
				TripleJunctionGeometry::resolve_rrr_endpoints(
						current_ridge_lines, extension_spin->value());
		if (!resolved.success)
		{
			throw std::runtime_error(resolved.error.toStdString());
		}

		std::vector<RidgeGeometryChange> ridge_changes;
		TripleJunctionGeometry::polyline_seq_type older_ridge_lines;
		for (unsigned int ridge_index = 0; ridge_index < ridges.size(); ++ridge_index)
		{
			const geometry_ptr_type stored_geometry =
					GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
							resolved.resolved_ridges[ridge_index],
							d_application_state.get_reconstruct_method_registry(),
							ridges[ridge_index].feature, current_time, tree_creator,
							GPlatesAppLogic::ReconstructParams(), true);
			const GPlatesMaths::PolylineOnSphere *stored_polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(stored_geometry.get());
			if (!stored_polyline)
			{
				throw std::runtime_error(
						"A resolved MOR could not be reverse-reconstructed for storage.");
			}

			GPlatesModel::TopLevelProperty::non_null_ptr_type before =
					(*ridges[ridge_index].geometry_property)->clone();
			GPlatesModel::TopLevelProperty::non_null_ptr_type after = before->clone();
			GPlatesFeatureVisitors::GeometrySetter geometry_setter(stored_geometry);
			geometry_setter.set_geometry(after.get());
			ridge_changes.push_back(RidgeGeometryChange(
					ridges[ridge_index].feature, ridges[ridge_index].geometry_property,
					before, after));

			const geometry_ptr_type older_geometry =
					GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
							stored_polyline->get_non_null_pointer(),
							d_application_state.get_reconstruct_method_registry(),
							ridges[ridge_index].feature, older_time, tree_creator,
							GPlatesAppLogic::ReconstructParams(), false);
			const GPlatesMaths::PolylineOnSphere *older_polyline =
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(older_geometry.get());
			if (!older_polyline)
			{
				throw std::runtime_error(
						"A resolved MOR could not be reconstructed at the older interval bound.");
			}
			older_ridge_lines.push_back(older_polyline->get_non_null_pointer());
		}

		const polygon_seq_type existing_ocean_crust =
				OceanCrustBandBuilder::get_existing_ocean_crust(d_application_state);
		std::map<GPlatesModel::integer_plate_id_type, polygon_seq_type> plate_bands;
		bool overlap_removed = false;
		for (unsigned int ridge_index = 0; ridge_index < ridges.size(); ++ridge_index)
		{
			const GPlatesModel::integer_plate_id_type sides[2] =
					{ ridges[ridge_index].left_plate, ridges[ridge_index].right_plate };
			for (unsigned int side_index = 0; side_index < 2; ++side_index)
			{
				const OceanCrustBandBuilder::Result band =
						OceanCrustBandBuilder::build_side_band(
								*resolved.resolved_ridges[ridge_index],
								*older_ridge_lines[ridge_index],
								sides[side_index], *older_tree, *current_tree,
								existing_ocean_crust);
				if (!band.success)
				{
					throw std::runtime_error(QObject::tr("Plate %1 band failed: %2")
							.arg(sides[side_index]).arg(band.error).toStdString());
				}
				overlap_removed = overlap_removed || band.existing_overlap_removed;
				plate_bands[sides[side_index]].insert(
						plate_bands[sides[side_index]].end(),
						band.polygons.begin(), band.polygons.end());
			}
		}
		unsigned int polygon_count = 0;
		for (std::map<GPlatesModel::integer_plate_id_type, polygon_seq_type>::iterator plate_iter =
			plate_bands.begin(); plate_iter != plate_bands.end(); ++plate_iter)
		{
			plate_iter->second = union_plate_components(plate_iter->second);
			polygon_count += static_cast<unsigned int>(plate_iter->second.size());
		}
		if (polygon_count == 0)
		{
			throw std::runtime_error(
					"Existing OceanicCrust already fills the selected RRR interval.");
		}

		RenderedGeometryCollection::child_layer_owner_ptr_type preview_layer =
				d_view_state.get_rendered_geometry_collection()
						.create_child_rendered_layer_and_transfer_ownership(
								RenderedGeometryCollection::RECONSTRUCTION_LAYER);
		preview_layer->set_active(true);
		for (TripleJunctionGeometry::polyline_seq_type::const_iterator ridge_iter =
			resolved.resolved_ridges.begin(); ridge_iter != resolved.resolved_ridges.end(); ++ridge_iter)
		{
			preview_layer->add_rendered_geometry(
					RenderedGeometryFactory::create_rendered_polyline_on_sphere(
							*ridge_iter, GPlatesGui::Colour::get_yellow(), 4.0f));
		}
		const GPlatesGui::Colour colours[3] =
		{
			GPlatesGui::Colour::get_aqua(),
			GPlatesGui::Colour(1.0f, 0.55f, 0.0f),
			GPlatesGui::Colour(0.75f, 0.35f, 1.0f)
		};
		const QString colour_names[3] =
		{
			QObject::tr("aqua"), QObject::tr("orange"), QObject::tr("purple")
		};
		const double radius_km = d_application_state.get_planetary_parameters().
				effective_radius_kilometres();
		QStringList plate_legend;
		unsigned int colour_index = 0;
		for (std::map<GPlatesModel::integer_plate_id_type, polygon_seq_type>::const_iterator plate_iter =
			plate_bands.begin(); plate_iter != plate_bands.end(); ++plate_iter, ++colour_index)
		{
			double plate_area_steradians = 0.0;
			for (polygon_seq_type::const_iterator piece_iter = plate_iter->second.begin();
				piece_iter != plate_iter->second.end(); ++piece_iter)
			{
				plate_area_steradians += (*piece_iter)->get_area().dval();
				preview_layer->add_rendered_geometry(
						RenderedGeometryFactory::create_rendered_polygon_on_sphere(
								*piece_iter, colours[colour_index % 3], 3.0f, true,
								GPlatesGui::Colour(0.12f, 0.18f, 0.24f)));
			}
			plate_legend.append(QObject::tr("%1: Plate %2 — %3 component(s), %4 million km²")
					.arg(colour_names[colour_index % 3])
					.arg(plate_iter->first)
					.arg(static_cast<unsigned int>(plate_iter->second.size()))
					.arg(plate_area_steradians * radius_km * radius_km / 1.0e6, 0, 'f', 3));
		}

		QMessageBox confirmation(
				QMessageBox::Question,
				QObject::tr("Confirm RRR Triple-Junction Fill"),
				QObject::tr(
						"Yellow shows the three proposed MOR extensions to one junction. The preview contains %1 OceanicCrust polygon(s) for Plates %2 over %3-%4 Ma. The farthest endpoint moved %5 degrees; branch angles are %6-%7 degrees. %8 existing OceanicCrust polygon(s) were checked%9.\n\n%10\n\nCommit the three MOR edits and all crust polygons as one undoable operation?")
						.arg(polygon_count)
						.arg(plate_id_descriptions.join(", "))
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2)
						.arg(resolved.maximum_extension_degrees, 0, 'f', 3)
						.arg(resolved.minimum_branch_angle_degrees, 0, 'f', 2)
						.arg(resolved.maximum_branch_angle_degrees, 0, 'f', 2)
						.arg(static_cast<unsigned int>(existing_ocean_crust.size()))
						.arg(overlap_removed
								? QObject::tr(" and overlapping area was removed")
								: QObject::tr("; no overlap was found"))
						.arg(plate_legend.join("\n")),
				QMessageBox::Ok | QMessageBox::Cancel,
				parent_widget);
		confirmation.button(QMessageBox::Ok)->setText(QObject::tr("Commit RRR Fill"));
		confirmation.setDefaultButton(QMessageBox::Cancel);
		confirmation.setDetailedText(ridge_descriptions.join("\n"));
		const int confirmation_result = confirmation.exec();
		preview_layer->clear_rendered_geometries();
		if (confirmation_result != QMessageBox::Ok)
		{
			return Result(OPERATION_CANCELLED,
					QObject::tr("RRR triple-junction preview rejected; no data changed."));
		}

		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> crust_features;
		for (std::map<GPlatesModel::integer_plate_id_type, polygon_seq_type>::const_iterator plate_iter =
			plate_bands.begin(); plate_iter != plate_bands.end(); ++plate_iter)
		{
			for (unsigned int piece_index = 0; piece_index < plate_iter->second.size(); ++piece_index)
			{
				const OceanCrustBandBuilder::polygon_ptr_type stored_polygon =
						OceanCrustBandBuilder::reverse_reconstruct_polygon(
								*plate_iter->second[piece_index], plate_iter->first, *current_tree);
				QString name = QObject::tr("RRR oceanic crust %1-%2 Ma - plate %3")
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2)
						.arg(plate_iter->first);
				if (plate_iter->second.size() > 1)
				{
					name += QObject::tr(" (part %1)").arg(piece_index + 1);
				}
				crust_features.push_back(OceanCrustBandBuilder::create_oceanic_crust_feature(
						name, current_time, current_time, plate_iter->first, stored_polygon));
			}
		}

		std::unique_ptr<QUndoCommand> command(new TripleJunctionCrustUndoCommand(
				d_feature_focus, d_model_interface, ridge_changes,
				output_collection, crust_features));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		d_last_output_collection = output_collection;
		return Result(OPERATION_COMPLETED,
				QObject::tr("Extended three half-stage MORs to one RRR junction and created %1 non-overlapping OceanicCrust polygon(s) for the %2-%3 Ma interval as one undoable edit.")
						.arg(static_cast<unsigned int>(crust_features.size()))
						.arg(older_time, 0, 'f', 2).arg(current_time, 0, 'f', 2));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Could not generate the RRR triple-junction crust: %1")
						.arg(exception.what()));
	}
}
