#ifndef FILTERWIDGET_H
#define FILTERWIDGET_H

#include <QLineEdit>

QT_BEGIN_NAMESPACE
class QAction;
class QActionGroup;
QT_END_NAMESPACE

class FilterWidget : public QLineEdit
{
    Q_OBJECT
    Q_PROPERTY(Qt::CaseSensitivity caseSensitivity READ caseSensitivity)
    Q_PROPERTY(PatternSyntax patternSyntax READ patternSyntax)
public:
    explicit FilterWidget(QWidget *parent = nullptr);

    Qt::CaseSensitivity caseSensitivity() const;

    enum PatternSyntax {
        RegularExpression,
        Wildcard,
        FixedString
    };
    Q_ENUM(PatternSyntax)

    PatternSyntax patternSyntax() const;

signals:
    void filterChanged();

private:
    QAction *m_caseSensitivityAction;
    QActionGroup *m_patternGroup;
};

#endif // FILTERWIDGET_H
