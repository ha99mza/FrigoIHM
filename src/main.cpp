#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
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
    parser.addOption({"alarm-start", "Chemin de l’exécutable qui active l’alarme", "path"});
    parser.addOption({"alarm-stop", "Chemin de l’exécutable qui arrête l’alarme", "path"});
    parser.addOption({"screenshot", "Enregistrer une capture puis quitter (simulation)", "path"});
    parser.addOption({"preview-page", "Écran à capturer en simulation", "name", "temp"});
    parser.process(app);
    Controller controller(parser.isSet("simulate"), parser.value("interface"), nullptr, parser.value("settings-file"));
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
