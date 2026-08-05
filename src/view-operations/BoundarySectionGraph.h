/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_BOUNDARYSECTIONGRAPH_H
#define GPLATES_VIEWOPERATIONS_BOUNDARYSECTIONGRAPH_H

#include <vector>
#include <boost/optional.hpp>
#include <QString>
#include "maths/PolylineOnSphere.h"
#include "model/types.h"

namespace GPlatesViewOperations
{
	/** Read-only audit and succession planner for ordered boundary sections. */
	class BoundarySectionGraph
	{
	public:
		enum IssueType
		{
			GAP, CROSSING, DUPLICATE, REVERSED, PLATE_PAIR_MISMATCH,
			POLARITY_MISMATCH, MISSING_LINEAGE, STALE_SECTION, UNCLOSED_NETWORK
		};

		struct Section
		{
			Section(
					const QString &feature_id_, const QString &feature_type_,
					GPlatesModel::integer_plate_id_type left_plate_,
					GPlatesModel::integer_plate_id_type right_plate_,
					const GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type &geometry_);
			QString feature_id;
			QString feature_type;
			GPlatesModel::integer_plate_id_type left_plate;
			GPlatesModel::integer_plate_id_type right_plate;
			QString polarity;
			QString source_feature_id;
			QString successor_feature_id;
			boost::optional<double> valid_time_older;
			boost::optional<double> valid_time_younger;
			GPlatesMaths::PolylineOnSphere::non_null_ptr_to_const_type geometry;
		};

		struct Issue
		{
			IssueType type;
			int first_section;
			int second_section;
			QString summary;
			QString suggested_fix;
		};

		struct SuccessorPlan
		{
			QString source_feature_id;
			QString proposed_feature_id;
			double event_time;
			QString action;
		};

		static std::vector<Issue> analyse(
				const std::vector<Section> &ordered_sections,
				double reconstruction_time,
				double endpoint_tolerance_degrees,
				bool require_closed_network);
		static std::vector<SuccessorPlan> plan_successors(
				const std::vector<Section> &ordered_sections, double event_time);
		static QString issue_name(IssueType type);
	};
}
#endif
