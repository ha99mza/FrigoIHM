#pragma once
#include <QObject>
#include <QThread>
#include <QSqlDatabase>
#include <QTimer>
#include <QVariant>
#include <QStringList>
#include <array>
#include <QVector>
#include <QPointF>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QLockFile>
#include <memory>

// All SQL work and the connection live in the worker thread.
class HistoryWriter : public QObject {
 Q_OBJECT
public:
 void open(const QString &path);
 void append(quint32 id,const QByteArray &payload,qint64 timestamp);
 void close();
 void readTemperatures();
 void configureCloud(const QString &file);
 void setCloudContext(QJsonObject context);
signals:
 void error(QString message);
 void temperaturesLoaded(QVector<QPointF> samples);
private:
 friend class StorageTests;
 QSqlDatabase db;
 QTimer *sampleTimer=nullptr;
 std::array<QVariant,15> values{};
 std::array<qint64,15> seen{};
 QStringList errors;
 QJsonObject cloudContext;
 QString databasePath,cloudSerial,cloudAccessToken,cloudApiToken;
 QNetworkAccessManager *network=nullptr;
 QNetworkReply *reply=nullptr;
 QTimer *retryTimer=nullptr;
 std::unique_ptr<QLockFile> cloudLock;
 qint64 cloudBlockedUntil=0;
 QUrl cloudEndpoint=QUrl("https://cloud.digisense.es/api/v1/deviceapi/event");
 void snapshot(qint64 timestamp,int errorCode=-1);
 void sendPending();
 bool migrateCloud();
};
class CanHistory : public QObject {
 Q_OBJECT
public:
 explicit CanHistory(QString path,QObject *parent=nullptr);
 ~CanHistory() override;
 void append(quint32 id,const QByteArray &payload,qint64 timestamp);
 void loadTemperatures();
 void configureCloud(const QString &file);
 void setCloudContext(QJsonObject context);
signals:
 void error(QString message);
 void temperaturesLoaded(QVector<QPointF> samples);
private:
 QThread thread;
 HistoryWriter *writer;
};
