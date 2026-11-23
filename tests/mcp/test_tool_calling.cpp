#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include "app/mcp/ToolCalling.h"
#include "app/mcp/MCPClient.h"

using namespace ModelBench;

namespace {
bool waitFor(std::function<bool()> cond, int msecs = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < msecs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return cond();
}
} // namespace

class TestToolCalling : public QObject {
    Q_OBJECT
private slots:
    void validationMissingRequired();
    void validationWrongType();
    void validationInteger();
    void validationEnum();
    void validationNestedObject();
    void validationValid();
    void callWithoutClient();
    void callEmitsStartedWithClient();
};

void TestToolCalling::validationMissingRequired() {
    QJsonObject schema;
    schema["type"] = "object";
    schema["required"] = QJsonArray() << "a" << "b";
    QJsonObject args({{"a", 1}});
    QJsonObject r = ToolCalling::validateArgs(schema, args);
    QVERIFY(!r["ok"].toBool());
    QVERIFY(r["error"].toString().contains("b"));

    QJsonObject all({{"a", 1}, {"b", 2}});
    QJsonObject ok = ToolCalling::validateArgs(schema, all);
    QVERIFY(ok["ok"].toBool());
}

void TestToolCalling::validationWrongType() {
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] =
        QJsonObject({{"text", QJsonObject({{"type", "string"}})}});
    QJsonObject args({{"text", 42}});
    QJsonObject r = ToolCalling::validateArgs(schema, args);
    QVERIFY(!r["ok"].toBool());
    QVERIFY(r["error"].toString().contains("text"));
}

void TestToolCalling::validationInteger() {
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] =
        QJsonObject({{"count", QJsonObject({{"type", "integer"}})}});
    QJsonObject good({{"count", 3}});
    QVERIFY(ToolCalling::validateArgs(schema, good)["ok"].toBool());
    QJsonObject bad({{"count", 3.5}});
    QVERIFY(!ToolCalling::validateArgs(schema, bad)["ok"].toBool());
    // "number" accepts integers too.
    QJsonObject schemaNum;
    schemaNum["type"] = "object";
    schemaNum["properties"] =
        QJsonObject({{"x", QJsonObject({{"type", "number"}})}});
    QJsonObject intAsNum({{"x", 3}});
    QVERIFY(ToolCalling::validateArgs(schemaNum, intAsNum)["ok"].toBool());
}

void TestToolCalling::validationEnum() {
    QJsonObject schema;
    schema["type"] = "object";
    QJsonObject prop;
    prop["type"] = "string";
    prop["enum"] = QJsonArray() << "a" << "b";
    schema["properties"] = QJsonObject({{"mode", prop}});
    QJsonObject good({{"mode", "a"}});
    QVERIFY(ToolCalling::validateArgs(schema, good)["ok"].toBool());
    QJsonObject bad({{"mode", "c"}});
    QVERIFY(!ToolCalling::validateArgs(schema, bad)["ok"].toBool());
}

void TestToolCalling::validationNestedObject() {
    QJsonObject inner;
    inner["type"] = "object";
    inner["required"] = QJsonArray() << "key";
    inner["properties"] =
        QJsonObject({{"key", QJsonObject({{"type", "string"}})}});
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = QJsonObject({{"cfg", inner}});

    QJsonObject good({{"cfg", QJsonObject({{"key", "v"}})}});
    QVERIFY(ToolCalling::validateArgs(schema, good)["ok"].toBool());

    QJsonObject badType({{"cfg", QJsonObject({{"key", 7}})}});
    QVERIFY(!ToolCalling::validateArgs(schema, badType)["ok"].toBool());

    QJsonObject missing({{"cfg", QJsonObject({})}});
    QVERIFY(!ToolCalling::validateArgs(schema, missing)["ok"].toBool());
}

void TestToolCalling::validationValid() {
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] =
        QJsonObject({{"a", QJsonObject({{"type", "number"}})}});
    QJsonObject args({{"a", 1.5}});
    QJsonObject r = ToolCalling::validateArgs(schema, args);
    QVERIFY(r["ok"].toBool());

    // Empty schema: nothing to validate.
    QJsonObject empty;
    QVERIFY(ToolCalling::validateArgs(empty, args)["ok"].toBool());
}

void TestToolCalling::callWithoutClient() {
    ToolCalling tc;
    bool finished = false;
    ToolCallResult last;
    connect(&tc, &ToolCalling::toolCallFinished, this,
            [&](const QString &, const QString &, const ToolCallResult &r) {
                last = r;
                finished = true;
            });

    tc.call("anything", QJsonObject({{"x", 1}}));
    QVERIFY(waitFor([&]() { return finished; }, 5000));
    QVERIFY(!last.ok);
    QVERIFY(last.error.contains("No MCP client"));
}

void TestToolCalling::callEmitsStartedWithClient() {
    MCPClient client;
    ToolCalling tc;
    tc.setClient(&client, "test-server");

    bool started = false;
    connect(&tc, &ToolCalling::toolCallStarted, this,
            [&](const QString &sid, const QString &name, const QJsonObject &) {
                QCOMPARE(sid, QString("test-server"));
                QCOMPARE(name, QString("echo"));
                started = true;
            });

    tc.call("echo", QJsonObject({{"text", "hi"}}));
    QVERIFY(waitFor([&]() { return started; }, 5000));
    // No server: approval never comes, so no finished event is expected.
}

QTEST_MAIN(TestToolCalling)
#include "test_tool_calling.moc"
