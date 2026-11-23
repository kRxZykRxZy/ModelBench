#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>

#include "app/agent/AgentMemory.h"
#include "app/agent/AgentOrchestrator.h"
#include "app/agent/AgentPlanner.h"
#include "app/tools/FileSystemTools.h"

using namespace ModelBench;

namespace {

bool waitFor(std::function<bool()> condition, int msecs = 10000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < msecs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}

QStringList toolNamesInPlan(const QJsonArray &steps) {
    QStringList names;
    for (const QJsonValue &v : steps) {
        const QString tool = v.toObject()[QStringLiteral("tool")].toString();
        if (!tool.isEmpty()) names << tool;
    }
    return names;
}

} // namespace

class TestAgent : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void plannerExplicitJson();
    void plannerKeywordGoals();
    void plannerStepLifecycle();
    void memoryBasics();
    void memorySerialization();
    void orchestratorReadOnlyRun();
    void orchestratorApprovalFlow();
    void orchestratorCancellation();
};

void TestAgent::initTestCase() {
    qRegisterMetaType<ApprovalLevel>("ApprovalLevel");
    qRegisterMetaType<ToolResult>("ModelBench::ToolResult");
}

void TestAgent::plannerExplicitJson() {
    AgentPlanner planner;
    const QJsonArray plan = planner.plan(
        QStringLiteral("[{\"description\":\"step one\",\"tool\":\"list_dir\","
                       "\"args\":{\"path\":\".\"},\"requiresTool\":true},"
                       "{\"description\":\"done\"}]"));
    QCOMPARE(plan.size(), 2);
    QCOMPARE(planner.step(0).toolName, QStringLiteral("list_dir"));
    QCOMPARE(planner.step(0).args[QStringLiteral("path")].toString(),
             QStringLiteral("."));
    QCOMPARE(planner.step(1).requiresTool, false);
    QVERIFY(planner.hasNext());
}

void TestAgent::plannerKeywordGoals() {
    AgentPlanner planner;
    const QJsonArray plan = planner.plan(
        QStringLiteral("Read the file src/main.cpp and search for TODO"));
    const QStringList names = toolNamesInPlan(plan);
    QVERIFY(names.contains(QStringLiteral("read_file")));
    QVERIFY(names.contains(QStringLiteral("search_code")));

    AgentPlanner gitPlanner;
    const QStringList gitTools = toolNamesInPlan(
        gitPlanner.plan(QStringLiteral("show git status and commit with message \"fix bug\"")));
    QVERIFY(gitTools.contains(QStringLiteral("git_status")));
    QVERIFY(gitTools.contains(QStringLiteral("git_commit")));
}

void TestAgent::plannerStepLifecycle() {
    AgentPlanner planner;
    QSignalSpy updated(&planner, &AgentPlanner::stepUpdated);
    const QJsonArray plan = planner.plan(QStringLiteral("list files in ."));
    QVERIFY(plan.size() >= 2);
    QCOMPARE(planner.nextPendingIndex(), 0);

    planner.markStep(0, PlanStep::Status::Done, QStringLiteral("observed"));
    QCOMPARE(planner.step(0).status, PlanStep::Status::Done);
    QCOMPARE(planner.step(0).observation, QStringLiteral("observed"));
    QCOMPARE(planner.nextPendingIndex(), 1);
    QCOMPARE(updated.count(), 1);

    PlanStep extra;
    extra.description = QStringLiteral("follow up");
    extra.toolName = QStringLiteral("git_status");
    extra.requiresTool = true;
    planner.appendStep(extra);
    QCOMPARE(planner.size(), plan.size() + 1);
    QCOMPARE(planner.step(planner.size() - 1).index, planner.size() - 1);

    AgentPlanner restored;
    restored.setPlan(planner.toJson());
    QCOMPARE(restored.size(), planner.size());
    QCOMPARE(restored.step(0).status, PlanStep::Status::Done);
}

void TestAgent::memoryBasics() {
    AgentMemory memory;
    QSignalSpy notes(&memory, &AgentMemory::noteAdded);
    memory.addNote(QStringLiteral("first"), QStringLiteral("goal"));
    memory.addNote(QStringLiteral("second"), QStringLiteral("observation"));
    QCOMPARE(memory.noteCount(), 2);
    QCOMPARE(memory.notes(QStringLiteral("goal")).size(), 1);
    QCOMPARE(notes.count(), 2);

    memory.setProjectValue(QStringLiteral("repo"), QStringLiteral("/src/app"));
    QCOMPARE(memory.projectValue(QStringLiteral("repo")).toString(),
             QStringLiteral("/src/app"));
    memory.rememberFact(QStringLiteral("branch"), QStringLiteral("main"));
    QVERIFY(memory.buildContextSummary().contains(QStringLiteral("main")));
    QVERIFY(memory.buildContextSummary().contains(QStringLiteral("second")));

    memory.forgetFact(QStringLiteral("branch"));
    QVERIFY(!memory.facts().contains(QStringLiteral("branch")));
    memory.clearNotes();
    QCOMPARE(memory.noteCount(), 0);
}

void TestAgent::memorySerialization() {
    AgentMemory memory;
    memory.addNote(QStringLiteral("keep me"), QStringLiteral("observation"));
    memory.setProjectValue(QStringLiteral("lang"), QStringLiteral("C++"));
    const QJsonObject data = memory.toJson();

    AgentMemory restored;
    QVERIFY(restored.fromJson(data));
    QCOMPARE(restored.noteCount(), 1);
    QCOMPARE(restored.notes().first().text, QStringLiteral("keep me"));
    QCOMPARE(restored.projectValue(QStringLiteral("lang")).toString(),
             QStringLiteral("C++"));

    restored.clear();
    QCOMPARE(restored.noteCount(), 0);
    QVERIFY(restored.projectContext().isEmpty());
}

void TestAgent::orchestratorReadOnlyRun() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    FileSystemTools::registerTools(registry, dir.path());
    AgentOrchestrator orchestrator(&registry);

    QSignalSpy started(&orchestrator, &AgentOrchestrator::agentStepStarted);
    QSignalSpy finished(&orchestrator, &AgentOrchestrator::agentStepFinished);
    QSignalSpy tokens(&orchestrator, &AgentOrchestrator::agentToken);
    QSignalSpy done(&orchestrator, &AgentOrchestrator::agentFinished);

    orchestrator.run(QStringLiteral("list files in %1").arg(dir.path()));
    QVERIFY(waitFor([&]() { return done.count() > 0; }));
    QVERIFY(started.count() >= 2);
    QVERIFY(finished.count() >= 2);
    QVERIFY(tokens.count() > 0);
    QVERIFY(done.at(0).at(0).toBool());

    const QJsonObject exported = orchestrator.exportRun();
    QVERIFY(exported.contains(QStringLiteral("log")));
    QVERIFY(exported[QStringLiteral("steps")].toArray().size() >= 2);
    QVERIFY(orchestrator.memory()->noteCount() > 0);
    QVERIFY(!orchestrator.isRunning());
}

void TestAgent::orchestratorApprovalFlow() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry; // Prompt mode: write_file needs confirmation
    FileSystemTools::registerTools(registry, dir.path());
    AgentOrchestrator orchestrator(&registry);

    QSignalSpy approval(&orchestrator, &AgentOrchestrator::approvalRequired);
    QSignalSpy done(&orchestrator, &AgentOrchestrator::agentFinished);
    const QString path = dir.filePath(QStringLiteral("approved.txt"));

    orchestrator.run(QStringLiteral("write file %1 with content \"hello agent\"").arg(path));
    QVERIFY(waitFor([&]() { return approval.count() > 0; }));
    QCOMPARE(approval.at(0).at(1).toString(), QStringLiteral("write_file"));
    QVERIFY(approval.at(0).at(3).toJsonObject().contains(QStringLiteral("args")));

    orchestrator.approveCurrent(true);
    QVERIFY(waitFor([&]() { return done.count() > 0; }));
    QVERIFY(done.at(0).at(0).toBool());
    QVERIFY(QFile::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("hello agent"));
}

void TestAgent::orchestratorCancellation() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    FileSystemTools::registerTools(registry, dir.path());
    AgentOrchestrator orchestrator(&registry);

    QSignalSpy approval(&orchestrator, &AgentOrchestrator::approvalRequired);
    QSignalSpy cancelled(&orchestrator, &AgentOrchestrator::agentCancelled);
    QSignalSpy done(&orchestrator, &AgentOrchestrator::agentFinished);
    const QString path = dir.filePath(QStringLiteral("cancelled.txt"));

    orchestrator.run(QStringLiteral("write file %1 with content \"should not exist\"").arg(path));
    QVERIFY(waitFor([&]() { return approval.count() > 0; }));
    orchestrator.cancel();

    QVERIFY(waitFor([&]() { return done.count() > 0; }));
    QCOMPARE(cancelled.count(), 1);
    QVERIFY(!done.at(0).at(0).toBool());
    QVERIFY(!QFile::exists(path));
    QVERIFY(!orchestrator.isRunning());
}

QTEST_MAIN(TestAgent)
#include "test_agent.moc"
