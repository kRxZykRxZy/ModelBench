#include <QtTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#include "app/tools/FileEditHistory.h"
#include "app/tools/FileSystemTools.h"
#include "app/tools/GitTools.h"
#include "app/tools/TerminalTool.h"

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

ToolResult runAndWait(ToolRegistry &registry, const QString &name,
                      const QJsonObject &args = {}) {
    ToolResult captured;
    bool done = false;
    const QMetaObject::Connection connection =
        QObject::connect(&registry, &ToolRegistry::toolFinished,
                         [&](qint64, const QString &toolName,
                             const ToolResult &result) {
                             if (toolName != name) return;
                             captured = result;
                             done = true;
                         });
    registry.execute(name, args);
    waitFor([&]() { return done; });
    QObject::disconnect(connection);
    return captured;
}

bool gitCommand(const QString &cwd, const QStringList &args) {
    QProcess process;
    process.setWorkingDirectory(cwd);
    process.start(QStringLiteral("git"), args);
    if (!process.waitForStarted(5000)) return false;
    if (!process.waitForFinished(10000)) {
        process.kill();
        return false;
    }
    return process.exitCode() == 0;
}

} // namespace

class TestBuiltinTools : public QObject {
    Q_OBJECT
private slots:
    void fileReadWriteEditSearch();
    void fileSandboxAndUndo();
    void terminalBasicAndEnv();
    void terminalTimeoutAndCwd();
    void gitWorkflows();
};

void TestBuiltinTools::fileReadWriteEditSearch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    FileEditHistory history;
    FileSystemTools::registerTools(registry, dir.path(), &history);
    QCOMPARE(registry.toolNames().size(), 5);

    const QString path = dir.filePath(QStringLiteral("src/hello.txt"));
    ToolResult written = runAndWait(
        registry, QStringLiteral("write_file"),
        QJsonObject({{QStringLiteral("path"), path},
                     {QStringLiteral("content"), QStringLiteral("Hello\nWorld\n")}}));
    QVERIFY2(written.ok, qPrintable(written.error));
    QVERIFY(QFile::exists(path));

    ToolResult read = runAndWait(registry, QStringLiteral("read_file"),
                                 QJsonObject({{QStringLiteral("path"), path}}));
    QVERIFY(read.ok);
    QCOMPARE(read.output[QStringLiteral("content")].toString(),
             QStringLiteral("Hello\nWorld\n"));

    ToolResult edited = runAndWait(
        registry, QStringLiteral("edit_file"),
        QJsonObject({{QStringLiteral("path"), path},
                     {QStringLiteral("oldText"), QStringLiteral("World")},
                     {QStringLiteral("newText"), QStringLiteral("Qt")}}));
    QVERIFY2(edited.ok, qPrintable(edited.error));
    QCOMPARE(edited.output[QStringLiteral("replacements")].toInt(), 1);

    ToolResult searched = runAndWait(
        registry, QStringLiteral("search_code"),
        QJsonObject({{QStringLiteral("query"), QStringLiteral("Qt")},
                     {QStringLiteral("path"), dir.path()},
                     {QStringLiteral("filePattern"), QStringLiteral("*.txt")}}));
    QVERIFY(searched.ok);
    QVERIFY(searched.output[QStringLiteral("count")].toInt() >= 1);

    ToolResult listed = runAndWait(
        registry, QStringLiteral("list_dir"),
        QJsonObject({{QStringLiteral("path"), dir.path()},
                     {QStringLiteral("recursive"), true},
                     {QStringLiteral("pattern"), QStringLiteral("*.txt")}}));
    QVERIFY(listed.ok);
    QVERIFY(listed.output[QStringLiteral("count")].toInt() >= 1);
}

void TestBuiltinTools::fileSandboxAndUndo() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    FileEditHistory history;
    FileSystemTools::registerTools(registry, dir.path(), &history);

    const QString escape = dir.filePath(QStringLiteral("../escape.txt"));
    ToolResult escaped = runAndWait(
        registry, QStringLiteral("write_file"),
        QJsonObject({{QStringLiteral("path"), escape},
                     {QStringLiteral("content"), QStringLiteral("nope")}}));
    QVERIFY(!escaped.ok);
    QVERIFY(escaped.error.contains(QStringLiteral("outside")));

    const QString path = dir.filePath(QStringLiteral("undo.txt"));
    QVERIFY(runAndWait(registry, QStringLiteral("write_file"),
                       QJsonObject({{QStringLiteral("path"), path},
                                    {QStringLiteral("content"), QStringLiteral("v1")}})).ok);
    QVERIFY(runAndWait(registry, QStringLiteral("edit_file"),
                       QJsonObject({{QStringLiteral("path"), path},
                                    {QStringLiteral("oldText"), QStringLiteral("v1")},
                                    {QStringLiteral("newText"), QStringLiteral("v2")}})).ok);

    QCOMPARE(history.count(), 2);
    QString error;
    QVERIFY(history.undoLast(&error));   // back to v1
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("v1"));
    file.close();
    QVERIFY(history.undoLast(&error));   // remove newly created file
    QVERIFY(!QFile::exists(path));
    QVERIFY(!history.undoLast(&error));
    QVERIFY(error.contains(QStringLiteral("Nothing")));
}

void TestBuiltinTools::terminalBasicAndEnv() {
    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    registry.registerTool(new TerminalTool(QString(), &registry));

    int outputChunks = 0;
    QObject::connect(&registry, &ToolRegistry::toolOutput,
                     [&](qint64, const QString &, const QString &,
                         const QString &) { ++outputChunks; });

    ToolResult result = runAndWait(
        registry, QStringLiteral("run_terminal"),
        QJsonObject({{QStringLiteral("command"),
                      QStringLiteral("echo hello_from_shell")},
                     {QStringLiteral("shell"), true}}));
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(result.output[QStringLiteral("stdout")].toString()
                .contains(QStringLiteral("hello_from_shell")));
    QVERIFY(outputChunks > 0);

#ifdef Q_OS_WIN
    const QString echoVar = QStringLiteral("echo %MB_TEST_VAR%");
#else
    const QString echoVar = QStringLiteral("echo $MB_TEST_VAR");
#endif
    QJsonObject env;
    env[QStringLiteral("MB_TEST_VAR")] = QStringLiteral("env-ok");
    ToolResult withEnv = runAndWait(
        registry, QStringLiteral("run_terminal"),
        QJsonObject({{QStringLiteral("command"), echoVar},
                     {QStringLiteral("shell"), true},
                     {QStringLiteral("env"), env}}));
    QVERIFY2(withEnv.ok, qPrintable(withEnv.error));
    QVERIFY(withEnv.output[QStringLiteral("stdout")].toString()
                .contains(QStringLiteral("env-ok")));
}

void TestBuiltinTools::terminalTimeoutAndCwd() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    registry.registerTool(new TerminalTool(dir.path(), &registry));

#ifdef Q_OS_WIN
    const QString slow = QStringLiteral("ping -n 3 127.0.0.1 > nul");
#else
    const QString slow = QStringLiteral("sleep 2");
#endif
    ToolResult timedOut = runAndWait(
        registry, QStringLiteral("run_terminal"),
        QJsonObject({{QStringLiteral("command"), slow},
                     {QStringLiteral("shell"), true},
                     {QStringLiteral("timeoutMs"), 150}}));
    QVERIFY(!timedOut.ok);
    QCOMPARE(timedOut.errorCode, QStringLiteral("timeout"));

    ToolResult escaped = runAndWait(
        registry, QStringLiteral("run_terminal"),
        QJsonObject({{QStringLiteral("command"), QStringLiteral("echo hi")},
                     {QStringLiteral("shell"), true},
                     {QStringLiteral("cwd"), dir.path() + QStringLiteral("/../")}}));
    QVERIFY(!escaped.ok);
    QCOMPARE(escaped.errorCode, QStringLiteral("bad_cwd"));
}

void TestBuiltinTools::gitWorkflows() {
    if (QProcess::execute(QStringLiteral("git"),
                          QStringList{QStringLiteral("--version")}) != 0)
        QSKIP("git is not installed");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(gitCommand(dir.path(), {QStringLiteral("init"), QStringLiteral("-q")}));
    QVERIFY(gitCommand(dir.path(), {QStringLiteral("config"),
                                    QStringLiteral("user.email"),
                                    QStringLiteral("test@example.com")}));
    QVERIFY(gitCommand(dir.path(), {QStringLiteral("config"),
                                    QStringLiteral("user.name"),
                                    QStringLiteral("Test User")}));

    QFile file(dir.filePath(QStringLiteral("a.txt")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("line one\n");
    file.close();
    QVERIFY(gitCommand(dir.path(), {QStringLiteral("add"), QStringLiteral("-A")}));
    QVERIFY(gitCommand(dir.path(), {QStringLiteral("commit"), QStringLiteral("-qm"),
                                    QStringLiteral("initial")}));

    ToolRegistry registry;
    registry.approval()->setMode(ApprovalMode::AutoApprove);
    GitTools::registerTools(registry, dir.path());
    QCOMPARE(registry.toolNames().size(), 7);

    ToolResult status = runAndWait(registry, QStringLiteral("git_status"));
    QVERIFY2(status.ok, qPrintable(status.error));

    ToolResult log = runAndWait(registry, QStringLiteral("git_log"));
    QVERIFY2(log.ok, qPrintable(log.error));
    QVERIFY(log.output[QStringLiteral("stdout")].toString()
                .contains(QStringLiteral("initial")));

    ToolResult branches = runAndWait(
        registry, QStringLiteral("git_branch"),
        QJsonObject({{QStringLiteral("action"), QStringLiteral("list")}}));
    QVERIFY2(branches.ok, qPrintable(branches.error));

    QVERIFY(file.open(QIODevice::Append));
    file.write("line two\n");
    file.close();
    ToolResult diff = runAndWait(registry, QStringLiteral("git_diff"));
    QVERIFY2(diff.ok, qPrintable(diff.error));
    QVERIFY(diff.output[QStringLiteral("stdout")].toString()
                .contains(QStringLiteral("line two")));

    ToolResult commit = runAndWait(
        registry, QStringLiteral("git_commit"),
        QJsonObject({{QStringLiteral("message"), QStringLiteral("second commit")},
                     {QStringLiteral("all"), true}}));
    QVERIFY2(commit.ok, qPrintable(commit.error));
    QCOMPARE(commit.output[QStringLiteral("exitCode")].toInt(), 0);
}

QTEST_MAIN(TestBuiltinTools)
#include "test_builtin_tools.moc"
