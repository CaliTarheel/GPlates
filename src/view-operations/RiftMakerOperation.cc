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

#include "RiftMakerOperation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUndoCommand>

#include "app-logic/ApplicationState.h"
#include "app-logic/GeometrySetter.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/Layer.h"
#include "app-logic/ReconstructGraph.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructionFeatureProperties.h"
#include "app-logic/ReconstructionTreeCreator.h"

#include "file-io/FeatureCollectionFileFormatRegistry.h"
#include "file-io/FileInfo.h"

#include "gui/FeatureFocus.h"

#include "maths/GeometryIntersect.h"
#include "maths/GeometryDistance.h"
#include "maths/LatLonPoint.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/ModelUtils.h"
#include "model/Gpgim.h"
#include "model/NotificationGuard.h"
#include "model/PropertyValueFinder.h"
#include "model/TopLevelPropertyInline.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "property-values/Enumeration.h"
#include "property-values/GmlPolygon.h"
#include "property-values/GmlTimePeriod.h"
#include "property-values/GpmlConstantValue.h"
#include "property-values/GpmlPlateId.h"
#include "property-values/XsDouble.h"
#include "property-values/XsString.h"

#include "utils/UnicodeStringUtils.h"

#include "view-operations/GeometryBuilder.h"
#include "view-operations/SplitPlateOperation.h"
#include "view-operations/UndoRedo.h"
#include "view-operations/WorldbuildingFeatureCollectionUtils.h"

namespace
{
	typedef GPlatesViewOperations::SplitPlateOperation Split;
	typedef GPlatesModel::FeatureHandle::non_null_ptr_type Feature;
	typedef GPlatesModel::FeatureCollectionHandle::weak_ref Collection;
	typedef GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type Geometry;
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
								"Choose a new collection or one using the selected polygons' model.");
					}
				}
			}
		}
		if (!found_layer && graph.get_default_reconstruction_tree_layer() != source_model)
		{
			return QObject::tr("This collection would use the default rotation model, which differs "
					"from the selected polygons' model. Choose a new collection.");
		}
		return QString();
	}


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


	void
	add_property(
			const GPlatesModel::FeatureHandle::weak_ref &feature,
			const GPlatesModel::PropertyName &name,
			const GPlatesModel::PropertyValue::non_null_ptr_type &value)
	{
		if (!GPlatesModel::ModelUtils::add_property(feature, name, value))
		{
			throw std::runtime_error(QString("Unable to create required rift property '%1'.")
					.arg(name.get_name().qstring()).toStdString());
		}
	}

	void
	require_plate(
			const GPlatesAppLogic::ReconstructionTreeCreator &creator,
			double time,
			GPlatesModel::integer_plate_id_type plate)
	{
		const auto tree = creator.get_reconstruction_tree(time);
		if (plate != tree->get_anchor_plate_id() && !tree->get_edge(plate))
		{
			throw std::invalid_argument(QString("Plate %1 has no rotation at %2 Ma.")
					.arg(plate).arg(time).toStdString());
		}
	}

	struct Choice
	{
		Choice(
				const Geometry &geometry_,
				const GPlatesAppLogic::Layer &layer_) :
			geometry(geometry_),
			layer(layer_),
			revision(geometry_->get_feature_ref()->revision_id()),
			collection(geometry_->feature_handle_ptr()->parent_ptr()->reference())
		{  }
		Geometry geometry;
		GPlatesAppLogic::Layer layer;
		GPlatesModel::RevisionId revision;
		Collection collection;
	};

	QString
	choice_label(
			const Choice &choice,
			GPlatesAppLogic::ApplicationState &application)
	{
		const auto feature = choice.geometry->get_feature_ref();
		const auto name = GPlatesModel::get_property_value<GPlatesPropertyValues::XsString>(
				feature, GPlatesModel::PropertyName::create_gml("name"));
		QString file_name = QObject::tr("Unsaved collection");
		for (const auto &file : application.get_feature_collection_file_state().get_loaded_files())
		{
			if (file.get_file().get_feature_collection() == choice.collection)
			{
				file_name = file.get_file().get_file_info().get_display_name(true);
				break;
			}
		}
		return QString("%1 | %2 | %3").arg(name ? GPlatesUtils::make_qstring((*name)->get_value())
				: QObject::tr("Unnamed")).arg(file_name).arg(feature->feature_id().get().qstring());
	}

	void
	validate_choice(
			const Choice &choice)
	{
		if (!choice.layer.is_valid() || !choice.layer.is_active() || !choice.collection.is_valid() ||
				!choice.geometry->is_valid() || !choice.geometry->property().is_still_valid() ||
				choice.geometry->feature_handle_ptr()->parent_ptr() != choice.collection.handle_ptr() ||
				choice.geometry->get_feature_ref()->revision_id() != choice.revision)
		{
			throw std::runtime_error("A selected feature or layer changed. Reopen Rift Maker.");
		}
		const auto output = choice.layer.get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
		if (!output)
		{
			throw std::runtime_error("The selected reconstruction layer changed. Reopen Rift Maker.");
		}
		const auto captured_tree = choice.geometry->get_reconstruction_tree();
		const auto current_tree = (*output)->get_reconstruct_method_context().
				reconstruction_tree_creator.get_reconstruction_tree(
						captured_tree->get_reconstruction_time());
		// A cache eviction can replace the tree object without changing its reconstruction.
		// Compare graph, anchor and age; a rotation edit must invalidate the captured geometry.
		if (!captured_tree->created_from_same_graph_with_same_parameters(*current_tree))
		{
			throw std::runtime_error("The rotation model or anchor changed. Reopen Rift Maker "
					"to preview freshly reconstructed geometry.");
		}
	}

	void
	get_choices(
			std::vector<Choice> &polygons,
			std::vector<Choice> &lines,
			GPlatesPresentation::ViewState &view)
	{
		std::set<const GPlatesModel::TopLevelProperty *> seen;
		const auto &layers = view.get_visual_layers();
		for (std::size_t index = 0; index < layers.size(); ++index)
		{
			const auto visual = layers.visual_layer_at(index).lock();
			if (!visual || !visual->is_visible())
			{
				continue;
			}
			const auto layer = visual->get_reconstruct_graph_layer();
			if (!layer.is_active() || layer.get_type() != GPlatesAppLogic::LayerTaskType::RECONSTRUCT)
			{
				continue;
			}
			const auto output = layer.get_layer_output<GPlatesAppLogic::ReconstructLayerProxy>();
			if (!output)
			{
				continue;
			}
			std::vector<Geometry> geometries;
			(*output)->get_reconstructed_feature_geometries(geometries);
			for (const auto &geometry : geometries)
			{
				if (!geometry->is_valid() || !geometry->property().is_still_valid() ||
						!geometry->feature_handle_ptr()->parent_ptr())
				{
					continue;
				}
				const auto shape = geometry->reconstructed_geometry();
				const bool polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(shape.get());
				const bool line = dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(shape.get());
				if ((!polygon && !line) || (polygon && !geometry->finite_rotation_reconstruction()) ||
						!seen.insert((*geometry->property()).get()).second)
				{
					continue;
				}
				(polygon ? polygons : lines).emplace_back(geometry, layer);
			}
		}
	}

	void
	validate_polygon_property(
			const Choice &choice)
	{
		const auto &feature = choice.geometry->get_feature_ref();
		std::size_t polygon_count = 0;
		for (auto property = feature->begin(); property != feature->end(); ++property)
		{
			if (GPlatesModel::get_property_value<GPlatesPropertyValues::GmlPolygon>(
					property, (*property)->get_property_name()))
			{
				++polygon_count;
			}
		}
		const auto *property = dynamic_cast<const GPlatesModel::TopLevelPropertyInline *>(
				(*choice.geometry->property()).get());
		if (polygon_count != 1 || !property || property->size() != 1)
		{
			throw std::invalid_argument("Rift Maker needs one polygon geometry per source feature. "
					"Separate multipart features before cutting them.");
		}
		const GPlatesModel::PropertyValue::non_null_ptr_to_const_type value_ptr = *property->begin();
		const GPlatesModel::PropertyValue *value = value_ptr.get();
		while (const auto *constant =
				dynamic_cast<const GPlatesPropertyValues::GpmlConstantValue *>(value))
		{
			value = constant->value().get();
		}
		if (!dynamic_cast<const GPlatesPropertyValues::GmlPolygon *>(value))
		{
			throw std::invalid_argument("Time-dependent or deformed polygon geometry cannot be cut.");
		}
	}

	struct Plan
	{
		std::size_t source;
		Split::SketchSplitResult geometry;
		std::vector<QSpinBox *> plate_controls;
	};

	bool
	span_interiors_overlap(
			const GPlatesMaths::PolylineOnSphere &first,
			const GPlatesMaths::PolylineOnSphere &second)
	{
		for (auto arc = first.begin(); arc != first.end(); ++arc)
		{
			const auto sample = arc->point_on_arc(0.5);
			auto closest = sample.position_vector();
			GPlatesMaths::minimum_distance(sample, second, boost::none, closest);
			const double dx = sample.position_vector().x().dval() - closest.x().dval();
			const double dy = sample.position_vector().y().dval() - closest.y().dval();
			const double dz = sample.position_vector().z().dval() - closest.z().dval();
			if (dx * dx + dy * dy + dz * dz < 1e-20)
			{
				return true;
			}
		}
		return false;
	}

	struct FeatureSnapshot
	{
		FeatureSnapshot(
				const Feature &target_,
				const Feature &contents) :
			target(target_)
		{
			auto slot = target->begin();
			for (auto property = contents->begin(); property != contents->end(); ++property)
			{
				properties.emplace_back(slot, (*property)->clone());
				if (slot != target->end())
				{
					++slot;
				}
			}
		}

		void
		restore()
		{
			for (auto property = target->begin(); property != target->end(); ++property)
			{
				if (std::none_of(properties.begin(), properties.end(),
						[&](const auto &saved) { return saved.first == property; }))
				{
					target->remove(property);
				}
			}
			for (auto &property : properties)
			{
				if (property.first.is_still_valid())
				{
					target->set(property.first, property.second->clone());
				}
				else
				{
					property.first = target->add(property.second->clone());
				}
			}
		}
		Feature target;
		std::vector<std::pair<GPlatesModel::FeatureHandle::iterator,
				GPlatesModel::TopLevelProperty::non_null_ptr_type>> properties;
	};

	struct Change
	{
		Change(
				const Feature &original,
				const Feature &replacement) :
			collection(original->parent_ptr()->reference()),
			before(original, original),
			after(original, replacement)
		{  }
		Collection collection;
		FeatureSnapshot before;
		FeatureSnapshot after;
	};

	class RiftUndoCommand :
			public QUndoCommand
	{
	public:
		RiftUndoCommand(
				GPlatesAppLogic::ApplicationState &application,
				const std::vector<Change> &changes,
				const std::vector<Output> &outputs) :
			d_application(application),
			d_changes(changes),
			d_outputs(outputs)
		{
			setText(QObject::tr("make rift"));
			for (const auto &output : outputs)
			{
				for (const auto &feature : output.features)
				{
					d_created.emplace_back(feature, feature);
				}
			}
		}

		bool
		available() const
		{
			for (const auto &change : d_changes)
			{
				if (!change.collection.is_valid() || !change.before.target->is_active() ||
						change.before.target->parent_ptr() != change.collection.handle_ptr())
				{
					return false;
				}
			}
			for (const auto &output : d_outputs)
			{
				if (!output.collection.is_valid())
				{
					return false;
				}
				for (const auto &feature : output.features)
				{
					// Removed features retain their parent pointer. A different parent means
					// an external move: do not steal the feature or apply only part of the rift.
					if (feature->parent_ptr() &&
							feature->parent_ptr() != output.collection.handle_ptr())
					{
						return false;
					}
				}
			}
			return true;
		}

		void
		redo() override
		{
			if (!available())
			{
				return;
			}
			GPlatesAppLogic::ApplicationState::ScopedReconstructGuard reconstruct(d_application, true);
			GPlatesModel::NotificationGuard guard(*d_application.get_model_interface().access_model());
			for (auto &change : d_changes)
			{
				change.after.restore();
			}
			std::size_t index = 0;
			for (const auto &output : d_outputs)
			{
				for (const auto &feature : output.features)
				{
					auto &snapshot = d_created[index++];
					if (!feature->is_active() || !feature->parent_ptr())
					{
						output.collection->add(feature);
						feature->set_active(true);
						snapshot.restore();
					}
				}
			}
			guard.release_guard();
			for (const auto &output : d_outputs)
			{
				connect_rotation_model(d_application.get_reconstruct_graph(), output);
			}
		}

		void
		undo() override
		{
			if (!available())
			{
				return;
			}
			GPlatesAppLogic::ApplicationState::ScopedReconstructGuard reconstruct(d_application, true);
			GPlatesModel::NotificationGuard guard(*d_application.get_model_interface().access_model());
			for (const auto &output : d_outputs)
			{
				for (const auto &feature : output.features)
				{
					if (feature->is_active() && feature->parent_ptr() == output.collection.handle_ptr())
					{
						feature->remove_from_parent();
					}
				}
			}
			for (auto &change : d_changes)
			{
				change.before.restore();
			}
		}
	private:
		GPlatesAppLogic::ApplicationState &d_application;
		std::vector<Change> d_changes;
		std::vector<Output> d_outputs;
		std::vector<FeatureSnapshot> d_created;
	};
}

GPlatesViewOperations::RiftMakerOperation::RiftMakerOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state) :
	d_application_state(application_state),
	d_view_state(view_state)
{  }

GPlatesModel::FeatureHandle::non_null_ptr_type
GPlatesViewOperations::RiftMakerOperation::create_mid_ocean_ridge(
		const GPlatesMaths::PolylineOnSphere &span,
		GPlatesModel::integer_plate_id_type left_plate,
		GPlatesModel::integer_plate_id_type right_plate,
		const GPlatesAppLogic::ReconstructionTreeCreator &tree_creator,
		double start_time,
		double end_time,
		const QString &name)
{
	if (left_plate == right_plate || !std::isfinite(start_time) || !std::isfinite(end_time) ||
			end_time < 0 || start_time < end_time)
	{
		throw std::invalid_argument("A ridge needs distinct left/right plates and a valid age range.");
	}
	require_plate(tree_creator, start_time, left_plate);
	require_plate(tree_creator, start_time, right_plate);
	const auto feature = GPlatesModel::FeatureHandle::create(
			GPlatesModel::FeatureType::create_gpml("MidOceanRidge"));
	const auto ref = feature->reference();
	add_property(ref, GPlatesModel::PropertyName::create_gml("name"),
			GPlatesPropertyValues::XsString::create(GPlatesUtils::make_icu_string_from_qstring(name)));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("leftPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(left_plate));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("rightPlate"),
			GPlatesPropertyValues::GpmlPlateId::create(right_plate));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("reconstructionMethod"),
			GPlatesPropertyValues::Enumeration::create(
					GPlatesPropertyValues::EnumerationType::create_gpml("ReconstructionMethodEnumeration"),
					"HalfStageRotationVersion3"));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("geometryImportTime"),
			GPlatesModel::ModelUtils::create_gml_time_instant(
					GPlatesPropertyValues::GeoTimeInstant(start_time)));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("spreadingAsymmetry"),
			GPlatesPropertyValues::XsDouble::create(0));
	add_property(ref, GPlatesModel::PropertyName::create_gml("validTime"),
			GPlatesModel::ModelUtils::create_gml_time_period(
					GPlatesPropertyValues::GeoTimeInstant(start_time),
					GPlatesPropertyValues::GeoTimeInstant(end_time)));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
			GPlatesAppLogic::GeometryUtils::create_geometry_property_value(span.get_non_null_pointer()));
	// Establish the native method/import age before reversing into the ridge's own frame.
	const auto stored = GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
			span.get_non_null_pointer(), GPlatesAppLogic::ReconstructMethodRegistry(), ref,
			start_time, tree_creator, GPlatesAppLogic::ReconstructParams(), true);
	feature->remove_properties_by_name(GPlatesModel::PropertyName::create_gpml("centerLineOf"));
	add_property(ref, GPlatesModel::PropertyName::create_gpml("centerLineOf"),
			GPlatesAppLogic::GeometryUtils::create_geometry_property_value(stored));
	return feature;
}
GPlatesViewOperations::RiftMakerOperation::Result
GPlatesViewOperations::RiftMakerOperation::trigger(
		QWidget *parent_widget)
{
	std::vector<Choice> polygons;
	std::vector<Choice> lines;
	get_choices(polygons, lines, d_view_state);
	const double time = d_application_state.get_current_reconstruction_time();
	const auto digitised = d_view_state.get_digitise_geometry_builder().get_geometry_on_sphere();
	const auto *digitised_line = digitised ?
			dynamic_cast<const GPlatesMaths::PolylineOnSphere *>((*digitised).get()) : nullptr;
	if (polygons.empty() || (!digitised_line && lines.empty()))
	{
		return Result(OPERATION_ERROR, QObject::tr("Rift Maker needs a visible rigid polygon and a "
				"polyline. Digitise a line with at least two vertices, or load an existing sketch."));
	}

	QDialog dialog(parent_widget);
	dialog.setObjectName("RiftMakerDialog");
	dialog.setWindowTitle(QObject::tr("Rift Maker"));
	dialog.resize(1000, 740);
	auto *layout = new QVBoxLayout(&dialog);
	auto *instructions = new QLabel(QObject::tr(
			"At %1 Ma, select polygons and a sketch, then preview the pieces. Only interior cut "
			"spans become MORs; the complete sketch is kept. Cuts through holes, tangencies and "
			"self-crossing sketches are rejected. Pieces stay in their source collections.")
			.arg(time), &dialog);
	instructions->setWordWrap(true);
	layout->addWidget(instructions);
	auto *sketch_choice = new QComboBox(&dialog);
	sketch_choice->setObjectName("riftSketchComboBox");
	if (digitised_line)
	{
		sketch_choice->addItem(QObject::tr("Current digitised sketch (kept in the digitising tool)"), -1);
	}
	for (std::size_t index = 0; index < lines.size(); ++index)
	{
		sketch_choice->addItem(choice_label(lines[index], d_application_state), static_cast<int>(index));
		if (!digitised_line && lines[index].geometry.get() ==
				d_view_state.get_feature_focus().associated_reconstruction_geometry().get())
		{
			sketch_choice->setCurrentIndex(sketch_choice->count() - 1);
		}
	}
	layout->addWidget(sketch_choice);
	auto *polygon_list = new QListWidget(&dialog);
	polygon_list->setObjectName("riftPolygonsListWidget");
	polygon_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
	for (const auto &choice : polygons)
	{
		polygon_list->addItem(choice_label(choice, d_application_state));
		if (choice.geometry.get() ==
				d_view_state.get_feature_focus().associated_reconstruction_geometry().get())
		{
			polygon_list->item(polygon_list->count() - 1)->setSelected(true);
		}
	}
	layout->addWidget(polygon_list);
	layout->addWidget(new QLabel(QObject::tr("MOR output collection / file:"), &dialog));
	auto *destination = new QComboBox(&dialog);
	destination->setObjectName("riftOutputCollectionComboBox");
	destination->addItem(QObject::tr("Create new MOR collection — save afterwards"), -1);
	std::vector<Collection> destinations;
	for (const auto &file : d_application_state.get_feature_collection_file_state().get_loaded_files())
	{
		if (!destination_file_error(file,
				d_application_state.get_feature_collection_file_format_registry()).isEmpty())
		{
			continue;
		}
		destination->addItem(file.get_file().get_file_info().get_display_name(true),
				static_cast<int>(destinations.size()));
		destinations.push_back(file.get_file().get_feature_collection());
	}
	layout->addWidget(destination);
	auto *preview = new QPushButton(QObject::tr("Preview pieces"), &dialog);
	preview->setObjectName("riftPreviewButton");
	layout->addWidget(preview);
	auto *table = new QTableWidget(0, 5, &dialog);
	table->setObjectName("riftPiecesTable");
	table->setHorizontalHeaderLabels({QObject::tr("Source / piece"), QObject::tr("Centre latitude"),
			QObject::tr("Centre longitude"), QObject::tr("Area (steradians)"), QObject::tr("Plate ID")});
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
	table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	layout->addWidget(table);
	auto *status = new QLabel(&dialog);
	status->setObjectName("riftStatusLabel");
	status->setWordWrap(true);
	layout->addWidget(status);
	auto *validity = new QLabel(QObject::tr(
			"Pieces retain the original validity range; assigned plate IDs apply throughout that "
			"range, including older times. No new rotations are created."), &dialog);
	validity->setWordWrap(true);
	layout->addWidget(validity);
	auto *review = new QCheckBox(QObject::tr(
			"I reviewed the piece IDs and their validity ranges."), &dialog);
	review->setObjectName("riftAssignmentsReviewedCheckBox");
	layout->addWidget(review);
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	auto *create = buttons->button(QDialogButtonBox::Ok);
	create->setText(QObject::tr("Create rift"));
	create->setObjectName("riftCreateButton");
	create->setEnabled(false);
	layout->addWidget(buttons);
	std::vector<Plan> plans;
	GPlatesAppLogic::Layer source_model;
	const auto invalidate = [&]()
	{
		plans.clear();
		table->setRowCount(0);
		review->setChecked(false);
		create->setEnabled(false);
		status->setText(QObject::tr("Preview the selected polygons before assigning plate IDs."));
	};
	const auto validate_sources = [&]()
	{
		if (d_application_state.get_current_reconstruction_time() != time)
		{
			throw std::runtime_error("The reconstruction time changed. Reopen Rift Maker.");
		}
		const int sketch_index = sketch_choice->currentData().toInt();
		if (sketch_index >= 0)
		{
			validate_choice(lines[sketch_index]);
		}
		else
		{
			const auto current = d_view_state.get_digitise_geometry_builder().get_geometry_on_sphere();
			if (!current || !digitised || (*current).get() != (*digitised).get())
			{
				throw std::runtime_error("The digitised sketch changed. Reopen Rift Maker.");
			}
		}
		for (const auto &plan : plans)
		{
			validate_choice(polygons[plan.source]);
			const auto model = rotation_model_for_layer(
					polygons[plan.source].layer, d_application_state.get_reconstruct_graph());
			if (model != source_model)
			{
				throw std::runtime_error("The polygons' rotation model changed. Reopen Rift Maker.");
			}
			const QString error = destination_error(polygons[plan.source].collection, source_model,
					d_application_state);
			if (!error.isEmpty())
			{
				throw std::runtime_error(error.toStdString());
			}
		}
		const int output = destination->currentData().toInt();
		if (output >= 0 && !plans.empty())
		{
			const QString error = destination_error(destinations[output], source_model, d_application_state);
			if (!error.isEmpty())
			{
				throw std::runtime_error(error.toStdString());
			}
		}
	};
	QObject::connect(sketch_choice, QOverload<int>::of(&QComboBox::currentIndexChanged),
			&dialog, [&](int) { invalidate(); });
	QObject::connect(polygon_list, &QListWidget::itemSelectionChanged, &dialog, invalidate);
	QObject::connect(review, &QCheckBox::toggled, &dialog,
			[&](bool checked) { create->setEnabled(checked && !plans.empty()); });
	QObject::connect(preview, &QPushButton::clicked, &dialog, [&]()
	{
		invalidate();
		try
		{
			validate_sources();
			const int sketch_index = sketch_choice->currentData().toInt();
			const auto *sketch = sketch_index < 0 ? digitised_line :
					dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(
							lines[sketch_index].geometry->reconstructed_geometry().get());
			std::set<const GPlatesModel::FeatureHandle *> selected_features;
			std::size_t piece_count = 0;
			for (int row = 0; row < polygon_list->count(); ++row)
			{
				if (!polygon_list->item(row)->isSelected())
				{
					continue;
				}
				const Choice &source = polygons[row];
				validate_choice(source);
				validate_polygon_property(source);
				if (!selected_features.insert(source.geometry->feature_handle_ptr()).second)
				{
					throw std::invalid_argument("Select only one geometry from each source feature.");
				}
				const auto model = rotation_model_for_layer(
						source.layer, d_application_state.get_reconstruct_graph());
				if (plans.empty())
				{
					source_model = model;
				}
				else if (model != source_model)
				{
					throw std::invalid_argument("Selected polygons use different rotation models. "
							"Create separate rifts for each model.");
				}
				const auto *polygon = dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(
						source.geometry->reconstructed_geometry().get());
				QString error;
				const auto cut = Split::split_polygon_along_sketch(error, *polygon, *sketch);
				if (!cut)
				{
					throw std::invalid_argument(error.toStdString());
				}
				piece_count += cut->polygons.size();
				if (piece_count > 1024)
				{
					throw std::invalid_argument("More than 1,024 pieces requested. Select fewer polygons.");
				}
				// Overlapping targets must not claim the same interior sketch span. Boundary
				// contacts between adjacent targets are allowed, but never overlapping interiors.
				for (const auto &earlier : plans)
				{
					for (const auto &a : earlier.geometry.cut_spans)
					{
						for (const auto &b : cut->cut_spans)
						{
							if (span_interiors_overlap(*a, *b) || span_interiors_overlap(*b, *a))
							{
								throw std::invalid_argument("Selected polygons overlap along the sketch. "
										"Choose non-overlapping targets.");
							}
							GPlatesMaths::GeometryIntersect::Graph intersections;
							GPlatesMaths::GeometryIntersect::intersect(intersections, *a, *b);
							for (const auto &event : intersections.unordered_intersections)
							{
								const auto at_end = [&](const GPlatesMaths::PolylineOnSphere &line)
								{
									return event.position == line.get_vertex(0) ||
											event.position == line.get_vertex(line.number_of_vertices() - 1);
								};
								if (!at_end(*a) || !at_end(*b))
								{
									throw std::invalid_argument("Selected polygons overlap along the sketch. "
											"Choose non-overlapping targets.");
								}
							}
						}
					}
				}
				plans.push_back(Plan{static_cast<std::size_t>(row), *cut, {}});
			}
			if (plans.empty())
			{
				throw std::invalid_argument("Select at least one polygon.");
			}
			validate_sources();
			QStringList sides;
			for (auto &plan : plans)
			{
				const auto &source = polygons[plan.source];
				const QString label = choice_label(source, d_application_state);
				const int first_row = table->rowCount();
				for (std::size_t piece = 0; piece < plan.geometry.polygons.size(); ++piece)
				{
					if (table->rowCount() >= 1024)
					{
						throw std::invalid_argument("More than 1,024 pieces requested. Select fewer polygons.");
					}
					const auto &shape = plan.geometry.polygons[piece];
					const auto centre = GPlatesMaths::make_lat_lon_point(
							GPlatesMaths::PointOnSphere(shape->get_outline_centroid()));
					const int row = table->rowCount();
					table->insertRow(row);
					table->setItem(row, 0, new QTableWidgetItem(
							QString("%1 — piece %2").arg(label).arg(piece + 1)));
					table->setItem(row, 1, new QTableWidgetItem(QString::number(centre.latitude(), 'f', 3)));
					table->setItem(row, 2, new QTableWidgetItem(QString::number(centre.longitude(), 'f', 3)));
					table->setItem(row, 3, new QTableWidgetItem(
							QString::number(shape->get_area().dval(), 'g', 7)));
					auto *plate = new QSpinBox(table);
					plate->setRange(0, std::numeric_limits<int>::max());
					plate->setValue(source.geometry->reconstruction_plate_id().get_value_or(0));
					plate->setObjectName(QString("riftPiecePlate%1").arg(row));
					table->setCellWidget(row, 4, plate);
					plan.plate_controls.push_back(plate);
				}
				for (const auto &adjacent : plan.geometry.adjacent_polygons)
				{
					sides.push_back(QObject::tr("MOR: row %1 left / row %2 right")
							.arg(first_row + adjacent.first + 1).arg(first_row + adjacent.second + 1));
				}
			}
			status->setText(QObject::tr("Assign distinct plate IDs across each MOR. Centres are outline "
					"centroids at the current age; left/right follow sketch direction.\n%1")
					.arg(sides.join("; ")));
		}
		catch (const std::exception &exception)
		{
			invalidate();
			status->setText(QString::fromUtf8(exception.what()));
		}
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
	{
		try
		{
			validate_sources();
			if (plans.empty() || !review->isChecked())
			{
				throw std::invalid_argument("Preview and review the piece assignments first.");
			}
			for (const auto &plan : plans)
			{
				for (const auto *plate : plan.plate_controls)
				{
					require_plate(polygons[plan.source].geometry->get_reconstruction_tree_creator(),
							time, plate->value());
				}
				for (const auto &adjacent : plan.geometry.adjacent_polygons)
				{
					if (plan.plate_controls[adjacent.first]->value() ==
							plan.plate_controls[adjacent.second]->value())
					{
						throw std::invalid_argument("The pieces on opposite sides of a MOR need distinct "
								"plate IDs. Choose existing plates; no rotations are created automatically.");
					}
				}
			}
			dialog.accept();
		}
		catch (const std::exception &exception)
		{
			status->setText(QString::fromUtf8(exception.what()));
		}
	});
	if (dialog.exec() != QDialog::Accepted)
	{
		return Result(CANCELLED, QString());
	}
	try
	{
		validate_sources();
		std::vector<Change> changes;
		std::vector<Output> outputs;
		Output ridges;
		ridges.rotation_layer = source_model;
		for (const auto &plan : plans)
		{
			const auto &source = polygons[plan.source];
			const auto original = source.geometry->get_feature_ref();
			Output children;
			children.collection = source.collection;
			for (std::size_t index = 0; index < plan.geometry.polygons.size(); ++index)
			{
				const auto feature = GPlatesModel::FeatureHandle::create(original->feature_type());
				boost::optional<GPlatesModel::FeatureHandle::iterator> shape_slot;
				for (auto property = original->begin(); property != original->end(); ++property)
				{
					const auto slot = feature->add((*property)->clone());
					if (property == source.geometry->property())
					{
						shape_slot = slot;
					}
				}
				feature->remove_properties_by_name(
						GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"));
				feature->remove_properties_by_name(
						GPlatesModel::PropertyName::create_gpml("reconstructionMethod"));
				add_property(feature->reference(),
						GPlatesModel::PropertyName::create_gpml("reconstructionPlateId"),
						GPlatesPropertyValues::GpmlPlateId::create(plan.plate_controls[index]->value()));
				const auto method_name = GPlatesModel::PropertyName::create_gpml("reconstructionMethod");
				// Crust features use ByPlateId by default and do not permit a method property.
				// Tectonic-section polygons can have a half-stage method, so override that
				// explicitly when their schema supports it, without extending the feature type.
				if (GPlatesModel::Gpgim::instance().get_feature_property(feature->feature_type(), method_name))
				{
					add_property(feature->reference(), method_name,
							GPlatesPropertyValues::Enumeration::create(
									GPlatesPropertyValues::EnumerationType::create_gpml("ReconstructionMethodEnumeration"),
									"ByPlateId"));
				}
				const auto stored = GPlatesAppLogic::ReconstructUtils::reconstruct_geometry(
						plan.geometry.polygons[index], GPlatesAppLogic::ReconstructMethodRegistry(),
						feature->reference(), time, source.geometry->get_reconstruction_tree_creator(),
						GPlatesAppLogic::ReconstructParams(), true);
				auto property = (*shape_slot.get())->clone();
				GPlatesAppLogic::GeometrySetter setter(stored);
				setter.set_geometry(property.get());
				feature->set(shape_slot.get(), property);
				if (index == 0)
				{
					changes.emplace_back(Feature(original.handle_ptr()), feature);
				}
				else
				{
					children.features.push_back(feature);
				}
			}
			outputs.push_back(children);
			GPlatesAppLogic::ReconstructionFeatureProperties properties;
			properties.visit_feature(original);
			const auto end = properties.get_time_of_disappearance();
			const double end_time = end && end->is_real() ? std::max(0.0, end->value()) : 0;
			for (std::size_t index = 0; index < plan.geometry.cut_spans.size(); ++index)
			{
				const auto adjacent = plan.geometry.adjacent_polygons[index];
				ridges.features.push_back(create_mid_ocean_ridge(*plan.geometry.cut_spans[index],
						plan.plate_controls[adjacent.first]->value(),
						plan.plate_controls[adjacent.second]->value(),
						source.geometry->get_reconstruction_tree_creator(), time, end_time,
						QObject::tr("Rift MOR %1").arg(index + 1)));
			}
		}
		validate_sources();
		GPlatesAppLogic::ApplicationState::ScopedReconstructGuard guard(d_application_state, true);
		const int output_index = destination->currentData().toInt();
		if (output_index < 0)
		{
			const auto file = create_named_empty_feature_collection(
					d_application_state.get_feature_collection_file_io(),
					QObject::tr("Rift MORs (%1 Ma)").arg(time));
			ridges.collection = file.get_file().get_feature_collection();
			ridges.pin_new_collection_model = true;
		}
		else
		{
			ridges.collection = destinations[output_index];
		}
		const std::size_t ridge_count = ridges.features.size();
		outputs.push_back(ridges);
		std::unique_ptr<QUndoCommand> command(new RiftUndoCommand(d_application_state, changes, outputs));
		auto &focus = d_view_state.get_feature_focus();
		// Feature-focus signals clear the editing undo stack. Never emit them from a command.
		if (focus.focused_feature().is_valid() || focus.associated_reconstruction_geometry())
		{
			focus.unset_focus();
		}
		UndoRedo::instance().get_active_undo_stack().push(command.release());
		return Result(RIFT_CREATED, QObject::tr("Created %1 trimmed MORs and %2 polygon pieces. "
				"The complete sketch is unchanged. Save the edited collections to keep the rift.")
				.arg(ridge_count).arg(table->rowCount()));
	}
	catch (const std::exception &exception)
	{
		return Result(OPERATION_ERROR, QString::fromUtf8(exception.what()));
	}
	catch (...)
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Rift Maker stopped because of a geometry or model error."));
	}
}
