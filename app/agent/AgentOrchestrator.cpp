#include "AgentOrchestrator.h"

#include "tools/FileEditHistory.h"
#include "tools/FileSystemTools.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>

namespace ModelBench {

AgentOrchestrator::AgentOrchestrator(ToolRegistry *registry,
                                     ToolApproval *approval, QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<ToolResult>("ModelBench::ToolResult");
    qRegisterMetaType<QJsonArray>("QJsonArray");

    if (!registry) {
        registry = new ToolRegistry(approval, this);
    } else if (approval && registry->approval() != approval) {
        registry->setApproval(approval);
    }
    m_registry = registry;
    m_approval = m_registry->approval();

    // Undoable edits apply to every registered file tool.
    m_history = new FileEditHistory(this);
    const QList<Tool *> registered = m_registry->tools();
    for (Tool *tool : registered) {
        if (auto *fileTool = qobject_cast<FileToolBase *>(tool))
            fileTool->setHistory(m_history);
    }

    connect(m_registry, &ToolRegistry::toolStarted, this,
            &AgentOrchestrator::onToolStarted, Qt::QueuedConnection);
    connect(m_registry, &ToolRegistry::toolFinished, this,
            &AgentOrchestrator::onToolFinished, Qt::QueuedConnection);
    connect(m_registry, &ToolRegistry::toolOutput, this,
            &AgentOrchestrator::onToolOutput, Qt::QueuedConnection);
    connect(m_registry, &ToolRegistry::toolApprovalRequired, this,
            &AgentOrchestrator::onToolApprovalRequired, Qt::QueuedConnection);
}

AgentOrchestrator::~AgentOrchestrator() = default;

void AgentOrchestrator::run(const QString &goal) {
    if (m_running) {
        emit agentError(-1, QStringLiteral("An agent run is already in progress"));
        return;
    }
    if (goal.trimmed().isEmpty()) {
        emit agentError(-1, QStringLiteral("Goal must not be empty"));
        return;
    }

    m_goal = goal.trimmed();
    m_running = true;
    m_cancelled = false;
    m_currentStep = -1;
    m_currentCallId = -1;
    m_toolStepsExecuted = 0;
    m_runLog = QJsonArray();

    emit agentStarted(m_goal);
    emit statusChanged();

    const QJsonArray steps = m_planner.plan(m_goal, m_memory.buildContextSummary());
    m_memory.addNote(QStringLiteral("goal: %1").arg(m_goal), QStringLiteral("goal"));
    emit planReady(steps);

    if (steps.isEmpty()) {
        finish(false, QStringLiteral("Could not produce a plan for this goal"));
        return;
    }
    advance();
}

void AgentOrchestrator::advance() {
    if (!m_running || m_cancelled) return;

    const int index = m_planner.nextPendingIndex();
    if (index < 0) {
        finish(true, QStringLiteral("Completed %1 step(s) for: %2")
                         .arg(m_planner.size()).arg(m_goal));
        return;
    }
    if (m_toolStepsExecuted >= m_maxToolSteps) {
        emit agentError(-1, QStringLiteral("Tool step limit reached (%1)")
                                .arg(m_maxToolSteps));
        finish(false, QStringLiteral("Stopped: tool step limit reached"));
        return;
    }

    m_currentStep = index;
    PlanStep step = m_planner.step(index);
    m_planner.markStep(index, PlanStep::Status::Running);
    emit agentStepStarted(index, step.description, step.toolName, step.args);
    emit statusChanged();

    // Reasoning steps produce an observation without invoking a tool.
    if (!step.requiresTool || step.toolName.isEmpty()) {
        emitObservation(step.description);
        m_planner.markStep(index, PlanStep::Status::Done, step.description);
        m_memory.addNote(step.description.left(400), QStringLiteral("reasoning"));
        emit agentStepFinished(index, step.description, true, step.description);
        QMetaObject::invokeMethod(this, &AgentOrchestrator::advance,
                                  Qt::QueuedConnection);
        return;
    }

    ++m_toolStepsExecuted;
    m_currentCallId = m_registry->execute(step.toolName, step.args);
}

void AgentOrchestrator::onToolStarted(qint64 callId, const QString &name,
                                      const QJsonObject & /*args*/) {
    if (!m_running || callId != m_currentCallId) return;
    emit agentToken(QStringLiteral("\n[%1] running\u2026\n").arg(name));
    emit statusChanged();
}

void AgentOrchestrator::onToolOutput(qint64 callId, const QString & /*name*/,
                                     const QString &stream,
                                     const QString &chunk) {
    if (!m_running || callId != m_currentCallId || chunk.isEmpty()) return;
    if (stream == QLatin1String("stderr")) emit agentToken(QStringLiteral("[stderr] "));
    emit agentToken(chunk);
}

void AgentOrchestrator::onToolApprovalRequired(
    qint64 callId, const QString &name, const QJsonObject &args,
    const QJsonObject &preview, ApprovalLevel level) {
    if (!m_running || callId != m_currentCallId) return;
    emit approvalRequired(callId, name, args, preview, level);
}

void AgentOrchestrator::onToolFinished(qint64 callId, const QString & /*name*/,
                                       const ToolResult &result) {
    if (!m_running || m_cancelled || callId != m_currentCallId) return;

    const int index = m_currentStep;
    const PlanStep step = m_planner.step(index);
    const QString observation = observationFrom(result);

    emitObservation(observation);
    m_planner.markStep(index,
                       result.ok ? PlanStep::Status::Done
                                 : PlanStep::Status::Failed,
                       observation);
    m_memory.addNote(QStringLiteral("%1: %2")
                         .arg(step.toolName, observation.left(240)),
                     result.ok ? QStringLiteral("observation")
                               : QStringLiteral("error"));
    emit agentStepFinished(index, step.description, result.ok, observation);

    appendRunLog(index, step, result, observation);
    m_currentCallId = -1;

    if (!result.ok) emit agentError(index, result.error);
    refine(step, result);

    emit statusChanged();
    QMetaObject::invokeMethod(this, &AgentOrchestrator::advance,
                              Qt::QueuedConnection);
}

QString AgentOrchestrator::observationFrom(const ToolResult &result) const {
    if (!result.ok) return result.error;

    static const QStringList textKeys{
        QStringLiteral("content"),   QStringLiteral("stdout"),
        QStringLiteral("text"),      QStringLiteral("output"),
        QStringLiteral("diff"),      QStringLiteral("commit"),
        QStringLiteral("results"),
    };
    for (const QString &key : textKeys) {
        if (!result.output.contains(key)) continue;
        const QJsonValue value = result.output.value(key);
        if (value.isString()) return value.toString();
        if (value.isArray()) {
            return QString::fromUtf8(
                QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
        }
        if (value.isObject()) {
            return QString::fromUtf8(
                QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
        }
    }
    if (result.output.isEmpty()) return QStringLiteral("OK");
    return QString::fromUtf8(
        QJsonDocument(result.output).toJson(QJsonDocument::Compact));
}

void AgentOrchestrator::emitObservation(const QString &text) {
    constexpr int kChunk = 160;
    constexpr int kMaxChars = 4000;
    const QString capped = text.left(kMaxChars);
    for (int i = 0; i < capped.size(); i += kChunk)
        emit agentToken(capped.mid(i, kChunk));
    if (text.size() > kMaxChars)
        emit agentToken(QStringLiteral("\n[observation truncated]\n"));
}

void AgentOrchestrator::refine(const PlanStep &step, const ToolResult &result) {
    if (!m_running || m_cancelled) return;

    if (!result.ok && step.toolName == QLatin1String("read_file") &&
        (result.errorCode == QLatin1String("not_found") ||
         result.error.contains(QStringLiteral("not a readable"),
                               Qt::CaseInsensitive))) {
        const QString fileName =
            QFileInfo(step.args.value(QStringLiteral("path")).toString()).fileName();
        if (!fileName.isEmpty() && m_planner.size() < m_maxToolSteps) {
            PlanStep followUp;
            followUp.description =
                QStringLiteral("Locate the missing file '%1'").arg(fileName);
            followUp.toolName = QStringLiteral("search_code");
            followUp.requiresTool = true;
            followUp.args = QJsonObject({{QStringLiteral("query"), fileName}});
            m_planner.appendStep(followUp);
            emit agentToken(QStringLiteral("\n[refine] searching for the missing file\u2026\n"));
        }
    }

    if (result.truncated && m_planner.size() < m_maxToolSteps) {
        PlanStep note;
        note.description = QStringLiteral(
            "Output of '%1' was truncated; narrow the next query if details are missing.")
                               .arg(step.toolName);
        m_planner.appendStep(note);
    }
}

void AgentOrchestrator::appendRunLog(int step, const PlanStep &planStep,
                                     const ToolResult &result,
                                     const QString &observation) {
    QJsonObject entry;
    entry[QStringLiteral("step")] = step;
    entry[QStringLiteral("description")] = planStep.description;
    entry[QStringLiteral("tool")] = planStep.toolName;
    entry[QStringLiteral("args")] = planStep.args;
    entry[QStringLiteral("ok")] = result.ok;
    entry[QStringLiteral("error")] = result.error;
    entry[QStringLiteral("observation")] = observation.left(2000);
    entry[QStringLiteral("durationMs")] = result.durationMs;
    m_runLog.append(entry);
}

void AgentOrchestrator::finish(bool ok, const QString &summary) {
    m_running = false;
    m_currentCallId = -1;
    m_memory.addNote(summary, ok ? QStringLiteral("system") : QStringLiteral("error"));
    emit agentFinished(ok, summary);
    emit statusChanged();
}

void AgentOrchestrator::cancel() {
    if (!m_running) return;
    m_cancelled = true;

    if (m_currentCallId >= 0) {
        if (m_approval && m_approval->isPending(m_currentCallId))
            m_approval->cancel(m_currentCallId);
        m_registry->cancel(m_currentCallId);
    }
    if (m_currentStep >= 0) {
        const PlanStep step = m_planner.step(m_currentStep);
        m_planner.markStep(m_currentStep, PlanStep::Status::Skipped,
                           QStringLiteral("cancelled by user"));
        emit agentStepFinished(m_currentStep, step.description, false,
                               QStringLiteral("cancelled by user"));
    }
    m_currentCallId = -1;
    emit agentCancelled();
    finish(false, QStringLiteral("Cancelled by user"));
}

void AgentOrchestrator::approve(qint64 callId, bool allowed) {
    if (m_approval) m_approval->decide(callId, allowed);
}

void AgentOrchestrator::approveCurrent(bool allowed) {
    if (m_currentCallId >= 0) approve(m_currentCallId, allowed);
}

bool AgentOrchestrator::undoLastEdit() {
    QString error;
    const bool ok = m_history->undoLast(&error);
    if (!ok) emit agentError(-1, error);
    return ok;
}

void AgentOrchestrator::clearMemory() { m_memory.clear(); }

QJsonObject AgentOrchestrator::status() const {
    QJsonObject o;
    o[QStringLiteral("running")] = m_running;
    o[QStringLiteral("cancelled")] = m_cancelled;
    o[QStringLiteral("goal")] = m_goal;
    o[QStringLiteral("currentStep")] = m_currentStep;
    o[QStringLiteral("steps")] = m_planner.size();
    o[QStringLiteral("toolStepsExecuted")] = m_toolStepsExecuted;
    o[QStringLiteral("pendingCall")] = m_currentCallId;
    o[QStringLiteral("awaitingApproval")] =
        m_approval && m_currentCallId >= 0 && m_approval->isPending(m_currentCallId);
    return o;
}

QJsonObject AgentOrchestrator::exportRun() const {
    QJsonObject o;
    o[QStringLiteral("goal")] = m_goal;
    o[QStringLiteral("steps")] = m_planner.toJson();
    o[QStringLiteral("log")] = m_runLog;
    o[QStringLiteral("memory")] = m_memory.toJson();
    o[QStringLiteral("edits")] = m_history ? m_history->journal() : QJsonArray();
    o[QStringLiteral("running")] = m_running;
    o[QStringLiteral("toolStepsExecuted")] = m_toolStepsExecuted;
    return o;
}

} // namespace ModelBench
