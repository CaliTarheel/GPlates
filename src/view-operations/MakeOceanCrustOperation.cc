/* $Id$ */

/**
 * \file
 * $Revision$
 * $Date$
 *
 * Copyright (C) 2026 The University of Sydney, Australia
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

#include "MakeOceanCrustOperation.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "app-logic/ApplicationState.h"
#include "app-logic/FlowlineGeometryPopulator.h"
#include "app-logic/FlowlineUtils.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/ReconstructedFlowline.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/FileInfo.h"

#include "gui/FeatureFocus.h"

#include "maths/MultiPointOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/ModelUtils.h"
#include "model/NotificationGuard.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlArray.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/OceanCrustSliverBuilder.h"
#include "view-operations/UndoRedo.h"
#include "view-operations/WorldbuildingFeatureCollectionUtils.h"


namespace
{
	const std::size_t MAX_INTERVALS = 10000;
	const std::size_t MAX_FLOWLINE_VERTICES = 2000000;
	const std::size_t MAX_CRUST_POLYGONS = 100000;

	bool
	has_resolvable_spreading(
			const GPlatesMaths::PolylineOnSphere &flowline)
	{
		// acos(dot) can report ~1e-8 radians for coincident points because of roundoff.
		// Squared chord lengths retain precision near zero; 1e-12 is about six micrometres
		// on Earth. Check all samples, since a flowline can return to its starting point.
		const auto &start = flowline.get_vertex(0).position_vector();
		for (auto vertex = flowline.vertex_begin(); vertex != flowline.vertex_end(); ++vertex)
		{
			const auto &position = vertex->position_vector();
			const double dx = position.x().dval() - start.x().dval();
			const double dy = position.y().dval() - start.y().dval();
			const double dz = position.z().dval() - start.z().dval();
			if (dx * dx + dy * dy + dz * dz > 1e-24)
			{
				return true;
			}
		}
		return false;
	}


	void
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, name, value))
		{
			throw std::runtime_error("Unable to add a required flowline property.");
		}
	}


	struct Ridge
	{
		Ridge(
				const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type &geometry_,
				const GPlatesAppLogic::Layer &layer_) :
			geometry(geometry_),
			layer(layer_),
			revision(geometry_->get_feature_ref()->revision_id())
		{  }

		GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type geometry;
		GPlatesAppLogic::Layer layer;
		GPlatesModel::RevisionId revision;
	};


	struct Output
	{
		GPlatesModel::FeatureCollectionHandle::weak_ref collection;
		std::vector<GPlatesModel::FeatureHandle::non_null_ptr_type> features;
		GPlatesAppLogic::Layer rotation_layer;
		bool pin_new_collection_model = false;
	};


	GPlatesAppLogic::Layer
	rotation_model_for_layer(
			const GPlatesAppLogic::Layer &layer,
			const GPlatesAppLogic::ReconstructGraph &graph)
	{
		for (const auto &input : layer.get_channel_inputs(
				GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE))
		{
			if (input.get_input_layer())
			{
				return *input.get_input_layer();
			}
		}
		return graph.get_default_reconstruction_tree_layer();
	}


	QString
	destination_file_error(
			const GPlatesAppLogic::FeatureCollectionFileState::file_reference &file,
			const GPlatesFileIO::FeatureCollectionFileFormat::Registry &registry)
	{
		const auto &info = file.get_file().get_file_info().get_qfileinfo();
		if (info.filePath().isEmpty())
		{
			return QString();
		}
		const auto format = registry.get_file_format(info);
		if (!format || (*format != GPlatesFileIO::FeatureCollectionFileFormat::GPML &&
				*format != GPlatesFileIO::FeatureCollectionFileFormat::GPMLZ) ||
				!registry.does_file_format_support_writing(*format))
		{
			return QObject::tr("Use an unsaved collection or a GPML/GPMLZ file to retain all properties.");
		}
		// This is an advisory permission check only. FileInfo::is_writable opens the file,
		// so it must not be used while presenting choices. Saving remains an explicit action.
		if (!info.isFile() || !info.isWritable())
		{
			return QObject::tr("The destination file is missing or read-only; choose another collection.");
		}
		return QString();
	}


	QString
	destination_error(
			const GPlatesModel::FeatureCollectionHandle::weak_ref &collection,
			const GPlatesAppLogic::Layer &source_model,
			GPlatesAppLogic::ApplicationState &application_state)
	{
		// A file_reference can outlive an unload. Re-resolve its weak collection before
		// accessing file information, rather than keeping a reference from the dialog list.
		bool found_file = false;
		for (const auto &file : application_state.get_feature_collection_file_state().get_loaded_files())
		{
			if (collection.is_valid() && file.get_file().get_feature_collection() == collection)
			{
				const QString error = destination_file_error(
						file, application_state.get_feature_collection_file_format_registry());
				if (!error.isEmpty())
				{
					return error;
				}
				found_file = true;
				break;
			}
		}
		if (!found_file)
		{
			return QObject::tr("The selected output collection is no longer loaded.");
		}

		const auto &graph = application_state.get_reconstruct_graph();
		bool found_layer = false;
		for (auto layer_iter = graph.begin(); layer_iter != graph.end(); ++layer_iter)
		{
			const GPlatesAppLogic::Layer layer = *layer_iter;
			if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}
			for (const auto &input : layer.get_channel_inputs(
					GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTABLE_FEATURES))
			{
				const auto file = input.get_input_file();
				if (file && file->get_feature_collection() == collection)
				{
					found_layer = true;
					if (rotation_model_for_layer(layer, graph) != source_model)
					{
						return QObject::tr("The output collection uses a different rotation model. "
								"Choose a new collection or one using the selected ridges' model.");
					}
				}
			}
		}
		if (!found_layer && graph.get_default_reconstruction_tree_layer() != source_model)
		{
			return QObject::tr("This collection would use the default rotation model, which differs "
					"from the selected ridges' model. Choose a new collection.");
		}
		return QString();
	}


	struct FeatureSnapshot
	{
		explicit
		FeatureSnapshot(
				const GPlatesModel::FeatureHandle::non_null_ptr_type &feature_) :
			feature(feature_)
		{
			for (auto property = feature->begin(); property != feature->end(); ++property)
			{
				properties.push_back(std::make_pair(property, (*property)->clone()));
			}
		}

		void
		restore()
		{
			for (auto property = feature->begin(); property != feature->end(); ++property)
			{
				const auto saved = std::find_if(properties.begin(), properties.end(),
						[&](const auto &entry) { return entry.first == property; });
				if (saved == properties.end())
				{
					feature->remove(property);
				}
			}
			for (auto &property : properties)
			{
				if (property.first.is_still_valid())
				{
					feature->set(property.first, property.second->clone());
				}
				else
				{
					property.first = feature->add(property.second->clone());
				}
			}
		}

		GPlatesModel::FeatureHandle::non_null_ptr_type feature;
		std::vector<std::pair<GPlatesModel::FeatureHandle::iterator,
				GPlatesModel::TopLevelProperty::non_null_ptr_type>> properties;
	};


	/** Keep each output on its source model, including on redo after its collection was emptied. */
	void
	connect_rotation_model(
			GPlatesAppLogic::ReconstructGraph &graph,
			const Output &output)
	{
		if (!output.pin_new_collection_model || !output.rotation_layer.is_valid())
		{
			return;
		}
		for (GPlatesAppLogic::ReconstructGraph::iterator it = graph.begin(); it != graph.end(); ++it)
		{
			GPlatesAppLogic::Layer layer = *it;
			if (layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}
			const std::vector<GPlatesAppLogic::Layer::InputConnection> inputs =
					layer.get_channel_inputs(
							GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTABLE_FEATURES);
			for (const auto &input : inputs)
			{
				const auto file = input.get_input_file();
				if (file && file->get_feature_collection() == output.collection)
				{
					layer.disconnect_channel_inputs(GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE);
					layer.connect_input_to_layer_output(output.rotation_layer,
							GPlatesAppLogic::LayerInputChannelName::RECONSTRUCTION_TREE);
					break;
				}
			}
		}
	}


	class AddRidgeOutputUndoCommand :
			public QUndoCommand
	{
	public:
		AddRidgeOutputUndoCommand(
				GPlatesAppLogic::ApplicationState &application_state,
				const std::vector<Output> &outputs,
				const QString &description) :
			d_application_state(application_state),
			d_outputs(outputs)
		{
			setText(description);
			for (const Output &output : d_outputs)
			{
				for (const auto &feature : output.features)
				{
					d_snapshots.push_back(FeatureSnapshot(feature));
				}
			}
		}

		void
		redo() override
		{
			GPlatesAppLogic::ApplicationState::ScopedReconstructGuard reconstruct_guard(
					d_application_state, true);
			GPlatesModel::NotificationGuard guard(
					*d_application_state.get_model_interface().access_model());
			std::size_t snapshot_index = 0;
			for (const Output &output : d_outputs)
			{
				for (const auto &feature : output.features)
				{
					FeatureSnapshot &snapshot = d_snapshots[snapshot_index++];
					if (output.collection.is_valid() &&
							(!feature->is_active() || !feature->parent_ptr()))
					{
						// Removal leaves the former parent pointer, and add does not reactivate.
						// Attach first so property edits cannot follow a stale parent after unload.
						output.collection->add(feature);
						feature->set_active(true);
						// In-place edits are allowed, so restore fresh clones of the saved properties.
						snapshot.restore();
					}
				}
			}
			// Releasing notifications creates the reconstruct layer for each newly populated file.
			guard.release_guard();
			for (const Output &output : d_outputs)
			{
				if (output.collection.is_valid())
				{
					connect_rotation_model(d_application_state.get_reconstruct_graph(), output);
				}
			}
		}

		void
		undo() override
		{
			GPlatesAppLogic::ApplicationState::ScopedReconstructGuard reconstruct_guard(
					d_application_state, true);
			GPlatesModel::NotificationGuard guard(
					*d_application_state.get_model_interface().access_model());
			for (const Output &output : d_outputs)
			{
				for (const auto &feature : output.features)
				{
					if (output.collection.is_valid() && feature->is_active() &&
							feature->parent_ptr() == output.collection.handle_ptr())
					{
						feature->remove_from_parent();
					}
				}
			}
			guard.release_guard();
		}

	private:
		GPlatesAppLogic::ApplicationState &d_application_state;
		std::vector<Output> d_outputs;
		std::vector<FeatureSnapshot> d_snapshots;
	};
}


GPlatesViewOperations::MakeOceanCrustOperation::MakeOceanCrustOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state)
{  }


std::vector<double>
GPlatesViewOperations::MakeOceanCrustOperation::sample_times(
		double start_time,
		double future_duration,
		double step_interval)
{
	if (!std::isfinite(start_time) || !std::isfinite(future_duration) ||
			!std::isfinite(step_interval) || start_time <= 0 || future_duration <= 0 ||
			step_interval <= 0 || future_duration > start_time)
	{
		throw std::invalid_argument(
				"Use a positive duration and step interval; the end age cannot be younger than 0 Ma.");
	}
	if (std::ceil(future_duration / step_interval) > MAX_INTERVALS)
	{
		throw std::invalid_argument("Use a larger step interval (at most 10,000 intervals per ridge).");
	}
	const double end_time = start_time - future_duration;
	if (end_time == start_time)
	{
		throw std::invalid_argument(
				"The duration is too small to change the current reconstruction time.");
	}
	std::vector<double> times(1, start_time);
	for (std::size_t index = 1; index < MAX_INTERVALS; ++index)
	{
		const double elapsed = index * step_interval;
		if (elapsed >= future_duration)
		{
			break;
		}
		const double time = start_time - elapsed;
		if (time <= end_time)
		{
			break;
		}
		if (time >= times.back())
		{
			throw std::invalid_argument("The step interval is too small at this reconstruction time.");
		}
		times.push_back(time);
	}
	times.push_back(end_time);
	std::reverse(times.begin(), times.end());
	return times;
}


GPlatesModel::FeatureHandle::non_null_ptr_type
GPlatesViewOperations::MakeOceanCrustOperation::create_flowline(
		const GPlatesMaths::PolylineOnSphere &reconstructed_ridge,
		GPlatesModel::integer_plate_id_type left_plate_id,
		GPlatesModel::integer_plate_id_type right_plate_id,
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		double start_time,
		double future_duration,
		double step_interval,
		const QString &name)
{
	const std::vector<double> times = sample_times(start_time, future_duration, step_interval);
	if (left_plate_id == right_plate_id)
	{
		throw std::invalid_argument("A ridge needs distinct left and right plate IDs.");
	}
	if (reconstructed_ridge.number_of_vertices() > MAX_FLOWLINE_VERTICES / (2 * times.size()))
	{
		throw std::invalid_argument("Too many flowline vertices; use a larger step interval.");
	}
	for (double time : times)
	{
		const auto tree = tree_creator.get_reconstruction_tree(time);
		for (auto plate_id : {left_plate_id, right_plate_id})
		{
			if (!tree->get_composed_absolute_rotation_or_none(plate_id))
			{
				throw std::invalid_argument(QString("No rotation for plate %1 at %2 Ma.")
						.arg(static_cast<qulonglong>(plate_id)).arg(time, 0, 'g', 15).toStdString());
			}
		}
	}

	const auto feature = GPlatesModel::FeatureHandle::create(
			GPlatesModel::FeatureType::create_gpml("Flowline"));
	const auto ref = feature->reference();
	add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
			GPlatesPropertyValues::XsString::create(
					GPlatesUtils::make_icu_string_from_qstring(name)));
	add_property(ref, GPlatesModel::PropertyName::create_gml("description"),
			GPlatesPropertyValues::XsString::create(GPlatesUtils::make_icu_string_from_qstring(
					QObject::tr("Normal symmetric spreading (50/50), seeded at every ridge vertex; "
							"%1 to %2 Ma, interval %3 Myr.")
							.arg(start_time, 0, 'g', 15).arg(times.front(), 0, 'g', 15)
							.arg(step_interval, 0, 'g', 15))));
	add_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
			GPlatesModel::ModelUtils::create_gml_time_period(
					GPlatesPropertyValues::GeoTimeInstant(start_time),
					GPlatesPropertyValues::GeoTimeInstant(0)));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(left_plate_id));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("rightPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(right_plate_id));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
			GPlatesPropertyValues::Enumeration::create(
					GPlatesPropertyValues::EnumerationType::create_gpml("ReconstructionMethodEnumeration"),
					"HalfStageRotationVersion3"));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
			GPlatesModel::ModelUtils::create_gml_time_instant(
					GPlatesPropertyValues::GeoTimeInstant(start_time)));
	std::vector<GPlatesModel::PropertyValue::non_null_ptr_type> periods;
	for (std::size_t index = 1; index < times.size(); ++index)
	{
		periods.push_back(GPlatesModel::ModelUtils::create_gml_time_period(
				GPlatesPropertyValues::GeoTimeInstant(times[index]),
				GPlatesPropertyValues::GeoTimeInstant(times[index - 1])));
	}
	add_property(ref, GPlatesModel::PropertyName::create_gpml("times"),
			GPlatesPropertyValues::GpmlArray::create(
					periods, GPlatesPropertyValues::StructuralType::create_gml("TimePeriod")));

	// Native seeds live in the present-day left-plate frame, not the ridge's half-stage frame.
	// Set import time and the complete time list before asking the native helper to reverse them.
	const auto seeds = GPlatesMaths::MultiPointOnSphere::create(
			reconstructed_ridge.vertex_begin(), reconstructed_ridge.vertex_end());
	const auto stored_seeds = GPlatesAppLogic::FlowlineUtils::reconstruct_flowline_seed_points(
			seeds, start_time, tree_creator, ref, true);
	add_property(ref, GPlatesModel::PropertyName::create_gpml("seedPoints"),
			GPlatesAppLogic::GeometryUtils::create_geometry_property_value(stored_seeds));

	// The native populator can skip invalid geometry instead of throwing. Verify that every seed
	// actually produces both halves before any generated feature is inserted into the user's model.
	std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
	GPlatesAppLogic::FlowlineGeometryPopulator populator(geometries, tree_creator, times.front());
	populator.visit_feature(ref);
	if (geometries.size() != reconstructed_ridge.number_of_vertices())
	{
		throw std::runtime_error("Some ridge vertices could not produce valid flowlines.");
	}
	for (std::size_t seed = 0; seed < geometries.size(); ++seed)
	{
		const auto *flowline = dynamic_cast<const GPlatesAppLogic::ReconstructedFlowline *>(
				geometries[seed].get());
		if (!flowline || flowline->left_flowline_points()->number_of_vertices() != times.size() ||
				flowline->right_flowline_points()->number_of_vertices() != times.size())
		{
			throw std::runtime_error(
					"The rotation model produced incomplete or degenerate flowlines; check spreading motion.");
		}
		if (!has_resolvable_spreading(*flowline->left_flowline_points()) ||
				!has_resolvable_spreading(*flowline->right_flowline_points()))
		{
			throw std::runtime_error(QString(
					"No resolvable spreading at ridge vertex %1 over the requested range; "
					"check the plate IDs and rotation model.").arg(seed + 1).toStdString());
		}
	}
	return feature;
}


GPlatesViewOperations::MakeOceanCrustOperation::Result
GPlatesViewOperations::MakeOceanCrustOperation::trigger(
		QWidget *parent_widget,
		Mode mode)
{
	const bool create_crust = mode == OCEAN_CRUST;
	const QString operation_name = create_crust
			? QObject::tr("Make Ocean Crust") : QObject::tr("Flow lines");
	const double start_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
	if (!std::isfinite(start_time) || start_time <= 0)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Choose a reconstruction time older than 0 Ma before creating ridge output."));
	}

	std::vector<Ridge> ridges;
	std::set<const GPlatesModel::TopLevelProperty *> seen;
	const auto &visual_layers = d_view_state.get_visual_layers();
	for (std::size_t index = 0; index < visual_layers.size(); ++index)
	{
		const auto visual_layer = visual_layers.visual_layer_at(index).lock();
		if (!visual_layer || !visual_layer->is_visible() ||
				!visual_layer->get_reconstruct_graph_layer().is_active() ||
				visual_layer->get_layer_type() !=
				static_cast<unsigned int>(GPlatesAppLogic::LayerTaskType::RECONSTRUCT))
		{
			continue;
		}
		const auto layer = visual_layer->get_reconstruct_graph_layer();
		const auto output = layer.get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
		if (!output)
		{
			continue;
		}
		std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> geometries;
		(*output)->get_reconstructed_feature_geometries(geometries);
		for (const auto &geometry : geometries)
		{
			if (geometry->is_valid() && geometry->property().is_still_valid() &&
					geometry->get_feature_ref()->feature_type() ==
							GPlatesModel::FeatureType::create_gpml("MidOceanRidge") &&
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
							geometry->reconstructed_geometry().get()) &&
					seen.insert((*geometry->property()).get()).second)
			{
				ridges.push_back(Ridge(geometry, layer));
			}
		}
	}
	if (ridges.empty())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("No visible reconstructed MidOceanRidge polylines are available at this time."));
	}

	QDialog dialog(parent_widget);
	dialog.setObjectName("makeOceanCrustDialog");
	dialog.setWindowTitle(operation_name);
	auto *layout = new QVBoxLayout(&dialog);
	auto *note = new QLabel(create_crust
			? QObject::tr("Select mid-ocean ridges to create ocean-crust polygon slivers on both sides "
					"as the ridges advance. Each ridge needs distinct left/right plate IDs and rotations "
					"covering the requested range.")
			: QObject::tr("Select mid-ocean ridges to seed. Symmetric spreading (50/50) creates two "
					"flowlines at every vertex. Each ridge needs distinct left/right plate IDs and "
					"rotations covering the requested range."), &dialog);
	note->setWordWrap(true);
	layout->addWidget(note);
	auto *list = new QListWidget(&dialog);
	list->setObjectName("ridgeList");
	layout->addWidget(list);
	for (const Ridge &ridge : ridges)
	{
		GPlatesAppLogic::FlowlineUtils::FlowlinePropertyFinder properties;
		properties.visit_feature(ridge.geometry->get_feature_ref());
		const auto left = properties.get_left_plate();
		const auto right = properties.get_right_plate();
		const bool usable = left && right && *left != *right;
		const auto *polyline = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
				ridge.geometry->reconstructed_geometry().get());
		const QString name = properties.get_name().isEmpty() ?
				QObject::tr("Unnamed ridge") : properties.get_name();
		QString collection_name = QObject::tr("Unsaved collection");
		const auto feature = ridge.geometry->get_feature_ref();
		for (const auto &file :
				d_application_state.get_feature_collection_file_state().get_loaded_files())
		{
			if (file.get_file().get_feature_collection().handle_ptr() == feature->parent_ptr())
			{
				collection_name = file.get_file().get_file_info().get_display_name(true);
				break;
			}
		}
		unsigned int property_index = 0;
		for (auto property = feature->begin();
				property != feature->end() && property != ridge.geometry->property();
				++property, ++property_index)
		{  }
		auto *item = new QListWidgetItem(QObject::tr(
				"%1 | left %2 / right %3 | %4 vertices | %5, geometry %6%7")
				.arg(name)
				.arg(left ? QString::number(*left) : QObject::tr("missing"))
				.arg(right ? QString::number(*right) : QObject::tr("missing"))
				.arg(polyline->number_of_vertices())
				.arg(collection_name).arg(property_index + 1)
				.arg(usable ? QString() : QObject::tr(" (set distinct plate IDs first)")), list);
		item->setToolTip(QObject::tr("%1\nFeature ID: %2\nGeometry property: %3")
				.arg(collection_name, feature->feature_id().get().qstring()).arg(property_index + 1));
		item->setFlags(usable ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
		item->setCheckState(usable && ridge.geometry->get_feature_ref() ==
				d_view_state.get_feature_focus().focused_feature() ? Qt::Checked : Qt::Unchecked);
	}
	auto *form = new QFormLayout;
	layout->addLayout(form);
	std::vector<GPlatesModel::FeatureCollectionHandle::weak_ref> destination_collections;
	auto *destination = new QComboBox(&dialog);
	destination->setObjectName("outputCollectionCombo");
	destination->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	destination->setMinimumContentsLength(36);
	destination->addItem(QObject::tr("Create new collection(s) — save afterwards"), -1);
	for (const auto &file :
			d_application_state.get_feature_collection_file_state().get_loaded_files())
	{
		const auto collection = file.get_file().get_feature_collection();
		if (!collection.is_valid())
		{
			continue;
		}
		const int collection_index = static_cast<int>(destination_collections.size());
		destination_collections.push_back(collection);
		QString label = file.get_file().get_file_info().get_display_name(true);
		if (label.isEmpty())
		{
			label = QObject::tr("Unsaved collection %1").arg(collection_index + 1);
		}
		if (file.get_file().get_file_info().get_qfileinfo().filePath().isEmpty())
		{
			label += QObject::tr(" (unsaved)");
		}
		destination->addItem(label, collection_index);
		destination->setItemData(destination->count() - 1, label, Qt::ToolTipRole);
		const QString reason = destination_file_error(
				file, d_application_state.get_feature_collection_file_format_registry());
		if (!reason.isEmpty())
		{
			destination->setItemData(destination->count() - 1, reason, Qt::ToolTipRole);
			auto *model = qobject_cast<QStandardItemModel *>(destination->model());
			if (model)
			{
				model->item(destination->count() - 1)->setEnabled(false);
			}
		}
	}
	form->addRow(QObject::tr("Output collection / file:"), destination);
	auto *save_note = new QLabel(QObject::tr(
			"Adds unsaved features to the selected collection. Save the collection afterwards "
			"to choose a new file or update its GPML/GPMLZ file."), &dialog);
	save_note->setWordWrap(true);
	form->addRow(save_note);
	auto *duration = new QDoubleSpinBox(&dialog);
	duration->setObjectName("futureDurationMyr");
	duration->setDecimals(6);
	duration->setRange(0.000001, std::max(start_time, 10000.0));
	duration->setValue(std::min(10.0, start_time));
	duration->setToolTip(QObject::tr(
			"Elapsed million years toward 0 Ma; cannot exceed the start age."));
	form->addRow(QObject::tr("Future duration (Myr):"), duration);
	auto *interval = new QDoubleSpinBox(&dialog);
	interval->setObjectName("stepIntervalMyr");
	interval->setDecimals(6);
	interval->setRange(0.000001, std::max(start_time, 10000.0));
	interval->setValue(1.0);
	interval->setToolTip(QObject::tr(
			"Time between samples, in million years. The final step may be shorter."));
	form->addRow(QObject::tr("Step interval (Myr):"), interval);
	auto *range = new QLabel(&dialog);
	range->setObjectName("flowlineTimeRange");
	range->setToolTip(QObject::tr(
			"Ma is age before present; smaller ages are farther into the future."));
	form->addRow(QObject::tr("Reconstruction age:"), range);
	auto *error = new QLabel(&dialog);
	error->setObjectName("flowlineValidationError");
	error->setWordWrap(true);
	layout->addWidget(error);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	auto *create_button = buttons->button(QDialogButtonBox::Ok);
	create_button->setObjectName("createFlowlinesButton");
	create_button->setText(create_crust
			? QObject::tr("Create Ocean Crust") : QObject::tr("Create Flowlines"));
	layout->addWidget(buttons);
	const auto update_range = [=]()
	{
		range->setText(QObject::tr("%1 Ma → %2 Ma")
				.arg(start_time, 0, 'g', 15).arg(start_time - duration->value(), 0, 'g', 15));
	};
	QObject::connect(duration, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
			&dialog, [=](double) { update_range(); });
	update_range();
	const auto selected_destination = [&]()
	{
		const int index = destination->currentData().toInt();
		return index >= 0 && index < static_cast<int>(destination_collections.size())
				? destination_collections[index] : GPlatesModel::FeatureCollectionHandle::weak_ref();
	};
	const auto validate_destination = [&]()
	{
		if (destination->currentData().toInt() < 0)
		{
			return;
		}
		std::set<GPlatesAppLogic::Layer> source_models;
		for (int index = 0; index < list->count(); ++index)
		{
			if (list->item(index)->checkState() == Qt::Checked)
			{
				if (!ridges[index].layer.is_valid())
				{
					throw std::runtime_error("A source layer is no longer loaded. Reopen this dialog.");
				}
				source_models.insert(rotation_model_for_layer(
						ridges[index].layer, d_application_state.get_reconstruct_graph()));
			}
		}
		if (source_models.size() != 1)
		{
			throw std::invalid_argument(
					"An existing output collection requires ridges using one rotation model. "
					"Choose new collections to keep different models separate.");
		}
		const QString reason = destination_error(
				selected_destination(), *source_models.begin(), d_application_state);
		if (!reason.isEmpty())
		{
			throw std::invalid_argument(reason.toStdString());
		}
	};
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
	{
		try
		{
			const auto times = sample_times(start_time, duration->value(), interval->value());
			std::size_t seed_count = 0;
			std::size_t segment_count = 0;
			for (int index = 0; index < list->count(); ++index)
			{
				if (list->item(index)->checkState() == Qt::Checked)
				{
					const auto count = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
							ridges[index].geometry->reconstructed_geometry().get())->number_of_vertices();
					seed_count += count;
					segment_count += count - 1;
				}
			}
			if (!seed_count)
			{
				throw std::invalid_argument("Select at least one ridge with distinct left/right plate IDs.");
			}
			if (!create_crust && seed_count > MAX_FLOWLINE_VERTICES / (2 * times.size()))
			{
				throw std::invalid_argument(
						"More than 2 million flowline vertices requested; "
						"select fewer ridges or a larger step interval.");
			}
			if (create_crust && segment_count > MAX_CRUST_POLYGONS / (2 * (times.size() - 1)))
			{
				throw std::invalid_argument(
						"More than 100,000 crust polygons requested; "
						"select fewer ridges or a larger step interval.");
			}
			validate_destination();
			dialog.accept();
		}
		catch (const std::exception &exception)
		{
			error->setText(QString::fromUtf8(exception.what()));
		}
	});
	dialog.resize(700, 510);
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(OPERATION_CANCELLED, QString());
	}

	try
	{
		if (d_application_state.get_current_reconstruction().get_reconstruction_time() != start_time)
		{
			throw std::runtime_error(
					"The reconstruction time changed. Reopen the dialog and try again.");
		}
		validate_destination();
		std::map<GPlatesAppLogic::Layer, Output> grouped;
		std::size_t seed_count = 0;
		std::size_t ridge_count = 0;
		std::size_t created_feature_count = 0;
		for (int index = 0; index < list->count(); ++index)
		{
			if (list->item(index)->checkState() != Qt::Checked)
			{
				continue;
			}
			const Ridge &ridge = ridges[index];
			if (!ridge.layer.is_valid() || !ridge.layer.is_active() ||
					!ridge.geometry->is_valid() || !ridge.geometry->property().is_still_valid() ||
					!ridge.geometry->get_feature_ref().is_valid() ||
					ridge.geometry->get_feature_ref()->revision_id() != ridge.revision)
			{
				throw std::runtime_error("A source ridge changed. Reopen the dialog and try again.");
			}
			GPlatesAppLogic::FlowlineUtils::FlowlinePropertyFinder properties;
			properties.visit_feature(ridge.geometry->get_feature_ref());
			if (!properties.get_left_plate() || !properties.get_right_plate() ||
					*properties.get_left_plate() == *properties.get_right_plate())
			{
				throw std::runtime_error(
						"A ridge needs distinct left and right plate IDs. Reopen the dialog.");
			}
			const auto *polyline = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
					ridge.geometry->reconstructed_geometry().get());
			const QString name = properties.get_name().isEmpty() ?
					QObject::tr("Unnamed ridge") : properties.get_name();
			try
			{
				const auto rotation_model = rotation_model_for_layer(
						ridge.layer, d_application_state.get_reconstruct_graph());
				Output &output = grouped[rotation_model];
				output.rotation_layer = rotation_model;
				if (create_crust)
				{
					const auto features = OceanCrustSliverBuilder::create_slivers(*polyline,
							*properties.get_left_plate(), *properties.get_right_plate(),
							ridge.geometry->get_reconstruction_tree_creator(), start_time,
							duration->value(), interval->value(), name);
					created_feature_count += features.size();
					if (created_feature_count > MAX_CRUST_POLYGONS)
					{
						throw std::invalid_argument("More than 100,000 crust polygons requested.");
					}
					output.features.insert(output.features.end(), features.begin(), features.end());
				}
				else
				{
					output.features.push_back(create_flowline(*polyline,
							*properties.get_left_plate(), *properties.get_right_plate(),
							ridge.geometry->get_reconstruction_tree_creator(), start_time,
							duration->value(), interval->value(), QObject::tr("%1 - Flow lines").arg(name)));
					++created_feature_count;
				}
			}
			catch (const std::exception &exception)
			{
				throw std::runtime_error(QString("%1: %2").arg(name, exception.what()).toStdString());
			}
			seed_count += polyline->number_of_vertices();
			++ridge_count;
		}
		if (!created_feature_count)
		{
			throw std::runtime_error("No output features were generated for the selected ridges.");
		}
		// All geometry generation and destination validation precedes creating any model file.
		validate_destination();

		GPlatesAppLogic::ApplicationState::ScopedReconstructGuard guard(d_application_state, true);
		std::vector<Output> outputs;
		const bool create_new_collection = destination->currentData().toInt() < 0;
		for (auto &group : grouped)
		{
			Output &output = group.second;
			if (create_new_collection)
			{
				const auto file = create_named_empty_feature_collection(
						d_application_state.get_feature_collection_file_io(),
						QObject::tr("%1 (%2 to %3 Ma) %4")
								.arg(create_crust ? QObject::tr("Ocean crust") : QObject::tr("Flow lines"))
								.arg(start_time).arg(start_time - duration->value()).arg(outputs.size() + 1));
				output.collection = file.get_file().get_feature_collection();
				output.pin_new_collection_model = true;
			}
			else
			{
				// A compatible existing destination already has the correct model. Do not change
				// any of its layer connections, including when this insertion is undone or redone.
				output.collection = selected_destination();
			}
			outputs.push_back(output);
		}
		std::unique_ptr<QUndoCommand> command(new AddRidgeOutputUndoCommand(
				d_application_state, outputs,
				create_crust ? QObject::tr("make ocean crust") : QObject::tr("create flow lines")));
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		d_application_state.set_reconstruction_time(start_time - duration->value());
		const QString description = create_crust
				? QObject::tr("%1 ocean-crust polygons").arg(created_feature_count)
				: QObject::tr("%1 paired flowlines").arg(seed_count);
		return Result(create_crust ? CRUST_CREATED : FLOWLINES_CREATED,
				QObject::tr("Created %1 from %2 ridge geometries, %3 to %4 Ma. "
						"Save the output collection(s) to keep these changes.")
						.arg(description).arg(ridge_count).arg(start_time).arg(start_time - duration->value()));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("%1: %2").arg(operation_name, exception.what()));
	}
	catch (...)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("%1 stopped because of a geometry or model error.").arg(operation_name));
	}
}
