#include "CodeIndexer.h"

#include "VectorStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>

#include <algorithm>

namespace ModelBench {

namespace {
QString normalizeRoot(const QString &rootPath) {
    return QDir::cleanPath(QDir(rootPath).absolutePath());
}

QString relativeToRoot(const QString &root, const QString &absPath) {
    QDir dir(root);
    return dir.relativeFilePath(absPath);
}

QString hashString(const QString &s) {
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha1).toHex());
}

bool matchesAnyPattern(const QString &name, const QStringList &patterns) {
    for (const QString &p : patterns) {
        if (p.isEmpty())
            continue;
        if (name.compare(p, Qt::CaseInsensitive) == 0)
            return true;
        QRegularExpression re(QRegularExpression::wildcardToRegularExpression(p),
                              QRegularExpression::CaseInsensitiveOption);
        if (re.match(name).hasMatch())
            return true;
    }
    return false;
}
} // namespace

CodeIndexer::CodeIndexer(VectorStore *store) : m_store(store) {}

QStringList CodeIndexer::defaultExtensions() const {
    return {
        QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"), QStringLiteral("cxx"), QStringLiteral("h"),
        QStringLiteral("hh"), QStringLiteral("hpp"), QStringLiteral("hxx"), QStringLiteral("inl"),
        QStringLiteral("cs"), QStringLiteral("go"), QStringLiteral("rs"), QStringLiteral("java"), QStringLiteral("kt"),
        QStringLiteral("js"), QStringLiteral("jsx"), QStringLiteral("ts"), QStringLiteral("tsx"),
        QStringLiteral("py"), QStringLiteral("rb"), QStringLiteral("php"), QStringLiteral("swift"),
        QStringLiteral("sql"), QStringLiteral("sh"), QStringLiteral("ps1"), QStringLiteral("bat"),
        QStringLiteral("cmake"), QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("json"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("toml"), QStringLiteral("xml"),
        QStringLiteral("qml"), QStringLiteral("qrc"), QStringLiteral("ui"), QStringLiteral("gradle")
    };
}

QStringList CodeIndexer::defaultExcludeDirs() const {
    return {
        QStringLiteral(".git"), QStringLiteral(".hg"), QStringLiteral(".svn"),
        QStringLiteral("build"), QStringLiteral("build-*"), QStringLiteral("cmake-build-*"),
        QStringLiteral("node_modules"), QStringLiteral("dist"), QStringLiteral("out"),
        QStringLiteral(".idea"), QStringLiteral(".vscode"), QStringLiteral(".cache"),
        QStringLiteral("__pycache__"), QStringLiteral(".next"), QStringLiteral(".turbo"),
        QStringLiteral("third_party"), QStringLiteral("vendor")
    };
}

bool CodeIndexer::shouldSkipDir(const QString &name, const QString &relPath, const IndexOptions &options) const {
    QStringList excludes = options.excludeDirs.isEmpty() ? defaultExcludeDirs() : options.excludeDirs;
    if (matchesAnyPattern(name, excludes))
        return true;
    if (matchesAnyPattern(relPath, excludes))
        return true;
    return false;
}

bool CodeIndexer::shouldIndexFile(const QString &filePath, const IndexOptions &options) const {
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    const QString name = fi.fileName().toLower();
    if (name == QStringLiteral("cmakelists.txt"))
        return true;
    QStringList exts = options.includeExtensions.isEmpty() ? defaultExtensions() : options.includeExtensions;
    for (const QString &e : exts) {
        if (ext == e.toLower())
            return true;
    }
    return false;
}

bool CodeIndexer::isLikelyBinary(const QByteArray &sample) {
    if (sample.isEmpty())
        return false;
    int suspicious = 0;
    const int n = sample.size();
    for (unsigned char c : sample) {
        if (c == 0) {
            return true;
        }
        if (c < 7 || (c > 13 && c < 32) || c == 127)
            ++suspicious;
    }
    return (suspicious * 10) > n; // >10% control chars
}

QString CodeIndexer::detectLanguage(const QString &filePath) {
    const QString ext = QFileInfo(filePath).suffix().toLower();
    const QString name = QFileInfo(filePath).fileName().toLower();
    if (name == QStringLiteral("cmakelists.txt"))
        return QStringLiteral("cmake");
    static const QHash<QString, QString> map = {
        {QStringLiteral("c"), QStringLiteral("c")},
        {QStringLiteral("cc"), QStringLiteral("cpp")},
        {QStringLiteral("cpp"), QStringLiteral("cpp")},
        {QStringLiteral("cxx"), QStringLiteral("cpp")},
        {QStringLiteral("h"), QStringLiteral("cpp")},
        {QStringLiteral("hh"), QStringLiteral("cpp")},
        {QStringLiteral("hpp"), QStringLiteral("cpp")},
        {QStringLiteral("hxx"), QStringLiteral("cpp")},
        {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("js"), QStringLiteral("javascript")},
        {QStringLiteral("jsx"), QStringLiteral("javascript")},
        {QStringLiteral("ts"), QStringLiteral("typescript")},
        {QStringLiteral("tsx"), QStringLiteral("typescript")},
        {QStringLiteral("qml"), QStringLiteral("qml")},
        {QStringLiteral("json"), QStringLiteral("json")},
        {QStringLiteral("md"), QStringLiteral("markdown")},
        {QStringLiteral("sql"), QStringLiteral("sql")},
        {QStringLiteral("yml"), QStringLiteral("yaml")},
        {QStringLiteral("yaml"), QStringLiteral("yaml")},
        {QStringLiteral("sh"), QStringLiteral("shell")},
        {QStringLiteral("ps1"), QStringLiteral("powershell")},
        {QStringLiteral("java"), QStringLiteral("java")},
        {QStringLiteral("go"), QStringLiteral("go")},
        {QStringLiteral("rs"), QStringLiteral("rust")},
    };
    return map.value(ext, ext);
}

QString CodeIndexer::projectNamespace(const QString &rootPath) {
    const QString norm = normalizeRoot(rootPath);
    return QStringLiteral("code:%1").arg(hashString(norm));
}

QVector<CodeChunk> CodeIndexer::chunkFile(const QString &rootPath,
                                          const QString &absolutePath,
                                          const IndexOptions &options) {
    QVector<CodeChunk> chunks;
    QFile f(absolutePath);
    if (!f.open(QIODevice::ReadOnly))
        return chunks;

    const QByteArray raw = f.read(options.maxFileBytes + 1);
    if (raw.size() > options.maxFileBytes)
        return chunks;
    if (isLikelyBinary(raw.left(4096)))
        return chunks;

    const QString text = QString::fromUtf8(raw);
    const QStringList lines = text.split('\n');
    if (lines.isEmpty())
        return chunks;

    const int chunkLines = std::max(10, options.chunkLines);
    const int overlap = std::clamp(options.chunkOverlapLines, 0, chunkLines - 1);
    const int step = std::max(1, chunkLines - overlap);
    const QString normRoot = normalizeRoot(rootPath);
    const QString rel = relativeToRoot(normRoot, absolutePath);
    const QString lang = detectLanguage(absolutePath);

    for (int start = 0; start < lines.size() && chunks.size() < options.maxChunksPerFile; start += step) {
        const int end = std::min(static_cast<int>(lines.size()), start + chunkLines);
        QStringList slice;
        slice.reserve(end - start);
        for (int i = start; i < end; ++i)
            slice.push_back(lines[i]);
        QString content = slice.join('\n').trimmed();
        if (content.isEmpty()) {
            if (end >= lines.size())
                break;
            continue;
        }

        CodeChunk c;
        c.filePath = rel;
        c.absolutePath = absolutePath;
        c.startLine = start + 1;
        c.endLine = end;
        c.content = content;
        c.language = lang;
        c.id = QStringLiteral("code_%1_%2_%3")
                   .arg(hashString(normRoot), hashString(rel), QString::number(c.startLine));
        chunks.push_back(std::move(c));

        if (end >= lines.size())
            break;
    }
    return chunks;
}

QJsonArray CodeIndexer::fileTree(const QString &rootPath, const IndexOptions &options) const {
    QJsonArray out;
    const QString root = normalizeRoot(rootPath);
    QDir rootDir(root);
    if (!rootDir.exists())
        return out;

    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    QMap<QString, QJsonArray> childrenByDir;
    childrenByDir.insert(root, QJsonArray{});

    while (it.hasNext()) {
        const QString abs = it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = relativeToRoot(root, abs);
        const QString parentRel = QFileInfo(rel).path();
        const QString parentKey = parentRel == QStringLiteral(".") ? root : QDir(root).absoluteFilePath(parentRel);

        if (fi.isDir()) {
            if (shouldSkipDir(fi.fileName(), rel, options)) {
                // QDirIterator doesn't support pruning easily; we just skip adding.
                continue;
            }
            if (!childrenByDir.contains(abs))
                childrenByDir.insert(abs, QJsonArray{});
            QJsonObject node;
            node.insert(QStringLiteral("name"), fi.fileName());
            node.insert(QStringLiteral("path"), rel);
            node.insert(QStringLiteral("type"), QStringLiteral("directory"));
            childrenByDir[parentKey].push_back(node);
            continue;
        }

        if (fi.isFile()) {
            // Skip files under excluded dirs
            bool excluded = false;
            const QStringList parts = rel.split('/');
            for (int i = 0; i + 1 < parts.size(); ++i) {
                if (shouldSkipDir(parts[i], parts.mid(0, i + 1).join('/'), options)) {
                    excluded = true;
                    break;
                }
            }
            if (excluded)
                continue;
            if (!shouldIndexFile(abs, options))
                continue;
            QJsonObject node;
            node.insert(QStringLiteral("name"), fi.fileName());
            node.insert(QStringLiteral("path"), rel);
            node.insert(QStringLiteral("type"), QStringLiteral("file"));
            node.insert(QStringLiteral("size"), static_cast<double>(fi.size()));
            node.insert(QStringLiteral("language"), detectLanguage(abs));
            childrenByDir[parentKey].push_back(node);
        }
    }

    // Build a simple flat tree representation with paths; for nested tree we'd need more work.
    // Return flat list sorted by path - easy for consumers and sufficient for "file tree + content chunks".
    QVector<QJsonObject> flat;
    for (auto itMap = childrenByDir.begin(); itMap != childrenByDir.end(); ++itMap) {
        for (const auto &v : itMap.value()) {
            flat.push_back(v.toObject());
        }
    }
    std::sort(flat.begin(), flat.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("path")).toString() < b.value(QStringLiteral("path")).toString();
    });
    for (const auto &o : flat)
        out.push_back(o);
    return out;
}

IndexStats CodeIndexer::indexProject(const QString &rootPath, const IndexOptions &options) {
    IndexStats stats;
    const QString root = normalizeRoot(rootPath);
    QDir rootDir(root);
    if (!rootDir.exists()) {
        stats.errors.push_back(QStringLiteral("Root does not exist: %1").arg(root));
        return stats;
    }

    if (m_store) {
        m_store->ensureSchema();
        m_store->removeByNamespace(projectNamespace(root));
    }

    QDirIterator it(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = relativeToRoot(root, abs);

        bool excluded = false;
        const QStringList parts = rel.split('/');
        for (int i = 0; i + 1 < parts.size(); ++i) {
            if (shouldSkipDir(parts[i], parts.mid(0, i + 1).join('/'), options)) {
                excluded = true;
                break;
            }
        }
        if (excluded)
            continue;
        if (!shouldIndexFile(abs, options))
            continue;

        ++stats.filesScanned;
        if (fi.size() > options.maxFileBytes) {
            ++stats.skippedLarge;
            continue;
        }

        QFile probe(abs);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray sample = probe.read(4096);
            if (isLikelyBinary(sample)) {
                ++stats.skippedBinary;
                continue;
            }
        }

        const QVector<CodeChunk> chunks = chunkFile(root, abs, options);
        if (chunks.isEmpty())
            continue;

        ++stats.filesIndexed;
        if (!m_store) {
            stats.chunksStored += chunks.size();
            continue;
        }

        for (const CodeChunk &c : chunks) {
            VectorEntry e;
            e.id = c.id;
            e.namespaceName = projectNamespace(root);
            e.content = QStringLiteral("%1:%2-%3\n%4").arg(c.filePath).arg(c.startLine).arg(c.endLine).arg(c.content);
            e.metadata = QJsonObject{
                {QStringLiteral("file_path"), c.filePath},
                {QStringLiteral("absolute_path"), c.absolutePath},
                {QStringLiteral("start_line"), c.startLine},
                {QStringLiteral("end_line"), c.endLine},
                {QStringLiteral("language"), c.language},
                {QStringLiteral("type"), QStringLiteral("code_chunk")},
                {QStringLiteral("root"), root},
            };
            e.createdAt = QDateTime::currentMSecsSinceEpoch();

            if (m_embeddingProvider) {
                e.embedding = m_embeddingProvider(e.content);
            } else if (m_store->embeddingProvider()) {
                e.embedding = m_store->embeddingProvider()(e.content);
            }

            if (m_store->upsert(e))
                ++stats.chunksStored;
            else
                stats.errors.push_back(QStringLiteral("Failed to upsert chunk: %1").arg(c.id));
        }
    }

    return stats;
}

QVector<CodeSearchResult> CodeIndexer::search(const QString &query,
                                              const QString &rootPath,
                                              int topK,
                                              float semanticWeight) const {
    QVector<CodeSearchResult> out;
    if (!m_store || query.trimmed().isEmpty())
        return out;

    QVector<float> queryEmbedding;
    if (m_embeddingProvider)
        queryEmbedding = m_embeddingProvider(query);
    else if (m_store->embeddingProvider())
        queryEmbedding = m_store->embeddingProvider()(query);

    const auto results = m_store->hybridSearch(query, queryEmbedding, projectNamespace(rootPath), topK, semanticWeight);
    out.reserve(results.size());
    for (const auto &r : results) {
        CodeSearchResult c;
        c.id = r.id;
        c.filePath = r.metadata.value(QStringLiteral("file_path")).toString();
        c.startLine = r.metadata.value(QStringLiteral("start_line")).toInt(1);
        c.endLine = r.metadata.value(QStringLiteral("end_line")).toInt(c.startLine);
        c.content = r.content;
        c.score = r.score;
        c.semanticScore = r.semanticScore;
        c.keywordScore = r.keywordScore;
        out.push_back(std::move(c));
    }
    return out;
}

} // namespace ModelBench
