#pragma once

#include <QObject>
#include <QQmlApplicationEngine>
#include <QString>
#include "inference/EngineInstaller.h"

namespace ModelFit {

class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject *parent = nullptr);
    ~Application() override;

    bool initialize();
    void registerQmlTypes(QQmlApplicationEngine &engine);

    QString dataDirectory() const;
    QString modelsDirectory() const;
    QString logsDirectory() const;

    EngineInstaller *engineInstaller() const { return m_engineInstaller; }

signals:
    void initialized();

private:
    bool initPaths();
    bool initDatabase();
    bool initLogging();

    QString m_dataDir;
    QString m_modelsDir;
    QString m_logsDir;
    EngineInstaller *m_engineInstaller = nullptr;
};

} // namespace ModelFit
