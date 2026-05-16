#include "infrastructure/storage/waterfall_storage_paths.h"

#include <QDateTime>
#include <QDir>
#include <QTimeZone>

namespace siriusscope::infrastructure::waterfall_storage_paths {
namespace {

QString sanitizedPathComponent(QString value)
{
    if (value.isEmpty()) {
        return QStringLiteral("session");
    }

    for (auto& ch : value) {
        const bool allowed = ch.isLetterOrNumber()
            || ch == QLatin1Char('-')
            || ch == QLatin1Char('_');
        if (!allowed) {
            ch = QLatin1Char('_');
        }
    }

    return value;
}

} // namespace

QString recordingsRootPath(const QString& dataRootPath)
{
    return QDir(dataRootPath).filePath(QStringLiteral("recordings"));
}

QString safeSessionDirectoryName(const WaterfallSessionMetadata& metadata)
{
    const QDateTime startTime =
        QDateTime::fromMSecsSinceEpoch(metadata.startUtcMs, QTimeZone::UTC);
    const QString timestamp = startTime.isValid()
        ? startTime.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"))
        : QStringLiteral("unknown-time");
    return timestamp + QLatin1Char('_') + sanitizedPathComponent(metadata.id.value);
}

QString metadataPath(const QString& sessionDirPath)
{
    return QDir(sessionDirPath).filePath(QStringLiteral("metadata.json"));
}

QString binPath(const QString& sessionDirPath)
{
    return QDir(sessionDirPath).filePath(QStringLiteral("waterfall.bin"));
}

QString indexPath(const QString& sessionDirPath)
{
    return QDir(sessionDirPath).filePath(QStringLiteral("waterfall.idx"));
}

} // namespace siriusscope::infrastructure::waterfall_storage_paths
