#ifndef SQL_LITE_H
#define SQL_LITE_H

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDate>
#include <QMessageBox>

#define DB_PATH "C:/Users/rocio/OneDrive/Desktop/Tintoreria/BaseDatos/laideal.db"

int readMaxValueInColumnFromTable(QSqlDatabase &db, QString column, QString table);
int readMaxNMinYearInColumnFromTable(QSqlDatabase &db, bool maxNMin, QString column, QString table);
QStringList readColumnFromTable(QSqlDatabase &db, QString column, QString table, QString orderByColumn);
float readGarmentPrice(QSqlDatabase &db, QString garment, QString service);
QString selectFromWhereLike(QSqlDatabase &db, QString itemToGet, QString table, QString columnToSearch, QString itemToSearch, bool exactMatch, bool printMsg);
QString searchItemFromClient(QSqlDatabase &db, QString item, QString client, bool printMsg);
bool updateItemToClient(QSqlDatabase &db, QString column, QString item, QString client);
bool addNewClient(QSqlDatabase &db, QString client, QString telFijo, QString direccion, QString movil);
float totalPriceBetweenDates(QSqlDatabase &db, QString table, QDate startDate, QDate endDate, int iva);
int readLockForMonthAndYear(QSqlDatabase &db, QString table, int month, int year);
void updateLockInIngresos(QSqlDatabase &db, int value, int month, int year);
int updateComasInDecimalData(QSqlDatabase &db, QString table, QString item);
void insertNewItemToTable(QSqlDatabase &db, QStringList items, QString table);
QString genHash16();

#endif // SQL_LITE_H
