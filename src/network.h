#pragma once
#include <QObject>
#include <QStringList>
class Network : public QObject {
 Q_OBJECT
public:
 using QObject::QObject;
 void scan();
 void connectWifi(const QString &ssid,const QString &password);
 void radio(bool enabled);
 static QString addresses();
signals:
 void networks(QStringList names);
 void status(QString text);
private:
 bool running=false;
 void run(QStringList args,QString password={},bool scanning=false);
};
