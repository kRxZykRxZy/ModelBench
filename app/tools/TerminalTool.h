#pragma once

#include "ToolRegistry.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariantMap>

class QProcess;
class QTimer;

namespace ModelBench {

// Runs a local command with QProcess. One process per call, so concurrent
// calls and cancellation are supported. stdout/stderr are streamed through
// Tool::output and capped to a configurable size.
class TerminalTool : public Tool {
    Q_OBJECT
public:
    explicit TerminalTool(const QString &sandboxRoot,
                          QObject *parent = nullptr);
    ~TerminalTool() override;

    QString name() const override { return QStringLiteral("run_terminal"); }
    QString description() const override;
    QJsonObject inputSchema() const override;
    ApprovalLevel approvalLevel() const override { return ApprovalLevel::Execute; }
    ApprovalLevel approvalLevelFor(const QJsonObject &args) const override;

    void execute(qint64 callId, const QJsonObject &args) override;
    void cancel(qint64 callId) override;

    int maxStreamBytes() const { return m_maxStreamBytes; }
    void setMaxStreamBytes(int bytes);

signals:
    void commandFinished(qint64 callId, const QString &command, int exitCode);

private:
    struct Running {
        QProcess *process = nullptr;
        QTimer *timer = nullptr;
        QByteArray out;
        QByteArray err;
        qint64 startedMs = 0;
        int timeoutMs = 30000;
        bool timedOut = false;
        bool truncated = false;
        QString command;
        QString cwd;
    };

    void complete(qint64 callId, const ToolResult &result);
    void fail(qint64 callId, const QString &message, const QString &code,
              bool cancelled = false);

    QHash<qint64, Running> m_running;
    QString m_root;
    int m_maxStreamBytes = 1024 * 1024;
};

// Interactive pseudo-terminal session used by the embedded TerminalView.
//
// This is deliberately separate from TerminalTool above: TerminalTool is a
// one-shot, non-interactive command runner for the agent loop, while a
// TerminalSession is a long-lived shell with a real TTY so that line editing,
// colour, signals (Ctrl+C) and full-screen programs all work.
//
// Backend: Windows ConPTY (loaded dynamically from kernel32 so the app still
// starts on older Windows versions) and POSIX ptys (macOS/Linux). All public
// entry points are QML-friendly (slots + signals); output is delivered as UTF-8
// text via dataReceived().
class TerminalSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(QString workingDirectory READ workingDirectory NOTIFY workingDirectoryChanged)
    Q_PROPERTY(int columns READ columns NOTIFY columnsChanged)
    Q_PROPERTY(int rows READ rows NOTIFY rowsChanged)

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    bool isRunning() const { return m_running; }
    QString workingDirectory() const { return m_cwd; }
    int columns() const { return m_cols; }
    int rows() const { return m_rows; }

public slots:
    // Starts a fresh shell. Closes any existing session first. `env` holds
    // extra/overriding environment variables (string values), like TerminalTool.
    void start(const QString &cwd = QString(), int cols = 80, int rows = 24,
               const QVariantMap &env = QVariantMap());
    void sendInput(const QByteArray &data);
    void sendText(const QString &text) { sendInput(text.toUtf8()); }
    void resize(int cols, int rows);
    void interrupt(); // sends Ctrl+C (SIGINT)
    void close();

signals:
    void dataReceived(const QString &text);
    void processFinished(int exitCode);
    void errorOccurred(const QString &message);
    void runningChanged(bool running);
    void workingDirectoryChanged(const QString &cwd);
    void columnsChanged(int cols);
    void rowsChanged(int rows);

private slots:
    void poll();

private:
    void cleanup();

    bool m_running = false;
    QString m_cwd;
    int m_cols = 80;
    int m_rows = 24;
    QTimer *m_pollTimer = nullptr;

#ifdef Q_OS_WIN
    // Opaque handles (HANDLE/HPCON) are stored as void* so windows.h is not
    // pulled into this header (and into moc output).
    struct Native {
        void *console = nullptr;  // HPCON
        void *inWrite = nullptr;  // HANDLE: our write end -> ConPTY stdin
        void *outRead = nullptr;  // HANDLE: ConPTY stdout -> our read end
        void *process = nullptr;  // HANDLE
    } m_native;
#else
    struct Native {
        int masterFd = -1;
        qint64 pid = -1;
    } m_native;
#endif
};

} // namespace ModelBench
