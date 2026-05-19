#ifndef INSERTNEWITEM_H
#define INSERTNEWITEM_H

#include <QWidget>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QDialog>
#include <QCoreApplication>
#include <QSqlDatabase>

class InsertNewItem : public QDialog
{
    Q_OBJECT
public:
    explicit InsertNewItem(QWidget *parent = nullptr);
    // UI
    QWidget *gridLayoutWidget;
    QGridLayout *gridLayout;
    QLabel *lbl_1, *lbl_2, *lbl_3, *lbl_4;
    QLineEdit *le_1, *le_2, *le_3, *le_4;
    QDialogButtonBox *bb_ok_cancel;
    // database
    QSqlDatabase db;
    // setup UI
    void setupUi(QDialog *InsertNewItem);
    void retranslateUi(QDialog *InsertNewItem);

private slots:
    // class functions
    void on_bb_ok_cancel_accepted();
    void on_bb_ok_cancel_rejected();

};

#endif // INSERTNEWITEM_H
