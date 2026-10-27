#ifndef FILTERWIDGET_H
#define FILTERWIDGET_H

#include <QLineEdit>

// The Listado search box: one mode, no options. What it matches is decided by
// MySortFilterProxyModel::setSearchText (accents and case ignored, every word must appear).
class FilterWidget : public QLineEdit
{
    Q_OBJECT
public:
    explicit FilterWidget(QWidget *parent = nullptr);

signals:
    void filterChanged();
};

#endif // FILTERWIDGET_H
