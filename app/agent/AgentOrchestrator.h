#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "AgentPlanner.h"
#include "AgentMemory.h"
#include "tools/ToolRegistry.h"

namespace ModelBench {

class ToolApproval;
class FileEditHistory;

// Drives an agent run: plan -> tool call -> observation -> refinement.
// The registry executes tools through the approval gate; all tool events are
// consumed on a queued connection so a synchronous tool result cannot
// re-enter the loop in the middle of a step.
class AgentOrchestrator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY statusChanged)
    Q_PROPERTY(QString goal READ goal NOTIFY statusChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY statusChanged)
public:
    explicit AgentOrchestrator(ToolRegistry *registry = nullptr,
                               ToolApproval *approval = nullptr,
                               QObject *parent = nullptr);
    ~AgentOrchestrator() override;

    ToolRegistry *registry() const { return m_registry; }
    ToolApproval *approval() const { return m_approval; }
    AgentMemory *memory() { return &m_memory; }
    AgentPlanner *planner() { return &m_planner; }
    FileEditHistory *editHistory() const { return m_history; }

    bool isRunning() const { return m_running; }
    QString goal() const { return m_goal; }
    int currentStep() const { return m_currentStep; }
    QJsonObject status() const;
    QJsonObject exportRun() const;

    Q_INVOKABLE void run(const QString &goal);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void approve(qint64 callId, bool allowed);
    Q_INVOKABLE void approveCurrent(bool allowed);
    Q_INVOKABLE bool undoLastEdit();
    Q_INVOKABLE void clearMemory();

signals:
    void agentStarted(const QString &goal);
    void planReady(const QJsonArray &steps);
    void agentStepStarted(int step, const QString &description,
                          const QString &tool, const QJsonObject &args);
    void agentStepFinished(int step, const QString &description, bool ok,
                           const QString &observation);
    void agentToken(const QString &token);
    void agentError(int step, const QString &error);
    void agentFinished(bool ok, const QString &summary);
    void agentCancelled();
    void approvalRequired(qint64 callId, const QString &tool,
                          const QJsonObject &args, const QJsonObject &preview,
                          ApprovalLevel level);
    void statusChanged();

private slots:
    void onToolStarted(qint64 callId, const QString &name,
                       const QJsonObject &args);
    void onToolFinished(qint64 callId, const QString &name,
                        const ToolResult &result);
    void onToolOutput(qint64 callId, const QString &name,
                      const QString &stream, const QString &chunk);
    void onToolApprovalRequired(qint64 callId, const QString &name,
                                const QJsonObject &args,
                                const QJsonObject &preview,
                                ApprovalLevel level);

private:
    void advance();
    void finish(bool ok, const QString &summary);
    void emitObservation(const QString &text);
    void refine(const PlanStep &step, const ToolResult &result);
    QString observationFrom(const ToolResult &result) const;
    void appendRunLog(int step, const PlanStep &planStep,
                      const ToolResult &result, const QString &observation);

    ToolRegistry *m_registry = nullptr; // non-owning when injected
    ToolApproval *m_approval = nullptr; // owned by the registry
    FileEditHistory *m_history = nullptr; // child of this
    AgentMemory m_memory;
    AgentPlanner m_planner;

    QString m_goal;
    bool m_running = false;
    bool m_cancelled = false;
    int m_currentStep = -1;
    qint64 m_currentCallId = -1;
    int m_toolStepsExecuted = 0;
    int m_maxToolSteps = 20;
    QJsonArray m_runLog;
};

} // namespace ModelBench
