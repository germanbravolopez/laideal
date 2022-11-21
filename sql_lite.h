#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QSqlDatabase>
#include <QSqlQuery>

#define DB_PATH "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/BaseDatos/laideal.db"

int read_max_value_in_column_from_table(QSqlDatabase &db, QString column, QString table);
QStringList read_column_from_table(QSqlDatabase &db, QString column, QString table);
float read_garment_price(QSqlDatabase &db, QString garment, QString service);
QString search_item_from_client(QSqlDatabase &db, QString item, QString client);
QString select_from_where_like(QSqlDatabase &db, QString item_to_get, QString table, QString column_to_search, QString item_to_search, bool exact_match);

#endif // SQL_LITE_H
