/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "BoundarySectionGraph.h"

#include <algorithm>
#include <cmath>

#include "maths/GeometryIntersect.h"

namespace
{
	double distance_degrees(
			const GPlatesMaths::PointOnSphere &first,
			const GPlatesMaths::PointOnSphere &second)
	{
		const double cosine = std::max(-1.0, std::min(1.0,
				GPlatesMaths::dot(first.position_vector(), second.position_vector()).dval()));
		return std::acos(cosine) * 180.0 / 3.14159265358979323846;
	}

	bool close(const GPlatesMaths::PointOnSphere &first,
			const GPlatesMaths::PointOnSphere &second, double tolerance)
	{
		return distance_degrees(first, second) <= tolerance;
	}

	std::pair<GPlatesModel::integer_plate_id_type, GPlatesModel::integer_plate_id_type>
	plate_pair(const GPlatesViewOperations::BoundarySectionGraph::Section &section)
	{
		return std::minmax(section.left_plate, section.right_plate);
	}

	void add_issue(
			std::vector<GPlatesViewOperations::BoundarySectionGraph::Issue> &issues,
			GPlatesViewOperations::BoundarySectionGraph::IssueType type,
			int first, int second, const QString &summary, const QString &fix)
	{
		GPlatesViewOperations::BoundarySectionGraph::Issue issue;
		issue.type = type;
		issue.first_section = first;
		issue.second_section = second;
		issue.summary = summary;
		issue.suggested_fix = fix;
		issues.push_back(issue);
	}
}

GPlatesViewOperations::BoundarySectionGraph::Section::Section(
		const QString &feature_id_, const QString &feature_type_,
		GPlatesModel::integer_plate_id_type left_plate_,
		GPlatesModel::integer_plate_id_type right_plate_,
		const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &geometry_) :
	feature_id(feature_id_), feature_type(feature_type_), left_plate(left_plate_),
	right_plate(right_plate_), geometry(geometry_)
{  }

std::vector<GPlatesViewOperations::BoundarySectionGraph::Issue>
GPlatesViewOperations::BoundarySectionGraph::analyse(
		const std::vector<Section> &sections, double reconstruction_time,
		double tolerance, bool require_closed_network)
{
	std::vector<Issue> issues;
	for (size_t index = 0; index < sections.size(); ++index)
	{
		const Section &section = sections[index];
		if ((section.valid_time_older && reconstruction_time > section.valid_time_older.get()) ||
				(section.valid_time_younger && reconstruction_time < section.valid_time_younger.get()))
			add_issue(issues, STALE_SECTION, static_cast<int>(index), -1,
					QString("%1 is outside its valid-time interval at %2 Ma.").arg(section.feature_id).arg(reconstruction_time),
					"Create or select the time-slice successor; do not stretch the old section silently.");
		if (section.source_feature_id.isEmpty() && section.successor_feature_id.isEmpty())
			add_issue(issues, MISSING_LINEAGE, static_cast<int>(index), -1,
					QString("%1 has no recorded source or successor.").arg(section.feature_id),
					"Record source/successor Feature IDs when a reviewed section is created.");

		for (size_t other_index = index + 1; other_index < sections.size(); ++other_index)
		{
			const Section &other = sections[other_index];
			const bool same = close(section.geometry->start_point(), other.geometry->start_point(), tolerance) &&
					close(section.geometry->end_point(), other.geometry->end_point(), tolerance);
			const bool opposite = close(section.geometry->start_point(), other.geometry->end_point(), tolerance) &&
					close(section.geometry->end_point(), other.geometry->start_point(), tolerance);
			if (same || opposite)
				add_issue(issues, DUPLICATE, static_cast<int>(index), static_cast<int>(other_index),
						QString("%1 and %2 have duplicate endpoints.").arg(section.feature_id, other.feature_id),
						"Preview removing the duplicate while retaining reviewed lineage.");

			GPlatesMaths::GeometryIntersect::Graph intersections;
			if (GPlatesMaths::GeometryIntersect::intersect(intersections, *section.geometry, *other.geometry))
			{
				bool crosses = false;
				for (GPlatesMaths::GeometryIntersect::intersection_seq_type::const_iterator intersection =
						intersections.unordered_intersections.begin();
						intersection != intersections.unordered_intersections.end(); ++intersection)
					crosses = crosses || intersection->type == GPlatesMaths::GeometryIntersect::Intersection::SEGMENTS_CROSS;
				if (crosses)
					add_issue(issues, CROSSING, static_cast<int>(index), static_cast<int>(other_index),
							QString("%1 crosses %2 away from a shared endpoint.").arg(section.feature_id, other.feature_id),
							"Preview a split/trim and review section order before applying.");
			}
		}
	}

	for (size_t index = 1; index < sections.size(); ++index)
	{
		const Section &previous = sections[index - 1];
		const Section &current = sections[index];
		if (!close(previous.geometry->end_point(), current.geometry->start_point(), tolerance))
		{
			if (close(previous.geometry->end_point(), current.geometry->end_point(), tolerance))
				add_issue(issues, REVERSED, static_cast<int>(index - 1), static_cast<int>(index),
						QString("%1 appears reversed after %2.").arg(current.feature_id, previous.feature_id),
						"Preview reversing the section while retaining plate-side semantics.");
			else
			{
				const bool ridge_pair = previous.feature_type.contains(
						QString::fromLatin1("MidOceanRidge"), Qt::CaseInsensitive) &&
						current.feature_type.contains(QString::fromLatin1("MidOceanRidge"), Qt::CaseInsensitive);
				add_issue(issues, ridge_pair ? POSSIBLE_MISSING_TRANSFORM : GAP,
						static_cast<int>(index - 1), static_cast<int>(index),
						QString("Gap of %1 degrees between %2 and %3.%4")
								.arg(distance_degrees(previous.geometry->end_point(), current.geometry->start_point()), 0, 'f', 4)
								.arg(previous.feature_id, current.feature_id)
								.arg(ridge_pair ? QString::fromLatin1(" Two MOR sections may need a transform handoff.") : QString()),
						ridge_pair
								? "Review the ordered endpoints and create a Transform section in Topology Tools; do not bridge the gap with a long MOR."
								: "Preview snapping endpoints or adding a reviewed connector.");
			}
		}
		if (plate_pair(previous) != plate_pair(current))
			add_issue(issues, PLATE_PAIR_MISMATCH, static_cast<int>(index - 1), static_cast<int>(index),
					QString("Plate pair changes from %1/%2 to %3/%4.")
							.arg(previous.left_plate).arg(previous.right_plate)
							.arg(current.left_plate).arg(current.right_plate),
					"Confirm a junction or correct the left/right plate assignment.");
		if (!previous.polarity.isEmpty() && !current.polarity.isEmpty() && previous.polarity != current.polarity)
			add_issue(issues, POLARITY_MISMATCH, static_cast<int>(index - 1), static_cast<int>(index),
					QString("Subduction polarity changes between %1 and %2.").arg(previous.feature_id, current.feature_id),
					"Confirm a polarity-reversal event or align the section metadata.");
	}

	if (require_closed_network && sections.size() > 1 &&
			!close(sections.back().geometry->end_point(), sections.front().geometry->start_point(), tolerance))
		add_issue(issues, UNCLOSED_NETWORK, static_cast<int>(sections.size() - 1), 0,
				"The final endpoint does not close onto the first section.",
				"Preview a closing connector; live topology remains unchanged until reviewed.");
	return issues;
}

std::vector<GPlatesViewOperations::BoundarySectionGraph::SuccessorPlan>
GPlatesViewOperations::BoundarySectionGraph::plan_successors(
		const std::vector<Section> &sections, double event_time)
{
	std::vector<SuccessorPlan> plans;
	for (std::vector<Section>::const_iterator section = sections.begin(); section != sections.end(); ++section)
	{
		SuccessorPlan plan;
		plan.source_feature_id = section->feature_id;
		plan.proposed_feature_id = QString("%1@%2Ma-successor").arg(section->feature_id).arg(event_time, 0, 'g', 12);
		plan.event_time = event_time;
		plan.action = "End source at the event, clone reviewed properties/geometry, then start successor.";
		plans.push_back(plan);
	}
	return plans;
}

QString GPlatesViewOperations::BoundarySectionGraph::issue_name(IssueType type)
{
	switch (type)
	{
	case GAP: return "gap"; case POSSIBLE_MISSING_TRANSFORM: return "possible missing transform";
	case CROSSING: return "crossing"; case DUPLICATE: return "duplicate";
	case REVERSED: return "reversed section"; case PLATE_PAIR_MISMATCH: return "plate-pair mismatch";
	case POLARITY_MISMATCH: return "polarity mismatch"; case MISSING_LINEAGE: return "missing lineage";
	case STALE_SECTION: return "stale section"; case UNCLOSED_NETWORK: return "unclosed network";
	}
	return "unknown";
}
