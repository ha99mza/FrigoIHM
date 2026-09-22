#pragma once
#include <QObject>
#include <QThread>
#include <QSqlDatabase>
#include <array>

// All SQL work and the connection live in the worker thread.
class HistoryWriter : public QObject {
 Q_OBJECT
public:
 void open(const QString &path);
 void append(quint32 id,const QByteArray &payload,qint64 timestamp);
 void close();
signals:
 void error(QString message);
private:
 QSqlDatabase db;
 std::array<double,3> caps{};
 std::array<qint64,3> seen{};
};
class CanHistory : public QObject {
 Q_OBJECT
public:
 explicit CanHistory(QString path,QObject *parent=nullptr);
 ~CanHistory() override;
 void append(quint32 id,const QByteArray &payload,qint64 timestamp);
signals:
 void error(QString message);
private:
 QThread thread;
 HistoryWriter *writer;
};
