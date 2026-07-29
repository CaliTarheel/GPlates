/* $Id$ */

/**
 * \file
 * World Building "Naturalize Coastline" operation.
 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <vector>

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QUndoCommand>
#include <QVBoxLayout>

#include "NaturalizeCoastlineOperation.h"

#include "NaturalizeCoastlineGeometry.h"
#include "RenderedGeometryCollection.h"
#include "RenderedGeometryFactory.h"
#include "RenderedGeometryLayer.h"
#include "UndoRedo.h"

#include "app-logic/ApplicationState.h"
#include "app-logic/ReconstructLayerProxy.h"
#include "app-logic/ReconstructUtils.h"
#include "app-logic/ReconstructedFeatureGeometry.h"
#include "app-logic/Reconstruction.h"
#include "app-logic/ReconstructionGeometryUtils.h"

#include "feature-visitors/GeometrySetter.h"
#include "feature-visitors/GeometryTypeFinder.h"

#include "gui/Colour.h"
#include "gui/FeatureFocus.h"

#include "maths/AngularDistance.h"
#include "maths/GeometryDistance.h"
#include "maths/GeometryIntersect.h"
#include "maths/PolygonOrientation.h"
#include "maths/PolygonOnSphere.h"
#include "maths/PolylineOnSphere.h"

#include "model/FeatureType.h"
#include "model/NotificationGuard.h"
#include "model/TopLevelProperty.h"

#include "utils/Earth.h"


namespace
{
	namespace NaturalizeGeometry = GPlatesViewOperations::NaturalizeCoastlineGeometry;
	typedef NaturalizeGeometry::point_seq_type point_seq_type;
	typedef std::vector<point_seq_type> path_seq_type;

	struct GeometryPaths
	{
		GeometryPaths() : polygon(false) {  }

		bool polygon;
		path_seq_type paths;
	};

	struct SegmentReplacement
	{
		GPlatesMaths::PointOnSphere start;
		GPlatesMaths::PointOnSphere end;
		point_seq_type points;

		SegmentReplacement(
				const GPlatesMaths::PointOnSphere &start_,
				const GPlatesMaths::PointOnSphere &end_,
				const point_seq_type &points_) :
			start(start_),
			end(end_),
			points(points_)
		{  }
	};

	typedef std::vector<SegmentReplacement> replacement_seq_type;

	struct SourceGeometry
	{
		SourceGeometry(
				const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &rfg_,
				const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type &geometry_) :
			rfg(rfg_),
			geometry(geometry_)
		{  }

		GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type rfg;
		GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry;
	};

	typedef std::vector<SourceGeometry> source_geometry_seq_type;

	struct CandidateChange
	{
		GPlatesModel::FeatureHandle::weak_ref feature;
		GPlatesModel::FeatureHandle::iterator property;
		GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_geometry;
		GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type preview_geometry;
		QString feature_id;
	};

	typedef std::vector<CandidateChange> candidate_change_seq_type;

	struct Candidate
	{
		Candidate() :
			success(false),
			inserted_points(0),
			shared_features(0),
			semantic_features(0),
			protected_segments(0),
			processed_segments(0),
			maximum_output_segment_km(0.0),
			maximum_area_change_percent(0.0),
			amplitude_scale(1.0)
		{  }

		bool success;
		QString error;
		candidate_change_seq_type changes;
		unsigned int inserted_points;
		unsigned int shared_features;
		unsigned int semantic_features;
		unsigned int protected_segments;
		unsigned int processed_segments;
		double maximum_output_segment_km;
		double maximum_area_change_percent;
		double amplitude_scale;
	};


	bool
	same_property(
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg1,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg2)
	{
		return rfg1.feature_handle_ptr() == rfg2.feature_handle_ptr() &&
			rfg1.property() == rfg2.property();
	}


	bool
	is_craton(
			const GPlatesModel::FeatureHandle &feature)
	{
		static const GPlatesModel::FeatureType CRATON =
				GPlatesModel::FeatureType::create_gpml("Craton");
		return feature.feature_type() == CRATON;
	}


	bool
	is_semantically_important_boundary(
			const GPlatesModel::FeatureHandle &feature)
	{
		static const char *const IMPORTANT_TYPES[] =
		{
			"SubductionZone", "MidOceanRidge", "Transform", "Fault", "Suture",
			"TerraneBoundary", "TopologicalClosedPlateBoundary", "TopologicalNetwork"
		};
		for (unsigned int type_index = 0;
				type_index < sizeof(IMPORTANT_TYPES) / sizeof(IMPORTANT_TYPES[0]);
				++type_index)
		{
			if (feature.feature_type() ==
					GPlatesModel::FeatureType::create_gpml(IMPORTANT_TYPES[type_index]))
			{
				return true;
			}
		}
		return false;
	}


	boost::optional<GeometryPaths>
	extract_paths(
			const GPlatesMaths::GeometryOnSphere &geometry)
	{
		GeometryPaths result;
		if (const GPlatesMaths::PolylineOnSphere *polyline =
				dynamic_cast<const GPlatesMaths::PolylineOnSphere *>(&geometry))
		{
			result.paths.push_back(point_seq_type(
					polyline->vertex_begin(), polyline->vertex_end()));
			return result;
		}

		if (const GPlatesMaths::PolygonOnSphere *polygon =
				dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(&geometry))
		{
			result.polygon = true;
			result.paths.push_back(point_seq_type(
					polygon->exterior_ring_vertex_begin(), polygon->exterior_ring_vertex_end()));
			for (unsigned int ring_index = 0;
					ring_index < polygon->number_of_interior_rings(); ++ring_index)
			{
				result.paths.push_back(point_seq_type(
						polygon->interior_ring_vertex_begin(ring_index),
						polygon->interior_ring_vertex_end(ring_index)));
			}
			return result;
		}
		return boost::none;
	}


	boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>
	create_geometry(
			const GeometryPaths &geometry_paths)
	{
		if (geometry_paths.paths.empty())
		{
			return boost::none;
		}

		if (!geometry_paths.polygon)
		{
			if (GPlatesMaths::PolylineOnSphere::evaluate_construction_parameter_validity(
					geometry_paths.paths.front(), true) != GPlatesMaths::PolylineOnSphere::VALID)
			{
				return boost::none;
			}
			const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry(
					GPlatesMaths::PolylineOnSphere::create(
							geometry_paths.paths.front(), true));
			return boost::optional<
					GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>(geometry);
		}

		if (GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
				geometry_paths.paths.front(), true) != GPlatesMaths::PolygonOnSphere::VALID)
		{
			return boost::none;
		}
		path_seq_type interior_rings;
		for (path_seq_type::const_iterator path_iter = geometry_paths.paths.begin() + 1;
				path_iter != geometry_paths.paths.end(); ++path_iter)
		{
			if (GPlatesMaths::PolygonOnSphere::evaluate_construction_parameter_validity(
					*path_iter, true) != GPlatesMaths::PolygonOnSphere::VALID)
			{
				return boost::none;
			}
			interior_rings.push_back(*path_iter);
		}
		const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry(
				GPlatesMaths::PolygonOnSphere::create(
						geometry_paths.paths.front(), interior_rings, true));
		return boost::optional<
				GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>(geometry);
	}


	GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type
	create_path_polyline(
			const point_seq_type &path,
			bool closed)
	{
		point_seq_type polyline_points(path);
		if (closed)
		{
			polyline_points.push_back(path.front());
		}
		return GPlatesMaths::PolylineOnSphere::create(polyline_points);
	}


	bool
	path_has_self_intersection(
			const point_seq_type &path,
			bool closed)
	{
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline1 =
				create_path_polyline(path, closed);
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline2 =
				create_path_polyline(path, closed);
		GPlatesMaths::GeometryIntersect::Graph graph;
		if (!GPlatesMaths::GeometryIntersect::intersect(graph, *polyline1, *polyline2))
		{
			return false;
		}

		const unsigned int segment_count = polyline1->number_of_segments();
		const unsigned int last_segment = segment_count - 1;
		for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator
				intersection_iter = graph.unordered_intersections.begin();
				intersection_iter != graph.unordered_intersections.end(); ++intersection_iter)
		{
			// A polyline intersection at its final vertex can use the fictitious
			// one-past-the-last segment index. For a closed path that vertex is the
			// start of segment zero, so normalise it before testing adjacency.
			const unsigned int segment1 = closed && intersection_iter->segment_index1 == segment_count
					? 0 : intersection_iter->segment_index1;
			const unsigned int segment2 = closed && intersection_iter->segment_index2 == segment_count
					? 0 : intersection_iter->segment_index2;
			if (segment1 == segment2 ||
					segment1 + 1 == segment2 || segment2 + 1 == segment1 ||
					(closed && ((segment1 == 0 && segment2 == last_segment) ||
							(segment2 == 0 && segment1 == last_segment))))
			{
				continue;
			}
			return true;
		}
		return false;
	}


	bool
	paths_are_topologically_valid(
			const GeometryPaths &paths)
	{
		for (path_seq_type::const_iterator path_iter = paths.paths.begin();
				path_iter != paths.paths.end(); ++path_iter)
		{
			if (path_has_self_intersection(*path_iter, paths.polygon))
			{
				return false;
			}
		}

		if (!paths.polygon)
		{
			return true;
		}

		const GPlatesMaths::PolygonOnSphere::non_null_ptr_to_const_type exterior =
				GPlatesMaths::PolygonOnSphere::create(paths.paths.front(), true);
		for (unsigned int ring1 = 1; ring1 < paths.paths.size(); ++ring1)
		{
			if (!exterior->is_point_in_polygon(paths.paths[ring1].front()))
			{
				return false;
			}
			const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline1 =
					create_path_polyline(paths.paths[ring1], true);
			for (unsigned int ring2 = 0; ring2 < ring1; ++ring2)
			{
				const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type polyline2 =
						create_path_polyline(paths.paths[ring2], true);
				GPlatesMaths::GeometryIntersect::Graph graph;
				if (GPlatesMaths::GeometryIntersect::intersect(graph, *polyline1, *polyline2))
				{
					return false;
				}
			}
		}
		return true;
	}


	void
	append_distinct(
			point_seq_type &points,
			const GPlatesMaths::PointOnSphere &point,
			const NaturalizeGeometry::Parameters &parameters)
	{
		if (points.empty() || !NaturalizeGeometry::points_are_close(
				points.back(), point, parameters.coincidence_tolerance_km,
				parameters.planet_radius_km))
		{
			points.push_back(point);
		}
	}


	bool
	apply_replacements(
			GeometryPaths &geometry_paths,
			const replacement_seq_type &replacements,
			const NaturalizeGeometry::Parameters &parameters)
	{
		bool changed = false;
		for (path_seq_type::iterator path_iter = geometry_paths.paths.begin();
				path_iter != geometry_paths.paths.end(); ++path_iter)
		{
			const point_seq_type original = *path_iter;
			if (original.size() < 2)
			{
				continue;
			}

			const unsigned int segment_count = geometry_paths.polygon
					? static_cast<unsigned int>(original.size())
					: static_cast<unsigned int>(original.size() - 1);
			point_seq_type output;
			output.reserve(original.size());
			output.push_back(original.front());
			for (unsigned int segment_index = 0; segment_index < segment_count; ++segment_index)
			{
				const GPlatesMaths::PointOnSphere &start = original[segment_index];
				const GPlatesMaths::PointOnSphere &end =
						original[(segment_index + 1) % original.size()];
				const SegmentReplacement *matching_replacement = NULL;
				bool reversed = false;
				for (replacement_seq_type::const_iterator replacement_iter = replacements.begin();
						replacement_iter != replacements.end(); ++replacement_iter)
				{
					if (NaturalizeGeometry::segments_match(
							start, end, replacement_iter->start, replacement_iter->end,
							parameters.coincidence_tolerance_km, parameters.planet_radius_km,
							reversed))
					{
						matching_replacement = &*replacement_iter;
						break;
					}
				}

				if (!matching_replacement)
				{
					append_distinct(output, end, parameters);
					continue;
				}

				changed = true;
				// Snap both owners to the same canonical endpoints as well as the
				// same inserted points. This removes tiny pre-existing discrepancies
				// that were within the configured coincidence tolerance.
				output.back() = reversed
						? matching_replacement->points.back()
						: matching_replacement->points.front();
				if (reversed)
				{
					for (point_seq_type::const_reverse_iterator point_iter =
							matching_replacement->points.rbegin() + 1;
							point_iter != matching_replacement->points.rend(); ++point_iter)
					{
						append_distinct(output, *point_iter, parameters);
					}
				}
				else
				{
					for (point_seq_type::const_iterator point_iter =
							matching_replacement->points.begin() + 1;
							point_iter != matching_replacement->points.end(); ++point_iter)
					{
						append_distinct(output, *point_iter, parameters);
					}
				}
			}

			if (geometry_paths.polygon && output.size() > 1 &&
					NaturalizeGeometry::points_are_close(
						output.front(), output.back(), parameters.coincidence_tolerance_km,
						parameters.planet_radius_km))
			{
				output.pop_back();
			}
			*path_iter = output;
		}
		return changed;
	}


	bool
	segment_is_protected(
			const GPlatesMaths::PointOnSphere &start,
			const GPlatesMaths::PointOnSphere &end,
			const source_geometry_seq_type &cratons,
			double buffer_km,
			double planet_radius_km)
	{
		if (cratons.empty())
		{
			return false;
		}
		point_seq_type points;
		points.push_back(start);
		points.push_back(end);
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type segment =
				GPlatesMaths::PolylineOnSphere::create(points);
		const GPlatesMaths::AngularDistance threshold =
				GPlatesMaths::AngularDistance::create_from_angle(buffer_km / planet_radius_km);
		for (source_geometry_seq_type::const_iterator craton_iter = cratons.begin();
				craton_iter != cratons.end(); ++craton_iter)
		{
			if (!GPlatesMaths::minimum_distance(*segment, *craton_iter->geometry)
					.is_precisely_greater_than(threshold))
			{
				return true;
			}
		}
		return false;
	}


	bool
	geometry_has_partial_overlap(
			const GPlatesMaths::GeometryOnSphere &geometry,
			const replacement_seq_type &replacements,
			const NaturalizeGeometry::Parameters &parameters)
	{
		const boost::optional<GeometryPaths> paths = extract_paths(geometry);
		if (!paths)
		{
			return false;
		}
		for (path_seq_type::const_iterator path_iter = paths->paths.begin();
				path_iter != paths->paths.end(); ++path_iter)
		{
			const unsigned int segment_count = paths->polygon
					? static_cast<unsigned int>(path_iter->size())
					: static_cast<unsigned int>(path_iter->size() - 1);
			for (unsigned int segment_index = 0; segment_index < segment_count; ++segment_index)
			{
				const GPlatesMaths::PointOnSphere &start = (*path_iter)[segment_index];
				const GPlatesMaths::PointOnSphere &end =
						(*path_iter)[(segment_index + 1) % path_iter->size()];
				for (replacement_seq_type::const_iterator replacement_iter = replacements.begin();
						replacement_iter != replacements.end(); ++replacement_iter)
				{
					if (NaturalizeGeometry::segments_partially_overlap(
							start, end, replacement_iter->start, replacement_iter->end,
							parameters.coincidence_tolerance_km, parameters.planet_radius_km))
					{
						return true;
					}
				}
			}
		}
		return false;
	}


	GPlatesMaths::PointOnSphere
	reverse_reconstruct_point(
			const GPlatesMaths::PointOnSphere &point,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg)
	{
		if (!rfg.reconstruction_plate_id())
		{
			return point;
		}
		return GPlatesAppLogic::ReconstructUtils::reconstruct_by_plate_id(
				point, *rfg.reconstruction_plate_id(), *rfg.get_reconstruction_tree(), true);
	}


	GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type
	reverse_reconstruct_geometry(
			const GeometryPaths &paths,
			const GPlatesAppLogic::ReconstructedFeatureGeometry &rfg)
	{
		GeometryPaths present_day_paths(paths);
		for (path_seq_type::iterator path_iter = present_day_paths.paths.begin();
				path_iter != present_day_paths.paths.end(); ++path_iter)
		{
			for (point_seq_type::iterator point_iter = path_iter->begin();
					point_iter != path_iter->end(); ++point_iter)
			{
				*point_iter = reverse_reconstruct_point(*point_iter, rfg);
			}
		}
		return *create_geometry(present_day_paths);
	}


	void
	gather_source_geometries(
			source_geometry_seq_type &source_geometries,
			GPlatesAppLogic::ApplicationState &application_state,
			const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type &focused_rfg)
	{
		std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type> layer_outputs;
		application_state.get_current_reconstruction().get_active_layer_outputs<
				GPlatesAppLogic::ReconstructLayerProxy>(layer_outputs);
		for (std::vector<GPlatesAppLogic::ReconstructLayerProxy::non_null_ptr_type>::const_iterator
				layer_iter = layer_outputs.begin(); layer_iter != layer_outputs.end(); ++layer_iter)
		{
			std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type> rf_geometries;
			(*layer_iter)->get_reconstructed_feature_geometries(rf_geometries);
			for (std::vector<GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_type>::const_iterator
					rfg_iter = rf_geometries.begin(); rfg_iter != rf_geometries.end(); ++rfg_iter)
			{
				if (!(*rfg_iter)->is_valid() || !(*rfg_iter)->property().is_still_valid())
				{
					continue;
				}
				const GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type geometry =
						(*rfg_iter)->reconstructed_geometry();
				if (!extract_paths(*geometry))
				{
					continue;
				}

				bool duplicate = false;
				for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
						source_iter != source_geometries.end(); ++source_iter)
				{
					if (same_property(*source_iter->rfg, **rfg_iter))
					{
						duplicate = true;
						break;
					}
				}
				if (!duplicate)
				{
					source_geometries.push_back(SourceGeometry(*rfg_iter, geometry));
				}
			}
		}

		bool have_focused = false;
		for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
				source_iter != source_geometries.end(); ++source_iter)
		{
			if (same_property(*source_iter->rfg, *focused_rfg))
			{
				have_focused = true;
				break;
			}
		}
		if (!have_focused)
		{
			source_geometries.push_back(SourceGeometry(
					focused_rfg, focused_rfg->reconstructed_geometry()));
		}
	}


	Candidate
	build_candidate_once(
			const SourceGeometry &focused_source,
			const source_geometry_seq_type &source_geometries,
			const NaturalizeGeometry::Parameters &input_parameters,
			double amplitude_scale,
			double craton_buffer_km,
			double area_tolerance_percent)
	{
		Candidate candidate;
		candidate.amplitude_scale = amplitude_scale;
		NaturalizeGeometry::Parameters parameters(input_parameters);
		parameters.amplitude_percent *= amplitude_scale;

		source_geometry_seq_type cratons;
		for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
				source_iter != source_geometries.end(); ++source_iter)
		{
			if (source_iter->rfg->feature_handle_ptr() &&
					is_craton(*source_iter->rfg->feature_handle_ptr()))
			{
				cratons.push_back(*source_iter);
			}
		}

		const boost::optional<GeometryPaths> focused_paths = extract_paths(*focused_source.geometry);
		if (!focused_paths)
		{
			candidate.error = QObject::tr("The selected feature is not a polygon or polyline.");
			return candidate;
		}

		replacement_seq_type replacements;
		for (path_seq_type::const_iterator path_iter = focused_paths->paths.begin();
				path_iter != focused_paths->paths.end(); ++path_iter)
		{
			const unsigned int segment_count = focused_paths->polygon
					? static_cast<unsigned int>(path_iter->size())
					: static_cast<unsigned int>(path_iter->size() - 1);
			for (unsigned int segment_index = 0; segment_index < segment_count; ++segment_index)
			{
				const GPlatesMaths::PointOnSphere &start = (*path_iter)[segment_index];
				const GPlatesMaths::PointOnSphere &end =
						(*path_iter)[(segment_index + 1) % path_iter->size()];
				const NaturalizeGeometry::SegmentResult result =
						NaturalizeGeometry::naturalize_segment(start, end, parameters);
				if (result.inserted_point_count == 0)
				{
					continue;
				}
				if (segment_is_protected(start, end, cratons, craton_buffer_km,
						parameters.planet_radius_km))
				{
					++candidate.protected_segments;
					continue;
				}
				replacements.push_back(SegmentReplacement(start, end, result.points));
				candidate.inserted_points += result.inserted_point_count;
				++candidate.processed_segments;
				candidate.maximum_output_segment_km = std::max(
						candidate.maximum_output_segment_km, result.maximum_segment_length_km);
			}
		}

		if (replacements.empty())
		{
			candidate.error = candidate.protected_segments
					? QObject::tr("Every over-length segment is inside the protected craton buffer.")
					: QObject::tr("No segment exceeds the requested maximum length.");
			return candidate;
		}

		for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
				source_iter != source_geometries.end(); ++source_iter)
		{
			if (!same_property(*source_iter->rfg, *focused_source.rfg) &&
					geometry_has_partial_overlap(*source_iter->geometry, replacements, parameters))
			{
				candidate.error = QObject::tr(
						"A partially coincident section was found on feature %1. The operation was blocked "
						"because editing only one side would create a gap. Split the shared section at the "
						"same vertices first, then preview again.")
						.arg(source_iter->rfg->feature_handle_ptr()->feature_id().get().qstring());
				return candidate;
			}
		}

		std::set<GPlatesModel::FeatureHandle *> shared_feature_set;
		std::set<GPlatesModel::FeatureHandle *> semantic_feature_set;
		for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
				source_iter != source_geometries.end(); ++source_iter)
		{
			GeometryPaths changed_paths = *extract_paths(*source_iter->geometry);
			if (!apply_replacements(changed_paths, replacements, parameters))
			{
				continue;
			}
			if (!paths_are_topologically_valid(changed_paths))
			{
				candidate.error = QObject::tr(
						"The preview would introduce a self-intersection, invalid hole, or overlapping ring. "
						"Reduce amplitude or increase wavelength.");
				return candidate;
			}

			if (is_craton(*source_iter->rfg->feature_handle_ptr()))
			{
				candidate.error = QObject::tr(
						"A changed section is owned by a craton feature. Increase the protected buffer or "
						"align the shared vertices before retrying.");
				return candidate;
			}

			const boost::optional<GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type>
					changed_geometry = create_geometry(changed_paths);
			if (!changed_geometry)
			{
				candidate.error = QObject::tr(
						"The requested parameters produce an invalid or collapsed geometry.");
				return candidate;
			}

			if (const GPlatesMaths::PolygonOnSphere *old_polygon =
					dynamic_cast<const GPlatesMaths::PolygonOnSphere *>(source_iter->geometry.get()))
			{
				const GPlatesMaths::PolygonOnSphere *new_polygon =
						dynamic_cast<const GPlatesMaths::PolygonOnSphere *>((*changed_geometry).get());
				if (GPlatesMaths::PolygonOrientation::calculate_polygon_exterior_ring_orientation(
						*old_polygon) !=
					GPlatesMaths::PolygonOrientation::calculate_polygon_exterior_ring_orientation(
						*new_polygon))
				{
					candidate.error = QObject::tr(
							"The preview would reverse polygon winding. Reduce amplitude or increase wavelength.");
					return candidate;
				}
				const double old_area = old_polygon->get_area().dval();
				const double new_area = new_polygon->get_area().dval();
				const double area_change = old_area > 0.0
						? 100.0 * std::fabs(new_area - old_area) / old_area : 0.0;
				candidate.maximum_area_change_percent = std::max(
						candidate.maximum_area_change_percent, area_change);
				if (area_change > area_tolerance_percent)
				{
					candidate.error = QObject::tr("area tolerance exceeded");
					return candidate;
				}
			}

			GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type present_day_geometry =
					reverse_reconstruct_geometry(changed_paths, *source_iter->rfg);
			CandidateChange change =
			{
				source_iter->rfg->get_feature_ref(),
				source_iter->rfg->property(),
				present_day_geometry,
				*changed_geometry,
				source_iter->rfg->feature_handle_ptr()->feature_id().get().qstring()
			};
			candidate.changes.push_back(change);

			if (!same_property(*source_iter->rfg, *focused_source.rfg))
			{
				shared_feature_set.insert(source_iter->rfg->feature_handle_ptr());
				if (is_semantically_important_boundary(*source_iter->rfg->feature_handle_ptr()))
				{
					semantic_feature_set.insert(source_iter->rfg->feature_handle_ptr());
				}
			}
		}

		candidate.shared_features = static_cast<unsigned int>(shared_feature_set.size());
		candidate.semantic_features = static_cast<unsigned int>(semantic_feature_set.size());
		candidate.success = !candidate.changes.empty();
		return candidate;
	}


	Candidate
	build_candidate(
			const SourceGeometry &focused_source,
			const source_geometry_seq_type &source_geometries,
			const NaturalizeGeometry::Parameters &parameters,
			double craton_buffer_km,
			double area_tolerance_percent)
	{
		double amplitude_scale = 1.0;
		Candidate candidate;
		for (unsigned int attempt = 0; attempt < 10; ++attempt)
		{
			candidate = build_candidate_once(
					focused_source, source_geometries, parameters, amplitude_scale,
					craton_buffer_km, area_tolerance_percent);
			if (candidate.success || candidate.error != QObject::tr("area tolerance exceeded"))
			{
				return candidate;
			}
			amplitude_scale *= 0.7;
		}
		candidate.error = QObject::tr(
				"The area correction pass could not satisfy the requested tolerance. "
				"Reduce amplitude or increase area tolerance.");
		return candidate;
	}


	class NaturalizeCoastlineUndoCommand :
			public QUndoCommand
	{
	public:
		struct Edit
		{
			GPlatesModel::FeatureHandle::weak_ref feature;
			GPlatesModel::FeatureHandle::iterator property;
			GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type before_geometry;
			GPlatesMaths::GeometryOnSphere::non_null_ptr_to_const_type after_geometry;
		};

		NaturalizeCoastlineUndoCommand(
				GPlatesModel::ModelInterface model_interface,
				const candidate_change_seq_type &changes,
				const NaturalizeGeometry::Parameters &parameters) :
			QUndoCommand(0),
			d_model_interface(model_interface)
		{
			for (candidate_change_seq_type::const_iterator change_iter = changes.begin();
					change_iter != changes.end(); ++change_iter)
			{
				Edit edit =
				{
					change_iter->feature,
					change_iter->property,
					GPlatesFeatureVisitors::find_first_geometry(change_iter->property),
					change_iter->present_day_geometry
				};
				d_edits.push_back(edit);
			}
			setText(QObject::tr(
					"naturalize coastline — %1 features; seed %2; max %3 km; amplitude %4%; wavelength %5 km; smoothing %6")
					.arg(d_edits.size()).arg(parameters.random_seed)
					.arg(parameters.maximum_segment_length_km)
					.arg(parameters.amplitude_percent)
					.arg(parameters.wavelength_km).arg(parameters.smoothing_passes));
		}

		virtual void redo() { apply(true); }
		virtual void undo() { apply(false); }

	private:
		void apply(bool use_after)
		{
			// Reconstruction can update several rendered layers while the model
			// notification guard is released. Coalesce those updates so observers
			// never see a partially-restored set of shared boundaries.
			GPlatesViewOperations::RenderedGeometryCollection::UpdateGuard update_guard;
			GPlatesModel::NotificationGuard notification_guard(*d_model_interface.access_model());
			for (std::vector<Edit>::const_iterator edit_iter = d_edits.begin();
					edit_iter != d_edits.end(); ++edit_iter)
			{
				if (edit_iter->feature.is_valid() && edit_iter->property.is_still_valid())
				{
					// Create a fresh, model-valid property wrapper for every transition.
					// Reusing a detached TopLevelProperty clone across redo/undo leaves
					// reconstruction visiting released property-value revision state.
					GPlatesModel::TopLevelProperty::non_null_ptr_type property_clone =
							(*edit_iter->property)->clone();
					GPlatesFeatureVisitors::GeometrySetter geometry_setter(
							use_after ? edit_iter->after_geometry : edit_iter->before_geometry);
					geometry_setter.set_geometry(property_clone.get());
					edit_iter->feature->set(
							edit_iter->property, property_clone);
				}
			}
			notification_guard.release_guard();
		}

		GPlatesModel::ModelInterface d_model_interface;
		std::vector<Edit> d_edits;
	};
}


GPlatesViewOperations::NaturalizeCoastlineOperation::NaturalizeCoastlineOperation(
		GPlatesGui::FeatureFocus &feature_focus,
		GPlatesAppLogic::ApplicationState &application_state,
		RenderedGeometryCollection &rendered_geometry_collection) :
	d_feature_focus(feature_focus),
	d_application_state(application_state),
	d_rendered_geometry_collection(rendered_geometry_collection),
	d_model_interface(application_state.get_model_interface())
{  }


GPlatesViewOperations::NaturalizeCoastlineOperation::Result
GPlatesViewOperations::NaturalizeCoastlineOperation::trigger(
		QWidget *parent_widget)
{
	if (!d_feature_focus.focused_feature().is_valid() ||
			!d_feature_focus.associated_reconstruction_geometry())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Select a coastline polygon or polyline first."));
	}

	boost::optional<const GPlatesAppLogic::ReconstructedFeatureGeometry *> focused_rfg_ptr =
			GPlatesAppLogic::ReconstructionGeometryUtils::get_reconstruction_geometry_derived_type<
					const GPlatesAppLogic::ReconstructedFeatureGeometry *>(
						d_feature_focus.associated_reconstruction_geometry());
	if (!focused_rfg_ptr || !extract_paths(*(*focused_rfg_ptr)->reconstructed_geometry()))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Naturalize Coastline supports reconstructed polygon and polyline features."));
	}
	if (!(*focused_rfg_ptr)->property().is_still_valid())
	{
		return Result(OPERATION_ERROR,
				QObject::tr("The selected geometry is not backed by an editable property."));
	}
	if (is_craton(*(*focused_rfg_ptr)->feature_handle_ptr()))
	{
		return Result(OPERATION_ERROR,
				QObject::tr("Craton features are protected and cannot be naturalized."));
	}

	const GPlatesAppLogic::ReconstructedFeatureGeometry::non_null_ptr_to_const_type focused_rfg =
			(*focused_rfg_ptr)->get_non_null_pointer_to_const();
	source_geometry_seq_type source_geometries;
	gather_source_geometries(source_geometries, d_application_state, focused_rfg);

	SourceGeometry focused_source(focused_rfg, focused_rfg->reconstructed_geometry());
	for (source_geometry_seq_type::const_iterator source_iter = source_geometries.begin();
			source_iter != source_geometries.end(); ++source_iter)
	{
		if (same_property(*source_iter->rfg, *focused_rfg))
		{
			focused_source = *source_iter;
			break;
		}
	}

	QDialog dialog(parent_widget);
	dialog.setWindowTitle(QObject::tr("Naturalize Coastline"));
	dialog.setModal(true);
	QVBoxLayout *dialog_layout = new QVBoxLayout(&dialog);

	QLabel *description = new QLabel(QObject::tr(
			"Roughen long sections on the sphere. Preview scans every reconstructed object and "
			"propagates identical points to complete coincident sections."), &dialog);
	description->setWordWrap(true);
	dialog_layout->addWidget(description);

	QGroupBox *controls_group = new QGroupBox(QObject::tr("Parameters"), &dialog);
	QFormLayout *form = new QFormLayout(controls_group);
	QDoubleSpinBox *maximum_length = new QDoubleSpinBox(controls_group);
	maximum_length->setRange(1.0, 10000.0);
	maximum_length->setDecimals(1);
	maximum_length->setValue(100.0);
	maximum_length->setSuffix(QObject::tr(" km"));
	form->addRow(QObject::tr("Maximum straight segment:"), maximum_length);

	QDoubleSpinBox *amplitude = new QDoubleSpinBox(controls_group);
	amplitude->setRange(0.0, 100.0);
	amplitude->setDecimals(1);
	amplitude->setValue(2.0);
	amplitude->setSuffix(QObject::tr(" %"));
	amplitude->setToolTip(QObject::tr(
			"Enhanced swiggle control: 1% allows up to 10% of the segment-length limit "
			"as sideways displacement."));
	form->addRow(QObject::tr("Swiggle amplitude:"), amplitude);

	QDoubleSpinBox *wavelength = new QDoubleSpinBox(controls_group);
	wavelength->setRange(1.0, 20000.0);
	wavelength->setDecimals(1);
	wavelength->setValue(400.0);
	wavelength->setSuffix(QObject::tr(" km"));
	form->addRow(QObject::tr("Noise wavelength:"), wavelength);

	QSpinBox *smoothing = new QSpinBox(controls_group);
	smoothing->setRange(0, 20);
	smoothing->setValue(2);
	form->addRow(QObject::tr("Smoothing passes:"), smoothing);

	QWidget *seed_widget = new QWidget(controls_group);
	QHBoxLayout *seed_layout = new QHBoxLayout(seed_widget);
	seed_layout->setContentsMargins(0, 0, 0, 0);
	QSpinBox *seed = new QSpinBox(seed_widget);
	seed->setRange(1, std::numeric_limits<int>::max());
	seed->setValue(static_cast<int>(QRandomGenerator::global()->generate() & 0x7fffffff));
	QPushButton *randomize_seed = new QPushButton(QObject::tr("Regenerate seed"), seed_widget);
	seed_layout->addWidget(seed);
	seed_layout->addWidget(randomize_seed);
	form->addRow(QObject::tr("Random seed:"), seed_widget);

	QDoubleSpinBox *craton_buffer = new QDoubleSpinBox(controls_group);
	craton_buffer->setRange(0.0, 5000.0);
	craton_buffer->setDecimals(1);
	craton_buffer->setValue(25.0);
	craton_buffer->setSuffix(QObject::tr(" km"));
	form->addRow(QObject::tr("Protected craton buffer:"), craton_buffer);

	QDoubleSpinBox *area_tolerance = new QDoubleSpinBox(controls_group);
	area_tolerance->setRange(0.001, 25.0);
	area_tolerance->setDecimals(3);
	area_tolerance->setValue(1.0);
	area_tolerance->setSuffix(QObject::tr(" %"));
	form->addRow(QObject::tr("Maximum polygon area change:"), area_tolerance);
	dialog_layout->addWidget(controls_group);

	QLabel *preview_status = new QLabel(QObject::tr("Choose Preview to calculate a safe edit."), &dialog);
	preview_status->setWordWrap(true);
	preview_status->setMinimumWidth(520);
	dialog_layout->addWidget(preview_status);

	QDialogButtonBox *buttons = new QDialogButtonBox(
			QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QPushButton *preview_button = buttons->addButton(
			QObject::tr("Preview / Regenerate"), QDialogButtonBox::ActionRole);
	QPushButton *apply_button = buttons->button(QDialogButtonBox::Ok);
	apply_button->setText(QObject::tr("Accept"));
	apply_button->setEnabled(false);
	dialog_layout->addWidget(buttons);

	std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type> preview_layers;
	preview_layers.push_back(
			d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
					RenderedGeometryCollection::RECONSTRUCTION_LAYER));
	preview_layers.push_back(
			d_rendered_geometry_collection.create_child_rendered_layer_and_transfer_ownership(
					RenderedGeometryCollection::FEATURE_INSPECTION_CANVAS_TOOL_WORKFLOW_LAYER));
	for (std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type>::const_iterator
			layer_iter = preview_layers.begin(); layer_iter != preview_layers.end(); ++layer_iter)
	{
		(*layer_iter)->set_active(true);
	}
	Candidate candidate;
	NaturalizeGeometry::Parameters accepted_parameters;

	auto regenerate_preview = [&]()
	{
		for (std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type>::const_iterator
				layer_iter = preview_layers.begin(); layer_iter != preview_layers.end(); ++layer_iter)
		{
			(*layer_iter)->clear_rendered_geometries();
		}
		NaturalizeGeometry::Parameters parameters;
		parameters.maximum_segment_length_km = maximum_length->value();
		parameters.amplitude_percent = amplitude->value();
		parameters.wavelength_km = wavelength->value();
		parameters.smoothing_passes = static_cast<unsigned int>(smoothing->value());
		parameters.random_seed = static_cast<boost::uint32_t>(seed->value());
		parameters.planet_radius_km = GPlatesUtils::Earth::MEAN_RADIUS_KMS;
		parameters.coincidence_tolerance_km = 0.01;

		candidate = build_candidate(
				focused_source, source_geometries, parameters,
				craton_buffer->value(), area_tolerance->value());
		accepted_parameters = parameters;
		apply_button->setEnabled(candidate.success);
		if (!candidate.success)
		{
			preview_status->setText(QObject::tr("Preview blocked: %1").arg(candidate.error));
			return;
		}

		for (candidate_change_seq_type::const_iterator change_iter = candidate.changes.begin();
				change_iter != candidate.changes.end(); ++change_iter)
		{
			const RenderedGeometry preview_rendered_geometry =
					RenderedGeometryFactory::create_rendered_geometry_on_sphere(
							change_iter->preview_geometry,
							GPlatesGui::Colour::get_aqua(), 5.0f, 3.0f);
			for (std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type>::const_iterator
					layer_iter = preview_layers.begin(); layer_iter != preview_layers.end(); ++layer_iter)
			{
				(*layer_iter)->add_rendered_geometry(preview_rendered_geometry);
			}
		}

		QString correction;
		if (candidate.amplitude_scale < 0.999)
		{
			correction = QObject::tr(" Area correction reduced amplitude to %1% of the requested value.")
					.arg(candidate.amplitude_scale * 100.0, 0, 'f', 1);
		}
		QString semantic_warning;
		if (candidate.semantic_features)
		{
			semantic_warning = QObject::tr(
					" Warning: %1 shared tectonic/semantic boundary feature(s) will also change.")
					.arg(candidate.semantic_features);
		}
		preview_status->setText(QObject::tr(
				"Aqua preview ready (Accept commits it): %1 feature(s), %2 shared owner(s), "
				"%3 processed section(s), %4 inserted points; "
				"max output segment %5 km; max area change %6%; %7 protected section(s) skipped.%8%9")
				.arg(candidate.changes.size()).arg(candidate.shared_features)
				.arg(candidate.processed_segments).arg(candidate.inserted_points)
				.arg(candidate.maximum_output_segment_km, 0, 'f', 3)
				.arg(candidate.maximum_area_change_percent, 0, 'f', 4)
				.arg(candidate.protected_segments).arg(correction).arg(semantic_warning));
	};

	QObject::connect(preview_button, &QPushButton::clicked, regenerate_preview);
	QObject::connect(randomize_seed, &QPushButton::clicked, [&]()
	{
		seed->setValue(static_cast<int>(QRandomGenerator::global()->generate() & 0x7fffffff));
		regenerate_preview();
	});
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	QObject::connect(buttons, &QDialogButtonBox::accepted, [&]()
	{
		if (candidate.semantic_features)
		{
			const QMessageBox::StandardButton confirmation = QMessageBox::warning(
					&dialog, QObject::tr("Shared tectonic boundaries"),
					QObject::tr(
							"%1 tectonic or semantically important shared feature(s) will receive the same "
							"coordinates. Continue?").arg(candidate.semantic_features),
					QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
			if (confirmation != QMessageBox::Yes)
			{
				return;
			}
		}
		dialog.accept();
	});

	dialog.adjustSize();
	if (parent_widget)
	{
		const QPoint parent_top_left = parent_widget->mapToGlobal(QPoint(0, 0));
		dialog.move(
				parent_top_left.x() + parent_widget->width() - dialog.width() - 16,
				parent_top_left.y() + 48);
	}

	regenerate_preview();
	if (dialog.exec() != QDialog::Accepted)
	{
		for (std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type>::const_iterator
				layer_iter = preview_layers.begin(); layer_iter != preview_layers.end(); ++layer_iter)
		{
			(*layer_iter)->clear_rendered_geometries();
		}
		return Result(NATURALIZE_CANCELLED, QObject::tr("Naturalize Coastline cancelled; no data changed."));
	}
	for (std::vector<RenderedGeometryCollection::child_layer_owner_ptr_type>::const_iterator
			layer_iter = preview_layers.begin(); layer_iter != preview_layers.end(); ++layer_iter)
	{
		(*layer_iter)->clear_rendered_geometries();
	}

	// Focus changes clear GPlates' active undo stack. Do this before QUndoStack::push()
	// (which calls redo immediately), never from inside the command itself; otherwise
	// an undo can re-entrantly delete the command that Qt is still executing.
	d_feature_focus.unset_focus();
	std::unique_ptr<QUndoCommand> command(new NaturalizeCoastlineUndoCommand(
			d_model_interface, candidate.changes, accepted_parameters));
	UndoRedo::instance().get_active_undo_stack().push(command.release());

	return Result(NATURALIZE_COMPLETED,
			QObject::tr("Naturalized %1 feature(s) with seed %2; undo restores every original property exactly.")
					.arg(candidate.changes.size()).arg(accepted_parameters.random_seed));
}
