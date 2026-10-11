#include "themedialog.h"
#include "common.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QListWidget>
#include <QVBoxLayout>

ThemeDialog::ThemeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Theme"));
    setModal(true);
    resize(350, 300);

    themeList = new QListWidget(this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(themeList);

    connect(themeList, SIGNAL(currentRowChanged(int)),
            this, SLOT(selectTheme(int)));

    loadThemes();
}

void ThemeDialog::loadThemes()
{
    themeList->clear();

    QString themeDir = Common::configDir() + "/themes";
    QDir().mkpath(themeDir);

    QDir dir(themeDir);
    QStringList themes = dir.entryList(QStringList() << "*.theme",
                                       QDir::Files, QDir::Name);
    QString currentTheme = Common::readSetting("themefile",
                                                "default.theme").toString();

    for (int i = 0; i < themes.size(); ++i) {
        QString filename = themes.at(i);
        QListWidgetItem *item = new QListWidgetItem(
            QFileInfo(filename).completeBaseName());
        item->setData(Qt::UserRole, filename);
        themeList->addItem(item);

        if (filename == currentTheme)
            themeList->setCurrentItem(item);
    }

    if (themeList->count() > 0 && themeList->currentRow() < 0)
        themeList->setCurrentRow(0);
}

void ThemeDialog::selectTheme(int row)
{
    if (row < 0)
        return;

    QListWidgetItem *item = themeList->item(row);
    if (!item)
        return;

    QString filename = item->data(Qt::UserRole).toString();
    QString theme = Common::configDir() + "/themes/" + filename;

    qApp->setPalette(Common::loadTheme(theme));
    Common::writeSetting("themefile", filename);
    emit themeChanged(theme);
}