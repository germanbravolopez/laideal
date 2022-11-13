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

QString search_item_from_client(QSqlDatabase &db, QString item, QString client)
{
    QString item_text;
    db.open();
    QSqlQuery q;
    q.exec("SELECT " + item + " FROM clientes WHERE nombre LIKE '" + client + "%'");
    if (q.isSelect())
    {
        if (q.first())
            item_text = q.value(0).toString();
        else
            qDebug() << "Client is not found in the database.";
    }
    else
        qDebug() << "Query is not Select!";
    q.clear();
    db.close();
    return item_text;
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
