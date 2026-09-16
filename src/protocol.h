#pragma once
#include <QByteArray>
#include <QString>
#include <array>
#include <optional>
namespace Protocol {
struct Setting { const char *label; const char *unit; double scale; bool signedValue; };
extern const std::array<Setting,13> settings;
using Config = std::array<qint64,13>;
QByteArray encode(qint64 value, int bytes = 4);
std::optional<qint64> decode(const QByteArray &data, int bytes, bool signedValue);
quint16 signature(const Config &config);
QString errorText(quint8 code);
bool valid(const Config &config);
}
