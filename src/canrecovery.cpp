#include "canrecovery.h"
#include <QStandardPaths>

CanRecovery::CanRecovery(QString iface,int timeout,QObject *parent):QObject(parent),interfaceName(iface),silenceMs(timeout){
 ipProgram=QStandardPaths::findExecutable("ip");
 if(ipProgram.isEmpty())ipProgram="/usr/sbin/ip";
 watchdog.setInterval(1000);commandTimeout.setSingleShot(true);commandTimeout.setInterval(5000);
 connect(&watchdog,&QTimer::timeout,this,&CanRecovery::check);
 connect(&commandTimeout,&QTimer::timeout,this,[this]{process.kill();});
 connect(&process,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
  completed(status==QProcess::NormalExit&&code==0,QString::fromLocal8Bit(process.readAllStandardError()).trimmed());
 });
 connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
  if(error==QProcess::FailedToStart)completed(false,process.errorString());
 });
}
CanRecovery::~CanRecovery(){process.disconnect(this);if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(1000);}}
void CanRecovery::start(){silence.start();watchdog.start();}
void CanRecovery::received(){silence.restart();}
void CanRecovery::check(){
 if(step||!silence.isValid()||silence.elapsed()<silenceMs||(cooldown.isValid()&&cooldown.elapsed()<30000))return;
 step=1;cooldown.start();emit restarting();launch();
}
void CanRecovery::launch(){
 if(testMode)return;
 commandTimeout.start();
 process.start("sudo",{"-n",ipProgram,"link","set",interfaceName,step==1?"down":"up"});
}
void CanRecovery::completed(bool success,QString detail){
 if(!step)return;
 commandTimeout.stop();
 // Always attempt up, even if down failed or timed out.
 if(step==1){step=2;launch();return;}
 step=0;silence.restart();
 emit finished(success,success?"Interface CAN relancee : lecture de la signature":"Relance CAN impossible (sudo/ip) : "+detail);
}
