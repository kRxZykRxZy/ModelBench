#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QUrl>
#include <QSet>

#include "app/core/AccountManager.h"

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

class MockHttpServer : public QObject {
    Q_OBJECT
public:
    explicit MockHttpServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this,
                &MockHttpServer::onConnection);
        m_server.listen(QHostAddress::LocalHost, 0);
    }
    quint16 port() const { return m_server.serverPort(); }
    void setHandler(std::function<QByteArray(const QString &, const QString &,
                                            const QByteArray &)> h) {
        m_handler = std::move(h);
    }

private slots:
    void onConnection() {
        while (m_server.hasPendingConnections()) {
            QTcpSocket *sock = m_server.nextPendingConnection();
            if (!sock->waitForReadyRead(2000)) {
                sock->deleteLater();
                continue;
            }
            QByteArray data = sock->readAll();
            while (sock->waitForReadyRead(100)) data += sock->readAll();

            // Parse request line + headers + body.
            int headerEnd = data.indexOf("\r\n\r\n");
            QByteArray headers = headerEnd >= 0 ? data.left(headerEnd) : data;
            QByteArray body = headerEnd >= 0 ? data.mid(headerEnd + 4) : QByteArray();
            QList<QByteArray> lines = headers.split('\n');
            QString requestLine = QString::fromUtf8(lines.value(0)).trimmed();
            QStringList parts = requestLine.split(' ');
            QString method = parts.value(0);
            QString path = parts.value(1);

            QByteArray response;
            if (m_handler) {
                QByteArray out = m_handler(method, path, body);
                // Allow handler to encode status via leading "STATUS:xxx" (optional).
                int status = 200;
                if (out.startsWith("STATUS:")) {
                    int nl = out.indexOf('\n');
                    status = out.mid(7, nl - 7).toInt();
                    out = out.mid(nl + 1);
                }
                response = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n"
                           "Content-Type: application/json\r\n"
                           "Content-Length: " + QByteArray::number(out.size()) +
                           "\r\nConnection: close\r\n\r\n" + out;
            } else {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            }
            sock->write(response);
            sock->flush();
            sock->waitForBytesWritten(1000);
            sock->disconnectFromHost();
        }
    }

private:
    QTcpServer m_server;
    std::function<QByteArray(const QString &, const QString &, const QByteArray &)>
        m_handler;
};
} // namespace

class TestAccountManager : public QObject {
    Q_OBJECT
private slots:
    void loginSignupFlow();
};

void TestAccountManager::loginSignupFlow() {
    MockHttpServer server;
    QVERIFY(server.port() > 0);

    QSet<QString> users;
    server.setHandler([&](const QString &method, const QString &path,
                         const QByteArray &body) {
        QJsonDocument doc = QJsonDocument::fromJson(body);
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        if (path == "/auth/signup") {
            QString email = obj.value("email").toString();
            if (users.contains(email))
                return QByteArray("STATUS:400\n{\"error\":\"user already exists\"}");
            users.insert(email);
            QJsonObject session;
            session["access_token"] = "mock-token-" + email;
            session["user"] = QJsonObject{{"id", "u1"}, {"email", email}};
            QJsonObject out{{"user", QJsonObject{{"id", "u1"}, {"email", email}}},
                            {"session", session}};
            return QJsonDocument(out).toJson(QJsonDocument::Compact);
        }
        if (path == "/auth/login") {
            QString email = obj.value("email").toString();
            QString password = obj.value("password").toString();
            if (!users.contains(email) || password != "secret")
                return QByteArray("STATUS:401\n{\"error\":\"invalid credentials\"}");
            QJsonObject session;
            session["access_token"] = "mock-token-" + email;
            session["user"] = QJsonObject{{"id", "u1"}, {"email", email}};
            QJsonObject out{{"user", QJsonObject{{"id", "u1"}, {"email", email}}},
                            {"session", session}};
            return QJsonDocument(out).toJson(QJsonDocument::Compact);
        }
        if (path == "/user/entitlements") {
            return QByteArray("{\"tier\":\"free\",\"features\":[\"inference\",\"chat\",\"models\"]}");
        }
        return QByteArray("STATUS:404\n{\"error\":\"not found\"}");
    });

    AccountManager acc;
    acc.setServerUrl(QString("http://127.0.0.1:%1").arg(server.port()));

    // Signup
    QSignalSpy successSpy(&acc, &AccountManager::authSuccess);
    QSignalSpy errorSpy(&acc, &AccountManager::authError);
    acc.signup("alice@example.com", "secret");
    QVERIFY(waitFor([&]() { return successSpy.count() > 0 || errorSpy.count() > 0; }));
    QVERIFY(successSpy.count() > 0);
    QVERIFY(acc.loggedIn());
    QCOMPARE(acc.email(), QString("alice@example.com"));

    // Duplicate signup
    int prevErrors = errorSpy.count();
    acc.signup("alice@example.com", "secret");
    QVERIFY(waitFor([&]() { return errorSpy.count() > prevErrors; }));
    QVERIFY(errorSpy.count() > prevErrors);

    // Logout
    acc.logout();
    QVERIFY(!acc.loggedIn());

    // Wrong password login
    prevErrors = errorSpy.count();
    acc.login("alice@example.com", "wrong");
    QVERIFY(waitFor([&]() { return errorSpy.count() > prevErrors; }));

    // Correct login
    int prevSuccess = successSpy.count();
    acc.login("alice@example.com", "secret");
    QVERIFY(waitFor([&]() { return successSpy.count() > prevSuccess; }));
    QVERIFY(acc.loggedIn());
    QCOMPARE(acc.email(), QString("alice@example.com"));

    // Entitlements refresh
    acc.refreshEntitlements();
    QVERIFY(waitFor([&]() { return acc.tier() == "free"; }, 5000));
    QCOMPARE(acc.tier(), QString("free"));
}

QTEST_MAIN(TestAccountManager)
#include "test_account_manager.moc"