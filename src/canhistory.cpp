#include "canhistory.h"
#include "protocol.h"
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QUuid>
#include <QVariant>
#include <QDateTime>
#include <limits>
#include <cmath>
#include <QFile>
#include <QJsonDocument>
#include <QSqlRecord>
#include <QNetworkRequest>
#include <QSet>

void HistoryWriter::open(const QString &path) {
 databasePath=QFileInfo(path).absoluteFilePath();
 if(!QDir().mkpath(QFileInfo(path).absolutePath())){emit error("Dossier SQLite inaccessible : "+path);return;}
 db=QSqlDatabase::addDatabase("QSQLITE",QUuid::createUuid().toString());
 db.setDatabaseName(path);db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=2000");
 if(!db.open()){emit error(db.lastError().text());close();return;}
 QSqlQuery q(db);
 for(const auto &sql:QStringList{
  "PRAGMA journal_mode=WAL",
  "CREATE TABLE IF NOT EXISTS releves (id INTEGER PRIMARY KEY, timestamp_ms INTEGER NOT NULL, date_utc TEXT NOT NULL, temp_cap1 REAL, temp_cap2 REAL, temp_cap3 REAL, temp_eva REAL, batterie REAL, porte_ouverte INTEGER, ventilateur_1 INTEGER, ventilateur_2 INTEGER, ventilateur_3 INTEGER, ventilateur_4 INTEGER, ventilateur_5 INTEGER, ventilateur_degivrage INTEGER, lampe INTEGER, compresseur INTEGER, relais_porte INTEGER, temperature_moyenne REAL, erreur TEXT)",
  "CREATE INDEX IF NOT EXISTS releves_time ON releves(timestamp_ms)",
  "PRAGMA foreign_keys=ON"}) {
  if(!q.exec(sql)){emit error(q.lastError().text());q.finish();db.close();return;}
 }
 if(!migrateCloud()){db.close();return;}
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
  snapshot(timestamp,quint8(p[0]));
 }
}
void HistoryWriter::snapshot(qint64 timestamp,int errorCode){
 if(!db.isOpen())return;
 QSqlQuery q(db);
 q.prepare("INSERT INTO releves(timestamp_ms,date_utc,temp_cap1,temp_cap2,temp_cap3,temp_eva,batterie,porte_ouverte,ventilateur_1,ventilateur_2,ventilateur_3,ventilateur_4,ventilateur_5,ventilateur_degivrage,lampe,compresseur,relais_porte,temperature_moyenne,erreur,cloud_context,cloud_error,event_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
 q.addBindValue(timestamp);q.addBindValue(QDateTime::fromMSecsSinceEpoch(timestamp,Qt::UTC).toString(Qt::ISODateWithMs));
 bool meanValid=true;double sum=0;
 for(int i=0;i<15;++i){
  const bool fresh=seen[i]>0 && timestamp>=seen[i] && timestamp-seen[i]<6000 && values[i].isValid();
  q.addBindValue(fresh?values[i]:QVariant());
  if(i<3){meanValid &= fresh;sum+=values[i].toDouble();}
 }
 q.addBindValue(meanValid?QVariant(sum/3):QVariant());
 q.addBindValue(errors.isEmpty()?QVariant():QVariant(errors.join("\n")));
 q.addBindValue(QString::fromUtf8(QJsonDocument(cloudContext).toJson(QJsonDocument::Compact)));
 q.addBindValue(errorCode<0?QVariant():QVariant(errorCode));
 q.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
 if(!q.exec()){emit error(q.lastError().text());return;}
 errors.clear();
 readTemperatures();
 sendPending();
}
void HistoryWriter::readTemperatures(){
 if(!db.isOpen())return;
 const auto now=QDateTime::currentMSecsSinceEpoch();
 QSqlQuery q(db);
 q.prepare("SELECT timestamp_ms,temperature_moyenne FROM releves WHERE timestamp_ms>=? AND timestamp_ms<=? ORDER BY timestamp_ms,id");
 q.addBindValue(now-qint64(7)*86400000);q.addBindValue(now);
 if(!q.exec()){emit error(q.lastError().text());return;}
 QVector<QPointF> points;
 while(q.next()){
  bool valid=false;const double value=q.value(1).toDouble(&valid);
  points.append({q.value(0).toLongLong()/1000.0,
   !q.value(1).isNull()&&valid&&std::isfinite(value)?value:std::numeric_limits<double>::quiet_NaN()});
 }
 emit temperaturesLoaded(points);
}
void HistoryWriter::close(){
 if(retryTimer)retryTimer->stop();
 if(reply){reply->disconnect(this);reply->abort();reply->deleteLater();reply=nullptr;}
 cloudLock.reset();
 if(sampleTimer)sampleTimer->stop();
 if(!db.isValid())return;
 auto name=db.connectionName();db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(name);
}
CanHistory::CanHistory(QString path,QObject *parent):QObject(parent),writer(new HistoryWriter){
 qRegisterMetaType<QVector<QPointF>>("QVector<QPointF>");
 writer->moveToThread(&thread);
 connect(writer,&HistoryWriter::error,this,&CanHistory::error);
 connect(writer,&HistoryWriter::temperaturesLoaded,this,&CanHistory::temperaturesLoaded);
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
void CanHistory::loadTemperatures(){
 QMetaObject::invokeMethod(writer,[this]{writer->readTemperatures();},Qt::QueuedConnection);
}

bool HistoryWriter::migrateCloud(){
 if(!db.transaction()){emit error(db.lastError().text());return false;}
 QSqlQuery q(db);QSet<QString> columns;
 if(q.exec("PRAGMA table_info(releves)"))while(q.next())columns.insert(q.value(1).toString());
 const QList<QPair<QString,QString>> additions={
  {"sent","INTEGER NOT NULL DEFAULT 0 CHECK(sent IN (0,1))"},
  {"event_id","TEXT"},{"cloud_context","TEXT"},{"cloud_data","TEXT"},{"cloud_serial","TEXT"},
  {"cloud_error","INTEGER"},{"send_attempts","INTEGER NOT NULL DEFAULT 0"},
  {"next_retry_ms","INTEGER NOT NULL DEFAULT 0"},{"last_http_status","INTEGER"}};
 for(const auto &column:additions)if(!columns.contains(column.first)){
  if(!q.exec("ALTER TABLE releves ADD COLUMN "+column.first+" "+column.second)){
   emit error(q.lastError().text());db.rollback();return false;
  }
 }
 if(!q.exec("CREATE INDEX IF NOT EXISTS releves_unsent ON releves(sent,next_retry_ms,timestamp_ms)")||
    !q.exec("PRAGMA user_version=3")||!db.commit()){
  emit error(q.lastError().text());db.rollback();return false;
 }
 return true;
}
void HistoryWriter::setCloudContext(QJsonObject context){cloudContext=context;}
void HistoryWriter::configureCloud(const QString &path){
 if(network||!db.isOpen())return;
 QFile file(path);if(!file.exists())return;
 if(!file.open(QIODevice::ReadOnly)){emit error("Configuration cloud illisible : "+path);return;}
 const auto doc=QJsonDocument::fromJson(file.readAll());const auto config=doc.object();
 cloudSerial=config.value("serial").toString();cloudAccessToken=config.value("accessToken").toString();cloudApiToken=config.value("apiToken").toString();
 if(cloudSerial.trimmed().isEmpty()||cloudAccessToken.isEmpty()||cloudApiToken.trimmed().isEmpty()||cloudApiToken.contains('\r')||cloudApiToken.contains('\n')){
  emit error("Configuration cloud invalide : renseigner serial, accessToken et apiToken");return;
 }
 cloudLock=std::make_unique<QLockFile>(databasePath+".cloud.lock");
 cloudLock->setStaleLockTime(0); // This lock belongs to the entire reporting session.
 if(!cloudLock->tryLock(0)){cloudLock.reset();emit error("Reporting cloud deja actif pour cette base");return;}
 network=new QNetworkAccessManager(this);retryTimer=new QTimer(this);retryTimer->setSingleShot(true);
 connect(retryTimer,&QTimer::timeout,this,&HistoryWriter::sendPending);
 sendPending();
}
void HistoryWriter::sendPending(){
 if(!network||reply||!db.isOpen())return;
 const qint64 now=QDateTime::currentMSecsSinceEpoch();
 if(now<cloudBlockedUntil){retryTimer->start(int(qMin(qint64(300000),cloudBlockedUntil-now)));return;}
 QSqlQuery q(db);
 q.prepare("SELECT * FROM releves WHERE sent=0 AND next_retry_ms<=? AND (cloud_serial IS NULL OR cloud_serial=?) ORDER BY (cloud_error IS NOT NULL) DESC,timestamp_ms,id LIMIT 1");
 q.addBindValue(now);q.addBindValue(cloudSerial);
 if(!q.exec()){emit error(q.lastError().text());retryTimer->start(30000);return;}
 if(!q.next()){retryTimer->start(30000);return;}
 const qint64 id=q.value("id").toLongLong();const int attempts=q.value("send_attempts").toInt();
 QString eventId=q.value("event_id").toString();if(eventId.isEmpty())eventId=QUuid::createUuid().toString(QUuid::WithoutBraces);
 QJsonObject data;
 if(!q.value("cloud_data").isNull())data=QJsonDocument::fromJson(q.value("cloud_data").toString().toUtf8()).object();
 else {
  const QList<QPair<QString,QString>> fields={
   {"temperature","temperature_moyenne"},{"tempSen1","temp_cap1"},{"tempSen2","temp_cap2"},{"tempSen3","temp_cap3"},{"tempSen4","temp_eva"},
   {"battery","batterie"},{"fan1","ventilateur_1"},{"fan2","ventilateur_2"},{"fan3","ventilateur_3"},{"fan4","ventilateur_4"},{"fan5","ventilateur_5"},
   {"lamp","lampe"},{"compressor","compresseur"},{"defrostFan","ventilateur_degivrage"},{"doorState","porte_ouverte"},{"error","cloud_error"}};
  for(const auto &f:fields)data.insert(f.first,q.value(f.second).isNull()?QJsonValue(QJsonValue::Null):QJsonValue::fromVariant(q.value(f.second)));
  const auto context=QJsonDocument::fromJson(q.value("cloud_context").toString().toUtf8()).object();
  for(const auto &key:{"settingsTempMax","settingsTempMin","settingsEvapMin","maintenanceMode"})
   data.insert(key,context.contains(key)?context.value(key):QJsonValue(QJsonValue::Null));
  data.insert("datetime",QDateTime::fromMSecsSinceEpoch(q.value("timestamp_ms").toLongLong()).toUTC().toString(Qt::ISODateWithMs));
 }
 q.finish();
 // Freeze data and identity on disk BEFORE any network request. Retries reuse them.
 QSqlQuery update(db);update.prepare("UPDATE releves SET cloud_data=?,cloud_serial=?,event_id=?,send_attempts=send_attempts+1 WHERE id=? AND sent=0");
 update.addBindValue(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));update.addBindValue(cloudSerial);update.addBindValue(eventId);update.addBindValue(id);
 if(!update.exec()){emit error(update.lastError().text());retryTimer->start(30000);return;}
 QNetworkRequest request(cloudEndpoint);
 request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
 request.setRawHeader("Authorization","Bearer "+cloudApiToken.toUtf8());request.setRawHeader("Idempotency-Key",eventId.toUtf8());
 request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
 const QJsonObject body{{"serial",cloudSerial},{"accessToken",cloudAccessToken},{"data",data}};
 reply=network->post(request,QJsonDocument(body).toJson(QJsonDocument::Compact));
 QTimer::singleShot(15000,reply,[pending=reply]{pending->abort();});
 connect(reply,&QNetworkReply::finished,this,[this,id,attempts]{
  auto pending=reply;reply=nullptr;
  const int status=pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  const bool success=pending->error()==QNetworkReply::NoError&&status>=200&&status<300&&
   QJsonDocument::fromJson(pending->readAll()).object().value("success")==QJsonValue(true);
  int delay=30000*(1<<qMin(attempts,3));
  if(status==401||status==403)delay=300000;
  bool valid=false;int requested=pending->rawHeader("Retry-After").toInt(&valid);
  if(valid&&requested>0)delay=qMax(delay,qMin(requested,3600)*1000);
  QSqlQuery done(db);done.prepare("UPDATE releves SET sent=?,last_http_status=?,next_retry_ms=? WHERE id=?");
  done.addBindValue(success?1:0);done.addBindValue(status);
  done.addBindValue(success?qint64(0):QDateTime::currentMSecsSinceEpoch()+delay);done.addBindValue(id);
  const bool saved=done.exec();if(!saved)emit error(done.lastError().text());
  if(!success)emit error(QString("Reporting cloud non confirme (HTTP %1), nouvelle tentative programmee").arg(status));
  pending->deleteLater();
  if(success&&saved)retryTimer->start(500);
  else {cloudBlockedUntil=QDateTime::currentMSecsSinceEpoch()+delay;retryTimer->start(delay);}
 });
}
void CanHistory::configureCloud(const QString &path){
 QMetaObject::invokeMethod(writer,[this,path]{writer->configureCloud(path);},Qt::QueuedConnection);
}
void CanHistory::setCloudContext(QJsonObject context){
 QMetaObject::invokeMethod(writer,[this,context]{writer->setCloudContext(context);},Qt::QueuedConnection);
}
