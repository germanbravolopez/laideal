// AeatQr: the Verifactu QR drawn locally. The structure is checked here (size, quiet
// zone, the three finder patterns); the sample written to $AEAT_XML_OUT/qr_sample.png
// was decoded with an independent reader when this was written (see the milestone).

#include <QtTest>

#include "aeathash.h"
#include "aeatqr.h"

class TestAeatQr : public QObject
{
    Q_OBJECT

private:
    // True when the 7x7 finder pattern starts at module (mx, my): dark ring, light
    // ring, dark 3x3 centre.
    static bool finderAt(const QImage &img, int mx, int my, int ppm)
    {
        const auto dark = [&](int x, int y) {
            return qGray(img.pixel((mx + x) * ppm + ppm / 2, (my + y) * ppm + ppm / 2)) < 128;
        };
        for (int i = 0; i < 7; ++i) {
            if (!dark(i, 0) || !dark(i, 6) || !dark(0, i) || !dark(6, i))
                return false;
        }
        for (int i = 1; i < 6; ++i) {
            if (dark(i, 1) || dark(i, 5) || dark(1, i) || dark(5, i))
                return false;
        }
        for (int y = 2; y < 5; ++y)
            for (int x = 2; x < 5; ++x)
                if (!dark(x, y))
                    return false;
        return true;
    }

private slots:
    void test_verificationUrlSymbol()
    {
        const QString url = AeatHash::qrValidationUrl("89890001K", "30837", QDate(2026, 10, 10), "30.00", false);
        const int modules = AeatQr::moduleCount(url);
        QVERIFY(modules >= 21 && (modules - 21) % 4 == 0);
        const int ppm = 4;
        const QImage img = AeatQr::image(url, ppm);
        QCOMPARE(img.width(), (modules + 8) * ppm);
        QCOMPARE(img.height(), img.width());
        // Quiet zone: the first and last 4 modules are white all around.
        for (int i = 0; i < img.width(); i += ppm) {
            QCOMPARE(qGray(img.pixel(i, 2)), 255);
            QCOMPARE(qGray(img.pixel(2, i)), 255);
            QCOMPARE(qGray(img.pixel(img.width() - 3, i)), 255);
        }
        QVERIFY(finderAt(img, 4, 4, ppm));
        QVERIFY(finderAt(img, 4 + modules - 7, 4, ppm));
        QVERIFY(finderAt(img, 4, 4 + modules - 7, ppm));
        QVERIFY(!finderAt(img, 4 + modules - 7, 4 + modules - 7, ppm));    // no finder bottom-right

        const QString dir = qEnvironmentVariable("AEAT_XML_OUT");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            QVERIFY(AeatQr::image(url, 8).save(dir + "/qr_sample.png"));
            QFile f(dir + "/qr_sample.txt");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(url.toUtf8());
        }
    }

    // A longer number needs a larger symbol; level M keeps the URL readable when printed small.
    void test_sizeGrowsWithText()
    {
        QVERIFY(AeatQr::moduleCount(QString(200, 'A')) > AeatQr::moduleCount("A"));
    }
};

QTEST_MAIN(TestAeatQr)
#include "test_aeat_qr.moc"
