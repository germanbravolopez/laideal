#include "sql_lite.h"

int readMaxValueInColumnFromTable(QSqlDatabase &db,
                                  QString column,
                                  QString table)
{
    int maxValue = 0;
    db.open();
    QSqlQuery q;
    q.exec("SELECT MAX(" + column + ") FROM " + table);
    if (q.isSelect()) {
        if (q.first())
            maxValue = q.value(0).toInt();
        else
            QMessageBox::warning(nullptr, "Error base de datos",
                                  "Búsqueda vacía al usar readMaxValueInColumnFromTable.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readMaxValueInColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return maxValue;
}

int readMaxNMinYearInColumnFromTable(QSqlDatabase &db,
                                     bool maxNMin,
                                     QString column,
                                     QString table)
{
    int value = 0;
    QString query;
    if (maxNMin)
        query = "SELECT MAX(substr(" + column + ",7,4)) FROM " + table;
    else
        query = "SELECT MIN(substr(" + column + ",7,4)) FROM " + table;
    db.open();
    QSqlQuery q;
    q.exec(query);
    if (q.isSelect()) {
        if (q.first())
            value = q.value(0).toInt();
        else
            QMessageBox::warning(nullptr, "Error base de datos",
                                  "Búsqueda vacía al usar readMaxNMinYearInColumnFromTable.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readMaxNMinYearInColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return value;
}

QStringList readColumnFromTable(QSqlDatabase &db,
                                QString column,
                                QString table,
                                QString orderByColumn)
{
    db.open();
    QSqlQuery q;
    QStringList list;
    if (orderByColumn == "")
        q.exec("SELECT " + column + " FROM " + table + ";");
    else
        q.exec("SELECT " + column + " FROM " + table + " ORDER BY " + orderByColumn + " ASC;");
    if (q.isSelect()) {
        while(q.next())
            list.append(q.value(0).toString());
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readColumnFromTable.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return list;
}

float readGarmentPrice(QSqlDatabase &db,
                       QString garment,
                       QString service)
{
    float price = 0.0;
    db.open();
    QSqlQuery q;
    if (service == "Limp.") {
        q.prepare("SELECT precio_limpieza FROM prendas WHERE nombre LIKE :garment");
        q.bindValue(":garment", garment);
        q.exec();
    } else if (service == "Plan.") {
        q.prepare("SELECT precio_plancha FROM prendas WHERE nombre LIKE :garment");
        q.bindValue(":garment", garment);
        q.exec();
    }
    if (q.isSelect()) {
        if (q.first()) {
            if (!q.value(0).toString().contains(","))
                price = q.value(0).toFloat();
            else {
                QMessageBox::critical(nullptr, "Error en la base de datos",
                                      "En la tabla prendas se ha detectado que hay valores decimales guardados con ','. "
                                      "Utilizar herramienta de limpiado de importes decimales.",
                                      QMessageBox::Ok, QMessageBox::Ok);
                price = -1;
            }
        }
        else {
            QMessageBox::warning(nullptr, "Error base de datos",
                                  "No se ha encontrado la prenda '" + garment +
                                  "' al usar readGarmentPrice para el servicio '" + service + "'.\n"
                                  "Añadir un precio en el listado de prendas antes de continuar con esta acción.",
                                  QMessageBox::Ok, QMessageBox::Ok);
            price = -1;
        }
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readGarmentPrice.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return price;
}

QString selectFromWhereLike(QSqlDatabase &db,
                            QString itemToGet,
                            QString table,
                            QString columnToSearch,
                            QString itemToSearch,
                            bool exactMatch,
                            bool printMsg)
{
    QString itemToSearchText;
    db.open();
    QSqlQuery q;
    q.prepare("SELECT " + itemToGet + " FROM " + table + " WHERE " + columnToSearch + " LIKE :item");
    q.bindValue(":item", exactMatch ? itemToSearch : itemToSearch + "%");
    q.exec();
    if (q.isSelect()) {
        if (q.first())
            itemToSearchText = q.value(0).toString();
        else if (printMsg)
            QMessageBox::warning(nullptr, "Búsqueda vacía",
                                  "El elemento '" + itemToSearch + "' no se ha encontrado en la base de datos para '" + columnToSearch + "'.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else if (printMsg)
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar selectFromWhereLike.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return itemToSearchText;
}

QString searchItemFromClient(QSqlDatabase &db,
                             QString item,
                             QString client,
                             bool printMsg)
{
    return selectFromWhereLike(db, item, "clientes", "nombre", client, false, printMsg);
}

bool updateItemToClient(QSqlDatabase &db,
                        QString column,
                        QString item,
                        QString client)
{
    QSqlQuery q;
    db.open();
    q.prepare("UPDATE clientes SET " + column + " = :item WHERE nombre = :client");
    q.bindValue(":item", item);
    q.bindValue(":client", client);
    bool ok = q.exec();
    db.close();
    return ok;
}

bool addNewClient(QSqlDatabase &db,
                  QString client,
                  QString telFijo,
                  QString direccion,
                  QString movil)
{
    QSqlQuery q;
    db.open();
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) VALUES(:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre", client);
    q.bindValue(":tel_fijo", telFijo);
    q.bindValue(":direccion", direccion);
    q.bindValue(":movil", movil);
    bool ok = q.exec();
    db.close();
    return ok;
}

float totalPriceBetweenDates(QSqlDatabase &db,
                             QString table,
                             QDate startDate,
                             QDate endDate,
                             int iva)
{
    db.open();
    QSqlQuery q;
    float totalPrice = 0.0;
    if (table == "ingresos") {
        q.exec("SELECT importe FROM ingresos WHERE (pagado = 'SI') AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date('"
               + startDate.toString("yyyy-MM-dd") + "')) AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date('"
               + endDate.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect()) {
            while(q.next()) {
                if (!q.value(0).toString().contains(","))
                    totalPrice = totalPrice + q.value(0).toFloat();
                else
                    QMessageBox::critical(nullptr, "Error en la base de datos",
                                          "En la tabla " + table + " "
                                          "se ha detectado que hay valores decimales guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                          QMessageBox::Ok, QMessageBox::Ok);
            }
        }
        else
            QMessageBox::critical(nullptr, "Error base de datos",
                                  "Acceso a la tabla da un error al usar totalPriceBetweenDates.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else if (table == "gastos") {
        q.exec("SELECT importe FROM gastos WHERE (iva = "
               + QString::number(iva)
               + ") AND (date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) BETWEEN date('"
               + startDate.toString("yyyy-MM-dd") + "') AND date('"
               + endDate.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect()) {
            while(q.next()) {
                if (!q.value(0).toString().contains(","))
                    totalPrice = totalPrice + q.value(0).toFloat();
                else
                    QMessageBox::critical(nullptr, "Error en la base de datos",
                                          "En la tabla " + table + " "
                                          "se ha detectado que hay valores decimales guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                          QMessageBox::Ok, QMessageBox::Ok);
            }
        }
        else
            QMessageBox::critical(nullptr, "Error base de datos",
                                  "Acceso a la tabla da un error al usar totalPriceBetweenDates.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "totalPriceBetweenDates cannot work with different table.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return totalPrice;
}

int readLockForMonthAndYear(QSqlDatabase &db,
                            QString table,
                            int month,
                            int year)
{
    // For months 1 and 2 to not mix with 11 and 12
    QString monthFix;
    if (month == 1)
        monthFix = "01";
    else if (month == 2)
        monthFix = "02";
    else
        monthFix = QString::number(month);

    db.open();
    QSqlQuery q;
    int editLock = 2; // means that the search has not found any data
    if (table == "ingresos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha_pago like '%" + monthFix + "-" + QString::number(year) + "'");
    else if (table == "gastos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha like '%" + monthFix + "-" + QString::number(year) + "'");
    else
        QMessageBox::critical(nullptr, "Error leyendo el bloqueo de contabilidad",
                              "Tabla solicitada no está soportada por la función 'readLockForMonthAndYear'.",
                              QMessageBox::Ok, QMessageBox::Ok);
    if (q.isSelect()) {
        if(q.first())
            editLock = q.value(0).toInt();
        else
            editLock = 0;
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar readLockForMonthAndYear.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return editLock;
}

void updateLockInIngresos(QSqlDatabase &db,
                          int value,
                          int month,
                          int year)
{
    // For months 1 and 2 to not mix with 11 and 12
    QString monthFix;
    if (month == 1)
        monthFix = "01";
    else if (month == 2)
        monthFix = "02";
    else
        monthFix = QString::number(month);

    db.open();
    QSqlQuery q;
    q.exec("UPDATE ingresos SET edit_lock ="
           + QString::number(value)
           + " WHERE fecha_pago like '%"
           + monthFix + "-"
           + QString::number(year) + "'");
    q.clear();
    q.exec("UPDATE gastos SET edit_lock ="
           + QString::number(value)
           + " WHERE fecha like '%"
           + monthFix + "-"
           + QString::number(year) + "'");
    q.clear();
    db.close();
}

int updateComasInDecimalData(QSqlDatabase &db,
                             QString table,
                             QString item)
{
    int errorCnt = 0;
    if (table == "ingresos") {
        QStringList items = readColumnFromTable(db, item, table, "");
        QStringList ids_1 = readColumnFromTable(db, "n_recibo", table, "");
        QStringList ids_2 = readColumnFromTable(db, "hash", table, "");

        for (int fra = 0; fra < items.count(); fra++) {
            if (items[fra].contains(",")) {
                QSqlQuery q;
                db.open();
                q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE ("
                          "n_recibo = :id_1 AND "
                          "hash     = :id_2)");
                q.bindValue(":value", items[fra].replace(",","."));
                q.bindValue(":id_1", ids_1[fra]);
                q.bindValue(":id_2", ids_2[fra]);
                q.exec();
                db.close();
                errorCnt++;
            }
        }
    }
    else if (table == "gastos") {
        QStringList items = readColumnFromTable(db, item, table, "id");
        QStringList ids = readColumnFromTable(db, "id", table, "id");
        for (int fra = 0; fra < items.count(); fra++) {
            if (items[fra].contains(",")) {
                QSqlQuery q;
                db.open();
                q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE id = :id");
                q.bindValue(":value", items[fra].replace(",","."));
                q.bindValue(":id", ids[fra]);
                q.exec();
                db.close();
                errorCnt++;
            }
        }
    }
    else if (table == "prendas") {
        QStringList items = readColumnFromTable(db, item, table, "nombre");
        QStringList ids = readColumnFromTable(db, "nombre", table, "nombre");
        for (int prenda = 0; prenda < items.count(); prenda++) {
            if (items[prenda].contains(",")) {
                QSqlQuery q;
                db.open();
                q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE nombre = :id");
                q.bindValue(":value", items[prenda].replace(",","."));
                q.bindValue(":id", ids[prenda]);
                q.exec();
                db.close();
                errorCnt++;
            }
        }
    }
    else
        QMessageBox::critical(nullptr, "Error en updateComasInDecimalData",
                              "La tabla especificada en " + table + " no está soportada.",
                              QMessageBox::Ok, QMessageBox::Ok);
    return errorCnt;
}

void insertNewItemToTable(QSqlDatabase &db,
                          QStringList items,
                          QString table)
{
    db.open();
    QSqlQuery q;
    QString query;
    query = "INSERT INTO " + table + " VALUES (";
    for (int item = 0; item < items.count(); item++) {
        query += "'" + items.value(item) + "'";
        if (item == items.count() - 1)
            query += ");";
        else
            query += ", ";
    }
    q.exec(query);
    q.clear();
    db.close();
}

QString genHash16()
{
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    const int len = 16;
    QString tmp_s;
    tmp_s.reserve(len);

    for (int i = 0; i < len; ++i) {
        tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return tmp_s;
}
