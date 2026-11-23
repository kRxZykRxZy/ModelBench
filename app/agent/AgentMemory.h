#pragma once

#include <QObject>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace ModelBench {

class VectorStore;
#if HAVE_LLAMA_CPP
struct llama_model;
#endif

struct MemoryNote {
    qint64 timestamp = 0;
    QString category;
    QString text;

    QJsonObject toJson() const;
    static MemoryNote fromJson(const QJsonObject &json);
};

struct ScratchpadEntry {
    QString conversationId;
    QString key;
    QString value;
    qint64 updatedAt = 0;
};

struct ProjectContextEntry {
    QString projectPath;
    QString key;
    QString value;
    qint64 updatedAt = 0;
};

struct MemoryRecord {
    QString id;
    QString conversationId;
    QString content;
    QJsonObject metadata;
    float score = 0.0f;
    float semanticScore = 0.0f;
    float keywordScore = 0.0f;
    qint64 createdAt = 0;
};

struct MemorySearchQuery {
    QString query;
    QString conversationId;
    QString projectPath;
    int topK = 8;
    float semanticWeight = 0.7f;
    bool includeProjectContext = true;
};

// Agent memory: per-conversation scratchpad, project context, durable facts,
// and optional semantic/keyword recall via VectorStore.
class AgentMemory : public QObject {
    Q_OBJECT
public:
    explicit AgentMemory(QObject *parent = nullptr);
    explicit AgentMemory(QSqlDatabase db, QObject *parent = nullptr);
    explicit AgentMemory(const QString &connectionName, QObject *parent = nullptr);
    ~AgentMemory() override;

    QSqlDatabase database() const;
    bool ensureSchema();

    void setVectorStore(VectorStore *store) { m_vectorStore = store; }
    VectorStore *vectorStore() const { return m_vectorStore; }

    // In-memory notes (compatibility API)
    Q_INVOKABLE void addNote(const QString &text, const QString &category = QStringLiteral("note"));
    QList<MemoryNote> notes(const QString &category = {}) const;
    Q_INVOKABLE int noteCount() const { return m_notes.size(); }
    Q_INVOKABLE void clearNotes();

    // In-memory project/facts (compatibility API)
    Q_INVOKABLE void setProjectValue(const QString &key, const QJsonValue &value);
    QJsonValue projectValue(const QString &key) const;
    QJsonObject projectContext() const { return m_project; }
    void setProjectContext(const QJsonObject &context);
    Q_INVOKABLE void rememberFact(const QString &key, const QJsonValue &value);
    Q_INVOKABLE void forgetFact(const QString &key);
    QJsonObject facts() const { return m_facts; }

    Q_INVOKABLE QString buildContextSummary(int maxNotes = 20) const;
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject &data);
    Q_INVOKABLE void clear();

    int maxNotes() const { return m_maxNotes; }
    void setMaxNotes(int count);

    // SQLite-backed per-conversation scratchpad
    bool setScratchpad(const QString &conversationId, const QString &key, const QString &value);
    std::optional<QString> getScratchpad(const QString &conversationId, const QString &key) const;
    QVector<ScratchpadEntry> listScratchpad(const QString &conversationId) const;
    bool removeScratchpad(const QString &conversationId, const QString &key);
    bool clearScratchpad(const QString &conversationId);

    // SQLite-backed project context
    bool setProjectContextEntry(const QString &projectPath, const QString &key, const QString &value);
    std::optional<QString> getProjectContextEntry(const QString &projectPath, const QString &key) const;
    QVector<ProjectContextEntry> listProjectContextEntries(const QString &projectPath) const;
    bool removeProjectContextEntry(const QString &projectPath, const QString &key);
    bool clearProjectContextEntries(const QString &projectPath);

    // Convenience overloads
    bool setProjectContext(const QString &projectPath, const QString &key, const QString &value) {
        return setProjectContextEntry(projectPath, key, value);
    }
    std::optional<QString> getProjectContext(const QString &projectPath, const QString &key) const {
        return getProjectContextEntry(projectPath, key);
    }
    QVector<ProjectContextEntry> listProjectContext(const QString &projectPath) const {
        return listProjectContextEntries(projectPath);
    }
    bool clearProjectContext(const QString &projectPath) { return clearProjectContextEntries(projectPath); }

    // Semantic/keyword memory via VectorStore
    QString remember(const QString &conversationId,
                     const QString &content,
                     const QJsonObject &metadata = {},
                     const QVector<float> &embedding = {});
    bool forget(const QString &memoryId);
    QVector<MemoryRecord> recall(const MemorySearchQuery &query) const;

    // Tool-friendly memory search
    QJsonObject searchTool(const QJsonObject &args) const;
    static QJsonObject searchToolSchema();

#if HAVE_LLAMA_CPP
    static std::function<QVector<float>(const QString &)> createLlamaEmbeddingProvider(llama_model *model);
#endif

    static QString memoryNamespace(const QString &conversationId);
    static QString projectNamespace(const QString &projectPath);

signals:
    void noteAdded(const QString &category, const QString &text);
    void contextChanged();
    void memoryCleared();

private:
    QString normalizeProjectPath(const QString &projectPath) const;

    QList<MemoryNote> m_notes;
    QJsonObject m_project;
    QJsonObject m_facts;
    int m_maxNotes = 200;

    QSqlDatabase m_db;
    QString m_connectionName;
    VectorStore *m_vectorStore = nullptr;
};

} // namespace ModelBench

Q_DECLARE_METATYPE(ModelBench::MemoryNote)
