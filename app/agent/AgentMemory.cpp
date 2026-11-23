#include "AgentMemory.h"

#include "app/storage/VectorStore.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include <algorithm>

namespace ModelBench {

QJsonObject MemoryNote::toJson() const {
    QJsonObject o;
    o[QStringLiteral("timestamp")] = timestamp;
    o[QStringLiteral("category")] = category;
    o[QStringLiteral("text")] = text;
    return o;
}

MemoryNote MemoryNote::fromJson(const QJsonObject &json) {
    MemoryNote note;
    note.timestamp = qint64(json.value(QStringLiteral("timestamp")).toDouble());
    note.category = json.value(QStringLiteral("category")).toString();
    note.text = json.value(QStringLiteral("text")).toString();
    return note;
}

AgentMemory::AgentMemory(QObject *parent) : QObject(parent) {}

AgentMemory::AgentMemory(QSqlDatabase db, QObject *parent) : QObject(parent), m_db(std::move(db)) {
    if (m_db.isValid())
        m_connectionName = m_db.connectionName();
}

AgentMemory::AgentMemory(const QString &connectionName, QObject *parent)
    : QObject(parent), m_connectionName(connectionName) {
    if (!m_connectionName.isEmpty())
        m_db = QSqlDatabase::database(m_connectionName);
}

AgentMemory::~AgentMemory() = default;

QSqlDatabase AgentMemory::database() const {
    if (m_db.isValid())
        return m_db;
    if (!m_connectionName.isEmpty())
        return QSqlDatabase::database(m_connectionName);
    if (QSqlDatabase::contains(QStringLiteral("modelfit")))
        return QSqlDatabase::database(QStringLiteral("modelfit"));
    return QSqlDatabase::database();
}

bool AgentMemory::ensureSchema() {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;

    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS agent_scratchpad ("
            "conversation_id TEXT NOT NULL, "
            "key TEXT NOT NULL, "
            "value TEXT NOT NULL, "
            "updated_at INTEGER NOT NULL, "
            "PRIMARY KEY(conversation_id, key))")))
        return false;
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS agent_project_context ("
            "project_path TEXT NOT NULL, "
            "key TEXT NOT NULL, "
            "value TEXT NOT NULL, "
            "updated_at INTEGER NOT NULL, "
            "PRIMARY KEY(project_path, key))")))
        return false;
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_agent_scratchpad_conv ON agent_scratchpad(conversation_id);"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_agent_project_context_path ON agent_project_context(project_path);"));

    if (m_vectorStore)
        m_vectorStore->ensureSchema();
    return true;
}

void AgentMemory::setMaxNotes(int count) {
    if (count <= 0)
        return;
    m_maxNotes = count;
    while (m_notes.size() > m_maxNotes)
        m_notes.removeFirst();
}

void AgentMemory::addNote(const QString &text, const QString &category) {
    if (text.trimmed().isEmpty())
        return;

    MemoryNote note;
    note.timestamp = QDateTime::currentMSecsSinceEpoch();
    note.category = category.isEmpty() ? QStringLiteral("note") : category;
    note.text = text;
    m_notes.append(note);
    while (m_notes.size() > m_maxNotes)
        m_notes.removeFirst();

    emit noteAdded(note.category, note.text);
}

QList<MemoryNote> AgentMemory::notes(const QString &category) const {
    if (category.isEmpty())
        return m_notes;
    QList<MemoryNote> filtered;
    for (const MemoryNote &note : m_notes)
        if (note.category == category)
            filtered.append(note);
    return filtered;
}

void AgentMemory::clearNotes() {
    if (m_notes.isEmpty())
        return;
    m_notes.clear();
    emit memoryCleared();
}

void AgentMemory::setProjectValue(const QString &key, const QJsonValue &value) {
    if (key.isEmpty())
        return;
    if (m_project.value(key) == value)
        return;
    m_project.insert(key, value);
    emit contextChanged();
}

QJsonValue AgentMemory::projectValue(const QString &key) const {
    return m_project.value(key);
}

void AgentMemory::setProjectContext(const QJsonObject &context) {
    if (m_project == context)
        return;
    m_project = context;
    emit contextChanged();
}

void AgentMemory::rememberFact(const QString &key, const QJsonValue &value) {
    if (key.isEmpty())
        return;
    if (m_facts.value(key) == value)
        return;
    m_facts.insert(key, value);
    emit contextChanged();
}

void AgentMemory::forgetFact(const QString &key) {
    if (!m_facts.contains(key))
        return;
    m_facts.remove(key);
    emit contextChanged();
}

QString AgentMemory::buildContextSummary(int maxNotes) const {
    QStringList lines;
    if (!m_project.isEmpty()) {
        lines << QStringLiteral("Project context:");
        for (auto it = m_project.constBegin(); it != m_project.constEnd(); ++it)
            lines << QStringLiteral("  %1: %2")
                         .arg(it.key(), QString::fromUtf8(QJsonDocument(QJsonObject{{it.key(), it.value()}}).toJson(QJsonDocument::Compact)));
    }
    if (!m_facts.isEmpty()) {
        lines << QStringLiteral("Known facts:");
        for (auto it = m_facts.constBegin(); it != m_facts.constEnd(); ++it)
            lines << QStringLiteral("  %1: %2")
                         .arg(it.key(), QString::fromUtf8(QJsonDocument(QJsonObject{{it.key(), it.value()}}).toJson(QJsonDocument::Compact)));
    }
    if (!m_notes.isEmpty()) {
        lines << QStringLiteral("Recent history:");
        const int from = qMax(0, m_notes.size() - qMax(0, maxNotes));
        for (int i = from; i < m_notes.size(); ++i) {
            const MemoryNote &note = m_notes.at(i);
            lines << QStringLiteral("  [%1] %2").arg(note.category, note.text.left(300));
        }
    }
    return lines.join(u'\n');
}

QJsonObject AgentMemory::toJson() const {
    QJsonArray noteArray;
    for (const MemoryNote &note : m_notes)
        noteArray.append(note.toJson());

    QJsonObject o;
    o[QStringLiteral("notes")] = noteArray;
    o[QStringLiteral("project")] = m_project;
    o[QStringLiteral("facts")] = m_facts;
    o[QStringLiteral("maxNotes")] = m_maxNotes;
    return o;
}

bool AgentMemory::fromJson(const QJsonObject &data) {
    QList<MemoryNote> notes;
    const QJsonArray arr = data.value(QStringLiteral("notes")).toArray();
    for (const QJsonValue &v : arr)
        notes.append(MemoryNote::fromJson(v.toObject()));

    m_notes = notes;
    while (m_notes.size() > m_maxNotes)
        m_notes.removeFirst();
    m_project = data.value(QStringLiteral("project")).toObject();
    m_facts = data.value(QStringLiteral("facts")).toObject();
    const int maxNotesVal = data.value(QStringLiteral("maxNotes")).toInt(m_maxNotes);
    if (maxNotesVal > 0)
        m_maxNotes = maxNotesVal;
    emit contextChanged();
    return true;
}

void AgentMemory::clear() {
    const bool hadNotes = !m_notes.isEmpty();
    m_notes.clear();
    m_project = QJsonObject();
    m_facts = QJsonObject();
    if (hadNotes)
        emit memoryCleared();
    emit contextChanged();
}

bool AgentMemory::setScratchpad(const QString &conversationId, const QString &key, const QString &value) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen() || conversationId.isEmpty() || key.isEmpty())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO agent_scratchpad (conversation_id, key, value, updated_at) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(conversation_id, key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at"));
    q.addBindValue(conversationId);
    q.addBindValue(key);
    q.addBindValue(value);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

std::optional<QString> AgentMemory::getScratchpad(const QString &conversationId, const QString &key) const {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return std::nullopt;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM agent_scratchpad WHERE conversation_id = ? AND key = ?"));
    q.addBindValue(conversationId);
    q.addBindValue(key);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return q.value(0).toString();
}

QVector<ScratchpadEntry> AgentMemory::listScratchpad(const QString &conversationId) const {
    QVector<ScratchpadEntry> out;
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT conversation_id, key, value, updated_at FROM agent_scratchpad "
        "WHERE conversation_id = ? ORDER BY key ASC"));
    q.addBindValue(conversationId);
    if (!q.exec())
        return out;
    while (q.next()) {
        ScratchpadEntry e;
        e.conversationId = q.value(0).toString();
        e.key = q.value(1).toString();
        e.value = q.value(2).toString();
        e.updatedAt = q.value(3).toLongLong();
        out.push_back(std::move(e));
    }
    return out;
}

bool AgentMemory::removeScratchpad(const QString &conversationId, const QString &key) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM agent_scratchpad WHERE conversation_id = ? AND key = ?"));
    q.addBindValue(conversationId);
    q.addBindValue(key);
    return q.exec();
}

bool AgentMemory::clearScratchpad(const QString &conversationId) {
    QSqlDatabase db = database();
    if (!db.isValid() || !db.isOpen())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM agent_scratchpad WHERE conversation_id = ?"));
    q.addBindValue(conversationId);
    return q.exec();
}

QString AgentMemory::normalizeProjectPath(const QString &projectPath) const {
    if (projectPath.trimmed().isEmpty())
        return {};
    return QDir::cleanPath(QDir(projectPath).absolutePath());
}

bool AgentMemory::setProjectContextEntry(const QString &projectPath, const QString &key, const QString &value) {
    QSqlDatabase db = database();
    const QString norm = normalizeProjectPath(projectPath);
    if (!db.isValid() || !db.isOpen() || norm.isEmpty() || key.isEmpty())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO agent_project_context (project_path, key, value, updated_at) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(project_path, key) DO UPDATE SET value=excluded.value, updated_at=excluded.updated_at"));
    q.addBindValue(norm);
    q.addBindValue(key);
    q.addBindValue(value);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    return q.exec();
}

std::optional<QString> AgentMemory::getProjectContextEntry(const QString &projectPath, const QString &key) const {
    QSqlDatabase db = database();
    const QString norm = normalizeProjectPath(projectPath);
    if (!db.isValid() || !db.isOpen() || norm.isEmpty())
        return std::nullopt;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT value FROM agent_project_context WHERE project_path = ? AND key = ?"));
    q.addBindValue(norm);
    q.addBindValue(key);
    if (!q.exec() || !q.next())
        return std::nullopt;
    return q.value(0).toString();
}

QVector<ProjectContextEntry> AgentMemory::listProjectContextEntries(const QString &projectPath) const {
    QVector<ProjectContextEntry> out;
    QSqlDatabase db = database();
    const QString norm = normalizeProjectPath(projectPath);
    if (!db.isValid() || !db.isOpen() || norm.isEmpty())
        return out;
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT project_path, key, value, updated_at FROM agent_project_context "
        "WHERE project_path = ? ORDER BY key ASC"));
    q.addBindValue(norm);
    if (!q.exec())
        return out;
    while (q.next()) {
        ProjectContextEntry e;
        e.projectPath = q.value(0).toString();
        e.key = q.value(1).toString();
        e.value = q.value(2).toString();
        e.updatedAt = q.value(3).toLongLong();
        out.push_back(std::move(e));
    }
    return out;
}

bool AgentMemory::removeProjectContextEntry(const QString &projectPath, const QString &key) {
    QSqlDatabase db = database();
    const QString norm = normalizeProjectPath(projectPath);
    if (!db.isValid() || !db.isOpen() || norm.isEmpty())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM agent_project_context WHERE project_path = ? AND key = ?"));
    q.addBindValue(norm);
    q.addBindValue(key);
    return q.exec();
}

bool AgentMemory::clearProjectContextEntries(const QString &projectPath) {
    QSqlDatabase db = database();
    const QString norm = normalizeProjectPath(projectPath);
    if (!db.isValid() || !db.isOpen() || norm.isEmpty())
        return false;
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM agent_project_context WHERE project_path = ?"));
    q.addBindValue(norm);
    return q.exec();
}

QString AgentMemory::memoryNamespace(const QString &conversationId) {
    return QStringLiteral("memory:conv:%1").arg(conversationId);
}

QString AgentMemory::projectNamespace(const QString &projectPath) {
    const QString norm = QDir::cleanPath(QDir(projectPath).absolutePath());
    return QStringLiteral("memory:project:%1").arg(QString::fromUtf8(norm.toUtf8().toBase64(QByteArray::Base64UrlEncoding)));
}

QString AgentMemory::remember(const QString &conversationId,
                              const QString &content,
                              const QJsonObject &metadata,
                              const QVector<float> &embedding) {
    if (!m_vectorStore || conversationId.isEmpty() || content.trimmed().isEmpty())
        return {};

    m_vectorStore->ensureSchema();

    VectorEntry entry;
    entry.id = QStringLiteral("mem_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    entry.namespaceName = memoryNamespace(conversationId);
    entry.content = content;
    entry.metadata = metadata;
    entry.metadata.insert(QStringLiteral("conversation_id"), conversationId);
    entry.metadata.insert(QStringLiteral("type"), QStringLiteral("memory"));
    entry.embedding = embedding;
    entry.createdAt = QDateTime::currentMSecsSinceEpoch();
    entry.metadata.insert(QStringLiteral("created_at"), static_cast<double>(entry.createdAt));

    bool ok = false;
    if (!entry.embedding.isEmpty()) {
        ok = m_vectorStore->upsert(entry);
    } else if (m_vectorStore->embeddingProvider()) {
        ok = m_vectorStore->embedAndUpsert(entry);
    } else {
        ok = m_vectorStore->upsert(entry);
    }
    return ok ? entry.id : QString{};
}

bool AgentMemory::forget(const QString &memoryId) {
    if (!m_vectorStore || memoryId.isEmpty())
        return false;
    return m_vectorStore->remove(memoryId);
}

QVector<MemoryRecord> AgentMemory::recall(const MemorySearchQuery &query) const {
    QVector<MemoryRecord> out;
    if (!m_vectorStore || query.query.trimmed().isEmpty())
        return out;

    QVector<float> queryEmbedding;
    if (m_vectorStore->embeddingProvider())
        queryEmbedding = m_vectorStore->embeddingProvider()(query.query);

    auto toRecord = [](const VectorSearchResult &r) {
        MemoryRecord m;
        m.id = r.id;
        m.conversationId = r.metadata.value(QStringLiteral("conversation_id")).toString();
        m.content = r.content;
        m.metadata = r.metadata;
        m.score = r.score;
        m.semanticScore = r.semanticScore;
        m.keywordScore = r.keywordScore;
        m.createdAt = r.metadata.value(QStringLiteral("created_at")).toVariant().toLongLong();
        return m;
    };

    QVector<VectorSearchResult> convResults;
    if (!query.conversationId.isEmpty()) {
        convResults = m_vectorStore->hybridSearch(
            query.query, queryEmbedding, memoryNamespace(query.conversationId),
            std::max(query.topK, 1), query.semanticWeight);
    }

    QVector<VectorSearchResult> projResults;
    if (query.includeProjectContext && !query.projectPath.trimmed().isEmpty()) {
        projResults = m_vectorStore->hybridSearch(
            query.query, queryEmbedding, projectNamespace(query.projectPath),
            std::max(query.topK, 1), query.semanticWeight);
    }

    QHash<QString, MemoryRecord> byId;
    for (const auto &r : convResults)
        byId.insert(toRecord(r).id, toRecord(r));
    for (const auto &r : projResults) {
        MemoryRecord m = toRecord(r);
        const auto it = byId.find(m.id);
        if (it == byId.end() || m.score > it->score)
            byId.insert(m.id, m);
    }

    out.reserve(byId.size());
    for (auto it = byId.begin(); it != byId.end(); ++it)
        out.push_back(it.value());

    std::sort(out.begin(), out.end(), [](const MemoryRecord &a, const MemoryRecord &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.id < b.id;
    });
    if (out.size() > query.topK)
        out.resize(query.topK);
    return out;
}

QJsonObject AgentMemory::searchToolSchema() {
    QJsonObject props;
    props.insert(QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                      {QStringLiteral("description"), QStringLiteral("Search query for memory recall")}});
    props.insert(QStringLiteral("conversation_id"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                                {QStringLiteral("description"), QStringLiteral("Conversation scope for memory lookup")}});
    props.insert(QStringLiteral("project_path"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                                                             {QStringLiteral("description"), QStringLiteral("Optional project path for project memory/context")}});
    props.insert(QStringLiteral("top_k"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                                                      {QStringLiteral("description"), QStringLiteral("Maximum results to return")}});
    props.insert(QStringLiteral("semantic_weight"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                                                                {QStringLiteral("description"), QStringLiteral("0..1 weight for semantic score in hybrid search")}});
    props.insert(QStringLiteral("include_project_context"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                                                                        {QStringLiteral("description"), QStringLiteral("Include project-scoped memory results")}});
    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("query")});
    return schema;
}

QJsonObject AgentMemory::searchTool(const QJsonObject &args) const {
    MemorySearchQuery q;
    q.query = args.value(QStringLiteral("query")).toString();
    q.conversationId = args.value(QStringLiteral("conversation_id")).toString();
    q.projectPath = args.value(QStringLiteral("project_path")).toString();
    q.topK = std::max(1, args.value(QStringLiteral("top_k")).toInt(8));
    q.semanticWeight = static_cast<float>(args.value(QStringLiteral("semantic_weight")).toDouble(0.7));
    q.includeProjectContext = args.value(QStringLiteral("include_project_context")).toBool(true);

    QJsonObject out;
    if (q.query.trimmed().isEmpty()) {
        out.insert(QStringLiteral("ok"), false);
        out.insert(QStringLiteral("error"), QStringLiteral("query is required"));
        return out;
    }

    const QVector<MemoryRecord> records = recall(q);
    QJsonArray arr;
    for (const auto &r : records) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), r.id);
        o.insert(QStringLiteral("conversation_id"), r.conversationId);
        o.insert(QStringLiteral("content"), r.content);
        o.insert(QStringLiteral("metadata"), r.metadata);
        o.insert(QStringLiteral("score"), static_cast<double>(r.score));
        o.insert(QStringLiteral("semantic_score"), static_cast<double>(r.semanticScore));
        o.insert(QStringLiteral("keyword_score"), static_cast<double>(r.keywordScore));
        arr.push_back(o);
    }

    QJsonArray scratch;
    if (!q.conversationId.isEmpty()) {
        for (const auto &e : listScratchpad(q.conversationId)) {
            scratch.push_back(QJsonObject{{QStringLiteral("key"), e.key},
                                          {QStringLiteral("value"), e.value},
                                          {QStringLiteral("updated_at"), static_cast<double>(e.updatedAt)}});
        }
    }
    QJsonArray project;
    if (!q.projectPath.trimmed().isEmpty()) {
        for (const auto &e : listProjectContextEntries(q.projectPath)) {
            project.push_back(QJsonObject{{QStringLiteral("key"), e.key},
                                          {QStringLiteral("value"), e.value},
                                          {QStringLiteral("updated_at"), static_cast<double>(e.updatedAt)}});
        }
    }

    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("query"), q.query);
    out.insert(QStringLiteral("results"), arr);
    out.insert(QStringLiteral("scratchpad"), scratch);
    out.insert(QStringLiteral("project_context"), project);
    return out;
}

} // namespace ModelBench

#if HAVE_LLAMA_CPP
#include <llama.h>

#include <vector>

namespace ModelBench {

std::function<QVector<float>(const QString &)> AgentMemory::createLlamaEmbeddingProvider(llama_model *model) {
    if (!model)
        return {};

    return [model](const QString &text) -> QVector<float> {
        if (text.trimmed().isEmpty())
            return {};

        const llama_vocab *vocab = llama_model_get_vocab(model);
        if (!vocab)
            return {};

        const std::string utf8 = text.toUtf8().toStdString();
        std::vector<llama_token> tokens(static_cast<size_t>(utf8.size()) + 8);
        const int nTok = llama_tokenize(vocab, utf8.c_str(), static_cast<int>(utf8.size()),
                                        tokens.data(), static_cast<int>(tokens.size()), true, true);
        if (nTok <= 0)
            return {};
        tokens.resize(static_cast<size_t>(nTok));

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = std::max(32, nTok + 8);
        cparams.n_batch = static_cast<uint32_t>(cparams.n_ctx);
        cparams.n_ubatch = static_cast<uint32_t>(cparams.n_ctx);
        cparams.embeddings = true;
        cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        cparams.n_threads = 4;

        llama_context *ctx = llama_init_from_model(model, cparams);
        if (!ctx)
            return {};

        llama_set_embeddings(ctx, true);

        llama_batch batch = llama_batch_init(static_cast<int>(tokens.size()), 0, 1);
        for (int i = 0; i < nTok; ++i) {
            batch.token[i] = tokens[static_cast<size_t>(i)];
            batch.pos[i] = i;
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = true;
        }
        batch.n_tokens = nTok;

        QVector<float> out;
        if (llama_decode(ctx, batch) == 0) {
            const int nEmb = llama_model_n_embd(model);
            float *emb = llama_get_embeddings_seq(ctx, 0);
            if (!emb)
                emb = llama_get_embeddings_ith(ctx, -1);
            if (!emb)
                emb = llama_get_embeddings(ctx);
            if (emb && nEmb > 0) {
                out.resize(nEmb);
                std::copy(emb, emb + nEmb, out.begin());
            }
        }

        llama_batch_free(batch);
        llama_free(ctx);
        return out;
    };
}

} // namespace ModelBench
#endif
