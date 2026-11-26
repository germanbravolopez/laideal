#include "aeatreconcilesummary.h"

#include <QCoreApplication>

QString aeatReconcileSummaryHtml(const VerifactuRemoteRecord &rec, bool matches,
                                 const QString &invoiceId, const QString &paymentDate,
                                 bool predatesProduction, const QString &productionStart)
{
    const auto tr = [](const char *text) { return QCoreApplication::translate("RecogPrendas", text); };
    if (!rec.parsed) {
        // Not evidence that the invoice is absent.
        return tr("<b>No se ha podido interpretar la respuesta de AEAT.</b><br>"
                  "Esto <i>no</i> significa que la factura no esté registrada. "
                  "Revisa el detalle de abajo y consulta la sede electrónica.");
    }
    if (!rec.found && predatesProduction) {
        return tr("<b>La AEAT real no tiene la factura %1, y es lo esperado.</b><br>"
                  "Es del %2, anterior al paso a PRODUCCIÓN (%3): se registró en el entorno de "
                  "pruebas o con IreneSolutions. No hay que reenviarla.")
            .arg(invoiceId, paymentDate, productionStart);
    }
    if (!rec.found) {
        // Deliberately no "safe to resend": an empty reply does not prove the InvoiceID filter
        // was applied, and a false all-clear would lead to a duplicate submission.
        return tr("<b>AEAT no ha devuelto ninguna factura con el número %1.</b><br>"
                  "Lo más probable es que no llegara a registrarse. Aun así, "
                  "antes de reenviar conviene confirmarlo en la sede electrónica "
                  "de la AEAT: si ya constara allí, el reenvío se rechazaría por "
                  "duplicado.").arg(invoiceId);
    }
    // Every attempt is stored, so a retried ticket returns several records; the fields
    // shown come from the accepted one.
    const QString attempts = rec.recordCount > 1
        ? tr("<br><i>AEAT ha devuelto %1 registros para este número (los reenvíos "
             "rechazados quedan guardados). Se muestran los datos del registro "
             "aceptado.</i>").arg(rec.recordCount)
        : QString();
    QString verdict;
    if (!matches)
        verdict = tr("<span style='color:#b00'>Los datos NO coinciden con los del "
                     "ticket - no se puede actualizar automáticamente.</span>");
    else if (!rec.hasUsableCsv())
        verdict = tr("<span style='color:#b00'>Ninguno de los registros fue aceptado "
                     "por AEAT (no hay CSV que recuperar).</span>");
    else
        verdict = tr("Los datos coinciden con los del ticket.");
    return tr("<b>AEAT tiene registrada esta factura.</b><br>%1%2").arg(verdict, attempts);
}
