#include "window.h"
#include <QSignalSpy>
#include <QtTest>
class WindowTests : public QObject {
    Q_OBJECT
    void draw(Window &w) {
        w.grab();
        QCoreApplication::processEvents();
    }
    void click(Window &w, int x, int y) {
        draw(w);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, {x, y});
        draw(w);
    }
  private slots:
    void navigationPinAndTheme() {
        Controller c(true, "test");
        Window w(&c);
        w.show();
        c.start();
        QTRY_VERIFY(c.synced);
        draw(w);
        auto image = w.grab().toImage();
        QCOMPARE(image.size(), QSize(1024, 600));
        QCOMPARE(image.pixelColor(32, 132), QColor("#1b2127"));
        click(w, 970, 28);
        QVERIFY(!w.dark);
        click(w, 970, 28);
        QVERIFY(w.dark);
        click(w, 896, 568);
        QCOMPARE(w.overlay, QString("pin"));
        click(w, 368, 259);
        click(w, 511, 259);
        click(w, 654, 259);
        click(w, 368, 333);
        QVERIFY(w.unlocked);
        QCOMPARE(w.screen, QString("set"));
        QVERIFY(w.overlay.isEmpty());
        click(w, 94, 471);
        QVERIFY(!w.unlocked);
        QCOMPARE(w.screen, QString("temp"));
    }
    void scrolledSettingsSave() {
        Controller c(true, "test");
        Window w(&c);
        w.show();
        c.start();
        QTRY_VERIFY(c.synced);
        QTest::qWait(250);
        w.preview("reg");
        draw(w);
        auto before = c.config[0];
        click(w, 954, 116);
        QCOMPARE(w.draft[0], before + 5);
        QVERIFY(w.dirty(false));
        QVERIFY(w.maxScroll > 0);
        w.scroll = w.maxScroll;
        draw(w);
        QSignalSpy spy(&c, &Controller::transmitted);
        click(w, 860, 472);
        QVERIFY(c.busy);
        QTRY_VERIFY(c.synced);
        QCOMPARE(c.config[0], before + 5);
        QVERIFY(w.savedAt != QString::fromUtf8("—"));
        QCOMPARE(spy.size(), 3);
        QCOMPARE(spy[0][0].toUInt(), uint(0x300));
    }
    void calibrationAndRelayMapping() {
        Controller c(true, "test");
        Window w(&c);
        w.show();
        c.start();
        QTRY_VERIFY(c.synced);
        QTest::qWait(250);
        w.preview("mnt");
        draw(w);
        click(w, 580, 246);
        QTRY_VERIFY(c.maintenanceActive);
        QCOMPARE(w.screen, QString("maint"));
        click(w, 561, 182);
        QCOMPARE(w.draft[9], qint64(1));
        click(w, 880, 448);
        QTRY_VERIFY(c.synced);
        QCOMPARE(c.config[9], qint64(1));
        click(w, 104, 193);
        QCOMPARE(w.maintenanceTab, QString("fan"));
        int before = c.packs[0];
        click(w, 352, 160);
        QTRY_COMPARE(c.packs[0], before ^ 1);
        click(w, 102, 260);
        QCOMPARE(w.maintenanceTab, QString("act"));
        int pack = c.packs[1];
        click(w, 405, 170);
        QTRY_COMPARE(c.packs[1], pack ^ 2);
        click(w, 98, 470);
        QTRY_VERIFY(!c.maintenanceActive);
        QCOMPARE(w.settingTab, QString("mnt"));
    }
    void alertAndOverlayBlockClicks() {
        Controller c(true, "test");
        Window w(&c);
        w.show();
        c.start();
        QTRY_VERIFY(c.synced);
        c.receive(1, QByteArray(1, char(0x53)));
        draw(w);
        click(w, 750, 79);
        QCOMPARE(w.screen, QString("alarms"));
        click(w, 894, 97);
        QVERIFY(c.alarm.isEmpty());
        w.preview("pin");
        draw(w);
        click(w, 384, 570);
        QCOMPARE(w.overlay, QString("pin"));
        c.receive(1, QByteArray(1, char(0x53)));
        w.preview("cal");
        draw(w);
        QVERIFY(w.maxScroll > 0);
    }
    void scaledTouchTargets() {
        Controller c(true, "test");
        Window w(&c);
        w.resize(1280, 800);
        w.show();
        draw(w);
        click(w, 1215, 60);
        QVERIFY(!w.dark);
    }
};
QTEST_MAIN(WindowTests)
#include "window_tests.moc"
