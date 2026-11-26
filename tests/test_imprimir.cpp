// Qt Test suite for Imprimir (the print dialog / engine) with the printer transport
// replaced by ThermalPrinter::setTransport, so no printer, spooler or AEAT is involved:
// what reaches the "printer" is captured and the failure paths are forced. A throwaway
// SQLite DB and throwaway settings, offscreen QPA.

#include <QtTest>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>

#include "appsettings.h"
#include "e2efixture.h"
#include "escpospreview.h"
// A test reaches the QR bookkeeping that only the direct backend sets in the app.
#define private public
#include "imprimir.h"
#undef private
#include "modalautocloser.h"
#include "sql_lite.h"
#include "thermalprinter.h"

class TestImprimir : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    QSqlDatabase m_db;
    ModalAutoCloser *m_popups = nullptr;
    QList<QByteArray> m_sent;       // every payload the fake transport received
    QStringList m_queues;           // and the queue it was addressed to
    bool m_transportOk = true;
    int m_failOnCall = -1;          // the transport call (1-based) that fails; -1 = none
    QString m_transportError;

    // Presses the dialog's Imprimir button for a ticket; returns the result panel text.
    static QString press(Imprimir *dlg, const QString &ticket)
    {
        dlg->le_n_ticket->setText(ticket);
        dlg->findChild<QPushButton *>("btnPrint")->click();
        return dlg->findChild<QLabel *>("lblResult")->text();
    }

    Imprimir *openDialog(bool recibo, bool complete = false)
    {
        auto *dlg = new Imprimir(m_db);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->isRecibo = recibo;
        dlg->isCompleteInvoice = complete;
        dlg->show();
        return dlg;
    }

    // The decoded text rows of a ticket (one string per printed line).
    static QList<EscPosPreview::PreviewBlock> textBlocks(const QByteArray &ticket)
    {
        QList<EscPosPreview::PreviewBlock> blocks;
        for (const auto &b : EscPosPreview::interpret(ticket))
            if (b.kind == EscPosPreview::PreviewBlock::Text)
                blocks << b;
        return blocks;
    }

    static QStringList textRows(const QByteArray &ticket)
    {
        QStringList rows;
        for (const auto &b : textBlocks(ticket))
            rows << b.text;
        return rows;
    }

    QString ticketState(const QString &nRecibo)
    {
        return E2e::scalar(m_db, QStringLiteral("SELECT COUNT(*) || '|' || group_concat(hash) || '|' || group_concat(pagado) "
                                                "FROM ingresos WHERE n_recibo='%1'").arg(nRecibo));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(E2e::configureSettings(QDir(m_dir.path())));
        m_db = QSqlDatabase::addDatabase("QSQLITE", "test_imprimir");
        m_db.setDatabaseName(m_dir.filePath("imprimir.db"));
        QVERIFY(E2e::createSchema(m_db));
        m_popups = new ModalAutoCloser(this);
        ThermalPrinter::setTransport([this](const QByteArray &bytes, const QString &queue, QString *err) {
            m_sent << bytes;
            m_queues << queue;
            const bool ok = m_transportOk && m_sent.size() != m_failOnCall;
            if (!ok && err)
                *err = m_transportError;
            return ok;
        });
    }

    void cleanupTestCase()
    {
        ThermalPrinter::setTransport({});
    }

    void init()
    {
        E2e::clearTables(m_db);
        m_sent.clear();
        m_queues.clear();
        m_popups->clear();
        m_transportOk = true;
        m_failOnCall = -1;
        m_transportError.clear();
        AppSettings *s = AppSettings::instance();
        s->setEnablePrinting(true);
        s->setUseStatusApi(false);
        s->setPrinterName("TEST-QUEUE");
        s->setPaperWidthMm(80);
    }

    // The transport sees the exact bytes of the built ticket and the configured queue.
    void test_printTicket_sendsBuiltBytesToTheConfiguredQueue()
    {
        QVERIFY(E2e::seedGarment(m_db, "700", "h700", "10.00"));
        Imprimir engine(m_db);
        engine.le_n_ticket->setText("700");
        engine.getTicketInfo();
        engine.buildTicket(true, false);
        QVERIFY(engine.printTicket());
        QCOMPARE(m_sent.size(), 1);
        QCOMPARE(m_sent.first(), engine.ticketBytes());
        QCOMPARE(m_queues, QStringList{ "TEST-QUEUE" });
    }

    // Nothing built (unknown ticket): nothing is sent and printTicket says so.
    void test_printTicket_withoutABuildSendsNothing()
    {
        Imprimir engine(m_db);
        engine.le_n_ticket->setText("none");
        engine.getTicketInfo();
        engine.buildTicket(true, false);
        QVERIFY(engine.ticketBytes().isEmpty());
        QVERIFY(!engine.printTicket());
        QVERIFY(m_sent.isEmpty());
    }

    // A failing transport (printer off / not found) on a non-visible engine: a pop-up
    // with the Spanish reason, and printTicket returns false.
    void test_printTicket_failureAsEnginePopsUpTheReason()
    {
        QVERIFY(E2e::seedGarment(m_db, "701", "h701", "10.00"));
        m_transportOk = false;
        m_transportError = QString::fromUtf8("No se ha enviado a 'TEST-QUEUE': la impresora está apagada o desconectada.");
        Imprimir engine(m_db);
        engine.le_n_ticket->setText("701");
        engine.getTicketInfo();
        engine.buildTicket(true, false);
        // The pop-up is modal: the closer must be running while exec() blocks.
        QVERIFY(!engine.printTicket());
        QVERIFY(m_popups->sawMessageContaining("No se pudo imprimir el ticket"));
        QVERIFY(m_popups->sawMessageContaining("apagada o desconectada"));
    }

    // Printer offline from the dialog: the reason is shown in the result panel (no
    // pop-up) and the ticket stays saved, untouched, so it can be printed again.
    void test_dialog_printerOfflineShowsTheWarningAndKeepsTheTicket()
    {
        QVERIFY(E2e::seedGarment(m_db, "702", "h702", "10.00"));
        m_transportOk = false;
        m_transportError = QString::fromUtf8("No se ha enviado a 'TEST-QUEUE': la impresora está apagada o desconectada.");
        const QString before = ticketState("702");
        QPointer<Imprimir> dlg = openDialog(true);
        const QString text = press(dlg, "702");
        QVERIFY2(text.contains(QString::fromUtf8("apagada o desconectada")), qPrintable(text));
        QVERIFY2(text.contains("No se pudo imprimir el ticket"), qPrintable(text));
        QVERIFY2(text.contains("no se ha impreso") && !text.contains(" impreso ("), qPrintable(text));
        QCOMPARE(m_sent.size(), 1);                         // no shop copy after a failed client copy
        QVERIFY(m_popups->messages().isEmpty());
        QVERIFY(!m_sent.isEmpty());                         // it did try to print
        QCOMPARE(ticketState("702"), before);
        // The printer comes back: pressing again prints and the problem text is gone.
        m_transportOk = true;
        m_sent.clear();
        const QString again = press(dlg, "702");
        QVERIFY2(!again.contains("apagada"), qPrintable(again));
        QVERIFY(!m_sent.isEmpty());
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // Recibo with the shop copy: two jobs to the transport; the bytes agree up to the copy
    // marker (header, body, totals) and differ in the marker and the client-only legal block.
    void test_dialog_twoCopiesGoToTheTransport()
    {
        QVERIFY(E2e::seedGarment(m_db, "703", "h703", "10.00"));
        QPointer<Imprimir> dlg = openDialog(true);
        QVERIFY(dlg->findChild<QCheckBox *>("chkShopCopy")->isChecked());
        press(dlg, "703");
        QCOMPARE(m_sent.size(), 2);
        const QByteArray client = m_sent[0], shop = m_sent[1];
        QVERIFY(client.contains("(Copia para el cliente)"));
        QVERIFY(client.contains("CONDICIONES GENERALES"));
        QVERIFY(shop.contains("(Copia para el establecimiento)"));
        QVERIFY(!shop.contains("CONDICIONES GENERALES"));
        const QByteArray clientHead = client.left(client.indexOf("(Copia para"));
        const QByteArray shopHead = shop.left(shop.indexOf("(Copia para"));
        QVERIFY(!clientHead.isEmpty());
        QCOMPARE(shopHead, clientHead);
        // Without the shop copy box: one job.
        dlg->findChild<QCheckBox *>("chkShopCopy")->setChecked(false);
        m_sent.clear();
        press(dlg, "703");
        QCOMPARE(m_sent.size(), 1);
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // Printing switched off in the settings: the ticket is built but never sent, and the
    // panel says so.
    void test_dialog_printingDisabledSendsNothing()
    {
        QVERIFY(E2e::seedGarment(m_db, "704", "h704", "10.00"));
        AppSettings::instance()->setEnablePrinting(false);
        QPointer<Imprimir> dlg = openDialog(true);
        const QString text = press(dlg, "704");
        QVERIFY2(text.contains(QString::fromUtf8("impresión está desactivada")), qPrintable(text));
        QVERIFY2(text.contains("preparado, no impreso"), qPrintable(text));
        QVERIFY2(!text.contains("704 impreso"), qPrintable(text));
        QVERIFY(m_sent.isEmpty());
        QVERIFY(!dlg->ticketBytes().isEmpty());
        dlg->close();
        QTRY_VERIFY(dlg.isNull());

        QVERIFY(E2e::seedSentGarment(m_db, "704", "h704f", "10.00", "10-02-2026", "A-704"));
        QPointer<Imprimir> factura = openDialog(false);
        const QString invoiceText = press(factura, "704");
        QVERIFY2(invoiceText.contains("preparada, no impresa"), qPrintable(invoiceText));
        QVERIFY2(!invoiceText.contains("704 impresa"), qPrintable(invoiceText));
        factura->close();
        QTRY_VERIFY(factura.isNull());
    }

    // The shop copy of a recibo reprint fails: the panel does not claim both copies printed.
    void test_dialog_shopCopyFailureIsSaidHonestly()
    {
        QVERIFY(E2e::seedGarment(m_db, "709", "h709", "10.00"));
        m_failOnCall = 2;
        m_transportError = "queue gone";
        QPointer<Imprimir> dlg = openDialog(true);
        const QString text = press(dlg, "709");
        QCOMPARE(m_sent.size(), 2);
        QVERIFY2(!text.contains("copia del cliente y del establecimiento"), qPrintable(text));
        QVERIFY2(text.contains("copia del establecimiento no"), qPrintable(text));
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // A complete invoice is never printed with a blank recipient address.
    void test_dialog_completaRefusedWithoutAnAddress()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "710", "h710", "10.00", "10-02-2026", "A-710"));
        QPointer<Imprimir> dlg = openDialog(false, true);
        dlg->findChild<QLineEdit *>("leDni")->setText("12345678Z");
        const QString text = press(dlg, "710");
        QVERIFY2(text.contains(QString::fromUtf8("dirección del cliente")), qPrintable(text));
        QVERIFY(m_sent.isEmpty());
        // The client's stored address is enough.
        QVERIFY(E2e::exec(m_db, "INSERT INTO clientes (nombre, direccion) VALUES ('Cliente E2E', 'Calle Stored 3')"));
        press(dlg, "710");
        QCOMPARE(m_sent.size(), 1);
        QVERIFY(textRows(m_sent[0]).contains(QString::fromUtf8("Dirección: Calle Stored 3")));
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // The raster width in bytes of the first image of a ticket (GS v 0 m xL xH yL yH), or -1.
    static int rasterWidthBytes(const QByteArray &ticket)
    {
        const int at = ticket.indexOf("v0");
        if (at < 0 || ticket.size() < at + 6)
            return -1;
        return uchar(ticket[at + 4]) | (uchar(ticket[at + 5]) << 8);
    }

    // A QR that cannot be redrawn is printed as received, never wider than the paper.
    void test_factura_unparseableQrPrintedAsReceivedAndFitsThePaper()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "711", "h711", "10.00", "10-02-2026", "A-711"));
        for (const int side : { 300, 700 }) {      // narrower / wider than the 576 dots of 80 mm
            QPixmap garbled(side, side);
            garbled.fill(Qt::black);                // no finder pattern: not a parseable symbol
            Imprimir engine(m_db);
            engine.isRecibo = false;
            engine.qrCode = garbled;
            engine.le_n_ticket->setText("711");
            engine.getTicketInfo();
            engine.buildTicket(true, false);
            const int widthBytes = rasterWidthBytes(engine.ticketBytes());
            QVERIFY2(widthBytes > 0, "no raster printed");
            if (side == 300)
                QCOMPARE(widthBytes, (300 + 7) / 8);
            else
                QVERIFY2(widthBytes <= 576 / 8, qPrintable(QString::number(widthBytes)));
        }
    }

    // A QR that came from the direct client's queued record keeps its leyenda when the same
    // object builds the ticket again (the cached QR must not lose where it came from).
    void test_factura_queuedQrLeyendaSurvivesASecondBuild()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "712", "h712", "10.00", "10-02-2026", ""));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET verifactu_estado='PENDIENTE', verifactu_csv='' WHERE n_recibo='712'"));
        const int module = 6, modules = 21, quiet = 4;
        QImage img((modules + 2 * quiet) * module, (modules + 2 * quiet) * module, QImage::Format_RGB32);
        img.fill(Qt::white);
        QPainter painter(&img);
        for (const QPoint &corner : { QPoint(0, 0), QPoint(modules - 7, 0), QPoint(0, modules - 7) })
            painter.fillRect((corner.x() + quiet) * module, (corner.y() + quiet) * module, 7 * module, 7 * module, Qt::black);
        painter.end();
        Imprimir engine(m_db);
        engine.isRecibo = false;
        engine.qrCode = QPixmap::fromImage(img);
        engine.m_qrFromQueuedRecord = true;   // as resolveQrCode() sets it for a queued record's QR
        engine.le_n_ticket->setText("712");
        engine.getTicketInfo();
        engine.buildTicket(true, false);
        QVERIFY(engine.ticketBytes().contains("v0"));
        QVERIFY(engine.ticketBytes().contains("Factura verificable"));
        engine.buildTicket(true, false);
        QVERIFY2(engine.ticketBytes().contains("Factura verificable"), "the second build lost the leyenda");
    }

    // Art. 7 RD 1619/2012: the factura shows the expedition date (payment date, 10/02),
    // not only the reception date (05/02); the recibo has no such line.
    void test_dialog_facturaShowsTheExpeditionDate()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "705", "h705", "12.10", "10-02-2026", "A-705"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET fecha_recepcion='05-02-2026' WHERE n_recibo='705'"));
        QPointer<Imprimir> factura = openDialog(false);
        press(factura, "705");
        QCOMPARE(m_sent.size(), 1);
        const QStringList rows = textRows(m_sent[0]);
        QVERIFY2(rows.contains(QString::fromUtf8("Fecha de expedición: 10/02/2026")), qPrintable(rows.join("|")));
        QVERIFY2(rows.contains(QString::fromUtf8("Recepción: 05/02/2026")), qPrintable(rows.join("|")));
        factura->close();
        QTRY_VERIFY(factura.isNull());

        m_sent.clear();
        QPointer<Imprimir> recibo = openDialog(true);
        press(recibo, "705");
        QVERIFY(!textRows(m_sent[0]).join("|").contains("Fecha de expedici"));
        recibo->close();
        QTRY_VERIFY(recibo.isNull());
    }

    // Factura completa (decision F2): it keeps the "FACTURA SIMPLIFICADA" heading (its record
    // is an F2) and adds the typed address and NIF.
    void test_dialog_completaKeepsSimplifiedHeadingAndCarriesAddressAndDni()
    {
        QVERIFY(E2e::seedSentGarment(m_db, "706", "h706", "10.00", "10-02-2026", "A-706"));
        QPointer<Imprimir> completa = openDialog(false, true);
        completa->findChild<QLineEdit *>("leDni")->setText("12345678Z");
        completa->findChild<QLineEdit *>("leAddress")->setText("Calle Mayor 1");
        press(completa, "706");
        QCOMPARE(m_sent.size(), 1);
        const QStringList rows = textRows(m_sent[0]);
        QVERIFY(rows.contains("FACTURA SIMPLIFICADA"));
        QVERIFY(rows.contains("DNI: 12345678Z"));
        QVERIFY(rows.contains(QString::fromUtf8("Dirección: Calle Mayor 1")));
        completa->close();
        QTRY_VERIFY(completa.isNull());
    }

    // The garment amounts and the size use the decimal comma; a zero size is hidden.
    void test_dialog_amountsAndSizeUseTheDecimalComma()
    {
        QVERIFY(E2e::seedGarment(m_db, "707", "h707", "3.50"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET size='1.50' WHERE n_recibo='707'"));
        QVERIFY(E2e::seedGarment(m_db, "707", "h707b", "4.00"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET size='0', prenda='Pantalon' WHERE hash='h707b'"));
        QPointer<Imprimir> dlg = openDialog(true);
        press(dlg, "707");
        const QString joined = textRows(m_sent[0]).join("\n");
        QVERIFY2(joined.contains("Camisa - 1,50"), qPrintable(joined));
        QVERIFY(joined.contains("3,50"));
        QVERIFY(joined.contains("Pantalon "));
        QVERIFY(!joined.contains("Pantalon -"));
        QVERIFY(!joined.contains("3.50"));
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // A 58 mm roll: no text row is wider than the 35 columns of Font A (17 double size) or
    // the 46 of the Font B legal block.
    void test_dialog_58mmRollFitsEveryRow()
    {
        AppSettings::instance()->setPaperWidthMm(58);
        QVERIFY(E2e::seedGarment(m_db, "708", "h708", "10.00"));
        QVERIFY(E2e::exec(m_db, "UPDATE ingresos SET prenda='Abrigo largo de lana con forro desmontable' WHERE n_recibo='708'"));
        QPointer<Imprimir> dlg = openDialog(true);
        press(dlg, "708");
        QVERIFY(!m_sent.isEmpty());
        for (const auto &b : textBlocks(m_sent[0])) {
            const int limit = b.font == EscPosBuilder::Font::A ? (b.dbl ? 17 : 35) : 46;
            QVERIFY2(b.text.size() <= limit, qPrintable(QString("%1 (%2 > %3)").arg(b.text).arg(b.text.size()).arg(limit)));
        }
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }

    // Unknown ticket: a warning in the panel, nothing sent, no pop-up.
    void test_dialog_unknownTicketSendsNothing()
    {
        QPointer<Imprimir> dlg = openDialog(true);
        const QString text = press(dlg, "9999");
        QVERIFY2(text.contains("No se ha encontrado el recibo"), qPrintable(text));
        QVERIFY(m_sent.isEmpty());
        QVERIFY(m_popups->messages().isEmpty());
        dlg->close();
        QTRY_VERIFY(dlg.isNull());
    }
};

QTEST_MAIN(TestImprimir)
#include "test_imprimir.moc"
