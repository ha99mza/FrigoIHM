#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDebug>
#include "canhistory.h"
#include <memory>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Frigo");
    app.setApplicationName("IHM");
    QCommandLineParser parser;
    parser.setApplicationDescription("IHM réfrigérateur 1024 × 600 · C++ / Qt");
    parser.addHelpOption();
    parser.addOption({"simulate", "Utiliser une carte simulée"});
    parser.addOption({"fullscreen", "Afficher en plein écran"});
    parser.addOption({"interface", "Interface SocketCAN", "name", "can0"});
    parser.addOption({"settings-file", "Fichier JSON des reglages confirmes", "path"});
    parser.addOption({"database", "Historique SQLite (defaut : ~/frigo.sqlite)", "path"});
    parser.addOption({"can-timeout", "Relancer CAN apres N secondes sans trame (0 desactive)", "seconds", "10"});
    parser.addOption({"alarm-start", "Chemin de l’exécutable qui active l’alarme", "path"});
    parser.addOption({"alarm-stop", "Chemin de l’exécutable qui arrête l’alarme", "path"});
    parser.addOption({"screenshot", "Enregistrer une capture puis quitter (simulation)", "path"});
    parser.addOption({"preview-page", "Écran à capturer en simulation", "name", "temp"});
    parser.process(app);
    bool validTimeout=false;const int timeout=parser.value("can-timeout").toInt(&validTimeout);
    if(!validTimeout||timeout<0||timeout>3600){qCritical()<<"--can-timeout doit etre compris entre 0 et 3600";return 2;}
    Controller controller(parser.isSet("simulate"), parser.value("interface"), nullptr, parser.value("settings-file"));
    std::unique_ptr<CanHistory> history;
    if(!controller.simulation||parser.isSet("database")){
        QString path=parser.value("database");if(path.isEmpty())path=QDir::homePath()+"/frigo.sqlite";
        if(path.startsWith("~/"))path=QDir::homePath()+path.mid(1);
        history=std::make_unique<CanHistory>(path);
        QObject::connect(&controller,&Controller::frameReceived,history.get(),&CanHistory::append);
        QObject::connect(history.get(),&CanHistory::error,&controller,[&](QString error){
            qCritical().noquote()<<"SQLite:"<<error;controller.status="Erreur historique SQLite : "+error;emit controller.changed();
        });
    }
#ifdef Q_OS_LINUX
    controller.enableRecovery(timeout*1000);
#endif
    controller.alarmStart = parser.value("alarm-start");
    controller.alarmStop = parser.value("alarm-stop");
    Window window(&controller);
    if (parser.isSet("fullscreen"))
        window.showFullScreen();
    else
        window.show();
    QTimer::singleShot(0, &controller, &Controller::start);
    if (parser.isSet("screenshot") && controller.simulation)
        QTimer::singleShot(2200, &window, [&] {
            window.preview(parser.value("preview-page"));
            window.grab().save(parser.value("screenshot"));
            app.quit();
        });
    return app.exec();
}
