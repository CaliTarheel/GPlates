/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "WorldbuildingAuditReport.h"

#include <algorithm>
#include <set>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "app-logic/FeatureCollectionFileState.h"
#include "feature-visitors/PropertyValueFinder.h"
#include "model/FeatureCollectionHandle.h"
#include "model/PropertyName.h"
#include "property-values/GmlTimePeriod.h"
#include "view-operations/FeatureEventVersioner.h"

namespace
{
	bool in_scope(double created, const boost::optional<double> &retired,
			const GPlatesViewOperations::WorldbuildingAuditReport::Request &request)
	{
		if (request.scope == GPlatesViewOperations::WorldbuildingAuditReport::ALL_TIMES) return true;
		const double younger = request.scope == GPlatesViewOperations::WorldbuildingAuditReport::CURRENT_TIME
				? request.current_time : std::min(request.current_time, request.older_time);
		const double older = request.scope == GPlatesViewOperations::WorldbuildingAuditReport::CURRENT_TIME
				? request.current_time : std::max(request.current_time, request.older_time);
		return created + 1e-9 >= younger && (!retired || retired.get() - 1e-9 <= older);
	}

	void add_finding(std::vector<GPlatesViewOperations::WorldbuildingAuditReport::Finding> &findings,
			const QString &severity, const QString &domain, const QString &code,
			const QString &feature_id, const boost::optional<double> &time,
			const QString &message, const QString &repair)
	{
		GPlatesViewOperations::WorldbuildingAuditReport::Finding finding;
		finding.severity = severity; finding.domain = domain; finding.code = code;
		finding.feature_id = feature_id; finding.time = time; finding.message = message;
		finding.suggested_repair = repair; findings.push_back(finding);
	}
}

GPlatesViewOperations::WorldbuildingAuditReport::Request::Request() :
	scope(CURRENT_TIME), current_time(0), older_time(0), old_crust_threshold_ma(200),
	project_revision("unrecorded")
{  }

GPlatesViewOperations::WorldbuildingAuditReport::Report
GPlatesViewOperations::WorldbuildingAuditReport::build(
		GPlatesAppLogic::FeatureCollectionFileState &file_state, const Request &request)
{
	Report report;
	report.request = request;
	std::set<QString> ids;
	const std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference> files =
			file_state.get_loaded_files();
	for (std::vector<GPlatesAppLogic::FeatureCollectionFileState::file_reference>::const_iterator
			file = files.begin(); file != files.end(); ++file)
	{
		const GPlatesModel::FeatureCollectionHandle::weak_ref collection =
				file->get_file().get_feature_collection();
		if (!collection.is_valid()) continue;
		for (GPlatesModel::FeatureCollectionHandle::iterator feature = collection->begin();
				feature != collection->end(); ++feature)
		{
			const QString id = (*feature)->feature_id().get().qstring();
			if (!ids.insert(id).second)
				add_finding(report.findings, "error", "feature", "duplicate-feature-id", id, boost::none,
						"Feature ID is duplicated across loaded collections.",
						"Promote for manual ID/lineage repair; do not auto-renumber.");
			boost::optional<double> created;
			boost::optional<double> retired;
			const boost::optional<GPlatesPropertyValues::GmlTimePeriod::non_null_ptr_to_const_type> valid_time =
					GPlatesFeatureVisitors::get_property_value<GPlatesPropertyValues::GmlTimePeriod>(
							(*feature)->reference(), GPlatesModel::PropertyName::create_gml("validTime"));
			if (valid_time)
			{
				if ((*valid_time)->begin()->get_time_position().is_real())
					created = (*valid_time)->begin()->get_time_position().value();
				if ((*valid_time)->end()->get_time_position().is_real())
					retired = (*valid_time)->end()->get_time_position().value();
				if (created && retired && created.get() + 1e-9 < retired.get())
					add_finding(report.findings, "error", "time", "reversed-valid-time", id, created,
							"Valid-time begin is younger than its end.",
							"Preview corrected older/younger bounds and create a reviewed feature version.");
			}

			const std::vector<FeatureEventVersioner::EventRecord> events =
					FeatureEventVersioner::events((*feature)->reference());
			const QString feature_type = (*feature)->feature_type().get_name().qstring();
			const bool boundary_or_topology = feature_type.contains("Ridge", Qt::CaseInsensitive) ||
					feature_type.contains("Transform", Qt::CaseInsensitive) ||
					feature_type.contains("Subduction", Qt::CaseInsensitive) ||
					feature_type.contains("Topolog", Qt::CaseInsensitive);
			if (boundary_or_topology && events.empty())
				add_finding(report.findings, "warning", "boundary/topology", "missing-lineage", id, request.current_time,
						"Boundary or topology has no Worldbuilding source/successor event record.",
						"Promote for lineage review in the existing boundary/topology tools.");

			if (feature_type == GPlatesModel::FeatureType::create_gpml("OceanicCrust").get_name().qstring() && created &&
					in_scope(created.get(), retired, request))
			{
				CrustRecord record;
				record.feature_id = id;
				record.created_time = created.get();
				record.retired_time = retired;
				record.age_at_current_time = std::max(0.0, created.get() - request.current_time);
				record.old_crust_advisory = record.age_at_current_time > request.old_crust_threshold_ma;
				if (request.current_time > created.get() + 1e-9) record.status = "not yet created";
				else if (retired && request.current_time < retired.get() - 1e-9) record.status = "retired";
				else record.status = "surviving";
				report.crust.push_back(record);
				if (record.old_crust_advisory && record.status == "surviving")
					add_finding(report.findings, "advisory", "crust", "old-ocean-crust", id, request.current_time,
							QString("Surviving ocean crust is %1 Ma old, above the editable %2 Ma advisory threshold.")
									.arg(record.age_at_current_time, 0, 'f', 1).arg(request.old_crust_threshold_ma, 0, 'f', 1),
							"Review provenance and nearby retirement history; do not delete automatically.");
			}
		}
	}
	std::stable_sort(report.crust.begin(), report.crust.end(), [](const CrustRecord &a, const CrustRecord &b)
	{
		return a.created_time > b.created_time;
	});
	return report;
}

QString GPlatesViewOperations::WorldbuildingAuditReport::scope_name(Scope scope)
{
	switch (scope) { case CURRENT_TIME: return "current time"; case INTERVAL: return "interval"; case ALL_TIMES: return "all times"; }
	return "unknown";
}

QString GPlatesViewOperations::WorldbuildingAuditReport::Report::to_markdown(
		const std::vector<int> &repair_queue) const
{
	QString text = QString("# Worldbuilding Audit\n\n- Revision: %1\n- Scope: %2\n- Current time: %3 Ma\n- Older bound: %4 Ma\n- Old-crust threshold: %5 Ma\n\n")
			.arg(request.project_revision, WorldbuildingAuditReport::scope_name(request.scope))
			.arg(request.current_time, 0, 'g', 12).arg(request.older_time, 0, 'g', 12)
			.arg(request.old_crust_threshold_ma, 0, 'g', 12);
	text += "## Findings\n\n| Severity | Domain | Code | Feature ID | Time (Ma) | Message | Suggested repair |\n|---|---|---|---|---:|---|---|\n";
	for (std::vector<Finding>::const_iterator finding = findings.begin(); finding != findings.end(); ++finding)
		text += QString("| %1 | %2 | %3 | %4 | %5 | %6 | %7 |\n")
				.arg(finding->severity, finding->domain, finding->code, finding->feature_id,
						finding->time ? QString::number(*finding->time, 'g', 12) : QString(),
						finding->message, finding->suggested_repair);
	text += "\n## Ocean-crust ledger\n\n| Feature ID | Created (Ma) | Retired (Ma) | Status | Age (Ma) | Advisory |\n|---|---:|---:|---|---:|---|\n";
	for (std::vector<CrustRecord>::const_iterator record = crust.begin(); record != crust.end(); ++record)
		text += QString("| %1 | %2 | %3 | %4 | %5 | %6 |\n").arg(record->feature_id)
				.arg(record->created_time, 0, 'g', 12)
				.arg(record->retired_time ? QString::number(*record->retired_time, 'g', 12) : QString())
				.arg(record->status).arg(record->age_at_current_time, 0, 'f', 1)
				.arg(record->old_crust_advisory ? "review" : "");
	text += "\n## User-promoted repair queue\n\n";
	for (std::vector<int>::const_iterator index = repair_queue.begin(); index != repair_queue.end(); ++index)
		if (*index >= 0 && static_cast<size_t>(*index) < findings.size())
			text += QString("- [%1] %2: %3\n").arg(findings[*index].code, findings[*index].feature_id, findings[*index].suggested_repair);
	return text;
}

QString GPlatesViewOperations::WorldbuildingAuditReport::Report::to_json(
		const std::vector<int> &repair_queue) const
{
	QJsonObject root;
	root["project_revision"] = request.project_revision;
	root["scope"] = WorldbuildingAuditReport::scope_name(request.scope);
	root["current_time_ma"] = request.current_time;
	root["older_bound_ma"] = request.older_time;
	root["old_crust_threshold_ma"] = request.old_crust_threshold_ma;
	QJsonArray finding_array;
	for (size_t index = 0; index < findings.size(); ++index)
	{
		const Finding &finding = findings[index];
		QJsonObject object;
		object["severity"] = finding.severity; object["domain"] = finding.domain;
		object["code"] = finding.code; object["feature_id"] = finding.feature_id;
		if (finding.time) object["time_ma"] = *finding.time;
		object["message"] = finding.message; object["suggested_repair"] = finding.suggested_repair;
		object["promoted_to_repair_queue"] = std::find(repair_queue.begin(), repair_queue.end(), static_cast<int>(index)) != repair_queue.end();
		finding_array.append(object);
	}
	root["findings"] = finding_array;
	QJsonArray crust_array;
	for (std::vector<CrustRecord>::const_iterator record = crust.begin(); record != crust.end(); ++record)
	{
		QJsonObject object; object["feature_id"] = record->feature_id;
		object["created_ma"] = record->created_time;
		if (record->retired_time) object["retired_ma"] = *record->retired_time;
		object["status"] = record->status; object["age_ma"] = record->age_at_current_time;
		object["old_crust_advisory"] = record->old_crust_advisory; crust_array.append(object);
	}
	root["ocean_crust"] = crust_array;
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
