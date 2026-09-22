#include "canhistory.h"
#include "protocol.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <QDateTime>

void HistoryWriter::open(const QString &path) {
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){emit error("Dossier SQLite inaccessible : "+path);return;}
 db=QSqlDatabase::addDatabase("QSQLITE",QUuid::createUuid().toString());
 db.setDatabaseName(path);db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=2000");
 if(!db.open()){emit error(db.lastError().text());close();return;}
 QSqlQuery q(db);
 for(const auto &sql:QStringList{
  "PRAGMA journal_mode=WAL",
  "CREATE TABLE IF NOT EXISTS releves (id INTEGER PRIMARY KEY, timestamp_ms INTEGER NOT NULL, date_utc TEXT NOT NULL, temp_cap1 REAL, temp_cap2 REAL, temp_cap3 REAL, temp_eva REAL, batterie REAL, porte_ouverte INTEGER, ventilateur_1 INTEGER, ventilateur_2 INTEGER, ventilateur_3 INTEGER, ventilateur_4 INTEGER, ventilateur_5 INTEGER, ventilateur_degivrage INTEGER, lampe INTEGER, compresseur INTEGER, relais_porte INTEGER, temperature_moyenne REAL, erreur TEXT)",
  "CREATE INDEX IF NOT EXISTS releves_time ON releves(timestamp_ms)",
  "PRAGMA user_version=2"}) {
  if(!q.exec(sql)){emit error(q.lastError().text());q.finish();db.close();return;}
 }
 // Create the timer here, on the SQL worker's thread.
 sampleTimer=new QTimer(this);sampleTimer->setInterval(30000);sampleTimer->setTimerType(Qt::PreciseTimer);
 connect(sampleTimer,&QTimer::timeout,this,[this]{snapshot(QDateTime::currentMSecsSinceEpoch());});
 sampleTimer->start();
}
void HistoryWriter::append(quint32 id,const QByteArray &p,qint64 timestamp) {
 auto set=[&](int i,QVariant value){values[i]=value;seen[i]=timestamp;};
 if(id>=0x100 && id<=0x104){
  const int i=int(id-0x100),bytes=int(p.size());
  if(i==4?bytes==2:(bytes==2||bytes==4))
   set(i,double(*Protocol::decode(p,bytes,true))/(i==4?1000.0:10.0));
 } else if(id==0x105 && p.size()==1){
  const int raw=quint8(p[0]);set(5,raw==2||raw==3?QVariant(raw==3?1:0):QVariant());
 } else if(id==0x106 && p.size()==1){
  for(int i=0;i<5;++i)set(6+i,(quint8(p[0])>>i)&1);
 } else if(id==0x107 && p.size()==1){
  set(11,(quint8(p[0])>>2)&1);set(12,quint8(p[0])&1);
  set(13,(quint8(p[0])>>1)&1);set(14,(quint8(p[0])>>3)&1);
 } else if(id==1 && p.size()==1){
  errors.append(QDateTime::fromMSecsSinceEpoch(timestamp,Qt::UTC).toString(Qt::ISODateWithMs)+
   QString(" 0x%1 : ").arg(quint8(p[0]),2,16,QChar('0'))+Protocol::errorText(quint8(p[0])));
 }
}
void HistoryWriter::snapshot(qint64 timestamp){
 if(!db.isOpen())return;
 QSqlQuery q(db);
 q.prepare("INSERT INTO releves(timestamp_ms,date_utc,temp_cap1,temp_cap2,temp_cap3,temp_eva,batterie,porte_ouverte,ventilateur_1,ventilateur_2,ventilateur_3,ventilateur_4,ventilateur_5,ventilateur_degivrage,lampe,compresseur,relais_porte,temperature_moyenne,erreur) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
 q.addBindValue(timestamp);q.addBindValue(QDateTime::fromMSecsSinceEpoch(timestamp,Qt::UTC).toString(Qt::ISODateWithMs));
 bool meanValid=true;double sum=0;
 for(int i=0;i<15;++i){
  const bool fresh=seen[i]>0 && timestamp>=seen[i] && timestamp-seen[i]<6000 && values[i].isValid();
  q.addBindValue(fresh?values[i]:QVariant());
  if(i<3){meanValid &= fresh;sum+=values[i].toDouble();}
 }
 q.addBindValue(meanValid?QVariant(sum/3):QVariant());
 q.addBindValue(errors.isEmpty()?QVariant():QVariant(errors.join("\n")));
 if(!q.exec()){emit error(q.lastError().text());return;}
 errors.clear();
}
void HistoryWriter::close(){
 if(sampleTimer)sampleTimer->stop();
 if(!db.isValid())return;
 auto name=db.connectionName();db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(name);
}
CanHistory::CanHistory(QString path,QObject *parent):QObject(parent),writer(new HistoryWriter){
 writer->moveToThread(&thread);
 connect(writer,&HistoryWriter::error,this,&CanHistory::error);
 connect(&thread,&QThread::finished,writer,&QObject::deleteLater);
 thread.start();QMetaObject::invokeMethod(writer,[this,path]{writer->open(path);},Qt::QueuedConnection);
}
CanHistory::~CanHistory(){
 // Drain all previously queued records before closing the connection on its owning thread.
 QMetaObject::invokeMethod(writer,[this]{writer->close();},Qt::BlockingQueuedConnection);
 thread.quit();thread.wait();
}
void CanHistory::append(quint32 id,const QByteArray &p,qint64 timestamp){
 QMetaObject::invokeMethod(writer,[this,id,p,timestamp]{writer->append(id,p,timestamp);},Qt::QueuedConnection);
}
