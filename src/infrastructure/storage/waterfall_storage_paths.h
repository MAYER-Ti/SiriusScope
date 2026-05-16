#pragma once

#include "app/waterfallstorage.h"

#include <QString>

namespace siriusscope::infrastructure::waterfall_storage_paths {

QString recordingsRootPath(const QString& dataRootPath);
QString safeSessionDirectoryName(const WaterfallSessionMetadata& metadata);
QString metadataPath(const QString& sessionDirPath);
QString binPath(const QString& sessionDirPath);
QString indexPath(const QString& sessionDirPath);

} // namespace siriusscope::infrastructure::waterfall_storage_paths
