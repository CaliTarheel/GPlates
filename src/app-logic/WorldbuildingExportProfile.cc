/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#include "WorldbuildingExportProfile.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ProjectTimestampSchedule.h"

namespace
{
	typedef GPlatesAppLogic::WorldbuildingExportProfile ExportProfile;

	ExportProfile::Profile make_profile(
			const char *name, const char *variant, const QStringList &layers,
			const QStringList &rasters, const char *style, const char *projection,
			int width, int height, int dpi, bool transparent)
	{
		ExportProfile::Profile profile;
		profile.name = QString::fromLatin1(name);
		profile.variant = QString::fromLatin1(variant);
		profile.layer_roles = layers;
		profile.raster_roles = rasters;
		profile.draw_style_profile = QString::fromLatin1(style);
		profile.projection = QString::fromLatin1(projection);
		profile.width = width;
		profile.height = height;
		profile.dpi = dpi;
		profile.transparent_background = transparent;
		profile.downstream_only = true;
		return profile;
	}

	QString time_token(double time)
	{
		QString token = QString::number(time, 'f', 4);
		while (token.contains('.') && token.endsWith('0')) token.chop(1);
		if (token.endsWith('.')) token.chop(1);
		return token;
	}

	QString planned_name(const QString &source, const ExportProfile::Profile &profile, double time)
	{
		QString name = source.trimmed().isEmpty()
				? QString::fromLatin1("worldbuilding-{variant}-{time}Ma") : source;
		name.replace(QString::fromLatin1("{profile}"), profile.name);
		name.replace(QString::fromLatin1("{variant}"), profile.variant);
		name.replace(QString::fromLatin1("{time}"), time_token(time));
		return name;
	}

	QJsonArray string_array(const QStringList &values)
	{
		QJsonArray array;
		for (QStringList::const_iterator value = values.begin(); value != values.end(); ++value)
			array.append(*value);
		return array;
	}
}

GPlatesAppLogic::WorldbuildingExportProfile::Request::Request() :
	schedule_mode(UNIFORM_INTERVAL), current_time(0), start_time(0), end_time(0),
	uniform_step(1), planet_radius_km(6371), project_revision(QString::fromLatin1("unrecorded")),
	output_template(QString::fromLatin1("worldbuilding-{variant}-{time}Ma"))
{  }

std::vector<GPlatesAppLogic::WorldbuildingExportProfile::Profile>
GPlatesAppLogic::WorldbuildingExportProfile::default_profiles()
{
	std::vector<Profile> profiles;
	profiles.push_back(make_profile("Tectonic review", "boundaries",
			QStringList() << "cratons" << "continental-crust" << "oceanic-crust" << "mors"
					<< "transforms" << "trenches" << "topologies",
			QStringList(), "Worldbuilding tectonics", "Orthographic", 2400, 2400, 150, false));
	profiles.push_back(make_profile("Geology", "events",
			QStringList() << "continental-crust" << "island-arcs-terranes" << "active-orogenies"
					<< "former-orogenies" << "lips" << "hotspots" << "motion-paths",
			QStringList() << "age-grid" << "relief", "Worldbuilding geology", "Robinson", 3200, 1800, 180, false));
	profiles.push_back(make_profile("Presentation", "clean-map",
			QStringList() << "continental-crust" << "oceanic-crust" << "mors" << "trenches",
			QStringList() << "relief", "Worldbuilding presentation", "Mollweide", 3840, 2160, 300, true));
	return profiles;
}

QString
GPlatesAppLogic::WorldbuildingExportProfile::schedule_mode_name(ScheduleMode mode)
{
	switch (mode)
	{
	case SINGLE_TIME: return QString::fromLatin1("single-time");
	case UNIFORM_INTERVAL: return QString::fromLatin1("uniform-interval");
	case PROJECT_TIMESTAMPS: return QString::fromLatin1("project-timestamps");
	}
	return QString::fromLatin1("unknown");
}

GPlatesAppLogic::WorldbuildingExportProfile::Plan
GPlatesAppLogic::WorldbuildingExportProfile::build(const Request &request)
{
	Plan plan;
	plan.request = request;
	if (request.schedule_mode == SINGLE_TIME)
	{
		plan.reconstruction_times.push_back(request.current_time);
	}
	else if (request.schedule_mode == PROJECT_TIMESTAMPS)
	{
		const double younger = std::min(request.start_time, request.end_time);
		const double older = std::max(request.start_time, request.end_time);
		for (std::vector<double>::const_iterator time = request.project_timestamps_older_to_younger.begin();
				time != request.project_timestamps_older_to_younger.end(); ++time)
			if (*time + 1e-9 >= younger && *time - 1e-9 <= older)
				plan.reconstruction_times.push_back(*time);
		if (request.start_time < request.end_time)
			std::reverse(plan.reconstruction_times.begin(), plan.reconstruction_times.end());
	}
	else if (request.uniform_step > 0)
	{
		plan.reconstruction_times = ProjectTimestampSchedule::build(
				std::min(request.start_time, request.end_time),
				std::max(request.start_time, request.end_time), request.uniform_step);
		if (request.start_time < request.end_time)
			std::reverse(plan.reconstruction_times.begin(), plan.reconstruction_times.end());
	}
	else
	{
		plan.warnings.append(QString::fromLatin1("Uniform timestep must be greater than zero."));
	}

	if (plan.reconstruction_times.empty())
		plan.warnings.append(QString::fromLatin1("No timestamps fall inside the requested export range."));
	if (request.project_revision.trimmed().isEmpty() || request.project_revision == QString::fromLatin1("unrecorded"))
		plan.warnings.append(QString::fromLatin1("Project revision is unrecorded."));
	if (!(request.planet_radius_km > 0) || !std::isfinite(request.planet_radius_km))
		plan.warnings.append(QString::fromLatin1("Planet radius is missing or invalid."));
	if (!request.profile.downstream_only)
		plan.warnings.append(QString::fromLatin1("Profile must be downstream-only; project mutation is not supported."));

	std::set<QString> unique_names;
	for (std::vector<double>::const_iterator time = plan.reconstruction_times.begin();
			time != plan.reconstruction_times.end(); ++time)
	{
		PlannedFile file;
		file.reconstruction_time = *time;
		file.file_name = planned_name(request.output_template, request.profile, *time);
		if (!unique_names.insert(file.file_name).second)
			plan.warnings.append(QString::fromLatin1("Duplicate planned filename: %1").arg(file.file_name));
		plan.files.push_back(file);
	}
	return plan;
}

QString
GPlatesAppLogic::WorldbuildingExportProfile::Plan::to_json() const
{
	QJsonObject root;
	root["format_version"] = 1;
	root["downstream_only"] = request.profile.downstream_only;
	root["project_revision"] = request.project_revision;
	root["planet_radius_km"] = request.planet_radius_km;

	QJsonObject profile;
	profile["name"] = request.profile.name;
	profile["variant"] = request.profile.variant;
	profile["layer_roles"] = string_array(request.profile.layer_roles);
	profile["raster_roles"] = string_array(request.profile.raster_roles);
	profile["draw_style_profile"] = request.profile.draw_style_profile;
	profile["projection"] = request.profile.projection;
	profile["width"] = request.profile.width;
	profile["height"] = request.profile.height;
	profile["dpi"] = request.profile.dpi;
	profile["transparent_background"] = request.profile.transparent_background;
	root["profile"] = profile;

	QJsonObject schedule;
	schedule["mode"] = schedule_mode_name(request.schedule_mode);
	schedule["requested_start_ma"] = request.start_time;
	schedule["requested_end_ma"] = request.end_time;
	schedule["uniform_step_my"] = request.uniform_step;
	QJsonArray times;
	for (std::vector<double>::const_iterator time = reconstruction_times.begin();
			time != reconstruction_times.end(); ++time) times.append(*time);
	schedule["exact_times_ma"] = times;
	root["schedule"] = schedule;

	QJsonArray file_array;
	for (std::vector<PlannedFile>::const_iterator file = files.begin(); file != files.end(); ++file)
	{
		QJsonObject object;
		object["time_ma"] = file->reconstruction_time;
		object["file"] = file->file_name;
		file_array.append(object);
	}
	root["files"] = file_array;
	root["warnings"] = string_array(warnings);
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
