/*
 * Copyright (C) 2026 The GPlates development team.
 *
 * This file is part of GPlates and is distributed under the GNU GPL v2 or later.
 */

#ifndef GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H
#define GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H

#include <QString>

#include "app-logic/FeatureCollectionFileIO.h"
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
}

#endif // GPLATES_VIEWOPERATIONS_WORLDBUILDINGFEATURECOLLECTIONUTILS_H
