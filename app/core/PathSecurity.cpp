#include "PathSecurity.h"

#include <QFileInfo>
#include <QDir>

namespace ModelFit::PathSecurity {

bool isInside(const QString &root, const QString &child) {
    QString r = QDir(root).absolutePath();
    QString c = QFileInfo(child).absoluteFilePath();
    if (!c.startsWith(r)) return false;
    // ensure boundary
    if (c.length() > r.length() && c[r.length()] != '/') return false;
    return true;
}

QString sanitizeFileName(const QString &name) {
    QString out = name;
    out.replace('/', '_');
    out.replace('\\', '_');
    out.replace("..", "_");
    return out;
}

} // namespace ModelFit::PathSecurity
