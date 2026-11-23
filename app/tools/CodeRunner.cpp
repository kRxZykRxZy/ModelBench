#include "CodeRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace ModelBench {
namespace {

QString normalizeLanguage(const QString &language) {
    const QString value = language.trimmed().toLower();
    if (value == QStringLiteral("c++") || value == QStringLiteral("cpp") ||
        value == QStringLiteral("cxx") || value == QStringLiteral("cc"))
        return QStringLiteral("cpp");
    if (value == QStringLiteral("py") || value == QStringLiteral("python3"))
        return QStringLiteral("python");
    if (value == QStringLiteral("js") || value == QStringLiteral("javascript") ||
        value == QStringLiteral("nodejs"))
        return QStringLiteral("javascript");
    return value;
}

QString findFirst(const QStringList &candidates) {
    for (const QString &name : candidates) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) return found;
    }
    return QString();
}

} // namespace

CodeRunner::CodeRunner(QObject *parent) : QObject(parent) {}

CodeRunner::~CodeRunner() { abortJob(); }

QStringList CodeRunner::availableLanguages() const {
    return {QStringLiteral("cpp"), QStringLiteral("python"),
            QStringLiteral("javascript")};
}

QString CodeRunner::displayName(const QString &language) const {
    const QString key = normalizeLanguage(language);
    if (key == QStringLiteral("cpp")) return QStringLiteral("C++");
    if (key == QStringLiteral("python")) return QStringLiteral("Python");
    if (key == QStringLiteral("javascript")) return QStringLiteral("JavaScript");
    return language;
}

QString CodeRunner::commandFor(const QString &language) const {
    const QString key = normalizeLanguage(language);
    if (key == QStringLiteral("cpp"))
        return findFirst({QStringLiteral("g++"), QStringLiteral("clang++"),
                          QStringLiteral("c++")});
    if (key == QStringLiteral("python"))
        return findFirst({QStringLiteral("python3"), QStringLiteral("python")});
    if (key == QStringLiteral("javascript"))
        return findFirst({QStringLiteral("node"), QStringLiteral("nodejs")});
    return QString();
}

bool CodeRunner::isAvailable(const QString &language) const {
    return !commandFor(language).isEmpty();
}

void CodeRunner::run(const QString &language, const QString &code,
                     const QString &cwd, int timeoutMs) {
    abortJob(); // only one snippet at a time; no stale signals for the old run

    const QString lang = normalizeLanguage(language);
    auto *job = new Job;
    job->language = lang;
    job->cwd = cwd;
    job->timeoutMs = qBound(1000, timeoutMs, 600000);
    job->startedMs = QDateTime::currentMSecsSinceEpoch();

    QString extension;
    if (lang == QStringLiteral("cpp")) {
        job->compileProgram = commandFor(lang);
        if (job->compileProgram.isEmpty()) {
            emit errorOccurred(QStringLiteral(
                "No C++ compiler found. Install g++ or clang++ and add it to PATH."));
            delete job->dir;
            delete job;
            return;
        }
        extension = QStringLiteral(".cpp");
    } else if (lang == QStringLiteral("python") || lang == QStringLiteral("javascript")) {
        job->runProgram = commandFor(lang);
        if (job->runProgram.isEmpty()) {
            emit errorOccurred(QStringLiteral("%1 was not found on PATH.")
                                   .arg(displayName(lang)));
            delete job->dir;
            delete job;
            return;
        }
        extension = lang == QStringLiteral("python") ? QStringLiteral(".py")
                                                      : QStringLiteral(".js");
    } else {
        emit errorOccurred(QStringLiteral("Unsupported language: %1").arg(language));
        delete job->dir;
        delete job;
        return;
    }

    job->dir = new QTemporaryDir();
    if (!job->dir->isValid()) {
        emit errorOccurred(QStringLiteral("Could not create a temporary directory."));
        delete job->dir;
        delete job;
        return;
    }
    const QString base = lang == QStringLiteral("cpp")
                             ? QStringLiteral("main")
                             : QStringLiteral("program");
    job->sourcePath = job->dir->path() + "/" + base + extension;

    QFile source(job->sourcePath);
    const QByteArray bytes = code.toUtf8();
    if (!source.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        source.write(bytes) != bytes.size()) {
        emit errorOccurred(QStringLiteral("Could not write the source file."));
        delete job->dir; // QTemporaryDir removes the directory on destruction
        delete job;
        return;
    }
    source.close();

    m_job = job;
    emit runningChanged(true);

    if (lang == QStringLiteral("cpp")) {
#ifdef Q_OS_WIN
        job->runProgram = job->dir->path() + QStringLiteral("/program.exe");
#else
        job->runProgram = job->dir->path() + QStringLiteral("/program");
#endif
        job->compileArgs = {QStringLiteral("-std=c++17"), QStringLiteral("-O0"),
                            QStringLiteral("-pipe"), job->sourcePath,
                            QStringLiteral("-o"), job->runProgram};
        startCompile(job);
    } else {
        job->runArgs = {job->sourcePath};
        startRun(job);
    }
}

void CodeRunner::restartTimer(Job *job, int ms) {
    if (m_job != job || !job->timer) return;
    job->timer->stop();
    job->timer->start(ms);
}

void CodeRunner::startCompile(Job *job) {
    if (m_job != job) return;

    job->timer = new QTimer(this);
    job->timer->setSingleShot(true);
    connect(job->timer, &QTimer::timeout, this, [this, job]() {
        if (m_job != job) return;
        job->timedOut = true;
        finish(job, false, QStringLiteral("Compilation timed out after %1 ms.")
                               .arg(job->timeoutMs));
    });

    auto *process = new QProcess(this);
    job->process = process;
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, job, process]() {
                appendOutput(job, QStringLiteral("compile"),
                             process->readAllStandardOutput());
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, job, process]() {
                appendOutput(job, QStringLiteral("compile"),
                             process->readAllStandardError());
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, job](QProcess::ProcessError error) {
                if (m_job == job && error == QProcess::FailedToStart)
                    finish(job, false, QStringLiteral("Could not start the compiler."));
            });
    connect(process, &QProcess::finished, this,
            [this, job, process](int code, QProcess::ExitStatus status) {
                if (m_job != job) return;
                if (status != QProcess::NormalExit || code != 0) {
                    finish(job, false,
                           QStringLiteral("Compilation failed (exit code %1).")
                               .arg(code));
                    return;
                }
                // The compiler process has ended; detach it before starting the
                // program so a later cancel() only kills the run stage.
                job->process = nullptr;
                process->deleteLater();
                startRun(job);
            });

    process->setWorkingDirectory(job->dir->path());
    process->start(job->compileProgram, job->compileArgs);
    restartTimer(job, qMax(job->timeoutMs, 30000));
}

void CodeRunner::startRun(Job *job) {
    if (m_job != job) return;

    if (!job->timer) {
        job->timer = new QTimer(this);
        job->timer->setSingleShot(true);
        connect(job->timer, &QTimer::timeout, this, [this, job]() {
            if (m_job != job) return;
            job->timedOut = true;
            finish(job, false, QStringLiteral("Run timed out after %1 ms.")
                                   .arg(job->timeoutMs));
        });
    }

    auto *process = new QProcess(this);
    job->process = process;
    process->setProcessEnvironment(QProcessEnvironment::systemEnvironment());
    process->setWorkingDirectory(job->cwd.isEmpty() ? job->dir->path() : job->cwd);

    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, job, process]() {
                appendOutput(job, QStringLiteral("stdout"),
                             process->readAllStandardOutput());
            });
    connect(process, &QProcess::readyReadStandardError, this,
            [this, job, process]() {
                appendOutput(job, QStringLiteral("stderr"),
                             process->readAllStandardError());
            });
    connect(process, &QProcess::errorOccurred, this,
            [this, job](QProcess::ProcessError error) {
                if (m_job == job && error == QProcess::FailedToStart)
                    finish(job, false, QStringLiteral("Could not start the program."));
            });
    connect(process, &QProcess::finished, this,
            [this, job](int code, QProcess::ExitStatus status) {
                if (m_job != job) return;
                job->exitCode = code;
                const bool ok = status == QProcess::NormalExit && code == 0 &&
                                !job->timedOut;
                finish(job, ok,
                       ok ? QString()
                          : QStringLiteral("Program exited with code %1.").arg(code));
            });

    process->start(job->runProgram, job->runArgs);
    restartTimer(job, job->timeoutMs);
}

void CodeRunner::appendOutput(Job *job, const QString &stream,
                              const QByteArray &bytes) {
    if (m_job != job || bytes.isEmpty()) return;
    QString &buffer = stream == QStringLiteral("compile")
                          ? job->compileOutput
                          : (stream == QStringLiteral("stderr") ? job->stderrText
                                                                : job->stdoutText);
    const qint64 room = qMax<qint64>(0, m_maxOutputBytes - buffer.size());
    const QByteArray kept = bytes.left(int(qMin<qint64>(bytes.size(), room)));
    if (kept.size() < bytes.size()) job->truncated = true;
    buffer.append(QString::fromUtf8(kept));
    emit output(stream, QString::fromUtf8(kept));
}

void CodeRunner::stopProcess(Job *job) {
    QProcess *process = job->process;
    job->process = nullptr;
    if (!process) return;
    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(500);
    }
    process->deleteLater();
}

void CodeRunner::finish(Job *job, bool ok, const QString &error) {
    if (m_job != job) return;
    stopProcess(job);
    if (job->timer) {
        job->timer->stop();
        job->timer->deleteLater();
        job->timer = nullptr;
    }

    QVariantMap result;
    result.insert(QStringLiteral("ok"), ok && !job->timedOut);
    result.insert(QStringLiteral("language"), job->language);
    result.insert(QStringLiteral("stdout"), job->stdoutText);
    result.insert(QStringLiteral("stderr"), job->stderrText);
    result.insert(QStringLiteral("compileOutput"), job->compileOutput);
    result.insert(QStringLiteral("exitCode"), job->exitCode);
    result.insert(QStringLiteral("durationMs"),
                  QDateTime::currentMSecsSinceEpoch() - job->startedMs);
    result.insert(QStringLiteral("timedOut"), job->timedOut);
    result.insert(QStringLiteral("truncated"), job->truncated);
    result.insert(QStringLiteral("error"), error);

    m_job = nullptr; // invalidate in-flight callbacks before emitting
    emit finished(result);
    if (!ok && !error.isEmpty()) emit errorOccurred(error);
    emit runningChanged(false);

    delete job->dir;
    delete job;
}

void CodeRunner::abortJob() {
    if (!m_job) return;
    Job *job = m_job;
    stopProcess(job);
    if (job->timer) {
        job->timer->stop();
        job->timer->deleteLater();
        job->timer = nullptr;
    }
    m_job = nullptr; // invalidate lambdas before emitting anything
    emit runningChanged(false);
    delete job->dir;
    delete job;
}

void CodeRunner::cancel() {
    if (!m_job) return;
    finish(m_job, false, QStringLiteral("Run cancelled"));
}

} // namespace ModelBench
