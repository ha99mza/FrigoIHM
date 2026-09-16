#include "protocol.h"
#include <limits>
namespace Protocol {
const std::array<Setting,13> settings = {{
 {"Température minimale","°C",10,true}, {"Température maximale","°C",10,true},
 {"Température minimale évaporateur","°C",10,true}, {"Intervalle de dégivrage","h",3600,false},
 {"Durée de dégivrage","min",60,false}, {"Timeout de dégivrage","min",60,false},
 {"Délai anti court-cycle","s",1,false}, {"Timeout limite température","s",1,false},
 {"Alarme porte ouverte","min",60,false}, {"Offset CAP1","°C",10,true},
 {"Offset CAP2","°C",10,true}, {"Offset CAP3","°C",10,true}, {"Offset EVA","°C",10,true}
}};
QByteArray encode(qint64 value,int bytes) {
 QByteArray out(bytes,0); const auto bits=quint64(value);
 for(int i=0;i<bytes;++i) out[i]=char((bits>>(8*i))&255); return out;
}
std::optional<qint64> decode(const QByteArray &data,int bytes,bool signedValue) {
 if(data.size()!=bytes || bytes<1 || bytes>4) return {};
 quint64 n=0; for(int i=0;i<bytes;++i) n|=quint64(quint8(data[i]))<<(8*i);
 return signedValue && (n&(quint64(1)<<(bytes*8-1))) ? qint64(n)-qint64(quint64(1)<<(bytes*8)) : qint64(n);
}
quint16 signature(const Config &config) {
 qint64 sum=0; for(auto v:config) sum+=v;
 return quint16((sum%65535+65535)%65535);
}
bool valid(const Config &c) {
 for(int i=0;i<13;++i) if(c[i]<(settings[i].signedValue ? std::numeric_limits<qint32>::min():0LL) ||
 c[i]>(settings[i].signedValue ? qint64(std::numeric_limits<qint32>::max()):qint64(std::numeric_limits<quint32>::max()))) return false;
 return c[0]<=c[1];
}
QString errorText(quint8 c) {
 switch(c) {
 case 0x11:return "RTC : initialisation"; case 0x12:return "RTC : lecture"; case 0x13:return "RTC : écriture";
 case 0x21:return "Défaut sonde CAP1"; case 0x22:return "Défaut sonde CAP2"; case 0x23:return "Défaut sonde CAP3"; case 0x24:return "Défaut sonde EVA";
 case 0x31:return "Flash : initialisation"; case 0x32:return "Flash : lecture"; case 0x33:return "Flash : écriture";
 case 0x41:return "EEPROM : initialisation"; case 0x42:return "EEPROM : lecture"; case 0x43:return "EEPROM : écriture";
 case 0x51:return "Timeout limite température"; case 0x52:return "Porte ouverte trop longtemps"; case 0x53:return "Timeout de dégivrage";
 default:return QString("Code erreur inconnu 0x%1").arg(c,2,16,QChar('0'));
 }
}
}
