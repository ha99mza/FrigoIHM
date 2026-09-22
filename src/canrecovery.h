#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <QProcess>

class CanRecovery : public QObject {
 Q_OBJECT
public:
 explicit CanRecovery(QString interfaceName,int silenceMs=10000,QObject *parent=nullptr);
 ~CanRecovery() override;
 void start();
 void received();
signals:
 void restarting();
 void finished(bool success,QString message);
private:
 friend class StorageTests;
 QString interfaceName,ipProgram;
 QTimer watchdog,commandTimeout;
 QElapsedTimer silence,cooldown;
 QProcess process;
 int silenceMs;
 int step=0;
 bool testMode=false;
 void check();
 void launch();
 void completed(bool success,QString detail);
};
