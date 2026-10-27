#include "filterwidget.h"

FilterWidget::FilterWidget(QWidget *parent)
    : QLineEdit(parent)
{
    setClearButtonEnabled(true);
    setPlaceholderText(tr("Buscar…"));
    setToolTip(tr("Sin distinguir tildes ni mayúsculas; con varias palabras, las filas que las contienen todas."));
    connect(this, &QLineEdit::textChanged, this, &FilterWidget::filterChanged);
}
