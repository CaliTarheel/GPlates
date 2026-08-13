/* Copyright (C) 2026 CaliTarheel. GPL v2 or later. */
#ifndef GPLATES_APP_LOGIC_WORLDBUILDINGEXPORTPROFILE_H
#define GPLATES_APP_LOGIC_WORLDBUILDINGEXPORTPROFILE_H

#include <vector>

#include <QString>
#include <QStringList>

namespace GPlatesAppLogic
{
	/**
	 * Portable, downstream-only export presets and an exact frame manifest.
	 * Profiles describe output intent; they never alter project features or layers.
	 */
	class WorldbuildingExportProfile
	{
	public:
		enum ScheduleMode { SINGLE_TIME, UNIFORM_INTERVAL, PROJECT_TIMESTAMPS };

		struct Profile
		{
			QString name;
			QString variant;
			QStringList layer_roles;
			QStringList raster_roles;
			QString draw_style_profile;
			QString projection;
			int width;
			int height;
			int dpi;
			bool transparent_background;
			bool downstream_only;
		};

		struct Request
		{
			Request();
			Profile profile;
			ScheduleMode schedule_mode;
			double current_time;
			double start_time;
			double end_time;
			double uniform_step;
			std::vector<double> project_timestamps_older_to_younger;
			double planet_radius_km;
			QString project_revision;
			QString output_template;
		};

		struct PlannedFile
		{
			double reconstruction_time;
			QString file_name;
		};

		struct Plan
		{
			Request request;
			std::vector<double> reconstruction_times;
			std::vector<PlannedFile> files;
			QStringList warnings;

			QString to_json() const;
		};

		static std::vector<Profile> default_profiles();
		static Plan build(const Request &request);
		static QString schedule_mode_name(ScheduleMode mode);
	};
}

#endif
