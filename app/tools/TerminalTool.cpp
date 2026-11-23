// POSIX pty APIs (posix_openpt/grantpt/ptsname) need a feature-test macro
// before ANY system header is pulled in (Qt headers included below).
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
#  define _XOPEN_SOURCE 600
#endif

#include "TerminalTool.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <QVariantMap>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <string>
#else
#  include <errno.h>
#  include <fcntl.h>
#  include <signal.h>
#  include <stdlib.h>
#  include <sys/ioctl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <vector>
#endif

namespace ModelBench {
namespace {

QJsonObject prop(const QString &type, const QString &description,
                 const QJsonValue &def = QJsonValue()) {
    QJsonObject o;
    o[QStringLiteral("type")] = type;
    o[QStringLiteral("description")] = description;
    if (!def.isNull()) o[QStringLiteral("default")] = def;
    return o;
}

bool isInsideRoot(const QString &root, const QString &candidate) {
    QString a = QDir::fromNativeSeparators(QDir::cleanPath(root));
    QString b = QDir::fromNativeSeparators(QDir::cleanPath(candidate));
#ifdef Q_OS_WIN
    a = a.toLower();
    b = b.toLower();
#endif
    if (a == b) return true;
    if (!a.endsWith(u'/')) a.append(u'/');
    return b.startsWith(a);
}

QString displayCommand(const QString &program, const QStringList &args) {
    QStringList parts{program};
    parts.append(args);
    for (QString &part : parts) {
        if (part.contains(u' ')) part = QStringLiteral("\"%1\"").arg(part);
    }
    return parts.join(u' ');
}

} // namespace

TerminalTool::TerminalTool(const QString &sandboxRoot, QObject *parent)
    : Tool(parent), m_root(sandboxRoot) {}

TerminalTool::~TerminalTool() {
    const QList<qint64> ids = m_running.keys();
    for (qint64 id : ids) cancel(id);
}

QString TerminalTool::description() const {
    return QStringLiteral(
        "Run a local command with optional cwd, timeout and environment "
        "variables. Set shell=true to pass command through the system shell.");
}

QJsonObject TerminalTool::inputSchema() const {
    QJsonObject properties{
        {QStringLiteral("command"),
         prop(QStringLiteral("string"),
              QStringLiteral("Executable path, or a raw shell line when shell=true."))},
        {QStringLiteral("args"),
         prop(QStringLiteral("array"),
              QStringLiteral("Arguments passed as separate values (no shell)."))},
        {QStringLiteral("cwd"),
         prop(QStringLiteral("string"),
              QStringLiteral("Working directory; restricted to the sandbox root."))},
        {QStringLiteral("timeoutMs"),
         prop(QStringLiteral("integer"),
              QStringLiteral("Kill the command after this many milliseconds."), 30000)},
        {QStringLiteral("env"),
         prop(QStringLiteral("object"),
              QStringLiteral("Extra environment variables (string values)."))},
        {QStringLiteral("shell"),
         prop(QStringLiteral("boolean"),
              QStringLiteral("Interpret command with /bin/sh -c or cmd /c."), false)},
    };
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = properties;
    schema[QStringLiteral("required")] = QJsonArray() << QStringLiteral("command");
    return schema;
}

ApprovalLevel TerminalTool::approvalLevelFor(const QJsonObject &args) const {
    // Shell commands can do anything; mark them as dangerous in the preview.
    return args.value(QStringLiteral("shell")).toBool(false)
               ? ApprovalLevel::Dangerous
               : ApprovalLevel::Execute;
}

void TerminalTool::setMaxStreamBytes(int bytes) {
    if (bytes > 0) m_maxStreamBytes = bytes;
}

void TerminalTool::execute(qint64 callId, const QJsonObject &args) {
    const QString command = args.value(QStringLiteral("command")).toString();
    if (command.isEmpty()) {
        fail(callId, QStringLiteral("command is required"),
             QStringLiteral("validation_error"));
        return;
    }

    QStringList argv;
    const QJsonValue argsValue = args.value(QStringLiteral("args"));
    if (argsValue.isArray()) {
        for (const QJsonValue &v : argsValue.toArray()) argv << v.toString();
    }

    const bool useShell = args.value(QStringLiteral("shell")).toBool(false);
    QString program;
    QStringList processArgs;
    if (useShell) {
#ifdef Q_OS_WIN
        program = QStringLiteral("cmd.exe");
        processArgs << QStringLiteral("/d") << QStringLiteral("/s")
                    << QStringLiteral("/c") << command;
#else
        program = QStringLiteral("/bin/sh");
        processArgs << QStringLiteral("-c") << command;
#endif
    } else {
        program = command;
        processArgs = argv;
    }
    const QString shown = useShell ? command : displayCommand(program, argv);

    // Working directory (with optional sandbox).
    QString cwdInput = args.value(QStringLiteral("cwd")).toString();
    QString cwd;
    if (!cwdInput.isEmpty()) {
        QFileInfo info(cwdInput);
        cwd = info.isAbsolute() ? cwdInput
              : (m_root.isEmpty() ? QDir().absoluteFilePath(cwdInput)
                                  : QDir(m_root).absoluteFilePath(cwdInput));
        cwd = QDir::cleanPath(cwd);
        if (!m_root.isEmpty() && !isInsideRoot(QDir(m_root).absolutePath(), cwd)) {
            fail(callId,
                 QStringLiteral("cwd '%1' is outside the allowed root").arg(cwdInput),
                 QStringLiteral("bad_cwd"));
            return;
        }
        if (!QFileInfo(cwd).isDir()) {
            fail(callId,
                 QStringLiteral("Working directory does not exist: %1").arg(cwd),
                 QStringLiteral("bad_cwd"));
            return;
        }
    }

    // Extra environment variables.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QJsonObject envObject = args.value(QStringLiteral("env")).toObject();
    for (auto it = envObject.constBegin(); it != envObject.constEnd(); ++it) {
        const QString key = it.key();
        if (key.isEmpty() || key.contains(u'=') || key.contains(QChar('\0'))) {
            fail(callId, QStringLiteral("Invalid environment variable name"),
                 QStringLiteral("bad_env"));
            return;
        }
        env.insert(key, it.value().toString());
    }

    Running run;
    run.startedMs = QDateTime::currentMSecsSinceEpoch();
    run.timeoutMs = qBound(100, args.value(QStringLiteral("timeoutMs")).toInt(30000), 600000);
    run.command = shown;
    run.cwd = cwd;

    auto *process = new QProcess(this);
    run.process = process;
    process->setProcessEnvironment(env);
    if (!cwd.isEmpty()) process->setWorkingDirectory(cwd);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    run.timer = timer;
    m_running.insert(callId, run);

    connect(timer, &QTimer::timeout, this, [this, callId]() {
        auto it = m_running.find(callId);
        if (it == m_running.end()) return;
        it->timedOut = true;
        if (it->process && it->process->state() != QProcess::NotRunning)
            it->process->kill();
        ToolResult r;
        r.ok = false;
        r.error = QStringLiteral("Command timed out after %1 ms")
                      .arg(it->timeoutMs);
        r.errorCode = QStringLiteral("timeout");
        r.cancelled = true;
        complete(callId, r);
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, callId](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart) return;
                auto it = m_running.find(callId);
                const QString hint =
                    it == m_running.end() ? QString() : it->command;
                fail(callId,
                     QStringLiteral("Failed to start command '%1'").arg(hint),
                     QStringLiteral("start_failed"));
            });

    auto pump = [this, callId](bool isStderr) {
        auto it = m_running.find(callId);
        if (it == m_running.end()) return;
        QProcess *p = it->process;
        const QByteArray incoming =
            isStderr ? p->readAllStandardError() : p->readAllStandardOutput();
        if (incoming.isEmpty()) return;

        QByteArray &buffer = isStderr ? it->err : it->out;
        QByteArray kept = incoming;
        const qint64 room = qMax<qint64>(0, m_maxStreamBytes - buffer.size());
        if (kept.size() > room) {
            kept.resize(int(room));
            it->truncated = true;
        }
        buffer.append(kept);
        emit output(callId, isStderr ? QStringLiteral("stderr")
                                     : QStringLiteral("stdout"),
                    QString::fromUtf8(kept));
    };
    connect(process, &QProcess::readyReadStandardOutput, this,
            [pump]() { pump(false); });
    connect(process, &QProcess::readyReadStandardError, this,
            [pump]() { pump(true); });

    connect(process, &QProcess::finished, this,
            [this, callId](int exitCode, QProcess::ExitStatus status) {
                auto it = m_running.find(callId);
                if (it == m_running.end()) return;
                const bool ok = status == QProcess::NormalExit && exitCode == 0 &&
                                !it->timedOut;

                QJsonObject out;
                out[QStringLiteral("command")] = it->command;
                out[QStringLiteral("cwd")] = it->cwd;
                out[QStringLiteral("exitCode")] = exitCode;
                out[QStringLiteral("stdout")] = QString::fromUtf8(it->out);
                out[QStringLiteral("stderr")] = QString::fromUtf8(it->err);
                out[QStringLiteral("timedOut")] = it->timedOut;
                out[QStringLiteral("truncated")] = it->truncated;

                ToolResult r;
                r.ok = ok;
                r.output = out;
                if (!ok) {
                    r.error = it->timedOut
                                  ? QStringLiteral("Command timed out")
                                  : QStringLiteral("Command exited with code %1: %2")
                                        .arg(exitCode)
                                        .arg(QString::fromUtf8(it->err).trimmed());
                    r.errorCode = it->timedOut ? QStringLiteral("timeout")
                                               : QStringLiteral("nonzero_exit");
                }
                complete(callId, r);
                emit commandFinished(callId, out[QStringLiteral("command")].toString(),
                                     exitCode);
            });

    timer->start(run.timeoutMs);
    process->start(program, processArgs);
}

void TerminalTool::complete(qint64 callId, const ToolResult &result) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return; // already completed
    const Running run = *it;
    m_running.erase(it);

    if (run.timer) {
        run.timer->stop();
        run.timer->deleteLater();
    }
    if (run.process) {
        disconnect(run.process, nullptr, this, nullptr);
        if (run.process->state() != QProcess::NotRunning) {
            run.process->kill();
            run.process->waitForFinished(1000);
        }
        run.process->deleteLater();
    }

    ToolResult r = result;
    r.toolName = name();
    r.durationMs = QDateTime::currentMSecsSinceEpoch() - run.startedMs;
    emit finished(callId, r);
}

void TerminalTool::fail(qint64 callId, const QString &message,
                        const QString &code, bool cancelled) {
    ToolResult r;
    r.ok = false;
    r.error = message;
    r.errorCode = code;
    r.cancelled = cancelled;
    complete(callId, r);
}

void TerminalTool::cancel(qint64 callId) {
    auto it = m_running.find(callId);
    if (it == m_running.end()) return;
    if (it->process && it->process->state() != QProcess::NotRunning)
        it->process->kill();
    ToolResult r;
    r.ok = false;
    r.error = QStringLiteral("Command cancelled");
    r.errorCode = QStringLiteral("cancelled");
    r.cancelled = true;
    complete(callId, r);
}

// ---------------------------------------------------------------------------
// TerminalSession - interactive pseudo-terminal (ConPTY / POSIX pty)
// ---------------------------------------------------------------------------

#ifdef Q_OS_WIN
namespace {

// ConPTY entry points are resolved at runtime so the executable keeps working
// on Windows versions predating the API (we fall back with a clear error).
struct ConPtyApi {
    using CreateFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, void **);
    using ResizeFn = HRESULT(WINAPI *)(void *, COORD);
    using CloseFn = void(WINAPI *)(void *);
    using InitAttrFn = BOOL(WINAPI *)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD, PSIZE_T);
    using UpdateAttrFn = BOOL(WINAPI *)(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR,
                                        PVOID, SIZE_T, PVOID, PSIZE_T);
    using DeleteAttrFn = void(WINAPI *)(LPPROC_THREAD_ATTRIBUTE_LIST);

    CreateFn create = nullptr;
    ResizeFn resize = nullptr;
    CloseFn close = nullptr;
    InitAttrFn initAttr = nullptr;
    UpdateAttrFn updateAttr = nullptr;
    DeleteAttrFn deleteAttr = nullptr;

    bool load() {
        if (create) return true;
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        if (!k) return false;
        create = reinterpret_cast<CreateFn>(
            GetProcAddress(k, "CreatePseudoConsole"));
        resize = reinterpret_cast<ResizeFn>(
            GetProcAddress(k, "ResizePseudoConsole"));
        close = reinterpret_cast<CloseFn>(
            GetProcAddress(k, "ClosePseudoConsole"));
        initAttr = reinterpret_cast<InitAttrFn>(
            GetProcAddress(k, "InitializeProcThreadAttributeList"));
        updateAttr = reinterpret_cast<UpdateAttrFn>(
            GetProcAddress(k, "UpdateProcThreadAttribute"));
        deleteAttr = reinterpret_cast<DeleteAttrFn>(
            GetProcAddress(k, "DeleteProcThreadAttributeList"));
        return create && resize && close && initAttr && updateAttr && deleteAttr;
    }
};

ConPtyApi &conPty() {
    static ConPtyApi api;
    api.load();
    return api;
}

// This is PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE from the 10.0.17763 SDK; spelled
// out so we do not depend on the SDK version used to compile Qt.
constexpr DWORD kProcThreadAttributePseudoConsole = 0x00020016;

QString environmentBlock(const QVariantMap &extra) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it)
        env.insert(it.key(), it.value().toString());
    QStringList entries;
    const QStringList keys = env.keys();
    for (const QString &key : keys)
        entries.append(key + QStringLiteral("=") + env.value(key));
    QString block = entries.join(QChar('\0'));
    block.append(QChar('\0')); // QString adds the second NUL itself
    return block;
}

} // namespace
#endif

TerminalSession::TerminalSession(QObject *parent) : QObject(parent) {
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10);
    connect(m_pollTimer, &QTimer::timeout, this, &TerminalSession::poll);
}

TerminalSession::~TerminalSession() { close(); }

void TerminalSession::start(const QString &cwd, int cols, int rows,
                            const QVariantMap &env) {
    close(); // a fresh session always replaces the previous one

    m_cols = qBound(2, cols, 500);
    m_rows = qBound(1, rows, 500);
    emit columnsChanged(m_cols);
    emit rowsChanged(m_rows);

    const QString resolvedCwd = cwd.isEmpty() ? QDir::currentPath()
                                              : QDir(cwd).absolutePath();
    if (!QFileInfo(resolvedCwd).isDir()) {
        emit errorOccurred(QStringLiteral("Working directory does not exist: %1")
                               .arg(resolvedCwd));
        return;
    }
    m_cwd = resolvedCwd;
    emit workingDirectoryChanged(m_cwd);

#ifdef Q_OS_WIN
    if (!conPty().load()) {
        emit errorOccurred(QStringLiteral(
            "ConPTY is unavailable; Windows 10 version 1809 or newer is required."));
        return;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE inRead = nullptr;
    HANDLE inWrite = nullptr;
    HANDLE outRead = nullptr;
    HANDLE outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0) ||
        !CreatePipe(&outRead, &outWrite, &sa, 0)) {
        if (inRead) CloseHandle(inRead);
        if (inWrite) CloseHandle(inWrite);
        if (outRead) CloseHandle(outRead);
        if (outWrite) CloseHandle(outWrite);
        emit errorOccurred(QStringLiteral("Failed to create PTY pipes."));
        return;
    }
    // Only the ConPTY-side ends may be inherited by the child.
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

    COORD size;
    size.X = static_cast<SHORT>(m_cols);
    size.Y = static_cast<SHORT>(m_rows);
    void *console = nullptr;
    const HRESULT hr = conPty().create(size, inRead, outWrite, 0, &console);
    if (FAILED(hr) || !console) {
        CloseHandle(inRead);
        CloseHandle(outWrite);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit errorOccurred(QStringLiteral("CreatePseudoConsole failed (HRESULT 0x%1).")
                               .arg(QString::number(static_cast<int>(hr), 16)));
        return;
    }
    // ConPTY keeps its own references; ours are no longer needed.
    CloseHandle(inRead);
    CloseHandle(outWrite);

    SIZE_T attrSize = 0;
    if (!conPty().initAttr(nullptr, 1, 0, &attrSize)) {
        conPty().close(console);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit errorOccurred(QStringLiteral("Could not size the process attribute list."));
        return;
    }
    auto *attrList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!attrList || !conPty().initAttr(attrList, 1, 0, &attrSize)) {
        if (attrList) HeapFree(GetProcessHeap(), 0, attrList);
        conPty().close(console);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit errorOccurred(QStringLiteral("Could not initialise the process attribute list."));
        return;
    }
    conPty().updateAttr(attrList, 0, kProcThreadAttributePseudoConsole,
                        console, sizeof(console), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList;
    PROCESS_INFORMATION pi{};

    QString shell = env.value(QStringLiteral("SHELL")).toString();
    if (shell.isEmpty()) shell = QStringLiteral("cmd.exe");
    std::wstring commandLine = shell.toStdWString();
    const QString envBlock = environmentBlock(env);
    const std::wstring wideEnv = envBlock.toStdWString();
    const std::wstring wideCwd = m_cwd.toStdWString();

    const BOOL started = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        const_cast<void*>(static_cast<const void*>(wideEnv.data())), wideCwd.data(), &si.StartupInfo, &pi);

    conPty().deleteAttr(attrList);
    HeapFree(GetProcessHeap(), 0, attrList);

    if (!started) {
        conPty().close(console);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        emit errorOccurred(QStringLiteral("Failed to start shell '%1' (error %2).")
                               .arg(shell)
                               .arg(static_cast<unsigned long>(GetLastError())));
        return;
    }
    CloseHandle(pi.hThread); // the terminal only needs the process handle
    m_native.console = console;
    m_native.inWrite = static_cast<void *>(inWrite);
    m_native.outRead = static_cast<void *>(outRead);
    m_native.process = static_cast<void *>(pi.hProcess);
#else
    int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0) {
        emit errorOccurred(QStringLiteral("posix_openpt failed (%1).").arg(errno));
        return;
    }
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        ::close(master);
        emit errorOccurred(QStringLiteral("Could not unlock the pseudo-terminal."));
        return;
    }
    char *slaveName = ::ptsname(master);
    if (!slaveName) {
        ::close(master);
        emit errorOccurred(QStringLiteral("Could not resolve the pty slave name."));
        return;
    }

    QString shell = env.value(QStringLiteral("SHELL")).toString();
    if (shell.isEmpty()) {
#ifdef Q_OS_MACOS
        shell = QStringLiteral("/bin/zsh");
#else
        shell = QStringLiteral("/bin/bash");
#endif
    }
    if (!QFileInfo(shell).isExecutable()) shell = QStringLiteral("/bin/sh");

    // Prepare everything the child needs BEFORE fork(): calling Qt/allocating
    // in a forked multithreaded process is not safe.
    const QByteArray cwdBytes = m_cwd.toUtf8();
    QByteArray shellBytes = shell.toUtf8(); // execve's argv needs char*, not const char*
    QProcessEnvironment penv = QProcessEnvironment::systemEnvironment();
    for (auto it = env.constBegin(); it != env.constEnd(); ++it)
        penv.insert(it.key(), it.value().toString());
    std::vector<QByteArray> assignments;
    const QStringList keys = penv.keys();
    assignments.reserve(keys.size());
    for (const QString &key : keys)
        assignments.push_back(key.toUtf8() + "=" + penv.value(key).toUtf8());
    std::vector<char *> envp;
    envp.reserve(assignments.size() + 1);
    for (QByteArray &item : assignments) envp.push_back(item.data());
    envp.push_back(nullptr);
    char *argv[] = {shellBytes.data(), nullptr};

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        emit errorOccurred(QStringLiteral("fork failed (%1).").arg(errno));
        return;
    }
    if (pid == 0) {
        ::setsid();
        const int slave = ::open(slaveName, O_RDWR);
        if (slave >= 0) {
#ifdef TIOCSCTTY
            ::ioctl(slave, TIOCSCTTY, 0);
#endif
            ::dup2(slave, STDIN_FILENO);
            ::dup2(slave, STDOUT_FILENO);
            ::dup2(slave, STDERR_FILENO);
            if (slave > STDERR_FILENO) ::close(slave);
            ::close(master);
        }
        if (!cwdBytes.isEmpty()) ::chdir(cwdBytes.constData());
        ::execve(shellBytes.constData(), argv, envp.data());
        ::execl("/bin/sh", "sh", static_cast<char *>(nullptr));
        ::_exit(127);
    }

    struct winsize ws {};
    ws.ws_col = static_cast<unsigned short>(m_cols);
    ws.ws_row = static_cast<unsigned short>(m_rows);
    ::ioctl(master, TIOCSWINSZ, &ws);
    m_native.masterFd = master;
    m_native.pid = static_cast<qint64>(pid);
#endif

    m_running = true;
    emit runningChanged(true);
    m_pollTimer->start();
}

void TerminalSession::sendInput(const QByteArray &data) {
    if (!m_running || data.isEmpty()) return;
#ifdef Q_OS_WIN
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(m_native.inWrite), data.constData(),
              DWORD(data.size()), &written, nullptr);
#else
    int offset = 0;
    while (offset < data.size()) {
        const ssize_t n = ::write(m_native.masterFd, data.constData() + offset,
                                  size_t(data.size() - offset));
        if (n > 0) {
            offset += int(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        break;
    }
#endif
}

void TerminalSession::resize(int cols, int rows) {
    cols = qBound(2, cols, 500);
    rows = qBound(1, rows, 500);
    if (cols == m_cols && rows == m_rows) return;
    m_cols = cols;
    m_rows = rows;
    emit columnsChanged(cols);
    emit rowsChanged(rows);

#ifdef Q_OS_WIN
    if (m_running && m_native.console) {
        COORD size;
        size.X = static_cast<SHORT>(cols);
        size.Y = static_cast<SHORT>(rows);
        conPty().resize(m_native.console, size);
    }
#else
    if (m_running && m_native.masterFd >= 0) {
        struct winsize ws {};
        ws.ws_col = static_cast<unsigned short>(cols);
        ws.ws_row = static_cast<unsigned short>(rows);
        ::ioctl(m_native.masterFd, TIOCSWINSZ, &ws);
    }
#endif
}

void TerminalSession::interrupt() {
#ifdef Q_OS_WIN
    sendInput(QByteArray(1, char(0x03))); // ConPTY turns this into SIGINT
#else
    if (m_running && m_native.pid > 0)
        ::kill(static_cast<pid_t>(m_native.pid), SIGINT);
#endif
}

void TerminalSession::poll() {
    if (!m_running) return;
    int exitCode = 0;
    bool exited = false;

#ifdef Q_OS_WIN
    HANDLE out = static_cast<HANDLE>(m_native.outRead);
    auto drain = [this, out]() {
        DWORD available = 0;
        while (PeekNamedPipe(out, nullptr, 0, nullptr, &available, nullptr) &&
               available > 0) {
            QByteArray buf;
            buf.resize(int(qMin<DWORD>(available, DWORD(65536))));
            DWORD got = 0;
            if (!ReadFile(out, buf.data(), DWORD(buf.size()), &got, nullptr) || got == 0)
                break;
            buf.resize(int(got));
            emit dataReceived(QString::fromUtf8(buf));
        }
    };
    drain();
    if (WaitForSingleObject(static_cast<HANDLE>(m_native.process), 0) == WAIT_OBJECT_0) {
        drain();
        DWORD code = 1;
        GetExitCodeProcess(static_cast<HANDLE>(m_native.process), &code);
        exitCode = int(code);
        exited = true;
    }
#else
    auto drain = [this]() {
        char buf[16384];
        for (;;) {
            const ssize_t n = ::read(m_native.masterFd, buf, sizeof(buf));
            if (n > 0) {
                emit dataReceived(QString::fromUtf8(buf, int(n)));
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                break;
            break; // EIO/EOF: the slave side closed
        }
    };
    drain();
    int status = 0;
    if (::waitpid(static_cast<pid_t>(m_native.pid), &status, WNOHANG) ==
        static_cast<pid_t>(m_native.pid)) {
        drain();
        if (WIFEXITED(status))
            exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            exitCode = 128 + WTERMSIG(status);
        else
            exitCode = -1;
        exited = true;
    }
#endif

    if (exited) {
        m_running = false;
        m_pollTimer->stop();
        emit runningChanged(false);
        emit processFinished(exitCode);
        cleanup();
    }
}

void TerminalSession::close() {
    if (!m_running) {
        cleanup();
        return;
    }
    m_running = false;
    m_pollTimer->stop();
#ifdef Q_OS_WIN
    if (m_native.process)
        TerminateProcess(static_cast<HANDLE>(m_native.process), 1);
#else
    if (m_native.pid > 0) {
        ::kill(static_cast<pid_t>(m_native.pid), SIGHUP);
        ::kill(static_cast<pid_t>(m_native.pid), SIGKILL);
        ::waitpid(static_cast<pid_t>(m_native.pid), nullptr, 0);
    }
#endif
    emit runningChanged(false);
    emit processFinished(-1); // distinguish an explicit close from a normal exit
    cleanup();
}

void TerminalSession::cleanup() {
#ifdef Q_OS_WIN
    if (m_native.console) {
        conPty().close(m_native.console);
        m_native.console = nullptr;
    }
    auto closeHandle = [](void *&h) {
        if (h) {
            CloseHandle(static_cast<HANDLE>(h));
            h = nullptr;
        }
    };
    closeHandle(m_native.inWrite);
    closeHandle(m_native.outRead);
    closeHandle(m_native.process);
#else
    if (m_native.masterFd >= 0) {
        ::close(m_native.masterFd);
        m_native.masterFd = -1;
    }
    m_native.pid = -1;
#endif
}

} // namespace ModelBench
