#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace ModelBench {

class VectorStore;

struct CodeChunk {
    QString id;
    QString filePath;      // relative to project root
    QString absolutePath;
    int startLine = 1;
    int endLine = 1;
    QString content;
    QString language;
};

struct IndexOptions {
    int maxFileBytes = 512 * 1024;
    int chunkLines = 80;
    int chunkOverlapLines = 10;
    int maxChunksPerFile = 200;
    QStringList includeExtensions; // empty = default text/code extensions
    QStringList excludeDirs;       // names or relative paths
};

struct IndexStats {
    int filesScanned = 0;
    int filesIndexed = 0;
    int chunksStored = 0;
    int skippedBinary = 0;
    int skippedLarge = 0;
    QStringList errors;
};

struct CodeSearchResult {
    QString id;
    QString filePath;
    int startLine = 1;
    int endLine = 1;
    QString content;
    float score = 0.0f;
    float semanticScore = 0.0f;
    float keywordScore = 0.0f;
};

class CodeIndexer {
public:
    using EmbeddingProvider = std::function<QVector<float>(const QString &text)>;

    explicit CodeIndexer(VectorStore *store = nullptr);

    void setVectorStore(VectorStore *store) { m_store = store; }
    VectorStore *vectorStore() const { return m_store; }
    void setEmbeddingProvider(EmbeddingProvider provider) { m_embeddingProvider = std::move(provider); }

    IndexStats indexProject(const QString &rootPath, const IndexOptions &options = {});
    QJsonArray fileTree(const QString &rootPath, const IndexOptions &options = {}) const;
    QVector<CodeSearchResult> search(const QString &query,
                                     const QString &rootPath,
                                     int topK = 10,
                                     float semanticWeight = 0.7f) const;

    static QString projectNamespace(const QString &rootPath);
    static QVector<CodeChunk> chunkFile(const QString &rootPath,
                                        const QString &absolutePath,
                                        const IndexOptions &options = {});
    static bool isLikelyBinary(const QByteArray &sample);
    static QString detectLanguage(const QString &filePath);

private:
    bool shouldSkipDir(const QString &name, const QString &relPath, const IndexOptions &options) const;
    bool shouldIndexFile(const QString &filePath, const IndexOptions &options) const;
    QStringList defaultExtensions() const;
    QStringList defaultExcludeDirs() const;

    VectorStore *m_store = nullptr;
    EmbeddingProvider m_embeddingProvider;
};

} // namespace ModelBench
