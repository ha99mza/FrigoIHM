#include "network.h"
#include <QProcess>
#include <QTimer>
#include <QNetworkInterface>
void Network::run(QStringList args,QString password,bool scanning) {
 if(running)return;running=true;emit status("Opération réseau en cours…");
 auto p=new QProcess(this);auto env=QProcessEnvironment::systemEnvironment();env.insert("LC_ALL","C");p->setProcessEnvironment(env);
 connect(p,&QProcess::started,this,[p,password]{if(!password.isEmpty())p->write(password.toUtf8()+"\n");p->closeWriteChannel();});
 connect(p,&QProcess::errorOccurred,this,[this,p](QProcess::ProcessError e){if(e==QProcess::FailedToStart){running=false;emit status("NetworkManager / nmcli indisponible sur ce système");p->deleteLater();}});
 connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,p,scanning](int code,QProcess::ExitStatus){
  running=false;auto output=QString::fromUtf8(p->readAllStandardOutput());
  if(code==0 && scanning){QStringList names;for(auto line:output.split('\n')){if(line.endsWith('\r'))line.chop(1);if(!line.isEmpty()&&!names.contains(line))names<<line;}emit networks(names);}
  emit status(code==0?"Réseau actualisé":QString::fromUtf8(p->readAllStandardError()).trimmed());p->deleteLater();
 });
 QTimer::singleShot(45000,p,[p]{if(p->state()!=QProcess::NotRunning)p->kill();});p->start("nmcli",args);
}
void Network::scan(){run({"--terse","--escape","no","--fields","SSID","device","wifi","list","--rescan","yes"},{},true);}
void Network::connectWifi(const QString &ssid,const QString &password){
 if(password.contains('\n')||password.contains('\r')){emit status("Mot de passe invalide");return;}
 run({"--wait","30","--ask","device","wifi","connect",ssid},password);
}
void Network::radio(bool enabled){run({"radio","wifi",enabled?"on":"off"});}
QString Network::addresses(){
 QStringList lines;for(const auto &i:QNetworkInterface::allInterfaces()){
  if(i.flags().testFlag(QNetworkInterface::IsLoopBack))continue;
  QStringList ips;for(const auto &a:i.addressEntries())ips<<a.ip().toString();
  lines<<i.humanReadableName()+"   MAC "+i.hardwareAddress()+"\nIP  "+(ips.isEmpty()?"—":ips.join(" · "));
 }return lines.join("\n\n");
}
