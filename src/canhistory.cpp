#include "canhistory.h"
#include "protocol.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>

void HistoryWriter::open(const QString &path) {
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){emit error("Dossier SQLite inaccessible : "+path);return;}
 db=QSqlDatabase::addDatabase("QSQLITE",QUuid::createUuid().toString());
 db.setDatabaseName(path);db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=2000");
 if(!db.open()){emit error(db.lastError().text());close();return;}
 QSqlQuery q(db);
 for(const auto &sql:QStringList{
  "PRAGMA journal_mode=WAL", "PRAGMA foreign_keys=ON",
  "CREATE TABLE IF NOT EXISTS can_frames (seq INTEGER PRIMARY KEY, timestamp_ms INTEGER NOT NULL, can_id INTEGER NOT NULL, payload BLOB NOT NULL)",
  "CREATE INDEX IF NOT EXISTS frames_time ON can_frames(timestamp_ms)",
  "CREATE TABLE IF NOT EXISTS measurements (frame_seq INTEGER NOT NULL REFERENCES can_frames(seq), name TEXT NOT NULL, value REAL, text TEXT, PRIMARY KEY(frame_seq,name))",
  "PRAGMA user_version=1"}) {
  if(!q.exec(sql)){emit error(q.lastError().text());q.finish();db.close();return;}
 }
}
void HistoryWriter::append(quint32 id,const QByteArray &p,qint64 timestamp) {
 if(!db.isOpen())return;
 if(!db.transaction()){emit error(db.lastError().text());return;}
 QSqlQuery q(db);q.prepare("INSERT INTO can_frames(timestamp_ms,can_id,payload) VALUES(?,?,?)");
 q.addBindValue(timestamp);q.addBindValue(id);q.addBindValue(p);
 if(!q.exec()){db.rollback();emit error(q.lastError().text());return;}
 const auto seq=q.lastInsertId();
 bool ok=true;
 auto measurement=[&](QString name,QVariant value,QString text={}){
  QSqlQuery m(db);m.prepare("INSERT INTO measurements(frame_seq,name,value,text) VALUES(?,?,?,?)");
  m.addBindValue(seq);m.addBindValue(name);m.addBindValue(value);m.addBindValue(text);
  if(!m.exec()){ok=false;emit error(m.lastError().text());}
 };
 if(id>=0x100 && id<=0x104){
  const int i=int(id-0x100),bytes=int(p.size());
  if(i==4?bytes==2:(bytes==2||bytes==4)){
   const double value=double(*Protocol::decode(p,bytes,true))/(i==4?1000.0:10.0);
   const QStringList names={"temp_cap1","temp_cap2","temp_cap3","temp_eva","battery"};
   measurement(names[i],value);
   if(i<3){
    caps[i]=value;seen[i]=timestamp;
    bool fresh=true;for(auto t:seen)fresh &= t>0 && timestamp>=t && timestamp-t<6000;
    measurement("temperature_mean",fresh?QVariant((caps[0]+caps[1]+caps[2])/3):QVariant());
   }
  }
 } else if(id==0x105 && p.size()==1){
  const int raw=quint8(p[0]);measurement("door_raw",raw);
  measurement("door_open",raw==2||raw==3?QVariant(raw==3?1:0):QVariant());
 } else if(id==0x106 && p.size()==1){
  for(int i=0;i<5;++i)measurement(QString("fan_%1").arg(i+1),(quint8(p[0])>>i)&1);
 } else if(id==0x107 && p.size()==1){
  const QStringList names={"lamp","compressor","defrost_fan","door_relay"};
  for(int i=0;i<4;++i)measurement(names[i],(quint8(p[0])>>i)&1);
 } else if(id==1 && p.size()==1)measurement("error",quint8(p[0]),Protocol::errorText(quint8(p[0])));
 if(!ok){db.rollback();return;}
 if(!db.commit()){emit error(db.lastError().text());db.rollback();}
}
void HistoryWriter::close(){
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
