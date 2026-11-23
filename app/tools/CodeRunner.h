#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QProcess;
class QTimer;
class QTemporaryDir;

namespace ModelBench {

// Compiles and executes a short snippet in a throw-away directory and reports
// the captured output back to QML. No shell is involved: the program and its
// arguments are always passed to QProcess as a list, so snippet text can never
// be interpreted as shell syntax.
//
// Supported languages: "cpp" (g++/clang++), "python" (python3/python) and
// "javascript" (node/nodejs). `runFinished` carries a map with the keys
// ok, language, stdout, stderr, compileOutput, exitCode, durationMs,
// timedOut, truncated and error.
class CodeRunner : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)

public:
    explicit CodeRunner(QObject *parent = nullptr);
    ~CodeRunner() override;

    bool isRunning() const { return m_job != nullptr; }

    Q_INVOKABLE QStringList availableLanguages() const;
    Q_INVOKABLE QString displayName(const QString &language) const;
    Q_INVOKABLE bool isAvailable(const QString &language) const;
    // Empty string when the interpreter/compiler is not installed.
    Q_INVOKABLE QString commandFor(const QString &language) const;

public slots:
    void run(const QString &language, const QString &code,
             const QString &cwd = QString(), int timeoutMs = 15000);
    void cancel();

signals:
    void output(const QString &stream, const QString &chunk);
    void finished(const QVariantMap &result);
    void errorOccurred(const QString &message);
    void runningChanged(bool running);

private:
    struct Job {
        QString language;
        QString cwd;
        QString sourcePath;
        QString compileProgram;
        QString runProgram;
        QStringList compileArgs;
        QStringList runArgs;
        QString compileOutput;
        QString stdoutText;
        QString stderrText;
        qint64 startedMs = 0;
        int timeoutMs = 15000;
        int exitCode = -1;
        bool timedOut = false;
        bool truncated = false;
        QProcess *process = nullptr;
        QTimer *timer = nullptr;
        QTemporaryDir *dir = nullptr;
    };

    void startCompile(Job *job);
    void startRun(Job *job);
    void restartTimer(Job *job, int ms);
    void appendOutput(Job *job, const QString &stream, const QByteArray &bytes);
    void stopProcess(Job *job);
    void abortJob(); // silent teardown used when a new run supersedes the old one
    void finish(Job *job, bool ok, const QString &error = QString());

    Job *m_job = nullptr;
    int m_maxOutputBytes = 1024 * 1024;
};

} // namespace ModelBench
