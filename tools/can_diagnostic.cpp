#include "controller.h"
#include "canhistory.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSettings>
#include <QTemporaryDir>

// Read-only CAN diagnostic: Controller::start() issues RTR requests only.
// Use an isolated cache so the real application's saved settings are untouched.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir cache;
    if (!cache.isValid()) return 2;
    QCoreApplication::setOrganizationName("FrigoDiagnostic");
    QCoreApplication::setApplicationName("ReadOnly");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, cache.path());
    Controller controller(false, argc > 1 ? QString::fromLocal8Bit(argv[1]) : "can0", nullptr, cache.filePath("settings.json"));
    const QString databasePath=argc>2?QString::fromLocal8Bit(argv[2]):cache.filePath("history.sqlite");
    CanHistory history(databasePath);
    bool storageFailed=false;
    QObject::connect(&controller,&Controller::frameReceived,&history,&CanHistory::append);
    QObject::connect(&history,&CanHistory::error,[&](QString error){storageFailed=true;qCritical().noquote()<<"SQLITE"<<error;});
    qInfo().noquote()<<"DATABASE"<<databasePath;
    QString last;
    QObject::connect(&controller, &Controller::changed, [&] {
        if (last != controller.status) {
            last = controller.status;
            qInfo().noquote() << "STATE" << controller.synced << last;
        }
    });
    QObject::connect(&controller, &Controller::transmitted,
                     [](quint32 id, QByteArray payload, bool remote) {
        qInfo().noquote() << (remote ? "TX RTR" : "UNEXPECTED DATA")
                         << QString::number(id, 16) << "DLC" << payload.size();
    });
    QObject::connect(&controller, &Controller::configChanged, [&] {
        qInfo() << "CONFIG signature" << Protocol::signature(controller.config);
        for (int i = 0; i < 13; ++i)
            qInfo().noquote() << QString::number(0x300 + i, 16) << controller.config[i];
    });
    QTimer::singleShot(0, &controller, &Controller::start);
    QTimer::singleShot(35000, &app, [&] {
        qInfo() << "FINAL synced=" << controller.synced << "mean=" << controller.mean();
        app.exit(controller.synced && !storageFailed ? 0 : 1);
    });
    return app.exec();
}
