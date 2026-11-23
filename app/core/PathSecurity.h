#pragma once

#include <QString>
#include <QDir>

namespace ModelBench::PathSecurity {

// Returns true if `child` resolves inside `root` (no traversal).
bool isInside(const QString &root, const QString &child);
// Sanitize a filename: strip path separators and traversal.
QString sanitizeFileName(const QString &name);

} // namespace ModelBench::PathSecurity
