#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QList>
#include <QMetaType>

#include <atomic>
#include <functional>
#include <memory>

#include "ChatTemplate.h"
#include "ContextManager.h"

class QThread;

namespace ModelBench {

struct InferenceConfig {
    QString modelPath;
    int contextLength = 4096;
    int threads = 4;
    int batchSize = 512;
    int gpuLayers = 0;
    float temperature = 0.7f;
    float topP = 0.9f;
    int topK = 40;
    float minP = 0.05f;
    float repeatPenalty = 1.1f;
    int penaltyLastN = 64;
    int maxTokens = 512;
    int seed = -1;
    bool addBos = true;
    QStringList stopSequences;
};

struct InferenceResult {
    QString text;
    int promptTokens = 0;
    int completionTokens = 0;
    double tokensPerSecond = 0;
    double timeToFirstTokenMs = 0;
    double promptEvalMs = 0;
    int contextTokensUsed = 0;
    int contextCapacity = 0;
    bool cancelled = false;
    QString error;
};

class IInferenceBackend {
public:
    virtual ~IInferenceBackend() = default;
    virtual bool loadModel(const QString &path, const InferenceConfig &cfg) = 0;
    virtual void unloadModel() = 0;
    virtual bool isLoaded() const = 0;
    virtual void generate(const QString &prompt, const InferenceConfig &cfg) = 0;
    virtual void cancel() = 0;
};

/**
 * Embedded llama.cpp backend.
 *
 * All llama.cpp calls run on a dedicated worker thread. Model/context/sampler
 * are owned by the worker (see Engine in the .cpp); the public methods are
 * safe to call from the GUI thread. Streaming signals are emitted from the
 * worker thread and therefore arrive queued on the GUI thread.
 */
class LlamaBackend : public QObject, public IInferenceBackend {
    Q_OBJECT
public:
    explicit LlamaBackend(QObject *parent = nullptr);
    ~LlamaBackend() override;

    bool loadModel(const QString &path, const InferenceConfig &cfg) override;
    void unloadModel() override;
    bool isLoaded() const override { return m_loaded.load(std::memory_order_acquire); }
    void generate(const QString &prompt, const InferenceConfig &cfg) override;
    void cancel() override;

    /// Apply the loaded model's chat template to a conversation.
    QString formatChat(const QList<ChatTurn> &messages,
                       const QString &tmplOverride = {},
                       bool addAssistant = true) const;

    /// The loaded model's chat template, or an empty string.
    QString chatTemplate(const QString &name = {}) const;

    ContextManager &contextManager() { return m_context; }
    const ContextManager &contextManager() const { return m_context; }

signals:
    void tokenGenerated(const QString &token);
    void generationFinished(const InferenceResult &result);
    void generationError(const QString &error);
    void modelLoaded();
    void modelUnloaded();
    void contextUsageChanged(int used, int capacity);

private:
    struct Engine;

    bool invokeWorker(std::function<void()> fn, bool blocking) const;

    std::unique_ptr<Engine> m_engine;
    QThread *m_thread = nullptr;
    QObject *m_threadContext = nullptr;
    ContextManager m_context;
    std::atomic_bool m_loaded{false};
    std::atomic_bool m_cancelRequested{false};
    std::atomic_bool m_generating{false};
};

class InferenceManager : public QObject {
    Q_OBJECT
public:
    explicit InferenceManager(QObject *parent = nullptr);

    LlamaBackend *backend() { return &m_backend; }
    bool loadModel(const QString &path, const InferenceConfig &cfg);
    Q_INVOKABLE bool loadModelFile(const QString &path);
    void unloadModel();
    Q_INVOKABLE void unloadModelQml() { unloadModel(); }
    bool isModelLoaded() const;
    Q_INVOKABLE bool isLoaded() const { return isModelLoaded(); }
    void generate(const QString &prompt, const InferenceConfig &cfg);
    Q_INVOKABLE void generateText(const QString &prompt);
    void cancel();
    Q_INVOKABLE void cancelGeneration() { cancel(); }
    Q_INVOKABLE int contextUsed() const;
    Q_INVOKABLE int contextCapacity() const;

    QString formatChat(const QList<ChatTurn> &messages,
                       const QString &tmplOverride = {},
                       bool addAssistant = true) const;
    QString chatTemplate(const QString &name = {}) const;
    ContextManager &contextManager() { return m_backend.contextManager(); }

signals:
    void tokenGenerated(const QString &token);
    void generationFinished(const InferenceResult &result);
    void generationError(const QString &error);
    void modelLoaded();
    void modelUnloaded();
    void contextUsageChanged(int used, int capacity);

private:
    LlamaBackend m_backend;
};

} // namespace ModelBench

Q_DECLARE_METATYPE(ModelBench::InferenceResult)
