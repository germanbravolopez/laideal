#ifndef INGRESOS_SCHEMA_H
#define INGRESOS_SCHEMA_H

// `ingresos` table column indices. Name = uppercase of the SQL column name so
// a grep for the column in the schema matches the constant directly. Order
// must match `CREATE TABLE ingresos` plus the trailing ALTER TABLEs in
// migrateDatabase() - append a new constant for a new column, do not renumber.
#define INGRESOS_COL_N_RECIBO                       0
#define INGRESOS_COL_CLIENTE                        1
#define INGRESOS_COL_FECHA_RECEPCION                2
#define INGRESOS_COL_FECHA_PAGO                     3
#define INGRESOS_COL_FECHA_RECOGIDA                 4
#define INGRESOS_COL_IMPORTE                        5
#define INGRESOS_COL_PAGADO                         6
#define INGRESOS_COL_ESTADO                         7
#define INGRESOS_COL_CANTIDAD                       8
#define INGRESOS_COL_PRENDA                         9
#define INGRESOS_COL_SIZE                          10
#define INGRESOS_COL_SERVICIO                      11
#define INGRESOS_COL_OBSERVACIONES                 12
#define INGRESOS_COL_EDIT_LOCK                     13
#define INGRESOS_COL_HASH                          14
#define INGRESOS_COL_VERIFACTU_CSV                 15
#define INGRESOS_COL_VERIFACTU_TIMESTAMP           16
#define INGRESOS_COL_VERIFACTU_ESTADO              17
#define INGRESOS_COL_VERIFACTU_ERROR               18
#define INGRESOS_COL_VERIFACTU_URL_QR              19
#define INGRESOS_COL_VERIFACTU_XML                 20
#define INGRESOS_COL_VERIFACTU_HASH                21
#define INGRESOS_COL_VERIFACTU_RECTIFIES_N_RECIBO  22
#define INGRESOS_COL_VERIFACTU_RECTIFICATION_TYPE  23
#define INGRESOS_COL_VERIFACTU_INVOICE_SEQ         24

#endif // INGRESOS_SCHEMA_H
