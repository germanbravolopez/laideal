#include "insertnewitem.h"
#include "sql_lite.h"

InsertNewItem::InsertNewItem(QWidget *parent)
    : QDialog{parent}
{
    setupUi(this);
}

void InsertNewItem::setupUi(QDialog *InsertNewItem)
{
    if (InsertNewItem->objectName().isEmpty())
        InsertNewItem->setObjectName("InsertNewItem");
    InsertNewItem->setWindowModality(Qt::WindowModal);
    InsertNewItem->resize(392, 165);
    QFont font;
    font.setPointSize(12);
    InsertNewItem->setFont(font);
    InsertNewItem->setModal(true);
    // grid layout
    gridLayoutWidget = new QWidget(InsertNewItem);
    gridLayoutWidget->setObjectName("gridLayoutWidget");
    gridLayoutWidget->setGeometry(QRect(10, 13, 371, 140));
    gridLayout = new QGridLayout(gridLayoutWidget);
    gridLayout->setObjectName("gridLayout");
    gridLayout->setContentsMargins(0, 0, 0, 0);
    // labels
    lbl_1 = new QLabel(gridLayoutWidget);
    lbl_1->setObjectName("lbl_1");
    lbl_1->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    gridLayout->addWidget(lbl_1, 0, 0, 1, 1);
    lbl_2 = new QLabel(gridLayoutWidget);
    lbl_2->setObjectName("lbl_2");
    lbl_2->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    gridLayout->addWidget(lbl_2, 1, 0, 1, 1);
    lbl_3 = new QLabel(gridLayoutWidget);
    lbl_3->setObjectName("lbl_3");
    lbl_3->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    gridLayout->addWidget(lbl_3, 2, 0, 1, 1);
    lbl_4 = new QLabel(gridLayoutWidget);
    lbl_4->setObjectName("lbl_4");
    lbl_4->setAlignment(Qt::AlignRight|Qt::AlignTrailing|Qt::AlignVCenter);
    gridLayout->addWidget(lbl_4, 3, 0, 1, 1);
    // line edits
    le_1 = new QLineEdit(gridLayoutWidget);
    le_1->setObjectName("le_1");
    gridLayout->addWidget(le_1, 0, 1, 1, 1);
    le_2 = new QLineEdit(gridLayoutWidget);
    le_2->setObjectName("le_2");
    gridLayout->addWidget(le_2, 1, 1, 1, 1);
    le_3 = new QLineEdit(gridLayoutWidget);
    le_3->setObjectName("le_3");
    gridLayout->addWidget(le_3, 2, 1, 1, 1);
    le_4 = new QLineEdit(gridLayoutWidget);
    le_4->setObjectName("le_4");
    gridLayout->addWidget(le_4, 3, 1, 1, 1);
    // button box
    bb_ok_cancel = new QDialogButtonBox(gridLayoutWidget);
    bb_ok_cancel->setObjectName("bb_ok_cancel");
    bb_ok_cancel->setOrientation(Qt::Horizontal);
    bb_ok_cancel->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);
    gridLayout->addWidget(bb_ok_cancel, 4, 0, 1, 3);

    retranslateUi(InsertNewItem);

    QObject::connect(bb_ok_cancel, &QDialogButtonBox::accepted, InsertNewItem, qOverload<>(&QDialog::accept));
    QObject::connect(bb_ok_cancel, &QDialogButtonBox::rejected, InsertNewItem, qOverload<>(&QDialog::reject));

    QMetaObject::connectSlotsByName(InsertNewItem);
} // setupUi

void InsertNewItem::retranslateUi(QDialog *InsertNewItem)
{
    InsertNewItem->setWindowTitle(QCoreApplication::translate("InsertNewItem", "Añadir nueva fila", nullptr));
    lbl_1->setText(QCoreApplication::translate("InsertNewItem", "Nombre:", nullptr));
    lbl_2->setText(QCoreApplication::translate("InsertNewItem", "Tlf. fijo:", nullptr));
    lbl_3->setText(QCoreApplication::translate("InsertNewItem", "Móvil:", nullptr));
    lbl_4->setText(QCoreApplication::translate("InsertNewItem", "Dirección:", nullptr));
} // retranslateUi

void InsertNewItem::on_bb_ok_cancel_accepted()
{
    insertNewItemToTable(db, {le_1->text(), le_2->text(), le_3->text(), le_4->text()}, "clientes");
} // on_bb_ok_cancel_accepted

void InsertNewItem::on_bb_ok_cancel_rejected()
{
    this->close();
} // on_bb_ok_cancel_rejected
