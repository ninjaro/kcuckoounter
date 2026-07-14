#include "arch/asset_locator.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace {

bool is_safe_relative_asset_name(const QString& relative_name) {
    if (relative_name.isEmpty() || QDir::isAbsolutePath(relative_name)) {
        return false;
    }

    const QString cleaned = QDir::cleanPath(relative_name);
    return cleaned != QStringLiteral("..")
        && !cleaned.startsWith(QStringLiteral("../"));
}

void append_candidate(QStringList* candidates, const QString& candidate) {
    if (candidates == nullptr || candidate.isEmpty()
        || candidates->contains(candidate)) {
        return;
    }
    candidates->push_back(candidate);
}

} // namespace

QString bundled_asset_path(const QString& relative_name) {
    if (!is_safe_relative_asset_name(relative_name)) {
        return {};
    }

    const QString cleaned = QDir::cleanPath(relative_name);
    const QString application_dir = QCoreApplication::applicationDirPath();
    QStringList candidates;

    append_candidate(
        &candidates, QStringLiteral(":/kcuckoounter/%1").arg(cleaned)
    );

    if (!application_dir.isEmpty()) {
        const QDir binary_dir(application_dir);
        append_candidate(
            &candidates,
            binary_dir.filePath(QStringLiteral("assets/%1").arg(cleaned))
        );
        append_candidate(
            &candidates,
            binary_dir.filePath(
                QStringLiteral("../share/kcuckoounter/assets/%1").arg(cleaned)
            )
        );
        append_candidate(
            &candidates,
            binary_dir.filePath(
                QStringLiteral("../Resources/assets/%1").arg(cleaned)
            )
        );
    }

    const QString generic_data = QStandardPaths::locate(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("kcuckoounter/assets/%1").arg(cleaned),
        QStandardPaths::LocateFile
    );
    append_candidate(&candidates, generic_data);

#if !defined(NDEBUG)
    const QDir source_dir(
        QFileInfo(QString::fromUtf8(__FILE__)).absolutePath()
    );
    append_candidate(
        &candidates,
        source_dir.filePath(QStringLiteral("../../assets/%1").arg(cleaned))
    );
#endif

    for (const QString& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isFile() && info.isReadable()) {
            return info.absoluteFilePath();
        }
    }

    return {};
}
