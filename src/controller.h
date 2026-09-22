#pragma once
#include "protocol.h"
#include <QObject>
#include <QCanBusDevice>
#include <QTimer>
#include <QQueue>
#include <QSet>
#include <QDateTime>
#include "canrecovery.h"
class Controller : public QObject {
 Q_OBJECT
public:
 explicit Controller(bool simulation,QString interfaceName,QObject *parent=nullptr,QString settingsFile={});
 void start();
 void enableRecovery(int silenceMs=10000);
 void synchronize();
 void save(const Protocol::Config &values);
 void maintenance(bool active);
 void requestMaintenance();
 void relay(int pack,int bit,bool active);
 void acknowledge();
 bool fresh(int index) const;
 double mean() const;
 Protocol::Config config{};
 std::array<double,5> temperatures{};
 std::array<qint64,5> seen{};
 int door=-1, packs[2]={-1,-1};
 bool maintenanceActive=false, synced=false, busy=false, simulation;
 QString status="Connexion…", alarm;
 QStringList alarmLog;
 QString alarmStart,alarmStop;
signals:
 void changed();
 void configChanged();
 void transmitted(quint32 id,QByteArray payload,bool remote);
 void frameReceived(quint32 id,QByteArray payload,qint64 timestamp);
public slots:
 void receive(quint32 id,const QByteArray &payload);
private:
 friend class Tests;
 struct Out { quint32 id; QByteArray data; bool remote; };
 QCanBusDevice *device=nullptr;
 CanRecovery *recovery=nullptr;
 QString interfaceName, settingsFile;
 QTimer sender, deadline, simulator, maintenanceDeadline;
 QQueue<Out> queue;
 QSet<int> received;
 Protocol::Config candidate{};
 Protocol::Config simulatedConfig{};
 bool cacheValid=false, fetching=false, verifying=false, waitingSignature=false;
 bool simulatedMaintenance=false, waitingMaintenance=false;
 quint16 expectedSignature=0;
 int attempts=0;
 void enqueue(quint32 id,QByteArray data,bool remote=false);
 void requestAll();
 void fail(const QString &reason);
 bool persist();
 void loadCache();
 void armDeadline();
 void sound(bool on);
};
