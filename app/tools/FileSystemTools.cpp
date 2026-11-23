#include "FileSystemTools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>

namespace ModelBench {
namespace {

QJsonObject prop(const QString &type, const QString &description,
                 const QJsonValue &defaultValue = QJsonValue()) {
    QJsonObject o;
    o[QStringLiteral("type")] = type;
    o[QStringLiteral("description")] = description;
    if (!defaultValue.isNull()) o[QStringLiteral("default")] = defaultValue;
    return o;
}

QJsonObject schema(const QJsonObject &properties, const QStringList &required) {
    QJsonObject s;
    s[QStringLiteral("type")] = QStringLiteral("object");
    if (!properties.isEmpty()) s[QStringLiteral("properties")] = properties;
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const QString &r : required) req.append(r);
        s[QStringLiteral("required")] = req;
    }
    return s;
}

bool isInsidePath(const QString &root, const QString &candidate) {
    QString a = QDir::fromNativeSeparators(QDir::cleanPath(root));
    QString b = QDir::fromNativeSeparators(QDir::cleanPath(candidate));
#ifdef Q_OS_WIN
    a = a.toLower();
    b = b.toLower();
#endif
    if (a == b) return true;
    if (!a.endsWith(u'/')) a.append(u'/');
    return b.startsWith(a);
}

QString canonicalOrClean(const QString &path) {
    QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath()
                         : QDir::cleanPath(QDir().absoluteFilePath(path));
}

ToolResult makeResult(const QString &name, bool ok, const QString &error = {},
                      const QString &code = {}) {
    ToolResult r;
    r.ok = ok;
    r.toolName = name;
    r.error = error;
    r.errorCode = code;
    return r;
}

} // namespace

FileToolBase::FileToolBase(const QString &sandboxRoot, QObject *parent)
    : Tool(parent), m_root(sandboxRoot) {}

QString FileToolBase::resolvePath(const QString &input, QString *error) const {
    if (input.isEmpty()) {
        if (error) *error = QStringLiteral("path is required");
        return {};
    }
    if (input.contains(QChar('\0'))) {
        if (error) *error = QStringLiteral("path contains a NUL byte");
        return {};
    }

    QFileInfo info(input);
    QString absolute = info.isAbsolute()
                           ? input
                           : (m_root.isEmpty() ? QDir().absoluteFilePath(input)
                                               : QDir(m_root).absoluteFilePath(input));
    const QString resolved = canonicalOrClean(absolute);

    if (!m_root.isEmpty()) {
        const QString root = canonicalOrClean(m_root);
        if (!isInsidePath(root, resolved)) {
            if (error)
                *error = QStringLiteral("path '%1' is outside the allowed root '%2'")
                             .arg(input, root);
            return {};
        }
    }
    return resolved;
}

// ---------------------------------------------------------------- read_file
ReadFileTool::ReadFileTool(const QString &sandboxRoot, QObject *parent)
    : FileToolBase(sandboxRoot, parent) {}

QString ReadFileTool::description() const {
    return QStringLiteral("Read a UTF-8 text file (optional byte offset/limit).");
}

QJsonObject ReadFileTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("path"), prop(QStringLiteral("string"),
                                          QStringLiteral("File path (absolute or relative to the sandbox root)."))},
            {QStringLiteral("offset"), prop(QStringLiteral("integer"),
                                            QStringLiteral("Byte offset to start from."), 0)},
            {QStringLiteral("limit"), prop(QStringLiteral("integer"),
                                           QStringLiteral("Maximum bytes to return."))},
        }),
        {QStringLiteral("path")});
}

void ReadFileTool::execute(qint64 callId, const QJsonObject &args) {
    QString error;
    const QString path = resolvePath(args.value(QStringLiteral("path")).toString(), &error);
    if (path.isEmpty()) {
        emit finished(callId, makeResult(name(), false, error, QStringLiteral("bad_path")));
        return;
    }
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("Not a readable file: %1").arg(path),
                                         QStringLiteral("not_found")));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit finished(callId, makeResult(name(), false, file.errorString(),
                                         QStringLiteral("open_failed")));
        return;
    }

    constexpr qint64 kMaxBytes = 1024 * 1024;
    const qint64 offset = qMax<qint64>(0, args.value(QStringLiteral("offset")).toInt());
    const QJsonValue limitValue = args.value(QStringLiteral("limit"));
    qint64 limit = kMaxBytes;
    bool hasLimit = false;
    if (limitValue.isDouble()) {
        limit = qBound<qint64>(0, qint64(limitValue.toDouble()), kMaxBytes);
        hasLimit = true;
    }
    if (!file.seek(offset)) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("Offset beyond end of file"),
                                         QStringLiteral("bad_offset")));
        return;
    }
    QByteArray bytes = file.read(hasLimit ? limit : kMaxBytes);
    const bool truncated = (!hasLimit && file.bytesAvailable() > 0) ||
                           (hasLimit && bytes.size() >= limit && file.bytesAvailable() > 0);

    QJsonObject out;
    out[QStringLiteral("path")] = path;
    out[QStringLiteral("content")] = QString::fromUtf8(bytes);
    out[QStringLiteral("bytes")] = bytes.size();
    out[QStringLiteral("offset")] = offset;
    out[QStringLiteral("lineCount")] = QString::fromUtf8(bytes).count(u'\n') +
                                       (bytes.isEmpty() ? 0 : 1);
    out[QStringLiteral("truncated")] = truncated;

    ToolResult r = makeResult(name(), true);
    r.output = out;
    r.truncated = truncated;
    emit finished(callId, r);
}

// --------------------------------------------------------------- write_file
WriteFileTool::WriteFileTool(const QString &sandboxRoot, QObject *parent)
    : FileToolBase(sandboxRoot, parent) {}

QString WriteFileTool::description() const {
    return QStringLiteral("Write UTF-8 text to a file (atomically via QSaveFile).");
}

QJsonObject WriteFileTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("path"), prop(QStringLiteral("string"),
                                          QStringLiteral("Destination file path."))},
            {QStringLiteral("content"), prop(QStringLiteral("string"),
                                             QStringLiteral("Text to write."))},
            {QStringLiteral("createDirs"), prop(QStringLiteral("boolean"),
                                                QStringLiteral("Create missing parent directories."), true)},
            {QStringLiteral("overwrite"), prop(QStringLiteral("boolean"),
                                               QStringLiteral("Allow replacing an existing file."), true)},
        }),
        {QStringLiteral("path"), QStringLiteral("content")});
}

void WriteFileTool::execute(qint64 callId, const QJsonObject &args) {
    QString error;
    const QString path = resolvePath(args.value(QStringLiteral("path")).toString(), &error);
    if (path.isEmpty()) {
        emit finished(callId, makeResult(name(), false, error, QStringLiteral("bad_path")));
        return;
    }
    if (!args.contains(QStringLiteral("content"))) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("content is required"),
                                         QStringLiteral("validation_error")));
        return;
    }
    const QString content = args.value(QStringLiteral("content")).toString();
    const bool overwrite = args.value(QStringLiteral("overwrite")).toBool(true);
    const bool existed = QFileInfo::exists(path);
    QByteArray before;
    if (existed) {
        QFile previous(path);
        if (previous.open(QIODevice::ReadOnly)) before = previous.readAll();
    }
    if (!overwrite && existed) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("File already exists: %1").arg(path),
                                         QStringLiteral("exists")));
        return;
    }

    const QFileInfo fi(path);
    if (args.value(QStringLiteral("createDirs")).toBool(true)) {
        if (!QDir().mkpath(fi.absolutePath())) {
            emit finished(callId, makeResult(name(), false,
                                             QStringLiteral("Could not create parent directory"),
                                             QStringLiteral("mkdir_failed")));
            return;
        }
    }
    // Re-check the final parent after mkpath to avoid escaping via symlinks.
    QString parentError;
    if (!sandboxRoot().isEmpty()) {
        resolvePath(fi.absolutePath(), &parentError);
        if (!parentError.isEmpty()) {
            emit finished(callId, makeResult(name(), false, parentError,
                                             QStringLiteral("bad_path")));
            return;
        }
    }

    QSaveFile file(path);
    file.setDirectWriteFallback(true);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit finished(callId, makeResult(name(), false, file.errorString(),
                                         QStringLiteral("open_failed")));
        return;
    }
    const QByteArray bytes = content.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        emit finished(callId, makeResult(name(), false, file.errorString(),
                                         QStringLiteral("write_failed")));
        return;
    }
    if (history()) history()->record(path, before, bytes, existed);

    QJsonObject out;
    out[QStringLiteral("path")] = path;
    out[QStringLiteral("bytesWritten")] = bytes.size();
    ToolResult r = makeResult(name(), true);
    r.output = out;
    emit finished(callId, r);
}

// ---------------------------------------------------------------- edit_file
EditFileTool::EditFileTool(const QString &sandboxRoot, QObject *parent)
    : FileToolBase(sandboxRoot, parent) {}

QString EditFileTool::description() const {
    return QStringLiteral("Replace text in a file (literal or regex search/replace).");
}

QJsonObject EditFileTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("path"), prop(QStringLiteral("string"),
                                          QStringLiteral("File to edit."))},
            {QStringLiteral("oldText"), prop(QStringLiteral("string"),
                                             QStringLiteral("Text (or regex) to find."))},
            {QStringLiteral("newText"), prop(QStringLiteral("string"),
                                             QStringLiteral("Replacement text."))},
            {QStringLiteral("replaceAll"), prop(QStringLiteral("boolean"),
                                                QStringLiteral("Replace every occurrence."), false)},
            {QStringLiteral("caseSensitive"), prop(QStringLiteral("boolean"),
                                                   QStringLiteral("Case-sensitive match."), true)},
            {QStringLiteral("useRegex"), prop(QStringLiteral("boolean"),
                                              QStringLiteral("Treat oldText as a regular expression."), false)},
        }),
        {QStringLiteral("path"), QStringLiteral("oldText"), QStringLiteral("newText")});
}

void EditFileTool::execute(qint64 callId, const QJsonObject &args) {
    QString error;
    const QString path = resolvePath(args.value(QStringLiteral("path")).toString(), &error);
    if (path.isEmpty()) {
        emit finished(callId, makeResult(name(), false, error, QStringLiteral("bad_path")));
        return;
    }
    const QString oldText = args.value(QStringLiteral("oldText")).toString();
    const QString newText = args.value(QStringLiteral("newText")).toString();
    if (oldText.isEmpty()) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("oldText must not be empty"),
                                         QStringLiteral("validation_error")));
        return;
    }
    const bool replaceAll = args.value(QStringLiteral("replaceAll")).toBool(false);
    const bool caseSensitive = args.value(QStringLiteral("caseSensitive")).toBool(true);
    const bool useRegex = args.value(QStringLiteral("useRegex")).toBool(false);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit finished(callId, makeResult(name(), false, file.errorString(),
                                         QStringLiteral("open_failed")));
        return;
    }
    const QByteArray before = file.readAll();
    QString text = QString::fromUtf8(before);
    file.close();

    int replacements = 0;
    if (useRegex) {
        QRegularExpression::PatternOptions options =
            QRegularExpression::NoPatternOption;
        if (!caseSensitive) options |= QRegularExpression::CaseInsensitiveOption;
        QRegularExpression re(oldText, options);
        if (!re.isValid()) {
            emit finished(callId, makeResult(name(), false, re.errorString(),
                                             QStringLiteral("bad_regex")));
            return;
        }
        if (replaceAll) {
            QRegularExpressionMatchIterator it = re.globalMatch(text);
            while (it.hasNext()) { it.next(); ++replacements; }
            text.replace(re, newText);
        } else {
            QRegularExpressionMatch match = re.match(text);
            if (match.hasMatch()) {
                replacements = 1;
                text.remove(match.capturedStart(), match.capturedLength());
                text.insert(match.capturedStart(), newText);
            }
        }
    } else {
        Qt::CaseSensitivity cs =
            caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        replacements = text.count(oldText, cs);
        if (replacements > 0) {
            if (replaceAll) {
                text.replace(oldText, newText, cs);
            } else {
                const int at = text.indexOf(oldText, 0, cs);
                text.replace(at, oldText.size(), newText);
            }
        }
    }

    if (replacements == 0) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("No matches found"),
                                         QStringLiteral("no_match")));
        return;
    }

    const QByteArray after = text.toUtf8();
    QSaveFile outFile(path);
    outFile.setDirectWriteFallback(true);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text) ||
        outFile.write(after) != after.size() || !outFile.commit()) {
        emit finished(callId, makeResult(name(), false, outFile.errorString(),
                                         QStringLiteral("write_failed")));
        return;
    }
    if (history()) history()->record(path, before, after, true);

    QJsonObject out;
    out[QStringLiteral("path")] = path;
    out[QStringLiteral("replacements")] = replacements;
    ToolResult r = makeResult(name(), true);
    r.output = out;
    emit finished(callId, r);
}

// ---------------------------------------------------------------- list_dir
ListDirTool::ListDirTool(const QString &sandboxRoot, QObject *parent)
    : FileToolBase(sandboxRoot, parent) {}

QString ListDirTool::description() const {
    return QStringLiteral("List directory entries (optionally recursive).");
}

QJsonObject ListDirTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("path"), prop(QStringLiteral("string"),
                                          QStringLiteral("Directory to list."))},
            {QStringLiteral("recursive"), prop(QStringLiteral("boolean"),
                                               QStringLiteral("Include subdirectories."), false)},
            {QStringLiteral("pattern"), prop(QStringLiteral("string"),
                                             QStringLiteral("Name filter, e.g. '*.cpp'."), QStringLiteral("*"))},
            {QStringLiteral("includeHidden"), prop(QStringLiteral("boolean"),
                                                   QStringLiteral("Include hidden files."), false)},
            {QStringLiteral("maxEntries"), prop(QStringLiteral("integer"),
                                                QStringLiteral("Maximum number of entries."), 500)},
        }),
        {QStringLiteral("path")});
}

void ListDirTool::execute(qint64 callId, const QJsonObject &args) {
    QString error;
    const QString path = resolvePath(args.value(QStringLiteral("path")).toString(), &error);
    if (path.isEmpty()) {
        emit finished(callId, makeResult(name(), false, error, QStringLiteral("bad_path")));
        return;
    }
    if (!QFileInfo(path).isDir()) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("Not a directory: %1").arg(path),
                                         QStringLiteral("not_found")));
        return;
    }

    QDir::Filters filters = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot;
    if (args.value(QStringLiteral("includeHidden")).toBool(false))
        filters |= QDir::Hidden;
    QDirIterator::IteratorFlags flags =
        args.value(QStringLiteral("recursive")).toBool(false)
            ? QDirIterator::Subdirectories
            : QDirIterator::NoIteratorFlags;

    QStringList nameFilters;
    const QJsonValue pattern = args.value(QStringLiteral("pattern"));
    if (pattern.isArray()) {
        for (const QJsonValue &v : pattern.toArray())
            nameFilters << v.toString();
    } else {
        nameFilters << (pattern.isString() ? pattern.toString() : QStringLiteral("*"));
    }
    if (nameFilters.isEmpty()) nameFilters << QStringLiteral("*");

    const int maxEntries = qBound(1, args.value(QStringLiteral("maxEntries")).toInt(500), 10000);
    QJsonArray entries;
    bool truncated = false;
    QDirIterator it(path, nameFilters, filters, flags);
    while (it.hasNext()) {
        if (entries.size() >= maxEntries) { truncated = true; break; }
        it.next();
        const QFileInfo fi = it.fileInfo();
        QJsonObject entry;
        entry[QStringLiteral("name")] = fi.fileName();
        entry[QStringLiteral("path")] = fi.absoluteFilePath();
        entry[QStringLiteral("type")] = fi.isDir() ? QStringLiteral("dir")
                                                   : QStringLiteral("file");
        entry[QStringLiteral("size")] = fi.isFile() ? fi.size() : 0;
        entry[QStringLiteral("modified")] =
            fi.lastModified().toString(Qt::ISODate);
        entries.append(entry);
    }

    QJsonObject out;
    out[QStringLiteral("path")] = path;
    out[QStringLiteral("entries")] = entries;
    out[QStringLiteral("count")] = entries.size();
    out[QStringLiteral("truncated")] = truncated;
    ToolResult r = makeResult(name(), true);
    r.output = out;
    r.truncated = truncated;
    emit finished(callId, r);
}

// -------------------------------------------------------------- search_code
SearchCodeTool::SearchCodeTool(const QString &sandboxRoot, QObject *parent)
    : FileToolBase(sandboxRoot, parent) {}

QString SearchCodeTool::description() const {
    return QStringLiteral("Search source code for text or a regex; returns matching lines.");
}

QJsonObject SearchCodeTool::inputSchema() const {
    return schema(
        QJsonObject({
            {QStringLiteral("query"), prop(QStringLiteral("string"),
                                           QStringLiteral("Text or regex to search for."))},
            {QStringLiteral("path"), prop(QStringLiteral("string"),
                                          QStringLiteral("Search root directory."))},
            {QStringLiteral("filePattern"), prop(QStringLiteral("string"),
                                                 QStringLiteral("File name filter."), QStringLiteral("*"))},
            {QStringLiteral("caseSensitive"), prop(QStringLiteral("boolean"),
                                                   QStringLiteral("Case-sensitive search."), false)},
            {QStringLiteral("useRegex"), prop(QStringLiteral("boolean"),
                                              QStringLiteral("Treat query as a regular expression."), false)},
            {QStringLiteral("maxResults"), prop(QStringLiteral("integer"),
                                                QStringLiteral("Maximum number of matches."), 200)},
            {QStringLiteral("contextLines"), prop(QStringLiteral("integer"),
                                                  QStringLiteral("Lines before/after each match."), 0)},
        }),
        {QStringLiteral("query")});
}

void SearchCodeTool::execute(qint64 callId, const QJsonObject &args) {
    const QString query = args.value(QStringLiteral("query")).toString();
    if (query.isEmpty()) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("query is required"),
                                         QStringLiteral("validation_error")));
        return;
    }
    const QString rootInput = args.value(QStringLiteral("path")).toString(
        sandboxRoot().isEmpty() ? QStringLiteral(".") : sandboxRoot());
    QString error;
    const QString root = resolvePath(rootInput, &error);
    if (root.isEmpty()) {
        emit finished(callId, makeResult(name(), false, error, QStringLiteral("bad_path")));
        return;
    }
    if (!QFileInfo(root).isDir()) {
        emit finished(callId, makeResult(name(), false,
                                         QStringLiteral("Not a directory: %1").arg(root),
                                         QStringLiteral("not_found")));
        return;
    }

    const bool caseSensitive = args.value(QStringLiteral("caseSensitive")).toBool(false);
    const bool useRegex = args.value(QStringLiteral("useRegex")).toBool(false);
    const int maxResults = qBound(1, args.value(QStringLiteral("maxResults")).toInt(200), 5000);
    const int contextLines = qBound(0, args.value(QStringLiteral("contextLines")).toInt(0), 20);
    const qint64 maxFileBytes = 2 * 1024 * 1024;
    const QString filePattern = args.value(QStringLiteral("filePattern")).toString(QStringLiteral("*"));

    QRegularExpression re;
    if (useRegex) {
        QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
        if (!caseSensitive) options |= QRegularExpression::CaseInsensitiveOption;
        re = QRegularExpression(query, options);
        if (!re.isValid()) {
            emit finished(callId, makeResult(name(), false, re.errorString(),
                                             QStringLiteral("bad_regex")));
            return;
        }
    }

    QJsonArray results;
    int filesSearched = 0;
    bool truncated = false;
    const Qt::CaseSensitivity cs =
        caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;

    QDirIterator it(root, QStringList{filePattern},
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (results.size() >= maxResults) { truncated = true; break; }
        it.next();
        const QString filePath = it.filePath();
        if (QFileInfo(filePath).size() > maxFileBytes) continue;

        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray raw = file.read(maxFileBytes);
        if (raw.contains('\0')) continue; // skip binary files
        const QStringList lines = QString::fromUtf8(raw).split(u'\n');
        ++filesSearched;

        for (int i = 0; i < lines.size(); ++i) {
            if (results.size() >= maxResults) { truncated = true; break; }
            int column = -1;
            if (useRegex) {
                QRegularExpressionMatch m = re.match(lines.at(i));
                if (m.hasMatch()) column = m.capturedStart();
            } else {
                column = lines.at(i).indexOf(query, 0, cs);
            }
            if (column < 0) continue;

            QJsonObject match;
            match[QStringLiteral("path")] = filePath;
            match[QStringLiteral("line")] = i + 1;
            match[QStringLiteral("column")] = column + 1;
            match[QStringLiteral("text")] = lines.at(i);
            if (contextLines > 0) {
                QJsonArray before, after;
                for (int j = qMax(0, i - contextLines); j < i; ++j)
                    before.append(lines.at(j));
                for (int j = i + 1; j < qMin(lines.size(), i + 1 + contextLines); ++j)
                    after.append(lines.at(j));
                match[QStringLiteral("before")] = before;
                match[QStringLiteral("after")] = after;
            }
            results.append(match);
        }
    }

    QJsonObject out;
    out[QStringLiteral("results")] = results;
    out[QStringLiteral("count")] = results.size();
    out[QStringLiteral("filesSearched")] = filesSearched;
    out[QStringLiteral("truncated")] = truncated;
    ToolResult r = makeResult(name(), true);
    r.output = out;
    r.truncated = truncated;
    emit finished(callId, r);
}

namespace FileSystemTools {

void registerTools(ToolRegistry &registry, const QString &sandboxRoot,
                   FileEditHistory *history) {
    auto *read = new ReadFileTool(sandboxRoot, &registry);
    auto *write = new WriteFileTool(sandboxRoot, &registry);
    auto *edit = new EditFileTool(sandboxRoot, &registry);
    write->setHistory(history);
    edit->setHistory(history);
    registry.registerTool(read);
    registry.registerTool(write);
    registry.registerTool(edit);
    registry.registerTool(new ListDirTool(sandboxRoot, &registry));
    registry.registerTool(new SearchCodeTool(sandboxRoot, &registry));
}

} // namespace FileSystemTools
} // namespace ModelBench
