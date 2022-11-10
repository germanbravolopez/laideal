#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QSqlDatabase>
#include <QSqlQuery>

int read_max_value_in_column_from_table(QSqlDatabase &db, QString column, QString table);
QStringList read_column_from_table(QSqlDatabase &db, QString column, QString table);
float read_garment_price(QSqlDatabase &db, QString garment, QString service);
QString search_item_from_client(QSqlDatabase &db, QString item, QString client);

#endif // SQL_LITE_H
