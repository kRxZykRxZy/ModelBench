#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include "app/mcp/MCPClient.h"

using namespace ModelBench;

namespace {
bool waitFor(std::function<bool()> cond, int msecs = 15000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < msecs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return cond();
}

QString mockServerPath() {
    QString env = qEnvironmentVariable("MODELFIT_MOCK_SERVER");
    if (!env.isEmpty()) return env;
    return QString(MODELFIT_SOURCE_DIR) + "/tests/mcp/mock_server.py";
}
} // namespace

class TestAgentToolLoop : public QObject {
    Q_OBJECT
private slots:
    void toolLoopEchoAndAdd();
};

void TestAgentToolLoop::toolLoopEchoAndAdd() {
    MCPClient client;
    bool connected = false;
    QList<MCPTool> tools;
    connect(&client, &MCPClient::connected, this, [&]() { connected = true; });
    connect(&client, &MCPClient::toolsDiscovered, this, [&](const QList<MCPTool> &t) { tools = t; });

    QVERIFY(client.connectStdio("python", QStringList() << mockServerPath(),
                                QString(MODELFIT_SOURCE_DIR) + "/tests/mcp"));
    QVERIFY(waitFor([&]() { return connected && !tools.isEmpty(); }));

    // Auto-approve all tool calls.
    connect(&client, &MCPClient::toolCallRequested, this,
            [&client](qint64 id, const QString &, const QJsonObject &) {
                client.approveToolCall(id, true);
            });

    // Step 1: echo
    QJsonObject echoResult;
    bool echoDone = false;
    connect(&client, &MCPClient::toolResult, this,
            [&](qint64, const QString &name, const QJsonObject &result) {
                if (name == "echo") { echoResult = result; echoDone = true; }
            });
    client.callTool("echo", QJsonObject({{"text", "agent-step-1"}}));
    QVERIFY(waitFor([&]() { return echoDone; }));
    QVERIFY(!echoResult.isEmpty());

    // Step 2: add
    QJsonObject addResult;
    bool addDone = false;
    connect(&client, &MCPClient::toolResult, this,
            [&](qint64, const QString &name, const QJsonObject &result) {
                if (name == "add") { addResult = result; addDone = true; }
            });
    client.callTool("add", QJsonObject({{"a", 7}, {"b", 5}}));
    QVERIFY(waitFor([&]() { return addDone; }));
    QVERIFY(!addResult.isEmpty());
    QCOMPARE(addResult.value("content").toArray().first().toObject().value("text").toString(),
             QString("12"));

    // Ensure tool list contains expected tools.
    QStringList names;
    for (const MCPTool &t : tools) names << t.name;
    QVERIFY(names.contains("echo"));
    QVERIFY(names.contains("add"));
}

QTEST_MAIN(TestAgentToolLoop)
#include "test_agent_tool_loop.moc"