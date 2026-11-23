#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include "app/mcp/MCPClient.h"
#include "app/mcp/ToolCalling.h"

using namespace ModelBench;

namespace {
bool waitFor(std::function<bool()> cond, int msecs = 10000) {
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

class TestToolCallingIntegration : public QObject {
    Q_OBJECT
private slots:
    void echoThroughToolCalling();
    void invalidArgsThroughToolCalling();
};

void TestToolCallingIntegration::echoThroughToolCalling() {
    MCPClient client;
    bool connected = false;
    QList<MCPTool> tools;
    connect(&client, &MCPClient::connected, this, [&]() { connected = true; });
    connect(&client, &MCPClient::toolsDiscovered, this, [&](const QList<MCPTool> &t) { tools = t; });

    QVERIFY(client.connectStdio("python", QStringList() << mockServerPath(),
                                QString(MODELFIT_SOURCE_DIR) + "/tests/mcp"));
    QVERIFY(waitFor([&]() { return connected && !tools.isEmpty(); }));

    ToolCalling tc;
    tc.setClient(&client, "mock-server");
    tc.setDefaultTimeoutMs(10000);

    bool finished = false;
    ToolCallResult result;
    connect(&tc, &ToolCalling::toolCallFinished, this,
            [&](const QString &, const QString &, const ToolCallResult &r) {
                result = r;
                finished = true;
            });

    // Auto-approve any tool call coming through the MCP client.
    connect(&client, &MCPClient::toolCallRequested, this,
            [&client](qint64 id, const QString &, const QJsonObject &) {
                client.approveToolCall(id, true);
            });

    tc.call("echo", QJsonObject({{"text", "integration-hello"}}));
    QVERIFY(waitFor([&]() { return finished; }));
    QVERIFY(result.ok);
    QCOMPARE(result.output.value("content").toArray().first().toObject().value("text").toString(),
             QString("integration-hello"));
}

void TestToolCallingIntegration::invalidArgsThroughToolCalling() {
    MCPClient client;
    bool connected = false;
    QList<MCPTool> tools;
    connect(&client, &MCPClient::connected, this, [&]() { connected = true; });
    connect(&client, &MCPClient::toolsDiscovered, this, [&](const QList<MCPTool> &t) { tools = t; });

    QVERIFY(client.connectStdio("python", QStringList() << mockServerPath(),
                                QString(MODELFIT_SOURCE_DIR) + "/tests/mcp"));
    QVERIFY(waitFor([&]() { return connected && !tools.isEmpty(); }));

    ToolCalling tc;
    tc.setClient(&client, "mock-server");

    bool finished = false;
    ToolCallResult result;
    connect(&tc, &ToolCalling::toolCallFinished, this,
            [&](const QString &, const QString &, const ToolCallResult &r) {
                result = r;
                finished = true;
            });
    connect(&client, &MCPClient::toolCallRequested, this,
            [&client](qint64 id, const QString &, const QJsonObject &) {
                client.approveToolCall(id, true);
            });

    // "add" with a string for `a` -> server returns a JSON-RPC error.
    tc.call("add", QJsonObject({{"a", "not-a-number"}, {"b", 2}}));
    QVERIFY(waitFor([&]() { return finished; }));
    QVERIFY(!result.ok);
}

QTEST_MAIN(TestToolCallingIntegration)
#include "test_tool_calling_integration.moc"