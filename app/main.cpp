#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickStyle>

#include "Application.h"
#include "core/Navigation.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("ModelBench");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("ModelBench");
    app.setOrganizationDomain("modelfit.app");

    ModelBench::Application core;
    if (!core.initialize()) {
        return 1;
    }

    ModelBench::Navigation navigation;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("Navigation", &navigation);
    core.registerQmlTypes(engine);

    const QUrl url(QStringLiteral("qrc:/ModelBench/app/ui/Main.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return app.exec();
}
