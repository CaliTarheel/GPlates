/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_VIEWOPERATIONS_WORLDBUILDINGAUDITREPORT_H
#define GPLATES_VIEWOPERATIONS_WORLDBUILDINGAUDITREPORT_H

#include <vector>
#include <boost/optional.hpp>
#include <QString>

namespace GPlatesAppLogic { class FeatureCollectionFileState; }

namespace GPlatesViewOperations
{
	/** Read-only multi-domain audit, ocean-crust ledger and portable report. */
	class WorldbuildingAuditReport
	{
	public:
		enum Scope { CURRENT_TIME, INTERVAL, ALL_TIMES };
		struct Request
		{
			Request();
			Scope scope;
			double current_time;
			double older_time;
			double old_crust_threshold_ma;
			QString project_revision;
		};
		struct Finding
		{
			QString severity;
			QString domain;
			QString code;
			QString feature_id;
			boost::optional<double> time;
			QString message;
			QString suggested_repair;
		};
		struct CrustRecord
		{
			QString feature_id;
			double created_time;
			boost::optional<double> retired_time;
			QString status;
			double age_at_current_time;
			bool old_crust_advisory;
		};
		struct Report
		{
			Request request;
			std::vector<Finding> findings;
			std::vector<CrustRecord> crust;
			QString to_markdown(const std::vector<int> &repair_queue = std::vector<int>()) const;
			QString to_json(const std::vector<int> &repair_queue = std::vector<int>()) const;
		};
		static Report build(GPlatesAppLogic::FeatureCollectionFileState &file_state, const Request &request);
		static QString scope_name(Scope scope);
	};
}
#endif
