#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>

class TestMcpJsonRpc : public QObject {
    Q_OBJECT
private slots:
    void jsonrpc_roundtrip() {
        QJsonObject req;
        req["jsonrpc"] = "2.0";
        req["id"] = 1;
        req["method"] = "tools/list";
        QByteArray b = QJsonDocument(req).toJson(QJsonDocument::Compact);
        QJsonDocument d = QJsonDocument::fromJson(b);
        QVERIFY(d.isObject());
        QCOMPARE(d.object().value("method").toString(), QString("tools/list"));
    }
};

QTEST_MAIN(TestMcpJsonRpc)
#include "test_mcp_jsonrpc.moc"
