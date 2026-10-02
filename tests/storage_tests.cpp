#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QFile>
#include "canhistory.h"
#include "canrecovery.h"
#include "controller.h"

class FakeCloud : public QObject {
public:
 QTcpServer server;
 QList<QByteArray> requests;
 QList<qint64> arrivalTimes;
 QList<QByteArray> responses;
 QList<int> statuses;
 FakeCloud(){
  server.listen(QHostAddress::LocalHost);
  connect(&server,&QTcpServer::newConnection,this,[this]{
   auto socket=server.nextPendingConnection();
   connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
   connect(socket,&QTcpSocket::readyRead,this,[this,socket]{
    QByteArray bytes=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",bytes);
    int split=bytes.indexOf("\r\n\r\n");if(split<0)return;
    int size=0;for(auto line:bytes.left(split).split('\n'))if(line.toLower().startsWith("content-length:"))size=line.mid(15).trimmed().toInt();
    if(bytes.size()<split+4+size||socket->property("handled").toBool())return;
    socket->setProperty("handled",true);requests.append(bytes);arrivalTimes.append(QDateTime::currentMSecsSinceEpoch());
    auto body=responses.isEmpty()?QByteArray("{\"success\":true}"):responses.takeFirst();
    auto status=statuses.isEmpty()?200:statuses.takeFirst();
    socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
    socket->disconnectFromHost();
   });
  });
 }
 QUrl url()const{return QUrl(QString("http://127.0.0.1:%1/event").arg(server.serverPort()));}
 static QJsonObject body(QByteArray request){return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n")+4)).object();}
};

class StorageTests : public QObject {
 Q_OBJECT
private slots:
 void backlogDrainsAcrossRestartThenWaitsForNewReading(){
  QTemporaryDir dir;auto config=dir.filePath("cloud.json"),path=dir.filePath("paced.sqlite");
  {QFile f(config);QVERIFY(f.open(QIODevice::WriteOnly));f.write("{\"apiToken\":\"test\",\"serial\":\"test\",\"accessToken\":\"test\"}");}
  FakeCloud server;
  {
   HistoryWriter writer;writer.open(path);writer.sampleTimer->stop();writer.cloudEndpoint=server.url();
   auto now=QDateTime::currentMSecsSinceEpoch();for(int i=0;i<3;++i)writer.snapshot(now+i);
   writer.configureCloud(config);QTRY_COMPARE(server.requests.size(),1);QTRY_VERIFY(!writer.reply);writer.close();
  }
  HistoryWriter writer;writer.open(path);writer.sampleTimer->stop();writer.cloudEndpoint=server.url();writer.configureCloud(config);
  QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),3,4000);QTRY_VERIFY(!writer.reply);
  QVERIFY(server.arrivalTimes[2]-server.arrivalTimes[1]>=490);
  for(auto request:server.requests)QVERIFY(FakeCloud::body(request)["data"].toObject()["error"].isNull());
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves WHERE sent=0"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);}
  writer.sendPending();QTest::qWait(1100);QCOMPARE(server.requests.size(),3); // Empty storage cannot resend stale data.
  writer.append(1,QByteArray(1,char(0x52)),QDateTime::currentMSecsSinceEpoch());
  QTRY_COMPARE(server.requests.size(),4);QTRY_VERIFY(!writer.reply);
  QCOMPARE(FakeCloud::body(server.requests[3])["data"].toObject()["error"].toString(),QString("Porte ouverte trop longtemps"));
  writer.sampleTimer->start();
  QTRY_COMPARE_WITH_TIMEOUT(server.requests.size(),5,32000);
  QVERIFY(server.arrivalTimes[4]-server.arrivalTimes[3]>=29900);
  QVERIFY(FakeCloud::body(server.requests[4])["data"].toObject()["error"].isNull());
  writer.close();
 }
 void meanUsesOneDecimal(){
  Controller c(true,"test");const int raw[]={20,21,23};
  for(int i=0;i<3;++i)c.receive(0x100+i,Protocol::encode(raw[i],2));QCOMPARE(c.mean(),2.1);
  for(int i=0;i<3;++i)c.receive(0x100+i,Protocol::encode(-raw[i],2));QCOMPARE(c.mean(),-2.1);
  QTemporaryDir dir;HistoryWriter writer;writer.open(dir.filePath("round.sqlite"));writer.sampleTimer->stop();
  auto now=QDateTime::currentMSecsSinceEpoch();
  for(int i=0;i<3;++i)writer.append(0x100+i,Protocol::encode(raw[i],2),now);
  writer.snapshot(now);
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT temperature_moyenne FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),2.1);}
  writer.close();
 }
 void cloudRejectsUnconfirmedResponses_data(){
  QTest::addColumn<int>("status");QTest::addColumn<QByteArray>("response");
  QTest::newRow("HTTP failure despite success body")<<503<<QByteArray("{\"success\":true}");
  QTest::newRow("credentials refused")<<401<<QByteArray("{\"success\":false}");
  QTest::newRow("malformed JSON")<<200<<QByteArray("not json");
  QTest::newRow("missing confirmation")<<200<<QByteArray("{}");
 }
 void cloudRejectsUnconfirmedResponses(){
  QFETCH(int,status);QFETCH(QByteArray,response);
  QTemporaryDir dir;auto config=dir.filePath("cloud.json");
  {QFile f(config);QVERIFY(f.open(QIODevice::WriteOnly));f.write("{\"apiToken\":\"test\",\"serial\":\"test\",\"accessToken\":\"test\"}");}
  FakeCloud server;server.statuses.append(status);server.responses.append(response);
  HistoryWriter writer;writer.open(dir.filePath("test.sqlite"));writer.sampleTimer->stop();writer.cloudEndpoint=server.url();
  writer.configureCloud(config);writer.snapshot(QDateTime::currentMSecsSinceEpoch());
  QTRY_COMPARE(server.requests.size(),1);QTRY_VERIFY(!writer.reply);
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT sent,last_http_status,next_retry_ms FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);QCOMPARE(q.value(1).toInt(),status);QVERIFY(q.value(2).toLongLong()>QDateTime::currentMSecsSinceEpoch());}
  writer.close();
 }
 void cloudMigrationPreservesOldReadings(){
  QTemporaryDir dir;const auto path=dir.filePath("legacy.sqlite");
  {
   auto db=QSqlDatabase::addDatabase("QSQLITE","old-cloud");db.setDatabaseName(path);QVERIFY(db.open());
   QSqlQuery q(db);QVERIFY(q.exec("CREATE TABLE releves(id INTEGER PRIMARY KEY,timestamp_ms INTEGER,date_utc TEXT,temperature_moyenne REAL)"));
   QVERIFY(q.exec("INSERT INTO releves VALUES(1,1000,'old',4.5)"));
  }
  QSqlDatabase::removeDatabase("old-cloud");
  HistoryWriter writer;writer.open(path);QVERIFY(writer.db.isOpen());writer.sampleTimer->stop();
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT temperature_moyenne,sent FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toDouble(),4.5);QCOMPARE(q.value(1).toInt(),0);}
  writer.close();
 }
 void cloudRetriesStablePayloadAndImmediateErrors(){
  QTemporaryDir dir;auto config=dir.filePath("cloud.json");
  {QFile f(config);QVERIFY(f.open(QIODevice::WriteOnly));f.write("{\"apiToken\":\"test-api\",\"serial\":\"FRIGO-TEST\",\"accessToken\":\"test-device\"}");}
  FakeCloud server;QVERIFY(server.server.isListening());server.responses.append("{\"success\":false}");
  const auto path=dir.filePath("cloud.sqlite");
  QString eventId;QByteArray original;
  {
   HistoryWriter writer;writer.open(path);writer.sampleTimer->stop();writer.cloudEndpoint=server.url();
   writer.setCloudContext({{"settingsTempMax",5.0},{"settingsTempMin",2.0},{"settingsEvapMin",-15.0},{"maintenanceMode",true}});
   auto now=QDateTime::currentMSecsSinceEpoch();for(int i=0;i<3;++i)writer.append(0x100+i,Protocol::encode(30+i*10,2),now);
   writer.configureCloud(config);writer.snapshot(now);
   QTRY_COMPARE(server.requests.size(),1);QTRY_VERIFY(!writer.reply);
   {
    QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT sent,event_id,next_retry_ms FROM releves"));QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(),0);eventId=q.value(1).toString();QVERIFY(!eventId.isEmpty());QVERIFY(q.value(2).toLongLong()>now);
   }
   original=server.requests[0];QVERIFY(original.contains("Authorization: Bearer test-api"));QVERIFY(original.contains(eventId.toUtf8()));
   auto body=FakeCloud::body(original);QCOMPARE(body["serial"].toString(),QString("FRIGO-TEST"));QCOMPARE(body["accessToken"].toString(),QString("test-device"));
   auto data=body["data"].toObject();QCOMPARE(data["temperature"].toDouble(),4.0);QCOMPARE(data["tempSen1"].toDouble(),3.0);
   QVERIFY(data["error"].isNull());QCOMPARE(data["settingsTempMax"].toDouble(),5.0);QVERIFY(data["maintenanceMode"].toBool());QVERIFY(data["datetime"].toString().endsWith('Z'));
   // No parallel sender for the same database.
   HistoryWriter other;other.open(path);other.sampleTimer->stop();other.cloudEndpoint=server.url();
   QSignalSpy locked(&other,&HistoryWriter::error);other.configureCloud(config);QCOMPARE(locked.size(),1);QVERIFY(!other.network);other.close();
   writer.close();
  }
  {
   HistoryWriter writer;writer.open(path);writer.sampleTimer->stop();writer.cloudEndpoint=server.url();writer.configureCloud(config);
   QCOMPARE(server.requests.size(),1); // Persisted backoff survives restart.
   {QSqlQuery q(writer.db);QVERIFY(q.exec("UPDATE releves SET next_retry_ms=0"));}
   writer.sendPending();QTRY_COMPARE(server.requests.size(),2);QTRY_VERIFY(!writer.reply);
   QCOMPARE(FakeCloud::body(server.requests[1]),FakeCloud::body(original));QVERIFY(server.requests[1].contains(eventId.toUtf8()));
   {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT sent,send_attempts FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);QCOMPARE(q.value(1).toInt(),2);}
   writer.sendPending();QTest::qWait(600);QCOMPARE(server.requests.size(),2);
   // Receipt of an error creates and sends a NEW row without waiting 30 seconds.
   writer.append(1,QByteArray(1,char(0x52)),QDateTime::currentMSecsSinceEpoch());
   QTRY_COMPARE(server.requests.size(),3);QTRY_VERIFY(!writer.reply);
   QCOMPARE(FakeCloud::body(server.requests[2])["data"].toObject()["error"].toString(),QString("Porte ouverte trop longtemps"));
   {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves WHERE sent=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);}
   writer.close();
  }
 }
 void loadsPersistedMeansAndRefreshes(){
  QTemporaryDir dir;HistoryWriter writer;writer.open(dir.filePath("history.sqlite"));writer.sampleTimer->stop();
  auto now=QDateTime::currentMSecsSinceEpoch();
  auto insert=[&](qint64 time,QVariant value){
   QSqlQuery q(writer.db);q.prepare("INSERT INTO releves(timestamp_ms,date_utc,temperature_moyenne,temp_cap1) VALUES(?,'test',?,99)");
   q.addBindValue(time);q.addBindValue(value);QVERIFY(q.exec());
  };
  insert(now-30000,4.5);insert(now-90000,2.5);insert(now-60000,QVariant());
  insert(now-qint64(8)*86400000,8.0);insert(now+86400000,9.0);
  QSignalSpy loaded(&writer,&HistoryWriter::temperaturesLoaded);
  writer.readTemperatures();QCOMPARE(loaded.size(),1);
  auto points=qvariant_cast<QVector<QPointF>>(loaded.takeFirst()[0]);QCOMPARE(points.size(),3);
  QCOMPARE(points[0].y(),2.5);QVERIFY(std::isnan(points[1].y()));QCOMPARE(points[2].y(),4.5);
  QCOMPARE(points[0].x(),(now-90000)/1000.0);
  for(int i=0;i<3;++i)writer.append(0x100+i,Protocol::encode(30+i*10,2),now);
  writer.snapshot(now);QCOMPARE(loaded.size(),1);
  points=qvariant_cast<QVector<QPointF>>(loaded[0][0]);QCOMPARE(points.size(),4);QCOMPARE(points.last().y(),4.0);
  writer.close();
 }
 void persistedTelemetry(){
  QTemporaryDir dir;auto path=dir.filePath("history.sqlite");
  HistoryWriter writer;writer.open(path);QSignalSpy errors(&writer,&HistoryWriter::error);
  QCOMPARE(writer.sampleTimer->interval(),30000);writer.sampleTimer->stop();
  writer.append(0x100,QByteArray::fromHex("1f010000"),30000);
  writer.append(0x101,QByteArray::fromHex("1c010000"),30001);
  writer.append(0x102,QByteArray::fromHex("25010000"),30002);
  writer.append(0x103,QByteArray::fromHex("83ffffff"),30003);
  writer.append(0x104,QByteArray::fromHex("9d2a"),30004);
  writer.append(0x105,QByteArray::fromHex("03"),30005);
  writer.append(0x106,QByteArray::fromHex("15"),30006);
  writer.append(0x107,QByteArray::fromHex("06"),30007);
  writer.append(1,QByteArray::fromHex("52"),30009);
  writer.append(1,QByteArray::fromHex("53"),30010);
  writer.append(0x100,QByteArray::fromHex("00"),30008); // Malformed frame cannot overwrite CAP1.
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);
  }
  writer.snapshot(30011);
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT * FROM releves"));QVERIFY(q.next());
   QCOMPARE(q.value("temp_cap1").toDouble(),28.7);
   QCOMPARE(q.value("temp_eva").toDouble(),-12.5);QCOMPARE(q.value("batterie").toDouble(),10.909);
   QVERIFY(qAbs(q.value("temperature_moyenne").toDouble()-28.8)<1e-8);
   QCOMPARE(q.value("porte_ouverte").toInt(),1);
   for(int i=1;i<=5;++i)QCOMPARE(q.value(QString("ventilateur_%1").arg(i)).toInt(),i%2);
   QCOMPARE(q.value("lampe").toInt(),0);QCOMPARE(q.value("compresseur").toInt(),1);
   QCOMPARE(q.value("ventilateur_degivrage").toInt(),1);QCOMPARE(q.value("relais_porte").toInt(),0);
   QVERIFY(q.value("erreur").toString().contains("0x52"));QCOMPARE(q.value("cloud_error").toInt(),0x52);
   QVERIFY(q.next());QVERIFY(q.value("erreur").toString().contains("0x53"));
  }
  writer.snapshot(60010);
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT * FROM releves ORDER BY id DESC"));QVERIFY(q.next());
   QVERIFY(q.value("temp_cap1").isNull());QVERIFY(q.value("temperature_moyenne").isNull());
   QVERIFY(q.value("porte_ouverte").isNull());QVERIFY(q.value("erreur").isNull());
  }
  QVERIFY(errors.isEmpty());writer.close();
  HistoryWriter reopened;reopened.open(path);reopened.sampleTimer->stop();
  {QSqlQuery q(reopened.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),4);}
  reopened.close();
 }
 void timerWritesAtThirtySeconds(){
  QTemporaryDir dir;HistoryWriter writer;writer.open(dir.filePath("timed.sqlite"));
  QSignalSpy tick(writer.sampleTimer,&QTimer::timeout);QElapsedTimer elapsed;elapsed.start();
  writer.append(0x100,QByteArray::fromHex("1f010000"),QDateTime::currentMSecsSinceEpoch());
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);}
  QTRY_COMPARE_WITH_TIMEOUT(tick.size(),1,32000);QVERIFY(elapsed.elapsed()>=29900);
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),1);}
  writer.close();
 }
 void preservesLegacyTables(){
  QTemporaryDir dir;const auto path=dir.filePath("old.sqlite");
  {
   auto db=QSqlDatabase::addDatabase("QSQLITE","legacy");db.setDatabaseName(path);QVERIFY(db.open());
   QSqlQuery q(db);QVERIFY(q.exec("CREATE TABLE can_frames(seq INTEGER PRIMARY KEY,payload BLOB)"));
   QVERIFY(q.exec("INSERT INTO can_frames VALUES(1,X'0102')"));
  }
  QSqlDatabase::removeDatabase("legacy");
  HistoryWriter writer;writer.open(path);writer.sampleTimer->stop();writer.snapshot(30000);
  {QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT hex(payload) FROM can_frames"));QVERIFY(q.next());QCOMPARE(q.value(0).toString(),QString("0102"));}
  writer.close();
 }
 void inaccessibleDatabaseReportsError(){
  QTemporaryDir dir;CanHistory history(dir.path());QSignalSpy errors(&history,&CanHistory::error);
  QTRY_VERIFY(!errors.isEmpty());
 }
 void recoverySequenceAndCooldown(){
  CanRecovery r("test0",1);r.testMode=true;
  QSignalSpy restarting(&r,&CanRecovery::restarting),finished(&r,&CanRecovery::finished);
  r.start();r.watchdog.stop();r.check();QCOMPARE(restarting.size(),0);
  QTest::qWait(5);r.received();r.check();QCOMPARE(restarting.size(),0);
  QTest::qWait(5);r.check();QCOMPARE(restarting.size(),1);QCOMPARE(r.step,1);
  r.check();QCOMPARE(restarting.size(),1);
  r.completed(false,"down failed");QCOMPARE(r.step,2);QCOMPARE(finished.size(),0);
  r.completed(true,{});QCOMPARE(r.step,0);QCOMPARE(finished.size(),1);QVERIFY(finished[0][0].toBool());
  QTest::qWait(5);r.check();QCOMPARE(restarting.size(),1); // cooldown prevents a restart loop.
  r.cooldown.invalidate();r.check();QCOMPARE(restarting.size(),2);
  r.completed(true,{});r.completed(false,"sudo denied");QCOMPARE(finished.size(),2);QVERIFY(!finished[1][0].toBool());
  QVERIFY(finished[1][1].toString().contains("sudo denied"));
 }
};
QTEST_GUILESS_MAIN(StorageTests)
#include "storage_tests.moc"
