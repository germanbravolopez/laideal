#include "ingresoscolumns.h"
#include "ingresos_schema.h"

#include <QHeaderView>

void placeIngresosDateColumns(QHeaderView *header)
{
    if (!header || header->count() <= INGRESOS_COL_FECHA_ANULACION)
        return;
    header->moveSection(header->visualIndex(INGRESOS_COL_FECHA_ANULACION),
                        header->visualIndex(INGRESOS_COL_FECHA_RECOGIDA) + 1);
}
