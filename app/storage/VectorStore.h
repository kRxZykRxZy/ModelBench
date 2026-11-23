#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace ModelBench {

struct VectorEntry {
    QString id;
    QString namespaceName;
    QString content;
    QJsonObject metadata;
    QVector<float> embedding;
    qint64 createdAt = 0;
};

struct VectorSearchResult {
    QString id;
    QString namespaceName;
    QString content;
    QJsonObject metadata;
    float score = 0.0f;      // higher is better
    float semanticScore = 0.0f;
    float keywordScore = 0.0f;
};

class VectorStore {
public:
    using EmbeddingProvider = std::function<QVector<float>(const QString &text)>;

    explicit VectorStore(QSqlDatabase db = {});
    explicit VectorStore(const QString &connectionName);

    QSqlDatabase database() const;
    bool ensureSchema();

    bool upsert(const VectorEntry &entry);
    bool remove(const QString &id);
    bool removeByNamespace(const QString &namespaceName);
    std::optional<VectorEntry> get(const QString &id) const;
    QVector<VectorEntry> listByNamespace(const QString &namespaceName, int limit = 1000) const;
    int count(const QString &namespaceName = {}) const;

    QVector<VectorSearchResult> semanticSearch(const QVector<float> &queryEmbedding,
                                               const QString &namespaceName = {},
                                               int topK = 10,
                                               float minScore = -1.0f) const;
    QVector<VectorSearchResult> keywordSearch(const QString &query,
                                              const QString &namespaceName = {},
                                              int topK = 10) const;
    QVector<VectorSearchResult> hybridSearch(const QString &query,
                                             const QVector<float> &queryEmbedding,
                                             const QString &namespaceName = {},
                                             int topK = 10,
                                             float semanticWeight = 0.7f) const;

    void setEmbeddingProvider(EmbeddingProvider provider);
    EmbeddingProvider embeddingProvider() const { return m_embeddingProvider; }
    bool embedAndUpsert(VectorEntry entry);

    static QByteArray vectorToBlob(const QVector<float> &vec);
    static QVector<float> blobToVector(const QByteArray &blob);
    static float cosineSimilarity(const QVector<float> &a, const QVector<float> &b);

private:
    bool ensureFts() const;
    static float keywordScoreFromRank(double rank, double bestRank);

    QSqlDatabase m_db;
    QString m_connectionName;
    EmbeddingProvider m_embeddingProvider;
    mutable bool m_ftsChecked = false;
    mutable bool m_ftsAvailable = false;
};

} // namespace ModelBench
