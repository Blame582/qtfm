#ifndef THEMEDIALOG_H
#define THEMEDIALOG_H

#include <QDialog>

class QListWidget;

class ThemeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ThemeDialog(QWidget *parent = nullptr);

private slots:
    void selectTheme(int row);

private:
    void loadThemes();
    QListWidget *themeList;
    
signals:
    void themeChanged(const QString &filename);   
};

#endif