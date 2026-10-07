/* $Id$ */

#include "PlateDirectionArrowsOperation.h"

#include <algorithm>
#include <set>
#include <vector>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>

#include "app-logic/ApplicationState.h"
#include "app-logic/GeometryUtils.h"
#include "app-logic/LayerTaskType.h"
#include "app-logic/PlateVelocityUtils.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/Reconstruction.h"

#include "gui/Colour.h"

#include "presentation/ViewState.h"
#include "presentation/VisualLayer.h"
#include "presentation/VisualLayers.h"

#include "view-operations/RenderedGeometryFactory.h"
#include "view-operations/RenderedGeometryLayer.h"


GPlatesViewOperations::PlateDirectionArrowsOperation::PlateDirectionArrowsOperation(
		GPlatesAppLogic::ApplicationState &application_state,
		GPlatesPresentation::ViewState &view_state,
		RenderedGeometryCollection &rendered_geometry_collection,
		QWidget *parent_widget) :
	d_application_state(application_state),
	d_view_state(view_state),
	d_rendered_geometry_collection(rendered_geometry_collection),
	d_parent_widget(parent_widget),
	d_enabled(false),
	d_plate_id(0),
	d_delta_time(1.0),
	d_arrow_scale(0.03),
	d_max_arrows_per_feature(4)
{
	QObject::connect(
			&d_application_state,
			SIGNAL(reconstructed(GPlatesAppLogic::ApplicationState &)),
			this,
			SLOT(handle_reconstructed(GPlatesAppLogic::ApplicationState &)));
}


void
GPlatesViewOperations::PlateDirectionArrowsOperation::show_dialog()
{
	QDialog dialog(d_parent_widget);
	dialog.setWindowTitle(QObject::tr("Plate Direction Arrows"));
	QFormLayout *layout = new QFormLayout(&dialog);
	QSpinBox *plate_spin = new QSpinBox(&dialog);
	plate_spin->setRange(0, 99999999);
	plate_spin->setValue(d_plate_id);
	QDoubleSpinBox *delta_spin = new QDoubleSpinBox(&dialog);
	delta_spin->setRange(0.000001, 1000.0);
	delta_spin->setDecimals(6);
	delta_spin->setValue(d_delta_time);
	delta_spin->setSuffix(QObject::tr(" My"));
	QDoubleSpinBox *scale_spin = new QDoubleSpinBox(&dialog);
	scale_spin->setRange(0.0001, 1.0);
	scale_spin->setDecimals(4);
	scale_spin->setSingleStep(0.005);
	scale_spin->setValue(d_arrow_scale);
	QSpinBox *density_spin = new QSpinBox(&dialog);
	density_spin->setRange(1, 50);
	density_spin->setValue(static_cast<int>(d_max_arrows_per_feature));
	layout->addRow(QObject::tr("Plate ID:"), plate_spin);
	layout->addRow(QObject::tr("Direction interval:"), delta_spin);
	layout->addRow(QObject::tr("Arrow scale:"), scale_spin);
	layout->addRow(QObject::tr("Maximum arrows per feature:"), density_spin);
	QLabel *note = new QLabel(QObject::tr(
			"Draws motion-direction arrows directly on visible reconstructed features. "
			"No velocity-domain feature collection is required."), &dialog);
	note->setWordWrap(true);
	layout->addRow(note);
	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	layout->addRow(buttons);
	QObject::connect(buttons, SIGNAL(accepted()), &dialog, SLOT(accept()));
	QObject::connect(buttons, SIGNAL(rejected()), &dialog, SLOT(reject()));
	if (dialog.exec() != QDialog::Accepted)
	{
		return;
	}

	d_plate_id = plate_spin->value();
	d_delta_time = delta_spin->value();
	d_arrow_scale = scale_spin->value();
	d_max_arrows_per_feature = density_spin->value();
	d_enabled = true;
	render(true);
}


void
GPlatesViewOperations::PlateDirectionArrowsOperation::clear()
{
	d_enabled = false;
	if (d_arrow_layer)
	{
		d_arrow_layer->clear_rendered_geometries();
		d_arrow_layer->set_active(false);
	}
}


void
GPlatesViewOperations::PlateDirectionArrowsOperation::handle_reconstructed(
		GPlatesAppLogic::ApplicationState &)
{
	if (d_enabled)
	{
		render();
	}
}


void
GPlatesViewOperations::PlateDirectionArrowsOperation::render(
		bool notify_if_empty)
{
	if (!d_arrow_layer)
	{
		d_arrow_layer = d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
				RenderedGeometryCollection::RECONSTRUCTION_LAYER);
	}
	d_arrow_layer->set_active(true);
	d_arrow_layer->clear_rendered_geometries();

	unsigned int arrow_count = 0;
	std::set<const GPlatesModel::TopLevelProperty *> seen_properties;
	const double reconstruction_time =
			d_application_state.get_current_reconstruction().get_reconstruction_time();
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
					!rfg.reconstruction_plate_id() || *rfg.reconstruction_plate_id() != d_plate_id ||
					!seen_properties.insert((*rfg.property()).get()).second)
			{
				continue;
			}

			std::vector<GPlatesMaths::PointOnSphere> points;
			GPlatesAppLogic::GeometryUtils::get_geometry_exterior_points(
					*rfg.reconstructed_geometry(), points);
			if (points.empty())
			{
				continue;
			}
			const size_t samples = std::min<size_t>(d_max_arrows_per_feature, points.size());
			for (size_t sample = 0; sample < samples; ++sample)
			{
				const GPlatesMaths::PointOnSphere &point = points[sample * points.size() / samples];
				const GPlatesMaths::Vector3D velocity =
						GPlatesAppLogic::PlateVelocityUtils::calculate_velocity_vector(
								point,
								d_plate_id,
								rfg.get_reconstruction_tree_creator(),
								reconstruction_time,
								d_delta_time,
								GPlatesAppLogic::VelocityDeltaTime::T_PLUS_DELTA_T_TO_T);
				if (!velocity.is_zero_magnitude())
				{
					d_arrow_layer->add_rendered_geometry(
							RenderedGeometryFactory::create_rendered_tangential_arrow(
									point,
									velocity,
									static_cast<float>(d_arrow_scale),
									GPlatesGui::Colour::get_yellow()));
					++arrow_count;
				}
			}
		}
	}

	if (arrow_count == 0 && notify_if_empty)
	{
		QMessageBox::information(d_parent_widget, QObject::tr("Plate Direction Arrows"),
				QObject::tr("No non-zero motion vectors were found for visible features on Plate ID %1.")
						.arg(d_plate_id));
	}
}
