#ifndef THEMEDIALOG_H
#define THEMEDIALOG_H

#include <QDialog>

class QListWidget;
class QPushButton;

class ThemeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeDialog(QWidget *parent = nullptr);

private slots:
    void applyTheme();
    void selectTheme(int row);

private:
    void loadThemes();

    QListWidget *themeList;
    QPushButton *applyButton;
};

#endif