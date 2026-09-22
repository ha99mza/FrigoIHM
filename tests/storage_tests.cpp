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
  {
   CanHistory history(path);QSignalSpy errors(&history,&CanHistory::error);
   history.append(0x100,QByteArray::fromHex("1f010000"),10000);
   history.append(0x101,QByteArray::fromHex("1c010000"),10001);
   history.append(0x102,QByteArray::fromHex("25010000"),10002);
   history.append(0x103,QByteArray::fromHex("83ffffff"),10003);
   history.append(0x104,QByteArray::fromHex("9d2a"),10004);
   history.append(0x105,QByteArray::fromHex("03"),10005);
   history.append(0x106,QByteArray::fromHex("15"),10006);
   history.append(0x107,QByteArray::fromHex("06"),10007);
   history.append(1,QByteArray::fromHex("52"),10008);
   history.append(0x100,QByteArray::fromHex("0000"),17000); // Other sensors are stale.
   history.append(0x100,QByteArray::fromHex("00"),17001); // Raw retained, no decoded value.
  } // Destruction must drain queued records.
  const auto name=QString("storage-test");
  {
   auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);QVERIFY(db.open());
   QSqlQuery q(db);QVERIFY(q.exec("SELECT count(*) FROM can_frames"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),11);
   auto value=[&](QString key,int seq)->QVariant {
    q.prepare("SELECT value FROM measurements WHERE name=? AND frame_seq=?");q.addBindValue(key);q.addBindValue(seq);
    if(!q.exec()||!q.next())return {};return q.value(0);
   };
   QCOMPARE(value("temp_cap1",1).toDouble(),28.7);QVERIFY(value("temperature_mean",1).isNull());
   QVERIFY(qAbs(value("temperature_mean",3).toDouble()-28.8)<1e-8);
   QCOMPARE(value("temp_eva",4).toDouble(),-12.5);QCOMPARE(value("battery",5).toDouble(),10.909);
   QCOMPARE(value("door_open",6).toInt(),1);
   for(int i=1;i<=5;++i)QCOMPARE(value(QString("fan_%1").arg(i),7).toInt(),i%2);
   QCOMPARE(value("lamp",8).toInt(),0);QCOMPARE(value("compressor",8).toInt(),1);
   QCOMPARE(value("defrost_fan",8).toInt(),1);QCOMPARE(value("door_relay",8).toInt(),0);
   QCOMPARE(value("error",9).toInt(),0x52);QVERIFY(value("temperature_mean",10).isNull());
   QVERIFY(q.exec("SELECT count(*) FROM measurements WHERE frame_seq=11"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),0);
   QVERIFY(q.exec("SELECT timestamp_ms,hex(payload) FROM can_frames WHERE seq=1"));QVERIFY(q.next());QCOMPARE(q.value(0).toLongLong(),qint64(10000));QCOMPARE(q.value(1).toString(),QString("1F010000"));
   db.close();
  }
  QSqlDatabase::removeDatabase(name);
  // Reopening must append rather than truncate.
  {CanHistory history(path);history.append(0x105,QByteArray::fromHex("02"),18000);}
  {
   auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);QVERIFY(db.open());
   QSqlQuery q(db);QVERIFY(q.exec("SELECT count(*) FROM can_frames"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),12);db.close();
  }
  QSqlDatabase::removeDatabase(name);
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
