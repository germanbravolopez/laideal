#include "sql_lite.h"

int read_max_value_in_column_from_table(QSqlDatabase &db, QString column, QString table)
{
    int max_value = 0;
    db.open();
    QSqlQuery q;
    q.exec("SELECT MAX(" + column + ") FROM " + table);
    if (q.isSelect())
    {
        if (q.first())
            max_value = q.value(0).toInt();
        else
            qDebug() << "Query is not available!";
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    return max_value;
}

QStringList read_column_from_table(QSqlDatabase &db, QString column, QString table)
{
    db.open();
    QSqlQuery q;
    QStringList list;
    q.exec("SELECT " + column + " FROM " + table);
    if (q.isSelect())
    {
        while(q.next())
            list.append(q.value(0).toString());
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    return list;
}

float read_garment_price(QSqlDatabase &db, QString garment, QString service)
{
    float price = 0.0;
    db.open();
    QSqlQuery q;
    if (service == "Limp.")
        q.exec("SELECT precio_limpieza FROM prendas WHERE nombre LIKE '" + garment + "'");
    else if (service == "Plan.")
        q.exec("SELECT precio_plancha FROM prendas WHERE nombre LIKE '" + garment + "'");
    if (q.isSelect())
    {
        if (q.first())
            price = q.value(0).toString().toFloat();
        else
            qDebug() << "Query is not available.";
    }
    else
        qDebug() << "Query is not Select.";
    q.clear();
    db.close();
    return price;
}

QString select_from_where_like(QSqlDatabase &db, QString item_to_get, QString table, QString column_to_search, QString item_to_search, bool exact_match)
{
    QString item_to_search_text;
    db.open();
    QSqlQuery q;
    if (exact_match)
        q.exec("SELECT " + item_to_get + " FROM " + table + " WHERE " + column_to_search + " like '" + item_to_search + "'");
    else
        q.exec("SELECT " + item_to_get + " FROM " + table + " WHERE " + column_to_search + " like '" + item_to_search + "%'");
    if (q.isSelect())
    {
        if (q.first())
            item_to_search_text = q.value(0).toString();
        else
            qDebug() << "Item is not found in the database.";
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    return item_to_search_text;
}

QString search_item_from_client(QSqlDatabase &db, QString item, QString client)
{
    return select_from_where_like(db, item, "clientes", "nombre", client, false);
}

bool update_item_to_client(QSqlDatabase &db, QString column, QString item, QString client)
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

bool add_new_client(QSqlDatabase &db, QString client, QString tel_fijo, QString direccion, QString movil)
{
    QSqlQuery q;
    db.open();
    q.prepare("INSERT INTO clientes (nombre, tel_fijo, direccion, movil) \
    VALUES(:nombre, :tel_fijo, :direccion, :movil)");
    q.bindValue(":nombre", client);
    q.bindValue(":tel_fijo", tel_fijo);
    q.bindValue(":direccion", direccion);
    q.bindValue(":movil", movil);
    bool ok = q.exec();
    db.close();
    return ok;
}

double total_price_between_dates(QSqlDatabase &db, QString table, QDate start_date, QDate end_date, int iva)
{
    db.open();
    QSqlQuery q;
    double total_price = 0.0;
    if (table == "ingresos")
    {
        q.exec("SELECT importe FROM ingresos WHERE (pagado = 'SI') AND "
               "(date(substr(fecha_pago,7,4)||'-'||substr(fecha_pago,4,2)||'-'||substr(fecha_pago,1,2)) BETWEEN date('"
               + start_date.toString("yyyy-MM-dd") + "') AND date('"
               + end_date.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect())
        {
            while(q.next())
                total_price = total_price + q.value(0).toFloat();
        }
        else
            qDebug() << "Query is not Select!";
    }
    else if (table == "gastos")
    {
        q.exec("SELECT importe FROM gastos WHERE (iva = "
               + QString::number(iva)
               + ") AND (date(substr(fecha,7,4)||'-'||substr(fecha,4,2)||'-'||substr(fecha,1,2)) BETWEEN date('"
               + start_date.toString("yyyy-MM-dd") + "') AND date('"
               + end_date.toString("yyyy-MM-dd") + "'))");
        if (q.isSelect())
        {
            while(q.next())
                total_price = total_price + q.value(0).toFloat();
        }
        else
            qDebug() << "Query is not Select!";
    }
    else
        qDebug() << "total_price_between_dates cannot work with different table";
    q.clear();
    db.close();
    return total_price;
}

int read_lock_for_trim_and_year(QSqlDatabase &db, int trim, int year)
{
    db.open();
    QSqlQuery q;
    int edit_lock = 2; // means that the search has not found any data
    int month = trim * 3;
    q.exec("SELECT edit_lock FROM ingresos WHERE fecha_pago like '%"
           + QString::number(month) + "-"
           + QString::number(year) + "'");
    if (q.isSelect())
    {
        if(q.first())
            edit_lock = q.value(0).toInt();
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    return edit_lock;
}

void update_lock_in_ingresos(QSqlDatabase &db, int value, int month, int year)
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
