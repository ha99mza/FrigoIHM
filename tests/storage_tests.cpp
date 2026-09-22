#include <QtTest>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QDir>
#include "canhistory.h"
#include "canrecovery.h"
#include "controller.h"

class StorageTests : public QObject {
 Q_OBJECT
private slots:
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
  writer.append(1,QByteArray::fromHex("52"),15000);
  writer.append(1,QByteArray::fromHex("53"),20000);
  writer.append(0x100,QByteArray::fromHex("00"),30008); // Malformed frame cannot overwrite CAP1.
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);
  }
  writer.snapshot(30010);
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT * FROM releves"));QVERIFY(q.next());
   QCOMPARE(q.value("temp_cap1").toDouble(),28.7);
   QCOMPARE(q.value("temp_eva").toDouble(),-12.5);QCOMPARE(q.value("batterie").toDouble(),10.909);
   QVERIFY(qAbs(q.value("temperature_moyenne").toDouble()-28.8)<1e-8);
   QCOMPARE(q.value("porte_ouverte").toInt(),1);
   for(int i=1;i<=5;++i)QCOMPARE(q.value(QString("ventilateur_%1").arg(i)).toInt(),i%2);
   QCOMPARE(q.value("lampe").toInt(),0);QCOMPARE(q.value("compresseur").toInt(),1);
   QCOMPARE(q.value("ventilateur_degivrage").toInt(),1);QCOMPARE(q.value("relais_porte").toInt(),0);
   QVERIFY(q.value("erreur").toString().contains("0x52"));QVERIFY(q.value("erreur").toString().contains("0x53"));
  }
  writer.snapshot(60010);
  {
   QSqlQuery q(writer.db);QVERIFY(q.exec("SELECT * FROM releves ORDER BY id DESC"));QVERIFY(q.next());
   QVERIFY(q.value("temp_cap1").isNull());QVERIFY(q.value("temperature_moyenne").isNull());
   QVERIFY(q.value("porte_ouverte").isNull());QVERIFY(q.value("erreur").isNull());
  }
  QVERIFY(errors.isEmpty());writer.close();
  HistoryWriter reopened;reopened.open(path);reopened.sampleTimer->stop();
  {QSqlQuery q(reopened.db);QVERIFY(q.exec("SELECT count(*) FROM releves"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),2);}
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
