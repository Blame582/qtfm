#include "themedialog.h"

#include "common.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

ThemeDialog::ThemeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Theme"));
    setModal(true);
    resize(350, 300);

    themeList = new QListWidget(this);
    applyButton = new QPushButton(tr("Apply"), this);
    QPushButton *closeButton = new QPushButton(tr("Close"), this);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(applyButton);
    buttons->addWidget(closeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(themeList);
    layout->addLayout(buttons);

    connect(applyButton,
            SIGNAL(clicked()),
            this,
            SLOT(applyTheme()));

    connect(closeButton,
            SIGNAL(clicked()),
            this,
            SLOT(reject()));

    connect(themeList,
            SIGNAL(currentRowChanged(int)),
            this,
            SLOT(selectTheme(int)));

    loadThemes();
}

void ThemeDialog::loadThemes()
{
    themeList->clear();

    QString themeDir = Common::configDir() + "/themes";
    QDir().mkpath(themeDir);

    QDir dir(themeDir);

    QStringList themes = dir.entryList(QStringList() << "*.theme",
                                       QDir::Files,
                                       QDir::Name);

    QString currentTheme = Common::readSetting("themefile",
                                                "default.theme").toString();

    for (int i = 0; i < themes.size(); ++i) {
        QString filename = themes.at(i);
        QString name = QFileInfo(filename).completeBaseName();

        QListWidgetItem *item = new QListWidgetItem(name);
        item->setData(Qt::UserRole, filename);

        themeList->addItem(item);

        if (filename == currentTheme) {
            themeList->setCurrentItem(item);
        }
    }

    if (themeList->count() > 0 && themeList->currentRow() < 0) {
        themeList->setCurrentRow(0);
    }
}

void ThemeDialog::selectTheme(int row)
{
    if (row < 0) {
        return;
    }

    QListWidgetItem *item = themeList->item(row);

    if (!item) {
        return;
    }

    QString filename = item->data(Qt::UserRole).toString();
    QString theme = Common::configDir() + "/themes/" + filename;

    qApp->setPalette(Common::loadTheme(theme));
}

void ThemeDialog::applyTheme()
{
    QListWidgetItem *item = themeList->currentItem();

    if (!item) {
        return;
    }

    QString filename = item->data(Qt::UserRole).toString();

    Common::writeSetting("themefile", filename);

    QString theme = Common::configDir() + "/themes/" + filename;

    qApp->setPalette(Common::loadTheme(theme));
}