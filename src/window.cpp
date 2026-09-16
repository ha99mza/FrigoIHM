#include "window.h"
#include <QApplication>
#include <QBoxLayout>
#include <QCryptographicHash>
#include <QDialog>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QNetworkInterface>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QUuid>
#include <QWheelEvent>
#include <cmath>
namespace {
QString hash(const QString &pin, const QString &salt) {
    return QCryptographicHash::hash((salt + pin).toUtf8(), QCryptographicHash::Sha256).toHex();
}
QString pinMask(const QString &s) {
    return QString(s.size(), QChar(0x2022)) + QString(qMax(0, 4 - s.size()), '_');
}
QFont plexFont(int pixels, int weight, bool mono) {
    QFont f(mono ? "IBM Plex Mono" : "IBM Plex Sans");
    f.setPixelSize(pixels);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    f.setWeight(QFont::Weight(weight));
#else
    f.setWeight(weight >= 700   ? QFont::Bold
                : weight >= 600 ? QFont::DemiBold
                : weight >= 500 ? QFont::Medium
                                : QFont::Normal);
#endif
    return f;
}
const QStringList keys = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "←", "0", "OK"};
struct PainterScope {
    QPainter *p;
    explicit PainterScope(QPainter *value) : p(value) {
        p->save();
    }
    ~PainterScope() {
        p->restore();
    }
};
} // namespace
Window::Window(Controller *controller) : c(controller) {
    for (auto file : {"IBMPlexSans-Regular.ttf", "IBMPlexMono-Regular.ttf", "IBMPlexMono-Medium.ttf",
                      "IBMPlexMono-SemiBold.ttf"})
        QFontDatabase::addApplicationFont(":/fonts/" + QString(file));
    setWindowTitle(c->simulation ? "Frigo · Simulation" : "Frigo · IHM CAN");
    resize(1024, 600);
    setMinimumSize(1024, 600);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    draft = c->config;
    connect(c, &Controller::changed, this, [this] {
        if (!c->busy && !c->synced)
            pendingSave = -1;
        setToolTip(c->status);
        update();
    });
    connect(c, &Controller::configChanged, this, [this] {
        if (pendingSave >= 0) {
            auto timestamp = QTime::currentTime().toString("HH:mm:ss");
            if (pendingSave == 1)
                calSavedAt = timestamp;
            else
                savedAt = timestamp;
            for (int i = pendingSave == 1 ? 9 : 0; i < (pendingSave == 1 ? 13 : 9); ++i)
                draft[i] = c->config[i];
            pendingSave = -1;
        } else
            draft = c->config;
        update();
    });
    connect(&network, &Network::networks, this, [this](QStringList names) {
        networks = names;
        overlay = "networks";
        scroll = 0;
        update();
    });
    connect(&network, &Network::status, this, [this](QString message) {
        networkMessage = message;
        update();
    });
    auto timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        if (std::isfinite(c->mean()))
            samples.append({double(QDateTime::currentSecsSinceEpoch()), c->mean()});
        while (!samples.isEmpty() && samples.first().x() < QDateTime::currentSecsSinceEpoch() - 7 * 86400)
            samples.removeFirst();
        update();
    });
    timer->start(1000);
    auto pulse = new QTimer(this);
    connect(pulse, &QTimer::timeout, this, [this] {
        if (!c->alarm.isEmpty())
            update();
    });
    pulse->start(80);
}
void Window::text(QRectF r, const QString &value, int size, QColor color, int weight, bool mono, int align,
                  qreal spacing) {
    auto f = plexFont(size, weight, mono);
    if (spacing)
        f.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    p->setFont(f);
    p->setPen(color);
    p->drawText(r, align, value);
}
void Window::box(QRectF r, QColor fill, QColor border, qreal radius) {
    p->setBrush(fill);
    p->setPen(border.isValid() ? QPen(border, 1) : QPen(Qt::NoPen));
    p->drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), radius, radius);
}
void Window::rule(qreal x1, qreal y1, qreal x2, qreal y2) {
    p->setPen(QPen(line, 1));
    p->drawLine(QPointF(x1, y1), QPointF(x2, y2));
}
void Window::action(QRectF r, std::function<void()> callback) {
    auto mapped = p->worldTransform().mapRect(r);
    mapped.translate(-origin);
    mapped = QRectF(mapped.topLeft() / scaleFactor, mapped.size() / scaleFactor);
    if (p->hasClipping()) {
        auto clip = p->worldTransform().mapRect(p->clipBoundingRect());
        clip.translate(-origin);
        clip = QRectF(clip.topLeft() / scaleFactor, clip.size() / scaleFactor);
        mapped = mapped.intersected(clip);
    }
    if (!mapped.isEmpty())
        hits.append({mapped, std::move(callback)});
}
void Window::button(QRectF r, const QString &value, std::function<void()> callback, QColor fill, QColor color,
                    QColor border, int size, bool enabled) {
    box(r, fill, border, 5);
    text(r, value, size, enabled ? color : muted, 600, false, Qt::AlignCenter);
    if (enabled)
        action(r, std::move(callback));
}
void Window::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    p = &painter;
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    bg = dark ? QColor("#12161a") : QColor("#eef1f4");
    panel = dark ? QColor("#1b2127") : QColor("#ffffff");
    line = dark ? QColor("#2b333b") : QColor("#d3dae0");
    tx = dark ? QColor("#e8edf2") : QColor("#161c22");
    muted = dark ? QColor("#93a1ad") : QColor("#5b6873");
    p->fillRect(rect(), QColor("#0a0d10"));
    scaleFactor = qMin(width() / 1024.0, height() / 600.0);
    origin = {(width() - 1024 * scaleFactor) / 2, (height() - 600 * scaleFactor) / 2};
    p->translate(origin);
    p->scale(scaleFactor, scaleFactor);
    p->fillRect(QRectF(0, 0, 1024, 600), bg);
    hits.clear();
    header();
    int top = 56;
    bool alarm = !c->alarm.isEmpty();
    if (alarm && screen != "alarms") {
        p->fillRect(QRectF(0, 56, 1024, 44), QColor("#b91c1c"));
        p->fillRect(QRectF(20, 73, 10, 10), Qt::white);
        text({44, 56, 780, 44}, c->alarm, 16, Qt::white, 600);
        text({830, 56, 174, 44}, "Voir les alarmes ›", 14, Qt::white);
        action({0, 56, 1024, 44}, [this] { go("alarms"); });
        top += 44;
    }
    int h = 516 - top;
    maxScroll = 0;
    if (screen == "temp")
        temperaturePage(top, h);
    else if (screen == "hist")
        historyPage(top, h);
    else if (screen == "alarms")
        alarmPage(top, h);
    else if (screen == "set")
        settingsPage(top, h);
    else if (screen == "maint")
        maintenancePage(top, h);
    navigation();
    if (alarm) {
        double pulse = (1 + std::cos(QDateTime::currentMSecsSinceEpoch() % 1100 * 6.283185 / 1100)) / 2;
        QColor glow = red;
        glow.setAlphaF(.15 + .45 * pulse);
        const double depth = 46 + 74 * pulse;
        auto edge = [&](QRectF rect, QPointF from, QPointF to) {
            QLinearGradient gradient(from, to);
            gradient.setColorAt(0, glow);
            QColor transparent = red;
            transparent.setAlpha(0);
            gradient.setColorAt(1, transparent);
            p->fillRect(rect, gradient);
        };
        edge({0, 0, 1024, depth}, {0, 0}, {0, depth});
        edge({0, 600 - depth, 1024, depth}, {0, 600}, {0, 600 - depth});
        edge({0, 0, depth, 600}, {0, 0}, {depth, 0});
        edge({1024 - depth, 0, depth, 600}, {1024, 0}, {1024 - depth, 0});
        p->setBrush(Qt::NoBrush);
        glow.setAlphaF(.5 + .5 * pulse);
        p->setPen(QPen(glow, 8));
        p->drawRect(QRectF(4, 4, 1016, 592));
    }
    if (!overlay.isEmpty())
        pinOverlay();
    p = nullptr;
}
void Window::header() {
    p->fillRect(QRectF(0, 0, 1024, 56), panel);
    rule(0, 55.5, 1024, 55.5);
    p->setBrush(c->alarm.isEmpty() ? green : red);
    p->setPen(Qt::NoPen);
    p->drawEllipse(QRectF(20, 22, 12, 12));
    text({42, 0, 226, 56}, "CHAMBRE FROIDE A2", 15, tx, 600, false, Qt::AlignVCenter | Qt::AlignLeft, 1.2);
    rule(278, 16, 278, 40);
    QString run = c->simulation ? "Simulation · compresseur "
                  : c->synced   ? "Régulation · compresseur "
                                : "CAN · non synchronisé";
    if (c->simulation || c->synced)
        run += c->packs[1] < 0 ? "—" : (c->packs[1] & 2) ? "ON" : "OFF";
    if (c->maintenanceActive)
        run = "Mode maintenance";
    text({296, 0, 407, 56}, run, 15, muted, 400, true);
    action({296, 0, 407, 56}, [this] {
        overlay = "status";
        update();
    });
    auto now = QDateTime::currentDateTime();
    text({711, 0, 89, 56}, now.toString("HH:mm:ss"), 16, tx, 500, true);
    text({815, 0, 107, 56}, now.toString("dd/MM/yyyy"), 14, muted, 400, true);
    button(
        {940, 10, 64, 36}, dark ? "JOUR" : "NUIT",
        [this] {
            dark = !dark;
            update();
        },
        bg, tx, line, 13);
}
void Window::navigation() {
    p->fillRect(QRectF(0, 516, 1024, 84), panel);
    rule(0, 516.5, 1024, 516.5);
    QStringList names = {"Température", "Historique", "Alarmes", "Réglages"},
                ids = {"temp", "hist", "alarms", "set"};
    for (int i = 0; i < 4; ++i) {
        bool selected = screen == ids[i];
        QColor color = selected ? accent : muted;
        if (selected) {
            p->fillRect(QRectF(i * 256, 517, 256, 83), dark ? QColor("#0d1f2b") : QColor("#e2f3fd"));
            p->fillRect(QRectF(i * 256, 517, 256, 3), accent);
        }
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(color, 2.5));
        if (i == 0 || i == 3)
            p->drawEllipse(QRectF(i * 256 + 115, 532, 26, 26));
        else
            p->drawRoundedRect(QRectF(i * 256 + 115, 532, 26, 26), 3, 3);
        text({qreal(i * 256), 566, 256, 23}, names[i], 16, color, 600, false, Qt::AlignCenter, .48);
        action({qreal(i * 256), 516, 256, 84}, [this, id = ids[i]] { go(id); });
        if (i == 2 && !c->alarm.isEmpty()) {
            box({i * 256 + 177., 528, 26, 24}, red, {}, 12);
            text({i * 256 + 177., 528, 26, 24}, "1", 14, Qt::white, 700, false, Qt::AlignCenter);
        }
    }
}
void Window::temperaturePage(int top, int height) {
    QRectF card(20, top + 16, 984, height - 32);
    box(card, panel, line);
    text({44, qreal(top + 34), 277, 22}, "TEMPÉRATURE CHAMBRE", 14, muted, 600, false,
         Qt::AlignLeft | Qt::AlignVCenter, 1.96);
    text({335, qreal(top + 34), 610, 22}, "moyenne 3 sondes · mise à jour 2 s", 13, muted, 400, true);
    double value = c->mean();
    QString str = std::isfinite(value) ? QString::number(value, 'f', 1) : "—";
    bool out =
        c->synced && std::isfinite(value) && (value < c->config[0] / 10. || value > c->config[1] / 10.);
    QFont f = plexFont(150, 600, true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, -4.5);
    p->setFont(f);
    QFontMetricsF fm(f);
    double center = (top + 62 + card.bottom() - 104) / 2,
           baseline = center + (fm.ascent() - fm.descent()) / 2;
    p->setPen(out ? red : tx);
    p->drawText(QPointF(44, baseline), str);
    double numberWidth = fm.horizontalAdvance(str);
    p->setFont(plexFont(52, 400, true));
    p->setPen(muted);
    p->drawText(QPointF(44 + numberWidth + 10, baseline), "°C");
    double y = card.bottom() - 89;
    rule(44, y, 980, y);
    rule(349, y + 14, 349, card.bottom() - 18);
    rule(664, y + 14, 664, card.bottom() - 18);
    QStringList titles = {"PLAGE AUTORISÉE", "ÉVAPORATEUR", "DERNIER DÉGIVRAGE"},
                vals = {c->synced ? QString("%1 … %2 °C")
                                        .arg(c->config[0] / 10., 0, 'f', 1)
                                        .arg(c->config[1] / 10., 0, 'f', 1)
                                  : "—",
                        c->fresh(3) ? QString::number(c->temperatures[3], 'f', 1) + " °C" : "— °C", "—"};
    for (int i = 0; i < 3; ++i) {
        double x = 44 + i * 315;
        text({x, y + 14, 296, 18}, titles[i], 12, muted, 400, false, Qt::AlignLeft | Qt::AlignVCenter, 1.2);
        text({x, y + 38, 296, 29}, vals[i], 22, tx, 500, true);
    }
}
void Window::historyPage(int top, int height) {
    text({20, qreal(top + 16), 580, 52}, "HISTORIQUE TEMPÉRATURE", 14, muted, 600, false,
         Qt::AlignVCenter | Qt::AlignLeft, 1.96);
    QStringList ranges = {"1h", "24h", "7j"};
    for (int i = 0; i < 3; ++i) {
        bool on = range == ranges[i];
        button(
            {qreal(712 + i * 100), qreal(top + 16), 92, 52}, ranges[i],
            [this, r = ranges[i]] {
                range = r;
                update();
            },
            on ? (dark ? QColor("#0d1f2b") : QColor("#e2f3fd")) : panel, on ? accent : muted,
            on ? accent : line, 17);
    }
    QRectF card(20, top + 82, 984, height - 204);
    box(card, panel, line);
    QRectF plot = card.adjusted(18, 14, -18, -36);
    const double end = QDateTime::currentSecsSinceEpoch(), start = end - (range == "1h"    ? 3600
                                                                          : range == "24h" ? 86400
                                                                                           : 604800);
    QVector<QPointF> plotted = samples;
    if (c->simulation) {
        // The reference's demo trace is shown only in the explicitly labelled simulator.
        plotted.clear();
        double amplitude = range == "1h" ? .7 : range == "24h" ? 1.1 : 1.4;
        int cycles = range == "1h" ? 1 : range == "24h" ? 3 : 14;
        for (int i = 0; i < 90; ++i)
            plotted.append(
                {start + i * (end - start) / 89,
                 4 - amplitude + ((i * cycles) % 18) / 18.0 * amplitude * 2 + std::sin(i / 6.0) * .25});
    }
    double low = -2, high = 8;
    for (auto s : plotted)
        if (s.x() >= start) {
            low = qMin(low, std::floor(s.y() - 1));
            high = qMax(high, std::ceil(s.y() + 1));
        }
    auto point = [&](QPointF v) {
        return QPointF(plot.left() + (v.x() - start) / (end - start) * plot.width(),
                       plot.bottom() - (v.y() - low) / (high - low) * plot.height());
    };
    p->save();
    p->setClipRect(plot);
    if (c->synced) {
        auto a = point({start, c->config[1] / 10.}), b = point({end, c->config[0] / 10.});
        QColor color = accent;
        color.setAlpha(23);
        p->fillRect(QRectF(a, b).normalized(), color);
    }
    for (int i = 0; i <= 5; ++i)
        rule(plot.left(), plot.top() + plot.height() * i / 5, plot.right(),
             plot.top() + plot.height() * i / 5);
    QPainterPath path;
    bool first = true;
    double previous = 0, sum = 0, min = 1e9, max = -1e9;
    int count = 0;
    QVector<QPointF> segment;
    auto fillSegment = [&] {
        if (segment.size() < 2)
            return;
        QPainterPath area;
        area.moveTo(segment.first().x(), plot.bottom());
        for (auto pt : segment)
            area.lineTo(pt);
        area.lineTo(segment.last().x(), plot.bottom());
        area.closeSubpath();
        QColor color = accent;
        color.setAlpha(36);
        p->fillPath(area, color);
    };
    for (auto s : plotted) {
        if (s.x() < start)
            continue;
        auto pt = point(s);
        if (first || (!c->simulation && s.x() - previous > 6)) {
            fillSegment();
            segment.clear();
            path.moveTo(pt);
        } else
            path.lineTo(pt);
        segment.append(pt);
        first = false;
        previous = s.x();
        sum += s.y();
        min = qMin(min, s.y());
        max = qMax(max, s.y());
        ++count;
    }
    fillSegment();
    p->setBrush(Qt::NoBrush);
    p->setPen(QPen(accent, 2.5));
    p->drawPath(path);
    p->restore();
    for (int i = 0; i <= 5; ++i)
        text({plot.left(), plot.top() + plot.height() * i / 5 - 8, 40, 16},
             QString::number(high - (high - low) * i / 5, 'g', 2), 12, muted, 400, true);
    if (first)
        text(plot, "En attente de mesures", 14, muted, 400, false, Qt::AlignCenter);
    QStringList labels = range == "1h"    ? QStringList{"-60m", "-48m", "-36m", "-24m", "-12m", "now"}
                         : range == "24h" ? QStringList{"-24h", "-19h", "-14h", "-10h", "-5h", "now"}
                                          : QStringList{"J-7", "J-6", "J-4", "J-3", "J-2", "auj."};
    for (int i = 0; i < 6; ++i)
        text({plot.left() + i * (plot.width() - 44) / 5, card.bottom() - 30, 44, 24}, labels[i], 12, muted,
             400, true);
    QStringList titles = {"MINIMUM", "MAXIMUM", "MOYENNE", "DÉGIVRAGES"},
                values = {count ? QString::number(min, 'f', 1) + " °C" : "—",
                          count ? QString::number(max, 'f', 1) + " °C" : "—",
                          count ? QString::number(sum / count, 'f', 1) + " °C" : "—", "—"};
    for (int i = 0; i < 4; ++i) {
        double x = 20 + 249 * i;
        box({x, qreal(top + height - 108), 237, 92}, panel, line);
        text({x + 16, qreal(top + height - 94), 205, 19}, titles[i], 12, muted, 400, false,
             Qt::AlignVCenter | Qt::AlignLeft, 1.44);
        text({x + 16, qreal(top + height - 69), 205, 36}, values[i], 28, tx, 600, true);
    }
}
void Window::scrollbar(int top, int height, int contentHeight) {
    maxScroll = qMax(0, contentHeight - height);
    scroll = qBound(0, scroll, maxScroll);
    if (maxScroll > 0)
        box({1014., qreal(top + scroll * height / double(contentHeight)), 8.,
             qreal(qMax(24, height * height / contentHeight))},
            QColor("#4a555f"), {}, 4);
}
void Window::alarmPage(int top, int height) {
    text({20, qreal(top + 16), 620, 52}, "JOURNAL DES ALARMES", 14, muted, 600, false,
         Qt::AlignVCenter | Qt::AlignLeft, 1.96);
    button(
        {792, qreal(top + 16), 212, 52}, "Acquitter tout", [this] { c->acknowledge(); }, QColor("#b91c1c"),
        Qt::white, {}, 16);
    p->save();
    p->setClipRect(20, top + 82, 984, height - 98);
    scrollbar(top + 82, height - 98, c->alarmLog.size() * 96);
    if (c->alarmLog.isEmpty()) {
        box({20, qreal(top + 82), 984, 100}, panel, line);
        text({38, qreal(top + 82), 948, 100}, "Aucune alarme enregistrée", 19, muted, 500);
    }
    for (int i = 0; i < c->alarmLog.size(); ++i) {
        double y = top + 82 + i * 96 - scroll;
        auto log = c->alarmLog[i];
        box({20, y, 984, 86}, panel, line);
        p->fillRect(QRectF(20, y + 1, 4, 84), red);
        text({38, y + 16, 96, 54}, log.mid(6, 8), 15, muted, 400, true);
        text({150, y + 12, 630, 32}, log.mid(17).trimmed(), 19, tx, 600);
        text({150, y + 45, 630, 24}, "Événement reçu du contrôleur", 14, muted);
        box({838, y + 27, 146, 32}, Qt::transparent, line, 3);
        text({838, y + 27, 146, 32}, i == 0 && !c->alarm.isEmpty() ? "ACTIVE" : "JOURNALISÉE", 13,
             i == 0 && !c->alarm.isEmpty() ? red : muted, 600, false, Qt::AlignCenter);
    }
    p->restore();
}
bool Window::dirty(bool calibration) const {
    for (int i = calibration ? 9 : 0; i < (calibration ? 13 : 9); ++i)
        if (draft[i] != c->config[i])
            return true;
    return false;
}
QString Window::number(int i) const {
    double scale = Protocol::settings[i].scale;
    QString unit = QString::fromUtf8(Protocol::settings[i].unit);
    if (i == 6 || i == 7) {
        scale = 60;
        unit = "min";
    }
    if (i == 8) {
        scale = 1;
        unit = "s";
    }
    double value = draft[i] / scale;
    return QString::number(value, 'f',
                           Protocol::settings[i].signedValue            ? 1
                           : std::abs(value - std::round(value)) > .001 ? 1
                                                                        : 0) +
           " " + unit;
}
void Window::saveRow(int y, bool calibration) {
    bool modified = dirty(calibration), enabled = c->synced && !c->busy && modified;
    int x = calibration ? 764 : 724, width = calibration ? 240 : 280, height = calibration ? 66 : 72;
    QString hint = modified ? (calibration ? "Calibration non enregistrée" : "Modifications non enregistrées")
                            : "Dernier enregistrement : " + (calibration ? calSavedAt : savedAt);
    if (c->busy)
        hint = "Envoi et vérification…";
    else if (!c->synced)
        hint = "Carte non synchronisée";
    text({228, qreal(y), qreal(x - 244), qreal(height)}, hint, 14, modified ? amber : muted, 600);
    button(
        {qreal(x), qreal(y), qreal(width), qreal(height)}, "ENREGISTRER",
        [this, calibration] { save(calibration); }, enabled ? accent : panel,
        enabled ? QColor("#08222f") : muted, enabled ? accent : line, 19, enabled);
}
void Window::settingsPage(int top, int height) {
    p->fillRect(QRectF(0, top, 208, height), panel);
    rule(207.5, top, 207.5, top + height);
    QStringList ids = {"reg", "deg", "alm", "net", "mnt"},
                labels = {"Régulation", "Dégivrage", "Alarmes", "Réseau", "Maintenance"};
    for (int i = 0; i < 5; ++i) {
        bool on = settingTab == ids[i];
        int stride = qMin(72, (height - 92) / 5);
        QRectF r(12, top + 16 + i * stride, 184, stride - 8);
        if (on) {
            box(r, dark ? QColor("#0d1f2b") : QColor("#e2f3fd"), {}, 5);
            p->fillRect(QRectF(r.left(), r.top() + 1, 3, r.height() - 2), accent);
        }
        text(r.adjusted(16, 0, 0, 0), labels[i], 16, on ? accent : muted, 600);
        action(r, [this, id = ids[i]] {
            settingTab = id;
            scroll = 0;
            update();
        });
    }
    button(
        {12, qreal(top + height - 72), 184, 56}, "Verrouiller",
        [this] {
            unlocked = false;
            go("temp");
        },
        panel, muted, line, 14);
    if (settingTab == "net") {
        networkPage(top, height);
        return;
    }
    if (settingTab == "mnt") {
        box({228, qreal(top + 16), 776, 111}, panel, amber);
        p->fillRect(QRectF(228, top + 17, 4, 109), amber);
        text({246, qreal(top + 30), 740, 24}, "Mode maintenance", 18, amber, 700);
        text({246, qreal(top + 63), 734, 48},
             "La régulation automatique est suspendue. Les sorties sont pilotées manuellement.\nRéservé au "
             "personnel technique.",
             14, muted);
        button(
            {228, qreal(top + 143), 776, 92},
            c->maintenanceActive ? "OUVRIR LE MODE MAINTENANCE" : "ACTIVER LE MODE MAINTENANCE",
            [this] {
                if (!c->maintenanceActive)
                    c->maintenance(true);
                screen = "maint";
                maintenanceTab = "cal";
                scroll = 0;
                update();
            },
            amber, QColor("#1a1204"), {}, 22, c->synced && !c->busy);
        for (int i = 0; i < 2; ++i) {
            box({qreal(228 + i * 394), qreal(top + 251), 382, 86}, panel, line);
            text({qreal(244 + i * 394), qreal(top + 265), 350, 18},
                 i == 0 ? "DERNIÈRE INTERVENTION" : "VERSION FIRMWARE", 12, muted, 400, false,
                 Qt::AlignVCenter | Qt::AlignLeft, 1.2);
            text({qreal(244 + i * 394), qreal(top + 288), 350, 29}, "—", 20, tx, 600, true);
        }
        return;
    }
    QVector<int> indices = settingTab == "reg"   ? QVector<int>{0, 1, 2, 6}
                           : settingTab == "deg" ? QVector<int>{3, 4, 5}
                                                 : QVector<int>{7, 8};
    const QStringList hints = {"Seuil bas de régulation",
                               "Seuil haut de régulation",
                               "Protection givrage évaporateur",
                               "Temps entre deux cycles de dégivrage",
                               "Durée nominale du cycle",
                               "Arrêt forcé si la consigne de fin n’est pas atteinte",
                               "Délai minimal entre deux démarrages compresseur",
                               "Délai avant alarme si hors plage",
                               "Délai avant déclenchement"},
                      names = {"Température min",
                               "Température max",
                               "Température évaporateur min",
                               "Intervalle dégivrage",
                               "Durée dégivrage",
                               "Timeout dégivrage",
                               "Anti short cycle delay",
                               "Timeout limite température",
                               "Alarme porte ouverte"};
    int content = indices.size() * 100 + 92;
    p->save();
    p->setClipRect(208, top, 816, height);
    scrollbar(top, height, content + 16);
    p->translate(0, -scroll);
    int y = top + 16;
    for (int i : indices) {
        box({228, qreal(y), 776, 88}, panel, line);
        text({244, qreal(y + 12), 374, 30}, names[i], 18, tx, 600);
        text({244, qreal(y + 45), 374, 30}, hints[i], 13, muted, 400, false,
             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
        text({638, qreal(y + 12), 190, 64}, number(i), 30, accent, 600, true,
             Qt::AlignRight | Qt::AlignVCenter);
        action({638, qreal(y + 12), 190, 64}, [this, i] { edit(i); });
        qint64 step = i < 3 ? 5 : (i == 3 ? 3600 : i == 5 || i == 7 ? 300 : i == 8 ? 10 : 60);
        button({844, qreal(y + 12), 68, 64}, "−", [this, i, step] { change(i, -step); }, bg, tx, line, 32);
        button({920, qreal(y + 12), 68, 64}, "+", [this, i, step] { change(i, step); }, bg, tx, line, 28);
        y += 100;
    }
    saveRow(y + 4);
    p->restore();
}
void Window::maintenancePage(int top, int height) {
    p->fillRect(QRectF(0, top, 208, height), panel);
    rule(207.5, top, 207.5, top + height);
    text({16, qreal(top + 14), 180, 20}, "MAINTENANCE", 12, amber, 700, false,
         Qt::AlignLeft | Qt::AlignVCenter, 1.2);
    QStringList ids = {"cal", "fan", "act", "pwd"},
                labels = {"1 · Capteurs", "2 · Ventilateurs", "3 · Actionneurs", "4 · Code d’accès"};
    for (int i = 0; i < 4; ++i) {
        bool on = maintenanceTab == ids[i];
        QRectF r(12, top + 40 + i * 68, 184, 60);
        if (on) {
            box(r, dark ? QColor("#0d1f2b") : QColor("#e2f3fd"), {}, 5);
            p->fillRect(QRectF(r.left(), r.top() + 1, 3, 58), accent);
        }
        text(r.adjusted(14, 0, 0, 0), labels[i], 15, on ? accent : muted, 600);
        action(r, [this, id = ids[i]] {
            maintenanceTab = id;
            scroll = 0;
            update();
        });
    }
    button(
        {12, qreal(top + height - 74), 184, 60}, "Quitter",
        [this] {
            c->maintenance(false);
            screen = "set";
            settingTab = "mnt";
            scroll = 0;
            update();
        },
        amber, QColor("#1a1204"), {}, 15);
    PainterScope contentScope(p);
    p->setClipRect(208, top, 816, height);
    scrollbar(top, height, 442);
    p->translate(0, -scroll);
    if (maintenanceTab == "pwd") {
        passwordPage(top);
        return;
    }
    if (maintenanceTab == "cal") {
        for (int i = 0; i < 4; ++i) {
            double x = 228 + (i % 2) * 394, y = top + 16 + (i / 2) * 172;
            box({x, y, 382, 158}, panel, line);
            text({x + 16, y + 14, 233, 24},
                 i == 3 ? "Capteur évaporateur" : "Capteur " + QString::number(i + 1), 17, tx, 600);
            text({x + 248, y + 14, 118, 24},
                 c->fresh(i) ? QString::number(c->temperatures[i], 'f', 1) + " °C" : "— °C", 15, muted, 400,
                 true, Qt::AlignRight | Qt::AlignVCenter);
            text({x + 16, y + 48, 350, 18}, "OFFSET DE CALIBRATION (°C)", 12, muted, 400, false,
                 Qt::AlignLeft | Qt::AlignVCenter, 1.2);
            button({x + 16, y + 80, 64, 60}, "−", [this, i] { change(9 + i, -1); }, bg, tx, line, 30);
            box({x + 90, y + 80, 202, 60}, bg, line, 5);
            text({x + 90, y + 80, 202, 60},
                 (draft[9 + i] >= 0 ? "+" : "") + QString::number(draft[9 + i] / 10., 'f', 1), 26, accent,
                 600, true, Qt::AlignCenter);
            action({x + 90, y + 80, 202, 60}, [this, i] { edit(i + 9); });
            button({x + 302, y + 80, 64, 60}, "+", [this, i] { change(9 + i, 1); }, bg, tx, line, 26);
        }
        saveRow(top + 360, true);
        return;
    }
    bool fans = maintenanceTab == "fan";
    int pack = fans ? 0 : 1;
    text({228, qreal(top + 16), 776, 20},
         fans ? "5 ventilateurs · masque : " +
                    (c->packs[0] < 0 ? QString("—") : QString::number(c->packs[0], 2).rightJustified(5, '0'))
              : "Commande manuelle des sorties — régulation suspendue",
         13, muted, 400, fans);
    QStringList names = fans ? QStringList{"Ventilateur 1", "Ventilateur 2", "Ventilateur 3", "Ventilateur 4",
                                           "Ventilateur 5"}
                             : QStringList{"Compresseur", "Relais porte", "Lampe", "Ventilateur dégivrage"};
    for (int i = 0; i < names.size(); ++i) {
        int bit = fans ? i : QVector<int>{1, 3, 0, 2}[i];
        bool known = c->packs[pack] >= 0, on = known && (c->packs[pack] & (1 << bit));
        int cols = fans ? 3 : 2;
        double w = fans ? 250.66 : 382, h = fans ? 104 : 118, x = 228 + (i % cols) * (w + 12),
               y = top + 48 + (i / cols) * (h + 12);
        box({x, y, w, h}, on ? (dark ? QColor("#0d1f2b") : QColor("#e2f3fd")) : panel, on ? accent : line);
        text({x + 16, y + 17, w - 32, 28}, names[i], fans ? 17 : 19, tx, 600);
        p->setPen(Qt::NoPen);
        p->setBrush(on ? accent : line);
        p->drawEllipse(QRectF(x + 16, y + 62, 12, 12));
        text({x + 38, y + 54, w - 54, 28}, known ? (on ? "Marche" : "Éteint") : "État inconnu",
             fans ? 15 : 16, on ? accent : muted, 600);
        if (known && c->maintenanceActive && c->synced && !c->busy)
            action({x, y, w, h}, [this, pack, bit, on] { c->relay(pack, bit, !on); });
    }
    if (!c->maintenanceActive)
        text({228, qreal(top + height - 44), 776, 28},
             "En attente de confirmation du mode maintenance par la carte", 14, amber);
}
void Window::networkPage(int top, int height) {
    PainterScope contentScope(p);
    p->setClipRect(208, top, 816, height);
    scrollbar(top, height, 440);
    p->translate(0, -scroll);
    box({228, qreal(top + 16), 776, 96}, panel, line);
    text({244, qreal(top + 30), 560, 27}, "Wi-Fi", 18, tx, 600);
    text({244, qreal(top + 61), 560, 27},
         networkMessage.isEmpty() ? "Sélectionnez un réseau pour vous connecter" : networkMessage, 13, muted);
    button(
        {868, qreal(top + 32), 120, 64}, radioOn ? "ACTIF" : "ARRÊT",
        [this] {
            radioOn = !radioOn;
            network.radio(radioOn);
        },
        radioOn ? accent : bg, radioOn ? QColor("#08222f") : muted, line, 17);
    QString ip = "—", mac = "—", netmask = "—";
    for (auto iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack) ||
            !iface.flags().testFlag(QNetworkInterface::IsUp))
            continue;
        for (auto a : iface.addressEntries())
            if (a.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                ip = a.ip().toString();
                mac = iface.hardwareAddress();
                netmask = a.netmask().toString();
                break;
            }
        if (ip != "—")
            break;
    }
    box({228, qreal(top + 124), 776, 224}, panel, line);
    QStringList titles = {"ADRESSE IP", "RÉSEAU (SSID)", "MASQUE", "PASSERELLE", "ADRESSE MAC", "MODE"},
                values = {ip, selectedSsid.isEmpty() ? "—" : selectedSsid, netmask, "—", mac, "Système"};
    for (int i = 0; i < 6; ++i) {
        double x = 246 + (i % 2) * 382, y = top + 140 + (i / 2) * 66;
        text({x, y, 354, 18}, titles[i], 12, muted, 400, false, Qt::AlignVCenter | Qt::AlignLeft, 1.2);
        text({x, y + 23, 354, 30}, values[i], 22, i == 0 ? accent : tx, 600, true);
    }
    button(
        {228, qreal(top + 360), 382, 64}, "Rechercher les réseaux", [this] { network.scan(); }, panel, tx,
        line, 16);
    button(
        {622, qreal(top + 360), 382, 64}, "Actualiser les adresses", [this] { update(); }, bg, accent, accent,
        16);
}
void Window::passwordPage(int top) {
    text({228, qreal(top + 16), 456, 54},
         pinMessage.isEmpty() ? "Saisissez un nouveau code à 4 chiffres" : pinMessage, 14, muted, 400, false,
         Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap);
    for (int i = 0; i < 2; ++i) {
        double y = top + 84 + i * 105;
        text({228, y, 456, 18}, i == 0 ? "NOUVEAU CODE" : "CONFIRMER", 12, muted, 400, false,
             Qt::AlignLeft | Qt::AlignVCenter, 1.2);
        box({228, y + 26, 456, 64}, panel, pinField == (i == 0 ? "new" : "conf") ? accent : line, 5);
        text({228, y + 26, 456, 64}, pinMask(i == 0 ? newPin : confirmPin), 30, tx, 400, true,
             Qt::AlignCenter, 15);
        action({228, y + 26, 456, 64}, [this, i] {
            pinField = i == 0 ? "new" : "conf";
            update();
        });
    }
    for (int i = 0; i < 12; ++i)
        button(
            {qreal(704 + (i % 3) * 102.66), qreal(top + 16 + (i / 3) * 64), 94.66, 56}, keys[i],
            [this, key = keys[i]] { passwordPress(key); }, panel, i == 9 || i == 11 ? accent : tx, line, 24);
}
void Window::pinOverlay() {
    hits.clear();
    p->fillRect(QRectF(0, 0, 1024, 600), QColor(6, 9, 12, 225));
    if (overlay == "status") {
        box({182, 172, 660, 256}, panel, line, 8);
        text({206, 190, 612, 32}, "État du contrôleur", 20, tx, 700);
        text({206, 236, 612, 80}, pinMessage.isEmpty() ? c->status : pinMessage, 17, muted, 400, false,
             Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap);
        button(
            {206, 340, 298, 60}, "Relire la carte",
            [this] {
                c->synchronize();
                overlay.clear();
                update();
            },
            bg, accent, line, 16, !c->busy);
        button(
            {520, 340, 298, 60}, "Fermer",
            [this] {
                overlay.clear();
                pinMessage.clear();
                update();
            },
            bg, tx, line);
        return;
    }
    if (overlay == "networks") {
        box({152, 48, 720, 504}, panel, line, 8);
        text({176, 64, 470, 40}, "Réseaux Wi-Fi disponibles", 20, tx, 700);
        button(
            {730, 64, 118, 40}, "Fermer",
            [this] {
                overlay.clear();
                scroll = 0;
                update();
            },
            bg, muted, line, 14);
        p->save();
        p->setClipRect(176, 120, 672, 408);
        maxScroll = qMax(0, int(networks.size()) * 64 - 408);
        scroll = qBound(0, scroll, maxScroll);
        for (int i = 0; i < networks.size(); ++i)
            button(
                {176, qreal(120 + i * 64 - scroll), 672, 56}, networks[i],
                [this, ssid = networks[i]] {
                    bool ok = false;
                    auto pw = keyboard("Mot de passe Wi-Fi · " + ssid, {}, true, &ok);
                    if (ok) {
                        selectedSsid = ssid;
                        network.connectWifi(ssid, pw);
                        overlay.clear();
                        scroll = 0;
                    }
                    update();
                },
                bg, tx, line, 17);
        if (networks.isEmpty())
            text({176, 120, 672, 80}, "Aucun réseau détecté", 18, muted);
        p->restore();
        return;
    }
    box({278, 43, 468, 514}, panel, line, 8);
    text({302, 63, 302, 30},
         overlay == "create"    ? "Créer le code d’accès"
         : overlay == "confirm" ? "Confirmer le code"
                                : "Accès réglages",
         20, tx, 700);
    text({628, 63, 94, 30}, "Annuler", 15, muted);
    action({628, 63, 94, 30}, [this] {
        overlay.clear();
        pin.clear();
        firstPin.clear();
        update();
    });
    text({302, 107, 420, 24},
         pinMessage.isEmpty()
             ? (c->simulation && overlay == "pin" ? "Saisissez le code technicien (démo : 1234)"
                                                  : "Saisissez votre code à 4 chiffres")
             : pinMessage,
         14, pinMessage.isEmpty() ? muted : red);
    box({302, 147, 420, 64}, bg, line, 5);
    text({302, 147, 420, 64}, pinMask(pin), 38, tx, 400, true, Qt::AlignCenter, 19);
    for (int i = 0; i < 12; ++i)
        button(
            {qreal(302 + (i % 3) * 143.333), qreal(227 + (i / 3) * 74), 133.333, 64}, keys[i],
            [this, key = keys[i]] { pinPress(key); }, bg, i == 9 || i == 11 ? accent : tx, line, 26);
}
void Window::pinPress(const QString &key) {
    if (key == "←") {
        pin.chop(1);
        pinMessage.clear();
        update();
        return;
    }
    if (key == "OK" && pin.size() != 4)
        return;
    if (key != "OK")
        pin = (pin + key).left(4);
    if (pin.size() == 4) {
        QSettings s;
        if (overlay == "create") {
            firstPin = pin;
            pin.clear();
            overlay = "confirm";
        } else if (overlay == "confirm") {
            if (pin != firstPin) {
                pinMessage = "Les codes ne correspondent pas";
                pin.clear();
            } else {
                auto salt = QUuid::createUuid().toString();
                s.setValue("access/salt", salt);
                s.setValue("access/hash", hash(pin, salt));
                unlocked = true;
                overlay.clear();
                screen = "set";
                pin.clear();
                firstPin.clear();
            }
        } else {
            bool valid = c->simulation ? pin == "1234"
                                       : hash(pin, s.value("access/salt").toString()) ==
                                             s.value("access/hash").toString();
            if (valid) {
                unlocked = true;
                overlay.clear();
                screen = "set";
            } else
                pinMessage = "Code incorrect";
            pin.clear();
        }
    }
    update();
}
void Window::passwordPress(const QString &key) {
    QString &field = pinField == "new" ? newPin : confirmPin;
    if (key == "←")
        field.chop(1);
    else if (key == "OK") {
        if (newPin.size() != 4) {
            pinMessage = "Le code doit contenir 4 chiffres";
            pinField = "new";
        } else if (newPin != confirmPin) {
            pinMessage = "Les deux codes ne correspondent pas";
            confirmPin.clear();
            pinField = "conf";
        } else if (c->simulation)
            pinMessage = "Simulation : code non enregistré";
        else {
            QSettings s;
            auto salt = QUuid::createUuid().toString();
            s.setValue("access/salt", salt);
            s.setValue("access/hash", hash(newPin, salt));
            pinMessage = "Code enregistré";
            newPin.clear();
            confirmPin.clear();
            pinField = "new";
        }
    } else {
        field = (field + key).left(4);
        pinMessage.clear();
        if (pinField == "new" && field.size() == 4)
            pinField = "conf";
    }
    update();
}
void Window::go(const QString &page) {
    if (page == "set" && !unlocked) {
        QSettings s;
        overlay = c->simulation || s.contains("access/hash") ? "pin" : "create";
        pin.clear();
        pinMessage.clear();
    } else {
        screen = page;
        scroll = 0;
    }
    update();
}
void Window::change(int index, qint64 delta) {
    qint64 minimum = Protocol::settings[index].signedValue ? -2147483648LL : 0,
           maximum = Protocol::settings[index].signedValue ? 2147483647LL : 4294967295LL;
    draft[index] = qBound(minimum, draft[index] + delta, maximum);
    update();
}
void Window::save(bool calibration) {
    auto values = c->config;
    for (int i = calibration ? 9 : 0; i < (calibration ? 13 : 9); ++i)
        values[i] = draft[i];
    if (!Protocol::valid(values)) {
        pinMessage = "La température minimale doit être inférieure au maximum";
        overlay = "status";
        update();
        return;
    }
    pendingSave = calibration ? 1 : 0;
    c->save(values);
    if (!c->busy)
        pendingSave = -1;
    update();
}
void Window::edit(int i) {
    bool ok = false;
    auto value =
        keyboard(QString::fromUtf8(Protocol::settings[i].label), number(i).section(' ', 0, 0), false, &ok);
    if (!ok)
        return;
    bool valid = false;
    double n = value.replace(',', '.').toDouble(&valid), scale = Protocol::settings[i].scale;
    if (i == 6 || i == 7)
        scale = 60;
    if (i == 8)
        scale = 1;
    if (!valid || !std::isfinite(n))
        return;
    double raw = n * scale;
    if (raw < (Protocol::settings[i].signedValue ? -2147483648.0 : 0) ||
        raw > (Protocol::settings[i].signedValue ? 2147483647.0 : 4294967295.0))
        return;
    draft[i] = qRound64(raw);
    update();
}
QPointF Window::logical(QPointF point) const {
    return (point - origin) / scaleFactor;
}
void Window::mousePressEvent(QMouseEvent *event) {
    pressPoint = logical(event->pos());
    dragging = false;
}
void Window::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        auto now = logical(event->pos());
        if (std::abs(now.y() - pressPoint.y()) > 8 && maxScroll > 0) {
            scroll = qBound(0, scroll + int(pressPoint.y() - now.y()), maxScroll);
            pressPoint = now;
            dragging = true;
            update();
        }
    }
}
void Window::mouseReleaseEvent(QMouseEvent *event) {
    if (dragging)
        return;
    auto point = logical(event->pos());
    for (int i = hits.size() - 1; i >= 0; --i)
        if (hits[i].rect.contains(point)) {
            auto callback = hits[i].action;
            callback();
            return;
        }
}
void Window::wheelEvent(QWheelEvent *event) {
    if (maxScroll > 0) {
        scroll = qBound(0, scroll - event->angleDelta().y() / 2, maxScroll);
        update();
    }
}
void Window::keyPressEvent(QKeyEvent *event) {
    if (overlay == "pin" || overlay == "create" || overlay == "confirm") {
        if (event->key() == Qt::Key_Backspace)
            pinPress("←");
        else if (event->key() == Qt::Key_Return)
            pinPress("OK");
        else if (event->key() == Qt::Key_Escape) {
            overlay.clear();
            update();
        } else if (event->text().size() == 1 && event->text()[0].isDigit())
            pinPress(event->text());
    } else
        QWidget::keyPressEvent(event);
}
QString Window::keyboard(const QString &title, const QString &initial, bool secret, bool *accepted) {
    QDialog dialog(this, Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setFixedSize(940, 460);
    dialog.setStyleSheet(QString("QWidget{background:%1;color:%2;font-family:'IBM Plex "
                                 "Sans';font-size:18px;}QPushButton,QLineEdit{background:%3;border:1px solid "
                                 "%4;border-radius:5px;padding:8px;min-height:36px;}QPushButton:pressed{"
                                 "background:#0d1f2b;color:#38bdf8;}")
                             .arg(panel.name(), tx.name(), bg.name(), line.name()));
    auto l = new QVBoxLayout(&dialog);
    l->addWidget(new QLabel(title));
    auto edit = new QLineEdit(initial);
    edit->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
    l->addWidget(edit);
    edit->selectAll();
    QList<QPushButton *> letters;
    bool upper = false;
    const QStringList rows = {"1234567890", "azertyuiop", "qsdfghjklm", "wxcvbn@._-", "!#$%&*()+="};
    for (auto row : rows) {
        auto r = new QHBoxLayout;
        l->addLayout(r);
        for (QChar ch : row) {
            auto b = new QPushButton(QString(ch));
            r->addWidget(b);
            letters << b;
            connect(b, &QPushButton::clicked, &dialog, [edit, b] { edit->insert(b->text()); });
        }
    }
    auto r = new QHBoxLayout;
    l->addLayout(r);
    auto add = [&](QString name, std::function<void()> cb) {
        auto b = new QPushButton(name);
        r->addWidget(b);
        connect(b, &QPushButton::clicked, &dialog, cb);
    };
    add("Maj", [&upper, letters] {
        upper = !upper;
        for (auto b : letters)
            b->setText(upper ? b->text().toUpper() : b->text().toLower());
    });
    add("Espace", [edit] { edit->insert(" "); });
    add("←", [edit] { edit->backspace(); });
    add("Annuler", [&dialog] { dialog.reject(); });
    add("Valider", [&dialog] { dialog.accept(); });
    connect(edit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);
    edit->setFocus();
    bool ok = dialog.exec() == QDialog::Accepted;
    if (accepted)
        *accepted = ok;
    return ok ? edit->text() : QString{};
}
void Window::preview(const QString &requested) {
    if (!c->simulation)
        return;
    QString name = requested;
    if (name.endsWith("-light")) {
        dark = false;
        name.chop(6);
    }
    if (name.endsWith("-alarm")) {
        c->receive(1, QByteArray(1, char(0x53)));
        name.chop(6);
    }
    overlay.clear();
    scroll = 0;
    if (name == "pin") {
        screen = "temp";
        overlay = "pin";
    } else if (QStringList{"reg", "deg", "alm", "net", "mnt"}.contains(name)) {
        screen = "set";
        settingTab = name;
    } else if (QStringList{"cal", "fan", "act", "pwd"}.contains(name)) {
        screen = "maint";
        maintenanceTab = name;
    } else
        screen = name;
    update();
}
