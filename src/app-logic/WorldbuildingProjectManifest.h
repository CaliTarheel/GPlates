/*
 * Copyright (C) 2026 CaliTarheel
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H
#define GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H

#include <vector>

#include <boost/optional.hpp>

#include <QString>
#include <QStringList>

#include "FeatureCollectionFileState.h"


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

		/** Make a reviewed manifest the active role map for generator output routing. */
		static void
		activate(
				const QString &project_directory,
				const Manifest &manifest);

		static bool
		has_active_manifest();

		static QString
		active_project_directory();

		static QStringList
		active_collection_roles();

		static QString
		managed_file_path_for_role(
				const QString &role);

		/** Resolve a role only when its exact managed file is already loaded. */
		static boost::optional<FeatureCollectionFileState::file_reference>
		resolve_loaded_collection(
				const QString &role,
				FeatureCollectionFileState &file_state);

		static QString
		default_file_name();

	private:
		static QString
		default_file_name_for_role(
				const QString &role);
	};
}

#endif // GPLATES_APP_LOGIC_WORLDBUILDINGPROJECTMANIFEST_H
