#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QThread>

#include "app/tools/ToolRegistry.h"
#include "app/tools/ToolApproval.h"

using namespace ModelBench;

namespace {

bool waitFor(std::function<bool()> condition, int msecs = 5000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < msecs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}

class DummyTool : public Tool {
public:
    DummyTool(const QString &toolName, ApprovalLevel level,
              const QString &description, QObject *parent = nullptr)
        : Tool(parent), m_name(toolName), m_level(level), m_description(description) {}

    QString name() const override { return m_name; }
    QString description() const override { return m_description; }
    ApprovalLevel approvalLevel() const override { return m_level; }
    QJsonObject inputSchema() const override {
        QJsonObject properties;
        properties[QStringLiteral("value")] =
            QJsonObject({{QStringLiteral("type"), QStringLiteral("string")}});
        return QJsonObject({
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), properties},
        });
    }
    void execute(qint64 callId, const QJsonObject &args) override {
        ++executions;
        lastArgs = args;
        ToolResult result;
        result.ok = true;
        result.output[QStringLiteral("echo")] = args;
        emit finished(callId, result);
    }

    QString m_name;
    ApprovalLevel m_level;
    QString m_description;
    int executions = 0;
    QJsonObject lastArgs;
};

ToolResult runAndWait(ToolRegistry &registry, const QString &name,
                      const QJsonObject &args = {}) {
    ToolResult captured;
    bool done = false;
    const QMetaObject::Connection connection =
        QObject::connect(&registry, &ToolRegistry::toolFinished,
                         [&](qint64, const QString &, const ToolResult &result) {
                             captured = result;
                             done = true;
                         });
    registry.execute(name, args);
    waitFor([&]() { return done; });
    QObject::disconnect(connection);
    return captured;
}

} // namespace

class TestToolRegistry : public QObject {
    Q_OBJECT
private slots:
    void registrationAndListing();
    void duplicateAndUnregister();
    void validation();
    void unknownTool();
    void readOnlyAutoApproved();
    void writeWaitsForApproval();
    void denialAndCancellation();
    void approvalModes();
    void alwaysAllowed();
    void previewRedactsSecrets();
    void describeTools();
};

void TestToolRegistry::registrationAndListing() {
    ToolRegistry registry;
    auto *tool = new DummyTool(QStringLiteral("zeta"), ApprovalLevel::ReadOnly,
                               QStringLiteral("Z tool"));
    QVERIFY(registry.registerTool(tool));
    QVERIFY(registry.registerTool(new DummyTool(QStringLiteral("alpha"),
                                                ApprovalLevel::Write,
                                                QStringLiteral("A tool"))));
    QCOMPARE(registry.toolNames(),
             QStringList({QStringLiteral("alpha"), QStringLiteral("zeta")}));
    QCOMPARE(registry.tool(QStringLiteral("zeta")), tool);
    QCOMPARE(tool->parent(), static_cast<QObject *>(&registry)); // registry owns tools
}

void TestToolRegistry::duplicateAndUnregister() {
    ToolRegistry registry;
    QVERIFY(registry.registerTool(new DummyTool(QStringLiteral("dup"),
                                                ApprovalLevel::ReadOnly, {})));
    QVERIFY(!registry.registerTool(new DummyTool(QStringLiteral("dup"),
                                                 ApprovalLevel::ReadOnly, {})));
    QVERIFY(registry.unregisterTool(QStringLiteral("dup")));
    QVERIFY(!registry.unregisterTool(QStringLiteral("dup")));
    QVERIFY(registry.toolNames().isEmpty());
}

void TestToolRegistry::validation() {
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("required")] = QJsonArray() << QStringLiteral("name");
    QJsonObject properties;
    properties[QStringLiteral("name")] =
        QJsonObject({{QStringLiteral("type"), QStringLiteral("string")}});
    schema[QStringLiteral("properties")] = properties;

    QVERIFY(ToolRegistry::validateArgs(schema, QJsonObject())
                .value(QStringLiteral("ok")).toBool() == false);
    QVERIFY(ToolRegistry::validateArgs(
                schema, QJsonObject({{QStringLiteral("name"), 42}}))
                .value(QStringLiteral("ok")).toBool() == false);
    QVERIFY(ToolRegistry::validateArgs(
                schema, QJsonObject({{QStringLiteral("name"), QStringLiteral("x")}}))
                .value(QStringLiteral("ok")).toBool());
}

void TestToolRegistry::unknownTool() {
    ToolRegistry registry;
    const ToolResult result = runAndWait(registry, QStringLiteral("does_not_exist"));
    QVERIFY(!result.ok);
    QCOMPARE(result.errorCode, QStringLiteral("unknown_tool"));
}

void TestToolRegistry::readOnlyAutoApproved() {
    ToolRegistry registry;
    auto *tool = new DummyTool(QStringLiteral("read"), ApprovalLevel::ReadOnly, {});
    registry.registerTool(tool);
    const ToolResult result = runAndWait(
        registry, QStringLiteral("read"),
        QJsonObject({{QStringLiteral("value"), QStringLiteral("hi")}}));
    QVERIFY(result.ok);
    QCOMPARE(tool->executions, 1);
    QVERIFY(!registry.approval()->isPending(1));
}

void TestToolRegistry::writeWaitsForApproval() {
    ToolRegistry registry;
    auto *tool = new DummyTool(QStringLiteral("write"), ApprovalLevel::Write, {});
    registry.registerTool(tool);

    int approvals = 0;
    QMetaObject::Connection connection = QObject::connect(
        registry.approval(), &ToolApproval::approvalRequested,
        [&](qint64 id, const QString &, const QJsonObject &,
            const QJsonObject &preview, ApprovalLevel) {
            ++approvals;
            QVERIFY(preview.contains(QStringLiteral("args")));
            QVERIFY(registry.approval()->isPending(id));
            registry.approve(id, true); // simulate the UI confirming
        });
    const ToolResult result = runAndWait(registry, QStringLiteral("write"));
    QObject::disconnect(connection);

    QCOMPARE(approvals, 1);
    QVERIFY(result.ok);
    QCOMPARE(tool->executions, 1);
}

void TestToolRegistry::denialAndCancellation() {
    ToolRegistry registry;
    auto *tool = new DummyTool(QStringLiteral("write"), ApprovalLevel::Execute, {});
    registry.registerTool(tool);

    QObject::connect(registry.approval(), &ToolApproval::approvalRequested,
                     [&](qint64 id, const QString &, const QJsonObject &,
                         const QJsonObject &, ApprovalLevel) {
                         registry.approve(id, false);
                     });
    const ToolResult denied = runAndWait(registry, QStringLiteral("write"));
    QVERIFY(!denied.ok);
    QCOMPARE(denied.errorCode, QStringLiteral("denied"));
    QCOMPARE(tool->executions, 0);

    QObject::disconnect(registry.approval(), nullptr, &registry, nullptr);
    QObject::connect(registry.approval(), &ToolApproval::approvalRequested,
                     [&](qint64 id, const QString &, const QJsonObject &,
                         const QJsonObject &, ApprovalLevel) {
                         registry.approval()->cancel(id);
                     });
    const ToolResult cancelled = runAndWait(registry, QStringLiteral("write"));
    QVERIFY(!cancelled.ok);
    QVERIFY(cancelled.cancelled);
}

void TestToolRegistry::approvalModes() {
    ToolRegistry registry;
    auto *tool = new DummyTool(QStringLiteral("write"), ApprovalLevel::Write, {});
    registry.registerTool(tool);

    registry.approval()->setMode(ApprovalMode::AutoApprove);
    QVERIFY(runAndWait(registry, QStringLiteral("write")).ok);

    registry.approval()->setMode(ApprovalMode::AutoDeny);
    const ToolResult denied = runAndWait(registry, QStringLiteral("write"));
    QVERIFY(!denied.ok);
    QCOMPARE(tool->executions, 1); // only the auto-approved call ran
}

void TestToolRegistry::alwaysAllowed() {
    ToolRegistry registry;
    registry.approval()->setAlwaysAllowed(QStringLiteral("write"), true);
    auto *tool = new DummyTool(QStringLiteral("write"), ApprovalLevel::Write, {});
    registry.registerTool(tool);
    QVERIFY(runAndWait(registry, QStringLiteral("write")).ok);
    QCOMPARE(tool->executions, 1);
}

void TestToolRegistry::previewRedactsSecrets() {
    const QJsonObject args = QJsonObject({
        {QStringLiteral("command"), QStringLiteral("echo hi")},
        {QStringLiteral("api_token"), QStringLiteral("super-secret")},
    });
    const QJsonObject preview = ToolApproval::makePreview(
        QStringLiteral("run_terminal"), args, ApprovalLevel::Execute);
    const QJsonObject safe = preview[QStringLiteral("args")].toObject();
    QCOMPARE(safe[QStringLiteral("command")].toString(), QStringLiteral("echo hi"));
    QCOMPARE(safe[QStringLiteral("api_token")].toString(),
             QStringLiteral("<redacted>"));
    QVERIFY(!QJsonDocument(preview).toJson().contains("super-secret"));
}

void TestToolRegistry::describeTools() {
    ToolRegistry registry;
    registry.registerTool(new DummyTool(QStringLiteral("read"),
                                        ApprovalLevel::ReadOnly,
                                        QStringLiteral("Read things")));
    const QJsonArray descriptions = registry.describeTools();
    QCOMPARE(descriptions.size(), 1);
    const QJsonObject entry = descriptions.at(0).toObject();
    QCOMPARE(entry[QStringLiteral("name")].toString(), QStringLiteral("read"));
    QCOMPARE(entry[QStringLiteral("approvalLevel")].toString(),
             QStringLiteral("read-only"));
    QVERIFY(!entry[QStringLiteral("requiresApproval")].toBool());
}

QTEST_MAIN(TestToolRegistry)
#include "test_tool_registry.moc"
