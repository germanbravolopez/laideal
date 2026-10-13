#ifndef PRODUCERDECLARATION_H
#define PRODUCERDECLARATION_H

// The declaración responsable of La Ideal as an invoicing system (SIF), signed by its
// producer (AEAT, 13-10-2026: also for a system made for one's own use). Content and
// order of art. 15 of Orden HAC/1177/2024: the title, then items a) to l), each value
// preceded by its description; an annex with recommended information (15.2). It must
// be readable inside the program and given to the client (15.3): Ayuda -> Acerca de
// Verifactu shows it and saves it as PDF.

#include <QDate>
#include <QString>

namespace ProducerDeclaration {

// What the producer fills in when signing (items j and l). Empty or invalid values
// are printed as pending.
struct Signature {
    QString postalAddress;   // j) complete postal address of the producer
    QDate   date;            // l) day, month, year
    QString place;           // l) town and country, in that order
};

// The signature of this version (one declaración per version, 15.1.c).
Signature currentSignature();

// Whether every item has its value (address, date and place included).
bool isComplete(const Signature &signature = currentSignature());

// The declaración as rich text, for the window and the PDF.
QString html(const QString &version, const Signature &signature = currentSignature());

} // namespace ProducerDeclaration

#endif // PRODUCERDECLARATION_H
