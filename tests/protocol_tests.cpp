#include <QtTest>
#include <QSignalSpy>
#include "controller.h"
class Tests:public QObject {
 Q_OBJECT
private slots:
 void repeatedSettingsPreserveSynchronization(){
  Controller c(true,"test");c.start();QTRY_VERIFY(c.synced);
  const auto verified=c.config;
  for(int repeat=0;repeat<3;++repeat)
   for(int i=0;i<13;++i){
    c.receive(0x300+i,Protocol::encode(verified[i]));
    QVERIFY(c.synced);QVERIFY(c.config==verified);
   }
  // External changes still require re-reading; they never silently update the cache.
  c.receive(0x309,Protocol::encode(verified[9]+1));
  QVERIFY(!c.synced);QVERIFY(c.config==verified);
  QVERIFY(c.status.contains("309"));
  c.receive(0x309,Protocol::encode(verified[9]));QVERIFY(!c.synced);
 }
 void capturedFourByteTemperatures(){
  Controller c(false,"can0"); // Replay RX without opening a device or running the simulator.
  c.receive(0x100,QByteArray::fromHex("1f010000"));
  c.receive(0x101,QByteArray::fromHex("1c010000"));
  c.receive(0x102,QByteArray::fromHex("25010000"));
  c.receive(0x103,QByteArray::fromHex("1c010000"));
  c.receive(0x104,QByteArray::fromHex("9d2a"));
  c.receive(0x105,QByteArray::fromHex("02"));
  c.receive(0x106,QByteArray::fromHex("1f"));
  c.receive(0x107,QByteArray::fromHex("06"));
  QCOMPARE(c.temperatures[0],28.7);QCOMPARE(c.temperatures[1],28.4);
  QCOMPARE(c.temperatures[2],29.3);QCOMPARE(c.temperatures[3],28.4);
  QVERIFY(qAbs(c.mean()-28.8)<1e-9);QCOMPARE(c.temperatures[4],10.909);
  QCOMPARE(c.door,0);QCOMPARE(c.packs[0],31);QCOMPARE(c.packs[1],6);
  QVERIFY(!c.synced); // Telemetry must not depend on configuration synchronization.
  for(int i=0;i<5;++i)QVERIFY(c.fresh(i));
 }
 void telemetryWidthsAndSignedness(){
  Controller c(false,"can0");
  c.receive(0x100,QByteArray::fromHex("83ffffff"));QCOMPARE(c.temperatures[0],-12.5);
  c.receive(0x101,QByteArray::fromHex("83ff"));QCOMPARE(c.temperatures[1],-12.5);
  c.receive(0x102,QByteArray::fromHex("00000100"));QCOMPARE(c.temperatures[2],6553.6);
  const auto timestamp=c.seen[0];
  for(int bytes:{0,1,3,5,8}){c.receive(0x100,QByteArray(bytes,0));QCOMPARE(c.temperatures[0],-12.5);QCOMPARE(c.seen[0],timestamp);}
  c.receive(0x104,QByteArray::fromHex("9d2a0000"));QCOMPARE(c.seen[4],qint64(0));
 }
 void signedLittleEndian(){QCOMPARE(Protocol::encode(-125,2).toHex(),QByteArray("83ff"));QCOMPARE(*Protocol::decode(QByteArray::fromHex("83ff"),2,true),qint64(-125));QCOMPARE(*Protocol::decode(QByteArray::fromHex("ffffffff"),4,false),qint64(4294967295LL));QVERIFY(!Protocol::decode(QByteArray(3,0),4,true));}
 void modulo(){Protocol::Config c{};c[0]=65535;QCOMPARE(Protocol::signature(c),quint16(0));c[0]=-1;QCOMPARE(Protocol::signature(c),quint16(65534));c[0]=65536;QCOMPARE(Protocol::signature(c),quint16(1));}
 void telemetry(){Controller c(true,"test");QVERIFY(std::isnan(c.mean()));c.receive(0x100,Protocol::encode(20,2));c.receive(0x101,Protocol::encode(30,2));c.receive(0x102,Protocol::encode(40,2));QCOMPARE(c.mean(),3.0);c.receive(0x100,QByteArray(1,0));QCOMPARE(c.mean(),3.0);c.receive(0x105,QByteArray(1,3));QCOMPARE(c.door,1);c.receive(1,QByteArray(1,0x52));QVERIFY(!c.alarm.isEmpty());c.receive(0x105,QByteArray(1,2));QVERIFY(c.alarm.isEmpty());c.seen[0]-=7000;QVERIFY(std::isnan(c.mean()));}
 void deltaAndCommit(){Controller c(true,"test");QSignalSpy spy(&c,&Controller::transmitted);c.start();QTRY_VERIFY(c.synced);QTest::qWait(250);spy.clear();auto target=c.config;target[9]=-12;c.save(target);QTRY_VERIFY_WITH_TIMEOUT(c.synced,2500);QCOMPARE(spy.size(),3);QCOMPARE(spy[0][0].toUInt(),uint(0x309));QCOMPARE(spy[0][1].toByteArray(),Protocol::encode(-12));QCOMPARE(spy[1][0].toUInt(),uint(0x30f));QCOMPARE(spy[1][1].toByteArray(),Protocol::encode(Protocol::signature(target),2));QVERIFY(spy[2][2].toBool());spy.clear();c.save(target);QTest::qWait(250);QCOMPARE(spy.size(),0);}
 void mismatchFetchesOffsets(){Controller c(true,"test");QSignalSpy spy(&c,&Controller::transmitted);c.start();c.receive(0x30f,Protocol::encode(Protocol::signature(c.config)+1,2));QTest::qWait(1600);QSet<uint> ids;for(auto row:spy)if(row[2].toBool())ids.insert(row[0].toUInt());for(uint id=0x300;id<=0x30c;++id)QVERIFY(ids.contains(id));QVERIFY(!c.synced);}
};
QTEST_GUILESS_MAIN(Tests)
#include "protocol_tests.moc"
