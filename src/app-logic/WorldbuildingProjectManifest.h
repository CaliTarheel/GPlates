/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H
#define GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H

#include <vector>

#include <QString>
#include <QStringList>


namespace GPlatesAppLogic
{
	/**
	 * Editable, external description of a Worldbuilding Pasta workspace.
	 *
	 * The manifest deliberately does not replace the native .gproj. It records stable
	 * collection roles and a presentation profile that can be audited before the GUI
	 * creates missing files or reapplies settings.
	 */
	class WorldbuildingProjectManifest
	{
	public:
		static const int CURRENT_FORMAT_VERSION = 1;

		struct Collection
		{
			QString role;
			QString file_name;
			QString display_name;
			QString default_feature_type;
			bool required;
		};

		struct LayerProfile
		{
			QString collection_role;
			int order;
			bool visible;
			QString draw_style;
			QString reconstruction_method;
			QStringList raster_connections;
		};

		struct Manifest
		{
			int format_version;
			int profile_revision;
			QString profile_name;
			QString apply_policy;
			std::vector<Collection> collections;
			std::vector<LayerProfile> layers;
		};

		enum IssueSeverity
		{
			INFO,
			WARNING,
			BLOCKING
		};

		struct Issue
		{
			IssueSeverity severity;
			QString code;
			QString message;
		};

		struct Audit
		{
			std::vector<Issue> issues;
			QStringList missing_files;

			bool has_blocking_issues() const;
			QString to_plain_text() const;
		};

		static Manifest
		default_manifest();

		static bool
		load(
				const QString &file_name,
				Manifest &manifest,
				QString *error_message = NULL);

		static bool
		save(
				const QString &file_name,
				const Manifest &manifest,
				QString *error_message = NULL);

		static Audit
		audit(
				const QString &project_directory,
				const Manifest &manifest);

		static QString
		default_file_name();

	private:
		static QString
		default_file_name_for_role(
				const QString &role);
	};
}

#endif // GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H
