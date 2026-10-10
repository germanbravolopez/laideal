#include "insertnewitem.h"
#include "sql_lite.h"
#include "uikit.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

InsertNewItem::InsertNewItem(const QSqlDatabase &database, QWidget *parent)
    : QDialog{parent}, db(database)
{
    setWindowModality(Qt::WindowModal);
    UiKit::setUpDialog(this, "Nuevo cliente", 440);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(UiKit::introPanel("Añade un cliente al listado. Solo el nombre es obligatorio."));

    QGroupBox *grpClient = new QGroupBox("Cliente");
    QFormLayout *form = new QFormLayout(grpClient);
    m_leName = new QLineEdit();
    m_leName->setObjectName("leName");   // stable names for the e2e test bench
    form->addRow("Nombre:", m_leName);
    m_lePhone = new QLineEdit();
    m_lePhone->setObjectName("lePhone");
    form->addRow("Teléfono fijo:", m_lePhone);
    m_leMobile = new QLineEdit();
    m_leMobile->setObjectName("leMobile");
    form->addRow("Móvil:", m_leMobile);
    m_leAddress = new QLineEdit();
    m_leAddress->setObjectName("leAddress");
    form->addRow("Dirección:", m_leAddress);
    layout->addWidget(grpClient);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addStretch();
    QPushButton *btnSave = UiKit::primaryButton("Guardar cliente", "btnSave");
    actions->addWidget(btnSave);
    layout->addLayout(actions);
    m_lblResult = new UiKit::ResultPanel("Rellene los datos y pulse \"Guardar cliente\".");
    layout->addWidget(m_lblResult);
    layout->addLayout(UiKit::closeRow(this, "Cancelar"));

    connect(btnSave, &QPushButton::clicked, this, &InsertNewItem::onSaveClicked);
}

void InsertNewItem::onSaveClicked()
{
    const QString name = m_leName->text().trimmed();
    if (name.isEmpty()) {
        m_lblResult->setText(UiKit::errorHtml("Introduzca el nombre del cliente."));
        m_leName->setFocus();
        return;
    }
    if (!insertNewItemToTable(db, { name, m_lePhone->text().trimmed(), m_leMobile->text().trimmed(),
                                    m_leAddress->text().trimmed() }, "clientes")) {
        m_lblResult->setText(UiKit::errorHtml("No se pudo guardar el cliente.")
                             + "<br>Consulte el log (Ayuda → Mostrar log).");
        return;
    }
    accept();
}
