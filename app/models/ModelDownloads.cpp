#include "core/Paths.h"
#include "models/ModelDownloader.h"
#include <QDir>
#include <QFileInfo>

namespace ModelBench {

QStringList ModelDownloader::listDownloadedModels() const {
    QStringList out;
    QDir dir(Paths::modelsDirectory());
    for (const QFileInfo &f : dir.entryInfoList({"*.gguf"}, QDir::Files, QDir::Name)) {
        out << f.absoluteFilePath();
    }
    return out;
}

} // namespace ModelBench
