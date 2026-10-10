#ifndef INSERTNEWITEM_H
#define INSERTNEWITEM_H

#include <QDialog>
#include <QSqlDatabase>

class QLineEdit;
namespace UiKit { class ResultPanel; }

// Listado de clientes -> Añadir fila: a new client (name, phones, address). Built
// in code with the shared UiKit style; closes itself once the client is saved.
class InsertNewItem : public QDialog
{
    Q_OBJECT
public:
    explicit InsertNewItem(const QSqlDatabase &database, QWidget *parent = nullptr);

private slots:
    void onSaveClicked();

private:
    QSqlDatabase db;
    QLineEdit *m_leName = nullptr;
    QLineEdit *m_lePhone = nullptr;
    QLineEdit *m_leMobile = nullptr;
    QLineEdit *m_leAddress = nullptr;
    UiKit::ResultPanel *m_lblResult = nullptr;
};

#endif // INSERTNEWITEM_H
