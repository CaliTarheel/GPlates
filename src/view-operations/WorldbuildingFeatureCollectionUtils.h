/*
 * Copyright (C) 2026 The GPlates development team.
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H
#define GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H

#include <QString>

#include "app-logic/FeatureCollectionFileIO.h"
#include "app-logic/WorldbuildingProjectManifest.h"
#include "file-io/FileInfo.h"


namespace GPlatesViewOperations
{
	/**
	 * Create an unsaved collection that has a useful UI label but no fabricated
	 * filesystem path. It therefore remains a normal unsaved collection and Save
	 * still prompts for the user's chosen location.
	 */
	inline
	GPlatesAppLogic::FeatureCollectionFileState::file_reference
	create_named_empty_feature_collection(
			GPlatesAppLogic::FeatureCollectionFileIO &file_io,
			const QString &display_name)
	{
		GPlatesAppLogic::FeatureCollectionFileState::file_reference file =
				file_io.create_empty_file();
		file.set_file_info(GPlatesFileIO::FileInfo(QString(), display_name));
		return file;
	}

	/**
	 * Route generated output into the active manifest role. If the managed file was
	 * unloaded it is loaded again; if it has not been saved yet an empty file reference
	 * is created with the manifest path so Save All writes to the intended collection.
	 */
	inline
	GPlatesAppLogic::FeatureCollectionFileState::file_reference
	resolve_or_create_worldbuilding_feature_collection(
			GPlatesAppLogic::FeatureCollectionFileIO &file_io,
			GPlatesAppLogic::FeatureCollectionFileState &file_state,
			const QString &role,
			const QString &fallback_display_name)
	{
		const boost::optional<GPlatesAppLogic::FeatureCollectionFileState::file_reference> loaded =
				GPlatesAppLogic::WorldbuildingProjectManifest::resolve_loaded_collection(
						role, file_state);
		if (loaded)
		{
			return *loaded;
		}

		const QString managed_path =
				GPlatesAppLogic::WorldbuildingProjectManifest::managed_file_path_for_role(role);
		if (!managed_path.isEmpty())
		{
			if (QFileInfo::exists(managed_path))
			{
				return file_io.load_file(managed_path);
			}
			GPlatesAppLogic::FeatureCollectionFileState::file_reference file =
					file_io.create_empty_file();
			file.set_file_info(GPlatesFileIO::FileInfo(managed_path));
			return file;
		}
		return create_named_empty_feature_collection(file_io, fallback_display_name);
	}
}

#endif // GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H
