#pragma once
#include "controller.h"
#include "network.h"
#include <QPainter>
#include <QWidget>
#include <functional>
// Native Qt drawing in the exact 1024 x 600 coordinate system of the reference.
class Window : public QWidget {
    Q_OBJECT
  public:
    explicit Window(Controller *controller);
    void preview(const QString &screen);

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    friend class WindowTests;
    struct Hit {
        QRectF rect;
        std::function<void()> action;
    };
    Controller *c;
    Network network;
    Protocol::Config draft{};
    QVector<Hit> hits;
    QVector<QPointF> samples;
    QString screen = "temp", settingTab = "reg", maintenanceTab = "cal", range = "24h";
    bool dark = true, unlocked = false, radioOn = true, dragging = false;
    int scroll = 0, maxScroll = 0;
    int pendingSave = -1;
    QPointF pressPoint;
    QString overlay, pin, pinMessage, firstPin, newPin, confirmPin, pinField = "new";
    QString savedAt = "—", calSavedAt = "—", networkMessage, selectedSsid;
    QStringList networks;
    QColor bg, panel, line, tx, muted, accent = "#38bdf8", red = "#ef4444", amber = "#f59e0b",
                                       green = "#4ade80";
    QPainter *p = nullptr;
    double scaleFactor = 1;
    QPointF origin;
    void text(QRectF r, const QString &value, int size, QColor color, int weight = 400, bool mono = false,
              int align = Qt::AlignLeft | Qt::AlignVCenter, qreal spacing = 0);
    void box(QRectF r, QColor fill, QColor border = {}, qreal radius = 6);
    void rule(qreal x1, qreal y1, qreal x2, qreal y2);
    void action(QRectF r, std::function<void()> callback);
    void button(QRectF r, const QString &value, std::function<void()> callback, QColor fill, QColor color,
                QColor border, int size = 16, bool enabled = true);
    void header();
    void navigation();
    void temperaturePage(int top, int height);
    void historyPage(int top, int height);
    void alarmPage(int top, int height);
    void settingsPage(int top, int height);
    void maintenancePage(int top, int height);
    void networkPage(int top, int height);
    void pinOverlay();
    void passwordPage(int top);
    void pinPress(const QString &key);
    void passwordPress(const QString &key);
    void go(const QString &page);
    void change(int index, qint64 delta);
    void save(bool calibration);
    void saveRow(int y, bool calibration = false);
    void scrollbar(int top, int height, int contentHeight);
    QString number(int index) const;
    bool dirty(bool calibration) const;
    void edit(int index);
    QString keyboard(const QString &title, const QString &initial = {}, bool secret = false,
                     bool *accepted = nullptr);
    QPointF logical(QPointF point) const;
};
