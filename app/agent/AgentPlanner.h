#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace ModelBench {

// A single step of an agent run. A step is either a reasoning step (no
// toolName) or a tool call whose observation feeds the next step.
struct PlanStep {
    Q_GADGET
public:
    enum class Status { Pending, Running, Done, Failed, Skipped };
    Q_ENUM(Status)

    int index = 0;
    QString description;
    QString toolName;
    QJsonObject args;
    bool requiresTool = false;
    Status status = Status::Pending;
    QString observation;

    QJsonObject toJson() const;
    static PlanStep fromJson(const QJsonObject &json);
    static QString statusName(Status status);
};

// Turns a user goal into an executable plan. An explicit JSON plan is
// accepted verbatim; otherwise a conservative keyword-based planner creates
// the steps. The LLM/UI can replace the plan at any time via setPlan().
class AgentPlanner : public QObject {
    Q_OBJECT
public:
    explicit AgentPlanner(QObject *parent = nullptr);
    ~AgentPlanner() override;

    // Builds a plan from a goal. `context` is the agent memory summary.
    QJsonArray plan(const QString &goal, const QString &context = {});
    void setPlan(const QJsonArray &stepsJson);

    QList<PlanStep> steps() const { return m_steps; }
    PlanStep step(int index) const;
    int size() const { return m_steps.size(); }
    int nextPendingIndex() const;
    bool hasNext() const { return nextPendingIndex() >= 0; }

    void markStep(int index, PlanStep::Status status,
                  const QString &observation = {});
    void appendStep(const PlanStep &step);
    void clear();

    QJsonArray toJson() const;
    int maxSteps() const { return m_maxSteps; }
    void setMaxSteps(int count);

signals:
    void planCreated(const QJsonArray &steps);
    void stepUpdated(int index, PlanStep::Status status,
                     const QString &observation);
    void planCleared();

private:
    QList<PlanStep> m_steps;
    int m_maxSteps = 12;
};

} // namespace ModelBench

Q_DECLARE_METATYPE(ModelBench::PlanStep)
Q_DECLARE_METATYPE(ModelBench::PlanStep::Status)
