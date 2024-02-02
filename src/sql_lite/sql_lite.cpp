#include "sql_lite.h"

int read_max_value_in_column_from_table(QSqlDatabase &db,
                                        QString column,
                                        QString table)
{
    int max_value = 0;
    db.open();
    QSqlQuery q;
    q.exec("SELECT MAX(" + column + ") FROM " + table);
    if (q.isSelect()) {
        if (q.first())
            max_value = q.value(0).toInt();
        else
            QMessageBox::warning(nullptr, "Error base de datos",
                                  "Búsqueda vacía al usar read_max_value_in_column_from_table.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar read_max_value_in_column_from_table.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return max_value;
}

int read_max_n_min_year_in_column_from_table(QSqlDatabase &db,
                                             bool max_n_min,
                                             QString column,
                                             QString table)
{
    int value = 0;
    QString query;
    if (max_n_min)
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
                                  "Búsqueda vacía al usar read_max_n_min_year_in_column_from_table.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar read_max_n_min_year_in_column_from_table.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return value;
}

QStringList read_column_from_table(QSqlDatabase &db,
                                   QString column,
                                   QString table,
                                   QString order_by_column)
{
    db.open();
    QSqlQuery q;
    QStringList list;
    if (order_by_column == "")
        q.exec("SELECT " + column + " FROM " + table + ";");
    else
        q.exec("SELECT " + column + " FROM " + table + " ORDER BY " + order_by_column + " ASC;");
    if (q.isSelect()) {
        while(q.next())
            list.append(q.value(0).toString());
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar read_column_from_table.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return list;
}

float read_garment_price(QSqlDatabase &db,
                         QString garment,
                         QString service)
{
    float price = 0.0;
    db.open();
    QSqlQuery q;
    if (service == "Limp.")
        q.exec("SELECT precio_limpieza FROM prendas WHERE nombre LIKE '" + garment + "'");
    else if (service == "Plan.")
        q.exec("SELECT precio_plancha FROM prendas WHERE nombre LIKE '" + garment + "'");
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
                                  "' al usar read_garment_price para el servicio '" + service + "'.\n"
                                  "Añadir un precio en el listado de prendas antes de continuar con esta acción.",
                                  QMessageBox::Ok, QMessageBox::Ok);
            price = -1;
        }
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar read_garment_price.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return price;
}

QString select_from_where_like(QSqlDatabase &db,
                               QString item_to_get,
                               QString table,
                               QString column_to_search,
                               QString item_to_search,
                               bool exact_match,
                               bool print_msg)
{
    QString item_to_search_text;
    db.open();
    QSqlQuery q;
    if (exact_match)
        q.exec("SELECT " + item_to_get + " FROM " + table + " WHERE " + column_to_search + " like '" + item_to_search + "'");
    else
        q.exec("SELECT " + item_to_get + " FROM " + table + " WHERE " + column_to_search + " like '" + item_to_search + "%'");
    if (q.isSelect()) {
        if (q.first())
            item_to_search_text = q.value(0).toString();
        else if (print_msg)
            QMessageBox::warning(nullptr, "Búsqueda vacía",
                                  "El elemento '" + item_to_search + "' no se ha encontrado en la base de datos para '" + column_to_search + "'.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else if (print_msg)
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar select_from_where_like.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return item_to_search_text;
}

QString search_item_from_client(QSqlDatabase &db,
                                QString item,
                                QString client,
                                bool print_msg)
{
    return select_from_where_like(db, item, "clientes", "nombre", client, false, print_msg);
}

bool update_item_to_client(QSqlDatabase &db,
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

bool add_new_client(QSqlDatabase &db,
                    QString client,
                    QString tel_fijo,
                    QString direccion,
                    QString movil)
{
    QSqlQuery q;
    db.open();
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) VALUES(:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre", client);
    q.bindValue(":tel_fijo", tel_fijo);
    q.bindValue(":direccion", direccion);
    q.bindValue(":movil", movil);
    bool ok = q.exec();
    db.close();
    return ok;
}

float total_price_between_dates(QSqlDatabase &db,
                                QString table,
                                QDate start_date,
                                QDate end_date,
                                int iva)
{
    db.open();
    QSqlQuery q;
    float total_price = 0.0;
    if (table == "ingresos") {
        q.exec("SELECT importe FROM ingresos WHERE (pagado = 'SI') AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) >= date('"
               + start_date.toString("yyyy-MM-dd") + "')) AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) < date('"
               + end_date.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect()) {
            while(q.next()) {
                if (!q.value(0).toString().contains(","))
                    total_price = total_price + q.value(0).toFloat();
                else
                    QMessageBox::critical(nullptr, "Error en la base de datos",
                                          "En la tabla " + table + " "
                                          "se ha detectado que hay valores decimales guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                          QMessageBox::Ok, QMessageBox::Ok);
            }
        }
        else
            QMessageBox::critical(nullptr, "Error base de datos",
                                  "Acceso a la tabla da un error al usar total_price_between_dates.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else if (table == "gastos") {
        q.exec("SELECT importe FROM gastos WHERE (iva = "
               + QString::number(iva)
               + ") AND (date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) BETWEEN date('"
               + start_date.toString("yyyy-MM-dd") + "') AND date('"
               + end_date.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect()) {
            while(q.next()) {
                if (!q.value(0).toString().contains(","))
                    total_price = total_price + q.value(0).toFloat();
                else
                    QMessageBox::critical(nullptr, "Error en la base de datos",
                                          "En la tabla " + table + " "
                                          "se ha detectado que hay valores decimales guardados con ','. Utilizar herramienta de limpiado de importes decimales.",
                                          QMessageBox::Ok, QMessageBox::Ok);
            }
        }
        else
            QMessageBox::critical(nullptr, "Error base de datos",
                                  "Acceso a la tabla da un error al usar total_price_between_dates.",
                                  QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "total_price_between_dates cannot work with different table.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return total_price;
}

int read_lock_for_month_and_year(QSqlDatabase &db,
                                 QString table,
                                 int month,
                                 int year)
{
    // For months 1 and 2 to not mix with 11 and 12
    QString month_fix;
    if (month == 1)
        month_fix = "01";
    else if (month == 2)
        month_fix = "02";
    else
        month_fix = QString::number(month);

    db.open();
    QSqlQuery q;
    int edit_lock = 2; // means that the search has not found any data
    if (table == "ingresos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha_pago like '%" + month_fix + "-" + QString::number(year) + "'");
    else if (table == "gastos")
        q.exec("SELECT edit_lock FROM " + table + " WHERE fecha like '%" + month_fix + "-" + QString::number(year) + "'");
    else
        QMessageBox::critical(nullptr, "Error leyendo el bloqueo de contabilidad",
                              "Tabla solicitada no está soportada por la función 'read_lock_for_month_and_year'.",
                              QMessageBox::Ok, QMessageBox::Ok);
    if (q.isSelect()) {
        if(q.first())
            edit_lock = q.value(0).toInt();
        else
            edit_lock = 0;
    }
    else
        QMessageBox::critical(nullptr, "Error base de datos",
                              "Acceso a la tabla da un error al usar read_lock_for_month_and_year.",
                              QMessageBox::Ok, QMessageBox::Ok);
    q.clear();
    db.close();
    return edit_lock;
}

void update_lock_in_ingresos(QSqlDatabase &db,
                             int value,
                             int month,
                             int year)
{
    // For months 1 and 2 to not mix with 11 and 12
    QString month_fix;
    if (month == 1)
        month_fix = "01";
    else if (month == 2)
        month_fix = "02";
    else
        month_fix = QString::number(month);

    db.open();
    QSqlQuery q;
    q.exec("UPDATE ingresos SET edit_lock ="
           + QString::number(value)
           + " WHERE fecha_pago like '%"
           + month_fix + "-"
           + QString::number(year) + "'");
    q.clear();
    q.exec("UPDATE gastos SET edit_lock ="
           + QString::number(value)
           + " WHERE fecha like '%"
           + month_fix + "-"
           + QString::number(year) + "'");
    q.clear();
    db.close();
}

int update_comas_in_decimal_data(QSqlDatabase &db,
                                 QString table,
                                 QString item)
{
    int error_cnt = 0;
    if (table == "ingresos") {
        QStringList items = read_column_from_table(db, item, table, "");
        QStringList ids_1 = read_column_from_table(db, "n_recibo", table, "");
        QStringList ids_2 = read_column_from_table(db, "hash", table, "");

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
                error_cnt++;
            }
        }
    }
    else if (table == "gastos") {
        QStringList items = read_column_from_table(db, item, table, "id");
        QStringList ids = read_column_from_table(db, "id", table, "id");
        for (int fra = 0; fra < items.count(); fra++) {
            if (items[fra].contains(",")) {
                QSqlQuery q;
                db.open();
                q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE id = :id");
                q.bindValue(":value", items[fra].replace(",","."));
                q.bindValue(":id", ids[fra]);
                q.exec();
                db.close();
                error_cnt++;
            }
        }
    }
    else if (table == "prendas") {
        QStringList items = read_column_from_table(db, item, table, "nombre");
        QStringList ids = read_column_from_table(db, "nombre", table, "nombre");
        for (int prenda = 0; prenda < items.count(); prenda++) {
            if (items[prenda].contains(",")) {
                QSqlQuery q;
                db.open();
                q.prepare("UPDATE " + table + " SET " + item + " = :value WHERE nombre = :id");
                q.bindValue(":value", items[prenda].replace(",","."));
                q.bindValue(":id", ids[prenda]);
                q.exec();
                db.close();
                error_cnt++;
            }
        }
    }
    else
        QMessageBox::critical(nullptr, "Error en update_comas_in_decimal_data",
                              "La tabla especificada en " + table + " no está soportada.",
                              QMessageBox::Ok, QMessageBox::Ok);
    return error_cnt;
}

void insert_new_item_to_table(QSqlDatabase &db,
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

QString gen_hash_16()
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
