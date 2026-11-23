#include "VectorStore.h"

#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

namespace ModelBench {

namespace {
constexpr int kDefaultTopK = 10;

QString likePattern(const QString &query) {
    QString escaped = query;
    escaped.replace('\\', "\\\\");
    escaped.replace('%', "\\%");
    escaped.replace('_', "\\_");
    return QStringLiteral("%") + escaped + QStringLiteral("%");
}
} // namespace

VectorStore::VectorStore(QSqlDatabase db) : m_db(std::move(db)) {
    if (m_db.isValid())
        m_connectionName = m_db.connectionName();
}

VectorStore::VectorStore(const QString &connectionName) : m_connectionName(connectionName) {
    if (!m_connectionName.isEmpty())
        m_db = QSqlDatabase::database(m_connectionName);
}

QSqlDatabase VectorStore::database() const {
    if (m_db.isValid())
        return m_db;
    if (!m_connectionName.isEmpty())
        return QSqlDatabase::database(m_connectionName);
    if (QSqlDatabase::contains("modelfit"))
        return QSqlDatabase::database("modelfit");
    return QSqlDatabase::database();
}

bool VectorStore::ensureSchema() {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS vector_entries ("
            "id TEXT PRIMARY KEY, "
            "namespace TEXT NOT NULL, "
            "content TEXT NOT NULL, "
            "metadata TEXT, "
            "embedding BLOB, "
            "dim INTEGER NOT NULL DEFAULT 0, "
            "created_at INTEGER NOT NULL)"))) {
        return false;
    }
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_vector_entries_namespace ON vector_entries(namespace);"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_vector_entries_created_at ON vector_entries(created_at);"));

    ensureFts();
    return true;
}

bool VectorStore::ensureFts() const {
    if (m_ftsChecked)
        return m_ftsAvailable;
    m_ftsChecked = true;

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen()) {
        m_ftsAvailable = false;
        return false;
    }

    QSqlQuery q(db);
    // FTS5 is preferred; if unavailable we fall back to LIKE search.
    m_ftsAvailable = q.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS vector_entries_fts USING fts5("
        "id UNINDEXED, namespace UNINDEXED, content, metadata, tokenize='porter unicode61')"));
    return m_ftsAvailable;
}

bool VectorStore::upsert(const VectorEntry &entry) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen() || entry.id.isEmpty())
        return false;

    const qint64 createdAt = entry.createdAt > 0 ? entry.createdAt : QDateTime::currentMSecsSinceEpoch();
    const QByteArray blob = vectorToBlob(entry.embedding);
    const QString metadata = QString::fromUtf8(QJsonDocument(entry.metadata).toJson(QJsonDocument::Compact));

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO vector_entries (id, namespace, content, metadata, embedding, dim, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET "
        "namespace=excluded.namespace, content=excluded.content, metadata=excluded.metadata, "
        "embedding=excluded.embedding, dim=excluded.dim, created_at=excluded.created_at"));
    q.addBindValue(entry.id);
    q.addBindValue(entry.namespaceName);
    q.addBindValue(entry.content);
    q.addBindValue(metadata);
    q.addBindValue(blob);
    q.addBindValue(entry.embedding.size());
    q.addBindValue(createdAt);
    if (!q.exec())
        return false;

    if (ensureFts()) {
        QSqlQuery del(db);
        del.prepare(QStringLiteral("DELETE FROM vector_entries_fts WHERE id = ?"));
        del.addBindValue(entry.id);
        del.exec();

        QSqlQuery ins(db);
        ins.prepare(QStringLiteral(
            "INSERT INTO vector_entries_fts (id, namespace, content, metadata) VALUES (?, ?, ?, ?)"));
        ins.addBindValue(entry.id);
        ins.addBindValue(entry.namespaceName);
        ins.addBindValue(entry.content);
        ins.addBindValue(metadata);
        ins.exec();
    }

    return true;
}

bool VectorStore::remove(const QString &id) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM vector_entries WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        return false;

    if (ensureFts()) {
        QSqlQuery f(db);
        f.prepare(QStringLiteral("DELETE FROM vector_entries_fts WHERE id = ?"));
        f.addBindValue(id);
        f.exec();
    }
    return true;
}

bool VectorStore::removeByNamespace(const QString &namespaceName) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM vector_entries WHERE namespace = ?"));
    q.addBindValue(namespaceName);
    if (!q.exec())
        return false;

    if (ensureFts()) {
        QSqlQuery f(db);
        f.prepare(QStringLiteral("DELETE FROM vector_entries_fts WHERE namespace = ?"));
        f.addBindValue(namespaceName);
        f.exec();
    }
    return true;
}

std::optional<VectorEntry> VectorStore::get(const QString &id) const {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return std::nullopt;

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, namespace, content, metadata, embedding, created_at FROM vector_entries WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return std::nullopt;

    VectorEntry e;
    e.id = q.value(0).toString();
    e.namespaceName = q.value(1).toString();
    e.content = q.value(2).toString();
    e.metadata = QJsonDocument::fromJson(q.value(3).toByteArray()).object();
    e.embedding = blobToVector(q.value(4).toByteArray());
    e.createdAt = q.value(5).toLongLong();
    return e;
}

QVector<VectorEntry> VectorStore::listByNamespace(const QString &namespaceName, int limit) const {
    QVector<VectorEntry> out;
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return out;

    QSqlQuery q(db);
    if (namespaceName.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT id, namespace, content, metadata, embedding, created_at "
            "FROM vector_entries ORDER BY created_at DESC LIMIT ?"));
        q.addBindValue(limit);
    } else {
        q.prepare(QStringLiteral(
            "SELECT id, namespace, content, metadata, embedding, created_at "
            "FROM vector_entries WHERE namespace = ? ORDER BY created_at DESC LIMIT ?"));
        q.addBindValue(namespaceName);
        q.addBindValue(limit);
    }
    if (!q.exec())
        return out;
    while (q.next()) {
        VectorEntry e;
        e.id = q.value(0).toString();
        e.namespaceName = q.value(1).toString();
        e.content = q.value(2).toString();
        e.metadata = QJsonDocument::fromJson(q.value(3).toByteArray()).object();
        e.embedding = blobToVector(q.value(4).toByteArray());
        e.createdAt = q.value(5).toLongLong();
        out.push_back(std::move(e));
    }
    return out;
}

int VectorStore::count(const QString &namespaceName) const {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return 0;
    QSqlQuery q(db);
    if (namespaceName.isEmpty()) {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM vector_entries"));
    } else {
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM vector_entries WHERE namespace = ?"));
        q.addBindValue(namespaceName);
    }
    if (!q.exec() || !q.next())
        return 0;
    return q.value(0).toInt();
}

QVector<VectorSearchResult> VectorStore::semanticSearch(const QVector<float> &queryEmbedding,
                                                        const QString &namespaceName,
                                                        int topK,
                                                        float minScore) const {
    QVector<VectorSearchResult> results;
    if (queryEmbedding.isEmpty() || topK <= 0)
        return results;

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return results;

    QSqlQuery q(db);
    if (namespaceName.isEmpty()) {
        q.prepare(QStringLiteral(
            "SELECT id, namespace, content, metadata, embedding FROM vector_entries WHERE dim > 0"));
    } else {
        q.prepare(QStringLiteral(
            "SELECT id, namespace, content, metadata, embedding FROM vector_entries WHERE namespace = ? AND dim > 0"));
        q.addBindValue(namespaceName);
    }
    if (!q.exec())
        return results;

    while (q.next()) {
        const QVector<float> emb = blobToVector(q.value(4).toByteArray());
        const float score = cosineSimilarity(queryEmbedding, emb);
        if (score < minScore)
            continue;
        VectorSearchResult r;
        r.id = q.value(0).toString();
        r.namespaceName = q.value(1).toString();
        r.content = q.value(2).toString();
        r.metadata = QJsonDocument::fromJson(q.value(3).toByteArray()).object();
        r.score = score;
        r.semanticScore = score;
        results.push_back(std::move(r));
    }

    std::sort(results.begin(), results.end(), [](const VectorSearchResult &a, const VectorSearchResult &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    });
    if (results.size() > topK)
        results.resize(topK);
    return results;
}

float VectorStore::keywordScoreFromRank(double rank, double bestRank) {
    // FTS5 bm25: more negative is better. Normalize to [0,1].
    if (bestRank >= 0.0)
        return 1.0f;
    const double denom = std::abs(bestRank);
    if (denom <= 0.0)
        return 0.0f;
    const double normalized = 1.0 - (std::abs(rank) / (denom + 1.0));
    return static_cast<float>(std::clamp(normalized, 0.0, 1.0));
}

QVector<VectorSearchResult> VectorStore::keywordSearch(const QString &query,
                                                       const QString &namespaceName,
                                                       int topK) const {
    QVector<VectorSearchResult> results;
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty() || topK <= 0)
        return results;

    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return results;

    if (ensureFts()) {
        QSqlQuery q(db);
        // Join to get full metadata/content from main table; FTS5 bm25 returns lower=better.
        QString sql = QStringLiteral(
            "SELECT e.id, e.namespace, e.content, e.metadata, bm25(vector_entries_fts) AS rank "
            "FROM vector_entries_fts f "
            "JOIN vector_entries e ON e.id = f.id "
            "WHERE vector_entries_fts MATCH ?");
        if (!namespaceName.isEmpty())
            sql += QStringLiteral(" AND e.namespace = ?");
        sql += QStringLiteral(" ORDER BY rank LIMIT ?");
        q.prepare(sql);
        q.addBindValue(trimmed);
        if (!namespaceName.isEmpty())
            q.addBindValue(namespaceName);
        q.addBindValue(topK);

        if (q.exec()) {
            QVector<std::pair<VectorSearchResult, double>> tmp;
            double bestRank = 0.0;
            bool first = true;
            while (q.next()) {
                VectorSearchResult r;
                r.id = q.value(0).toString();
                r.namespaceName = q.value(1).toString();
                r.content = q.value(2).toString();
                r.metadata = QJsonDocument::fromJson(q.value(3).toByteArray()).object();
                const double rank = q.value(4).toDouble();
                if (first) {
                    bestRank = rank;
                    first = false;
                }
                tmp.push_back({std::move(r), rank});
            }
            for (auto &p : tmp) {
                p.first.keywordScore = keywordScoreFromRank(p.second, bestRank);
                p.first.score = p.first.keywordScore;
                results.push_back(std::move(p.first));
            }
            return results;
        }
        // If MATCH syntax fails (e.g. special chars), fall through to LIKE.
    }

    QSqlQuery q(db);
    QString sql = QStringLiteral(
        "SELECT id, namespace, content, metadata FROM vector_entries "
        "WHERE (content LIKE ? ESCAPE '\\' OR metadata LIKE ? ESCAPE '\\')");
    if (!namespaceName.isEmpty())
        sql += QStringLiteral(" AND namespace = ?");
    sql += QStringLiteral(" ORDER BY created_at DESC LIMIT ?");
    q.prepare(sql);
    const QString pat = likePattern(trimmed);
    q.addBindValue(pat);
    q.addBindValue(pat);
    if (!namespaceName.isEmpty())
        q.addBindValue(namespaceName);
    q.addBindValue(topK);
    if (!q.exec())
        return results;
    int rank = 0;
    while (q.next()) {
        VectorSearchResult r;
        r.id = q.value(0).toString();
        r.namespaceName = q.value(1).toString();
        r.content = q.value(2).toString();
        r.metadata = QJsonDocument::fromJson(q.value(3).toByteArray()).object();
        // Simple descending score by recency/rank in fallback.
        r.keywordScore = 1.0f / static_cast<float>(1 + rank);
        r.score = r.keywordScore;
        results.push_back(std::move(r));
        ++rank;
    }
    return results;
}

QVector<VectorSearchResult> VectorStore::hybridSearch(const QString &query,
                                                      const QVector<float> &queryEmbedding,
                                                      const QString &namespaceName,
                                                      int topK,
                                                      float semanticWeight) const {
    if (topK <= 0)
        topK = kDefaultTopK;
    semanticWeight = std::clamp(semanticWeight, 0.0f, 1.0f);
    const float keywordWeight = 1.0f - semanticWeight;

    const QVector<VectorSearchResult> semantic =
        queryEmbedding.isEmpty() ? QVector<VectorSearchResult>{}
                                 : semanticSearch(queryEmbedding, namespaceName, std::max(topK * 3, topK), -1.0f);
    const QVector<VectorSearchResult> keyword =
        query.trimmed().isEmpty() ? QVector<VectorSearchResult>{}
                                  : keywordSearch(query, namespaceName, std::max(topK * 3, topK));

    struct Accum {
        VectorSearchResult r;
        bool hasSemantic = false;
        bool hasKeyword = false;
    };
    QHash<QString, Accum> merged;

    auto upsertResult = [&](const VectorSearchResult &src, bool isSemantic) {
        auto it = merged.find(src.id);
        if (it == merged.end()) {
            Accum a;
            a.r = src;
            a.r.semanticScore = isSemantic ? src.semanticScore : 0.0f;
            a.r.keywordScore = isSemantic ? 0.0f : src.keywordScore;
            a.hasSemantic = isSemantic;
            a.hasKeyword = !isSemantic;
            merged.insert(src.id, std::move(a));
            return;
        }
        if (isSemantic) {
            it->r.semanticScore = src.semanticScore;
            it->hasSemantic = true;
        } else {
            it->r.keywordScore = src.keywordScore;
            it->hasKeyword = true;
            // Prefer keyword content/metadata if semantic didn't fill it.
            if (it->r.content.isEmpty())
                it->r.content = src.content;
            if (it->r.metadata.isEmpty())
                it->r.metadata = src.metadata;
        }
        if (it->r.namespaceName.isEmpty())
            it->r.namespaceName = src.namespaceName;
    };

    // Normalize semantic scores to [0,1] for hybrid combination.
    float semMax = 0.0f;
    float semMin = 1.0f;
    for (const auto &r : semantic) {
        semMax = std::max(semMax, r.semanticScore);
        semMin = std::min(semMin, r.semanticScore);
    }
    const float semRange = semMax - semMin;
    for (const auto &r : semantic) {
        VectorSearchResult norm = r;
        if (semRange > 1e-6f)
            norm.semanticScore = (r.semanticScore - semMin) / semRange;
        else
            norm.semanticScore = r.semanticScore > 0.0f ? 1.0f : 0.0f;
        upsertResult(norm, true);
    }

    float kwMax = 0.0f;
    for (const auto &r : keyword)
        kwMax = std::max(kwMax, r.keywordScore);
    for (const auto &r : keyword) {
        VectorSearchResult norm = r;
        norm.keywordScore = kwMax > 1e-6f ? (r.keywordScore / kwMax) : r.keywordScore;
        upsertResult(norm, false);
    }

    QVector<VectorSearchResult> out;
    out.reserve(merged.size());
    for (auto it = merged.begin(); it != merged.end(); ++it) {
        VectorSearchResult r = it->r;
        const float sem = it->hasSemantic ? r.semanticScore : 0.0f;
        const float kw = it->hasKeyword ? r.keywordScore : 0.0f;
        if (queryEmbedding.isEmpty())
            r.score = kw;
        else if (query.trimmed().isEmpty())
            r.score = sem;
        else
            r.score = semanticWeight * sem + keywordWeight * kw;
        out.push_back(std::move(r));
    }

    std::sort(out.begin(), out.end(), [](const VectorSearchResult &a, const VectorSearchResult &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    });
    if (out.size() > topK)
        out.resize(topK);
    return out;
}

void VectorStore::setEmbeddingProvider(EmbeddingProvider provider) {
    m_embeddingProvider = std::move(provider);
}

bool VectorStore::embedAndUpsert(VectorEntry entry) {
    if (!m_embeddingProvider)
        return false;
    if (entry.embedding.isEmpty())
        entry.embedding = m_embeddingProvider(entry.content);
    if (entry.embedding.isEmpty())
        return false;
    return upsert(entry);
}

QByteArray VectorStore::vectorToBlob(const QVector<float> &vec) {
    if (vec.isEmpty())
        return {};
    QByteArray blob;
    blob.resize(static_cast<int>(vec.size() * sizeof(float)));
    std::memcpy(blob.data(), vec.constData(), static_cast<size_t>(blob.size()));
    return blob;
}

QVector<float> VectorStore::blobToVector(const QByteArray &blob) {
    if (blob.isEmpty() || (blob.size() % static_cast<int>(sizeof(float))) != 0)
        return {};
    const int count = blob.size() / static_cast<int>(sizeof(float));
    QVector<float> vec(count);
    std::memcpy(vec.data(), blob.constData(), static_cast<size_t>(blob.size()));
    return vec;
}

float VectorStore::cosineSimilarity(const QVector<float> &a, const QVector<float> &b) {
    if (a.isEmpty() || b.isEmpty() || a.size() != b.size())
        return 0.0f;
    double dot = 0.0;
    double na = 0.0;
    double nb = 0.0;
    for (int i = 0; i < a.size(); ++i) {
        const double av = static_cast<double>(a[i]);
        const double bv = static_cast<double>(b[i]);
        dot += av * bv;
        na += av * av;
        nb += bv * bv;
    }
    if (na <= 0.0 || nb <= 0.0)
        return 0.0f;
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

} // namespace ModelBench
