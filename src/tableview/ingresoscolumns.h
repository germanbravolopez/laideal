#ifndef INGRESOSCOLUMNS_H
#define INGRESOSCOLUMNS_H

class QHeaderView;

// Shows fecha_anulacion right after the other three dates (recepcion, pago,
// recogida) in an ingresos grid. The column is appended at the end of the table
// (added by migrateDatabase), so only its visual position moves; the model's
// INGRESOS_COL_* logical indices are untouched. Idempotent.
void placeIngresosDateColumns(QHeaderView *header);

#endif // INGRESOSCOLUMNS_H
