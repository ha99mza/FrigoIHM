#include "controller.h"
#include <QCanBus>
#include <QSettings>
#include <QProcess>
#include <QVariant>
#include <cmath>
#include <limits>
Controller::Controller(bool sim,QString iface,QObject *parent):QObject(parent),simulation(sim),interfaceName(iface) {
 QSettings s; cacheValid=s.value("config/valid",false).toBool();
 for(int i=0;i<13;++i) { cacheValid &= s.contains(QString("config/%1").arg(i)); config[i]=s.value(QString("config/%1").arg(i),0).toLongLong(); }
 cacheValid &= Protocol::valid(config);
 sender.setInterval(100); deadline.setSingleShot(true); deadline.setInterval(5000);
 connect(&sender,&QTimer::timeout,this,[this]{
  if(queue.isEmpty()) {sender.stop();return;}
  auto out=queue.dequeue();
  if(out.remote && out.id==0x30f) waitingSignature=true;
  emit transmitted(out.id,out.data,out.remote);
  if(simulation) {
   if(out.remote) QTimer::singleShot(10,this,[this,out]{
    if(out.id==0x30f) receive(out.id,Protocol::encode(Protocol::signature(config),2));
    else if(out.id>=0x300 && out.id<=0x30c) receive(out.id,Protocol::encode(config[out.id-0x300]));
    else if(out.id==0x30e) receive(out.id,Protocol::encode(maintenanceActive,1));
   });
   else if(out.id>=0x300 && out.id<=0x30c) config[out.id-0x300]=*Protocol::decode(out.data,4,Protocol::settings[out.id-0x300].signedValue);
   else if(out.id!=0x30f) receive(out.id,out.data);
  } else {
   QCanBusFrame f(out.id,out.data); if(out.remote) f.setFrameType(QCanBusFrame::RemoteRequestFrame);
   if(!device || !device->writeFrame(f)) {fail("Échec d’envoi CAN");return;}
  }
 });
 connect(&deadline,&QTimer::timeout,this,[this]{
  if(++attempts<=2 && !verifying) { if(fetching) requestAll(); else {enqueue(0x30f,QByteArray(2,0),true);deadline.start();} }
  else fail("Synchronisation non confirmée : vérifier la carte puis resynchroniser");
 });
 connect(&simulator,&QTimer::timeout,this,[this]{
  const double t=QDateTime::currentMSecsSinceEpoch()/10000.0;
  for(int i=0;i<4;++i) receive(0x100+i,Protocol::encode(qRound((i==3?-12:3.5+0.15*i+0.3*std::sin(t))*10),2));
  receive(0x104,Protocol::encode(12400,2));receive(0x105,QByteArray(1,2));
 });
}
void Controller::start() {
 if(simulation) {
  config={20,50,-150,21600,1200,1800,180,600,120,0,0,0,0};cacheValid=true;
  packs[0]=3;packs[1]=2;simulator.start(1000);synchronize();return;
 }
 QString error;device=QCanBus::instance()->createDevice("socketcan",interfaceName,&error);
 if(!device) {fail(error);return;}device->setParent(this);
 // Qt 5.15 SocketCAN adds a default BitRateKey of 500000. Remove that
 // configuration before opening: Linux owns the bitrate (250000 on can0),
 // and this unprivileged application must not attempt to change it.
 device->setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant());
 connect(device,&QCanBusDevice::framesReceived,this,[this]{
  while(device->framesAvailable()) {auto f=device->readFrame();
   if(f.frameType()==QCanBusFrame::DataFrame && !f.hasExtendedFrameFormat() && !f.hasLocalEcho()) receive(f.frameId(),f.payload());}
 });
 connect(device,&QCanBusDevice::errorOccurred,this,[this](QCanBusDevice::CanBusError e){if(e!=QCanBusDevice::NoError) fail(device->errorString());});
 if(!device->connectDevice()) {fail(device->errorString());return;}synchronize();
}
void Controller::enqueue(quint32 id,QByteArray data,bool remote) {queue.enqueue({id,data,remote});if(!sender.isActive()) sender.start();}
void Controller::synchronize() {
 if(busy)return;
 if(!simulation && (!device || device->state()!=QCanBusDevice::ConnectedState)) {fail("CAN déconnecté — relancer après configuration de can0");return;}
 synced=false;busy=true;fetching=false;verifying=false;waitingSignature=true;attempts=0;
 status="Lecture signature carte…"; enqueue(0x30f,QByteArray(2,0),true);enqueue(0x30e,QByteArray(1,0),true);deadline.start();emit changed();
}
void Controller::requestAll() {
 fetching=true;waitingSignature=false;received.clear();candidate={};queue.clear();
 status="Récupération des 13 paramètres…";
 for(int i=0;i<13;++i) enqueue(0x300+i,QByteArray(4,0),true);
 deadline.start();emit changed();
}
void Controller::persist() {
 if(simulation)return;
 QSettings s; s.setValue("config/valid",true);for(int i=0;i<13;++i)s.setValue(QString("config/%1").arg(i),config[i]);s.sync();
}
void Controller::fail(const QString &reason) {
 queue.clear();sender.stop();deadline.stop();synced=false;busy=false;fetching=false;verifying=false;waitingSignature=false;status=reason;emit changed();
}
void Controller::save(const Protocol::Config &values) {
 if(!synced || busy)return;
 if(!Protocol::valid(values)) {status="Valeurs invalides : vérifier les limites et min ≤ max";emit changed();return;}
 bool diff=false;for(int i=0;i<13;++i)diff|=values[i]!=config[i];
 if(!diff){status="Aucune modification";emit changed();return;}
 candidate=values;busy=true;synced=false;verifying=true;waitingSignature=false;attempts=0;
 for(int i=0;i<13;++i)if(values[i]!=config[i])enqueue(0x300+i,Protocol::encode(values[i]));
 enqueue(0x30f,Protocol::encode(Protocol::signature(values),2));enqueue(0x30f,QByteArray(2,0),true);
 status="Envoi puis vérification de la signature…";deadline.start();emit changed();
}
void Controller::receive(quint32 id,const QByteArray &p) {
 if(id>=0x100 && id<=0x104) {
  const int i=int(id-0x100);
  // CAP1..CAP3 and EVA: original firmware uses int16 LE, observed firmware
  // sends int32 LE. Both are scaled by 10. Battery remains int16 LE / 1000.
  const int bytes=int(p.size());
  if(i==4 ? bytes!=2 : (bytes!=2 && bytes!=4)) return;
  auto value=Protocol::decode(p,bytes,true);
  if(!value) return;
  temperatures[i]=double(*value)/(i==4?1000.0:10.0);
  seen[i]=QDateTime::currentMSecsSinceEpoch();
 }
 else if(id==0x105 && p.size()==1) {door=quint8(p[0])==3; if(quint8(p[0])==2 && alarm==Protocol::errorText(0x52)){sound(false);alarm.clear();}}
 else if((id==0x106 || id==0x107) && p.size()==1) packs[id-0x106]=quint8(p[0]);
 else if(id==0x30e && p.size()==1) maintenanceActive=quint8(p[0])==1;
 else if(id==1 && p.size()==1) {
  auto code=quint8(p[0]); alarm=Protocol::errorText(code);alarmLog.prepend(QDateTime::currentDateTime().toString("dd/MM HH:mm:ss")+"   "+alarm);
  while(alarmLog.size()>500)alarmLog.removeLast();if(code==0x52)sound(true);
 }
 else if(id>=0x300 && id<=0x30c) {
  int i=id-0x300;auto v=Protocol::decode(p,4,Protocol::settings[i].signedValue);if(!v)return;
  if(fetching){candidate[i]=*v;received.insert(i);if(received.size()==13){
   if(!Protocol::valid(candidate) || Protocol::signature(candidate)!=expectedSignature){fail("Signature incohérente après lecture des paramètres");return;}
   config=candidate;cacheValid=true;fetching=false;busy=false;synced=true;deadline.stop();persist();status="Paramètres synchronisés";emit configChanged();
  }} else if(!busy && *v!=config[i]){
   // Late replies and requests from another CAN client can repeat a setting.
   // Only an actual change invalidates the configuration already verified.
   synced=false;
   status=QString("Paramètre 0x%1 modifié sur la carte : relire les réglages").arg(id,3,16);
  }
 }
 else if(id==0x30f && p.size()==2 && waitingSignature) {
  auto sig=quint16(*Protocol::decode(p,2,false));
  if(verifying) {
   if(sig!=Protocol::signature(candidate)){fail("La carte n’a pas confirmé les nouveaux paramètres");return;}
   config=candidate;verifying=false;waitingSignature=false;busy=false;synced=true;cacheValid=true;deadline.stop();persist();status="Réglages confirmés par la carte";emit configChanged();
  } else if(cacheValid && sig==Protocol::signature(config)) {synced=true;busy=false;waitingSignature=false;deadline.stop();status="Paramètres synchronisés";emit configChanged();}
  else {expectedSignature=sig;requestAll();}
 }
 emit changed();
}
bool Controller::fresh(int i) const {return seen[i]>0 && QDateTime::currentMSecsSinceEpoch()-seen[i]<6000;}
double Controller::mean() const {if(!fresh(0)||!fresh(1)||!fresh(2))return std::numeric_limits<double>::quiet_NaN();return (temperatures[0]+temperatures[1]+temperatures[2])/3;}
void Controller::maintenance(bool active) {if(synced&&!busy)enqueue(0x30e,Protocol::encode(active,1));}
void Controller::relay(int pack,int bit,bool active) {
 if(!maintenanceActive||!synced||busy||pack<0||pack>1||bit<0||bit> (pack==0?4:3)||packs[pack]<0)return;
 auto value=active ? packs[pack]|(1<<bit):packs[pack]&~(1<<bit);enqueue(0x106+pack,Protocol::encode(value,1));
}
void Controller::sound(bool on) {if(simulation)return;const auto path=on?alarmStart:alarmStop;if(!path.isEmpty())QProcess::startDetached(path,QStringList{});}
void Controller::acknowledge(){sound(false);alarm.clear();status="Acquittement local (aucune commande CAN définie)";emit changed();}
