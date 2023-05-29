#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDate>
#include <QMessageBox>

#define DB_PATH "C:/Users/Usuario/OneDrive/Desktop/Tintoreria/BaseDatos/laideal.db"

int read_max_value_in_column_from_table(
            QSqlDatabase &db,
            QString      column,
            QString      table
        );
QStringList read_column_from_table(
            QSqlDatabase &db,
            QString      column,
            QString      table
        );
float read_garment_price(
            QSqlDatabase &db,
            QString      garment,
            QString      service
        );
QString select_from_where_like(
            QSqlDatabase &db,
            QString      item_to_get,
            QString      table,
            QString      column_to_search,
            QString      item_to_search,
            bool         exact_match
        );
QString search_item_from_client(
            QSqlDatabase &db,
            QString      item,
            QString      client
        );
bool update_item_to_client(
            QSqlDatabase &db,
            QString      column,
            QString      item,
            QString      client
        );
bool add_new_client(
            QSqlDatabase &db,
            QString      client,
            QString      tel_fijo,
            QString      direccion,
            QString      movil
        );
float total_price_between_dates(
            QSqlDatabase &db,
            QString      table,
            QDate        start_date,
            QDate        end_date,
            int          iva
        );
int read_lock_for_month_and_year(
            QSqlDatabase &db,
            int          month,
            int          year
        );
void update_lock_in_ingresos(
            QSqlDatabase &db,
            int          value,
            int          month,
            int          year
        );
int update_comas_in_decimal_data(
            QSqlDatabase &db,
            QString table,
            QString item
        );

#endif // SQL_LITE_H
