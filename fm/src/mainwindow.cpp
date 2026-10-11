/****************************************************************************
* This file is part of qtFM, a simple, fast file manager.
* Copyright (C) 2010,2011,2012 Wittfella
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*
* Contact e-mail: wittfella@qtfm.org
*
****************************************************************************/

#include <QtGui>
#include <QDockWidget>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include <QApplication>
#include <QStatusBar>
#include <QMenu>
#include <QMenuBar>
#include <QThreadPool>
#include <QGraphicsDropShadowEffect>

#ifndef NO_DBUS
#include <QDBusConnection>
#include <QDBusError>
#endif

#include <fcntl.h>

#include <QtConcurrent/QtConcurrent>

#include "mainwindow.h"
#include "mymodel.h"
#include "progressdlg.h"
#include "fileutils.h"
#include "applicationdialog.h"
#include "themedialog.h"
#include "common.h"

#ifdef Q_OS_MAC
#include <QStyleFactory>
#endif

MainWindow::MainWindow()
{	activePane = 1;

    // setup icon theme search path
    QStringList iconsPath = QIcon::themeSearchPaths();
    QString iconsHomeLocal = QString("%1/.local/share/icons").arg(QDir::homePath());
    QString iconsHome = QString("%1/.icons").arg(QDir::homePath());

    if (QFile::exists(iconsHomeLocal) && !iconsPath.contains(iconsHomeLocal)) {
        iconsPath.prepend(iconsHomeLocal);
    }

    if (QFile::exists(iconsHome) && !iconsPath.contains(iconsHome)) {
        iconsPath.prepend(iconsHome);
    }

    iconsPath << QString("%1/../share/icons").arg(qApp->applicationDirPath());
    QIcon::setThemeSearchPaths(iconsPath);
    qDebug() << "using icon theme search path" << QIcon::themeSearchPaths();

    // libdisks
#ifndef NO_UDISKS
    disks = new Disks(this);
    connect(disks, SIGNAL(updatedDevices()), this, SLOT(populateMedia()));
    connect(disks, SIGNAL(mountpointChanged(QString,QString)),
            this, SLOT(handleMediaMountpointChanged(QString,QString)));
    connect(disks, SIGNAL(foundNewDevice(QString)),
            this, SLOT(handleMediaAdded(QString)));
    connect(disks, SIGNAL(removedDevice(QString)),
            this, SLOT(handleMediaRemoved(QString)));
    connect(disks, SIGNAL(mediaChanged(QString,bool)),
            this, SLOT(handleMediaChanged(QString,bool)));
    connect(disks, SIGNAL(deviceErrorMessage(QString,QString)),
            this, SLOT(handleMediaError(QString,QString)));
#endif

    // dbus service
#ifndef NO_DBUS
    if (QDBusConnection::sessionBus().isConnected()) {
        if (QDBusConnection::sessionBus().registerService(FM_SERVICE)) {
            service = new qtfm(this);
            connect(service, SIGNAL(pathRequested(QString)),
                    this, SLOT(handlePathRequested(QString)));

            if (!QDBusConnection::sessionBus().registerObject(
                    FM_PATH, service, QDBusConnection::ExportAllSlots)) {
                qWarning() << QDBusConnection::sessionBus().lastError().message();
            }
        } else {
            qWarning() << QDBusConnection::sessionBus().lastError().message();
        }
    }
#endif

    // get path from cmd
    startPath = QDir::currentPath();
    QStringList args = QApplication::arguments();

    if (args.count() > 1) {
        startPath = args.at(1);

        if (startPath == ".") {
            startPath = getenv("PWD");
        } else if (QUrl(startPath).isLocalFile()) {
            startPath = QUrl(args.at(1)).toLocalFile();
        }
    }

    panePath1 = startPath;
    panePath2 = startPath;

    settings = new QSettings(Common::configFile(), QSettings::IniFormat);

    if (settings->value("clearCache").toBool()) {
        qDebug() << "clear cache";
        Common::removeFileCache();
        Common::removeFolderCache();
        Common::removeThumbsCache();
        settings->setValue("clearCache", false);
    }

    if (!settings->contains("themefile")) {
        settings->setValue("themefile", "default.theme");
    }

    // Theme
    QString themeDir = Common::configDir() + "/themes";
    QDir().mkpath(themeDir);

    QString defaultTheme = themeDir + "/default.theme";

    if (!QFile::exists(defaultTheme)) {
        QString systemTheme = Common::systemThemeFile();

        if (!systemTheme.isEmpty()) {
            QFile::copy(systemTheme, defaultTheme);
        }
    }

    QString theme = Common::themeFile();

    if (!QFile::exists(theme)) {
        theme = defaultTheme;
    }

    qApp->setPalette(Common::loadTheme(theme));

    // set icon theme
#ifdef Q_OS_MAC
#ifdef DEPLOY
    QIcon::setThemeName("Breeze");
    qApp->setStyle(QStyleFactory::create("fusion"));
#else
    Common::setupIconTheme(qApp->applicationFilePath());
#endif
#else
    Common::setupIconTheme(qApp->applicationFilePath());
#endif

    // Create mime utils
    mimeUtils = new MimeUtils(this);
    QString name = settings->value("defMimeAppsFile", MIME_APPS).toString();
    mimeUtils->setDefaultsFileName(name);

    // Create filesystem model
    bool realMime = settings->value("realMimeTypes", true).toBool();
    modelList = new myModel(realMime, mimeUtils, this);
    modelList->setTheme(theme);
    connect(modelList, SIGNAL(reloadDir(QString)),
            this, SLOT(handleReloadDir(QString)));

    dockTree = new QDockWidget(tr("Tree"), this, Qt::SubWindow);
    dockTree->setObjectName("treeDock");

    tree = new QTreeView(dockTree);
    dockTree->setWidget(tree);
    addDockWidget(Qt::LeftDockWidgetArea, dockTree);

    dockBookmarks = new QDockWidget(tr("Bookmarks"), this, Qt::SubWindow);
    dockBookmarks->setObjectName("bookmarksDock");
    dockBookmarks->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    bookmarksList = new QListView(dockBookmarks);
    bookmarksList->setMinimumHeight(24);
    bookmarksList->setFocusPolicy(Qt::ClickFocus);
    dockBookmarks->setWidget(bookmarksList);
    addDockWidget(Qt::LeftDockWidgetArea, dockBookmarks);

    QWidget *main = new QWidget;
    mainLayout = new QVBoxLayout(main);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0,0,0,0);

    folderSplitter = new QSplitter(Qt::Horizontal, main);
    folderSplitter->setChildrenCollapsible(false);

    QWidget *folderPane1 = new QWidget;
    QVBoxLayout *folderLayout1 = new QVBoxLayout(folderPane1);
    folderLayout1->setSpacing(0);
    folderLayout1->setContentsMargins(0,0,0,0);

    stackWidget = new QStackedWidget(folderPane1);

    QWidget *page = new QWidget;
    QHBoxLayout *hl1 = new QHBoxLayout(page);
    hl1->setSpacing(0);
    hl1->setContentsMargins(0,0,0,0);

    list = new QListView(page);
    hl1->addWidget(list);
    stackWidget->addWidget(page);

    QWidget *page2 = new QWidget;
    hl1 = new QHBoxLayout(page2);
    hl1->setSpacing(0);
    hl1->setContentsMargins(0,0,0,0);

    detailTree = new QTreeView(page2);
    hl1->addWidget(detailTree);
    stackWidget->addWidget(page2);

    tabs = new tabBar(modelList->folderIcons);

    folderLayout1->addWidget(stackWidget);
    folderLayout1->addWidget(tabs);

    QWidget *folderPane2 = new QWidget;
    QVBoxLayout *folderLayout2 = new QVBoxLayout(folderPane2);
    folderLayout2->setSpacing(0);
    folderLayout2->setContentsMargins(0,0,0,0);

    stackWidget2 = new QStackedWidget(folderPane2);

    QWidget *page3 = new QWidget;
    QHBoxLayout *hl2 = new QHBoxLayout(page3);
    hl2->setSpacing(0);
    hl2->setContentsMargins(0,0,0,0);

    list2 = new QListView(page3);
    hl2->addWidget(list2);
    stackWidget2->addWidget(page3);

    QWidget *page4 = new QWidget;
    hl2 = new QHBoxLayout(page4);
    hl2->setSpacing(0);
    hl2->setContentsMargins(0,0,0,0);

    detailTree2 = new QTreeView(page4);
    hl2->addWidget(detailTree2);
    stackWidget2->addWidget(page4);

    tabs2 = new tabBar(modelList->folderIcons);

    folderLayout2->addWidget(stackWidget2);
    folderLayout2->addWidget(tabs2);

    folderSplitter->addWidget(folderPane1);
    folderSplitter->addWidget(folderPane2);
    folderSplitter->setSizes(QList<int>() << 1 << 0);
    folderPane2->hide();
    
    updateActivePaneFrame();

    mainLayout->addWidget(folderSplitter);

    setCentralWidget(main);

    modelTree = new mainTreeFilterProxyModel();
    modelTree->setSourceModel(modelList);
    modelTree->setSortCaseSensitivity(Qt::CaseInsensitive);

    tree->setHeaderHidden(true);
    tree->setUniformRowHeights(true);
    tree->setModel(modelTree);
    tree->hideColumn(1);
    tree->hideColumn(2);
    tree->hideColumn(3);
    tree->hideColumn(4);

    modelView = new viewsSortProxyModel();
    modelView->setSourceModel(modelList);
    modelView->setSortCaseSensitivity(Qt::CaseInsensitive);

    list->setWrapping(true);
    list->setWordWrap(true);
    list->setModel(modelView);
    ivdelegate = new IconViewDelegate();
    ildelegate = new IconListDelegate();
    list->setTextElideMode(Qt::ElideNone);
    listSelectionModel = list->selectionModel();

    detailTree->setRootIsDecorated(false);
    detailTree->setItemsExpandable(false);
    detailTree->setUniformRowHeights(true);
    detailTree->setModel(modelView);
    detailTree->setSelectionModel(listSelectionModel);

    list2->setWrapping(true);
    list2->setWordWrap(true);
    list2->setModel(modelView);
    list2->setTextElideMode(Qt::ElideNone);
    listSelectionModel2 = list2->selectionModel();

    detailTree2->setRootIsDecorated(false);
    detailTree2->setItemsExpandable(false);
    detailTree2->setUniformRowHeights(true);
    detailTree2->setModel(modelView);
    detailTree2->setSelectionModel(listSelectionModel2);

    pathEdit = new QComboBox();
    pathEdit->setEditable(true);
    pathEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    pathEdit->setMinimumWidth(100);

    status = statusBar();
    status->setSizeGripEnabled(true);
    statusName = new QLabel();
    statusSize = new QLabel();
    statusDate = new QLabel();

    status->addPermanentWidget(statusName);
    status->addPermanentWidget(statusSize);
    status->addPermanentWidget(statusDate);

    treeSelectionModel = tree->selectionModel();

    connect(treeSelectionModel,
            SIGNAL(currentChanged(QModelIndex,QModelIndex)),
            this,
            SLOT(treeSelectionChanged(QModelIndex,QModelIndex)));

    tree->setCurrentIndex(
        modelTree->mapFromSource(
            modelList->index(startPath)));

    tree->scrollTo(tree->currentIndex());

    createActions();
    createToolBars();
    createMenus();

    setWindowIcon(
        QIcon::fromTheme(
            "qtfm",
            QIcon(":/images/qtfm.png")));

    setWindowTitle(APP_NAME);

    // Create custom action manager
    customActManager =
        new CustomActionsManager(
            settings,
            actionList,
            this);

    // Create bookmarks model
    modelBookmarks =
        new bookmarkmodel(modelList->folderIcons);

    connect(modelBookmarks,
            SIGNAL(bookmarksChanged()),
            this,
            SLOT(handleBookmarksChanged()));

    // Load settings before showing window
    loadSettings();

    // show window
    show();

    trashDir = Common::trashDir();
    ignoreReload = false;

    qApp->installEventFilter(this);

    QTimer::singleShot(0, this, SLOT(lateStart()));
}

void MainWindow::applyTheme(const QString &filename)
{
    modelList->setTheme(filename);
}

void MainWindow::showThemeDialog()
{
    ThemeDialog dialog(this);
    connect(&dialog, SIGNAL(themeChanged(QString)),
            this, SLOT(applyTheme(QString)));
    dialog.exec();
}

/**
 * @brief Initialization
 */
void MainWindow::lateStart()
{
    // Update status panel
    status->showMessage(
        Common::getDriveInfo(curIndex.filePath()));

    // Configure bookmarks list
    bookmarksList->setDragDropMode(QAbstractItemView::DragDrop);
    bookmarksList->setDropIndicatorShown(true);
    bookmarksList->setDefaultDropAction(Qt::MoveAction);
    bookmarksList->setSelectionMode(QAbstractItemView::ExtendedSelection);

    // Configure tree view
    tree->setDragDropMode(QAbstractItemView::DragDrop);
    tree->setDefaultDropAction(Qt::MoveAction);
    tree->setDropIndicatorShown(true);
    tree->setEditTriggers(
        QAbstractItemView::EditKeyPressed |
        QAbstractItemView::SelectedClicked);

    // Configure detail view
    detailTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    detailTree->setDragDropMode(QAbstractItemView::DragDrop);
    detailTree->setDefaultDropAction(Qt::MoveAction);
    detailTree->setDropIndicatorShown(true);
    detailTree->setEditTriggers(
        QAbstractItemView::EditKeyPressed |
        QAbstractItemView::SelectedClicked);

    // Configure list view
    list->setResizeMode(QListView::Adjust);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setSelectionRectVisible(true);
    list->setFocus();
    list->setEditTriggers(
        QAbstractItemView::EditKeyPressed |
        QAbstractItemView::SelectedClicked);

    // Configure second detail view
    detailTree2->setSelectionMode(QAbstractItemView::ExtendedSelection);
    detailTree2->setDragDropMode(QAbstractItemView::DragDrop);
    detailTree2->setDefaultDropAction(Qt::MoveAction);
    detailTree2->setDropIndicatorShown(true);
    detailTree2->setEditTriggers(
        QAbstractItemView::EditKeyPressed |
        QAbstractItemView::SelectedClicked);

    // Configure second list view
    list2->setResizeMode(QListView::Adjust);
    list2->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list2->setSelectionRectVisible(true);
    list2->setEditTriggers(
        QAbstractItemView::EditKeyPressed |
        QAbstractItemView::SelectedClicked);

    // Clipboard configuration
    progress = Q_NULLPTR;
    clipboardChanged();

    // Completer configuration
    customComplete = new myCompleter;
    customComplete->setModel(modelTree);
    customComplete->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    customComplete->setMaxVisibleItems(10);
    pathEdit->setCompleter(customComplete);

    // Tabs configuration
    tabs->setDrawBase(0);
    tabs->setExpanding(0);
    tabs2->setDrawBase(0);
    tabs2->setExpanding(0);

    // Connect mouse clicks in views
    if (settings->value("singleClick").toInt() == 1) {
        connect(list, &QAbstractItemView::clicked,
                this, &MainWindow::listItemClicked);
        connect(detailTree, &QAbstractItemView::clicked,
                this, &MainWindow::listItemClicked);
        connect(list2, &QAbstractItemView::clicked,
                this, &MainWindow::listItemClicked);
        connect(detailTree2, &QAbstractItemView::clicked,
                this, &MainWindow::listItemClicked);
    }

    if (settings->value("singleClick").toInt() == 2) {
        connect(list, &QAbstractItemView::clicked,
                this, &MainWindow::listDoubleClicked);
        connect(detailTree, &QAbstractItemView::clicked,
                this, &MainWindow::listDoubleClicked);
        connect(list2, &QAbstractItemView::clicked,
                this, &MainWindow::listDoubleClicked);
        connect(detailTree2, &QAbstractItemView::clicked,
                this, &MainWindow::listDoubleClicked);
    }

    // Connect list views
    connect(list, &QAbstractItemView::activated,
            this, &MainWindow::listDoubleClicked);
    connect(list2, &QAbstractItemView::activated,
            this, &MainWindow::listDoubleClicked);

    // Connect custom action manager
    connect(customActManager, &CustomActionsManager::actionMapped,
            this, &MainWindow::actionMapper);
    connect(customActManager, &CustomActionsManager::actionsLoaded,
            this, &MainWindow::readShortcuts);
    connect(customActManager, &CustomActionsManager::actionFinished,
            this, &MainWindow::clearCutItems);

    // Connect path edit
    connect(pathEdit, &QComboBox::textActivated,
            this, &MainWindow::pathEditChanged);

    connect(customComplete,
            qOverload<const QString &>(&QCompleter::activated),
            this,
            &MainWindow::pathEditChanged);

    connect(pathEdit->lineEdit(),
            &QLineEdit::cursorPositionChanged,
            this,
            &MainWindow::addressChanged);

    // Connect bookmarks
    connect(bookmarksList, SIGNAL(activated(QModelIndex)),
            this, SLOT(bookmarkClicked(QModelIndex)));
    connect(bookmarksList, SIGNAL(clicked(QModelIndex)),
            this, SLOT(bookmarkClicked(QModelIndex)));
    connect(bookmarksList, SIGNAL(pressed(QModelIndex)),
            this, SLOT(bookmarkPressed(QModelIndex)));

    // Connect selection
    connect(QApplication::clipboard(),
            SIGNAL(changed(QClipboard::Mode)),
            this,
            SLOT(clipboardChanged()));

    connect(detailTree, SIGNAL(activated(QModelIndex)),
            this, SLOT(listDoubleClicked(QModelIndex)));
    connect(detailTree2, SIGNAL(activated(QModelIndex)),
            this, SLOT(listDoubleClicked(QModelIndex)));

    connect(listSelectionModel,
            SIGNAL(selectionChanged(
                const QItemSelection,
                const QItemSelection)),
            this,
            SLOT(listSelectionChanged(
                const QItemSelection,
                const QItemSelection)));

    connect(listSelectionModel2,
            SIGNAL(selectionChanged(
                const QItemSelection,
                const QItemSelection)),
            this,
            SLOT(listSelectionChanged(
                const QItemSelection,
                const QItemSelection)));

    // Connect copy progress
    connect(this,
            SIGNAL(copyProgressFinished(int,QStringList)),
            this,
            SLOT(progressFinished(int,QStringList)));

    // Connect bookmark model
    connect(modelBookmarks,
            SIGNAL(bookmarkPaste(
                const QMimeData *, QString, QStringList, bool)),
            this,
            SLOT(pasteLauncher(
                const QMimeData *, QString, QStringList, bool)));

    connect(modelBookmarks,
            SIGNAL(rowsInserted(QModelIndex, int, int)),
            this,
            SLOT(readShortcuts()));

    connect(modelBookmarks,
            SIGNAL(rowsRemoved(QModelIndex, int, int)),
            this,
            SLOT(readShortcuts()));

    // Connect list model
    connect(modelList,
            SIGNAL(dragDropPaste(
                const QMimeData *, QString, Common::DragMode)),
            this,
            SLOT(dragLauncher(
                const QMimeData *, QString, Common::DragMode)));

    // Connect tabs
    connect(tabs, SIGNAL(currentChanged(int)),
            this, SLOT(tabChanged(int)));

    connect(tabs2, SIGNAL(currentChanged(int)),
            this, SLOT(tabChanged(int)));

    connect(tabs,
            SIGNAL(dragDropTab(
                const QMimeData *, QString, QStringList)),
            this,
            SLOT(pasteLauncher(
                const QMimeData *, QString, QStringList)));

    connect(tabs2,
            SIGNAL(dragDropTab(
                const QMimeData *, QString, QStringList)),
            this,
            SLOT(pasteLauncher(
                const QMimeData *, QString, QStringList)));

    connect(list, SIGNAL(pressed(QModelIndex)),
            this, SLOT(listItemPressed(QModelIndex)));
    connect(detailTree, SIGNAL(pressed(QModelIndex)),
            this, SLOT(listItemPressed(QModelIndex)));
    connect(list2, SIGNAL(pressed(QModelIndex)),
            this, SLOT(listItemPressed(QModelIndex)));
    connect(detailTree2, SIGNAL(pressed(QModelIndex)),
            this, SLOT(listItemPressed(QModelIndex)));

    connect(modelList, SIGNAL(thumbUpdate(QString)),
            this, SLOT(thumbUpdate(QString)));

    qApp->setKeyboardInputInterval(1000);

    // Read custom actions
    QTimer::singleShot(
        100,
        customActManager,
        SLOT(readActions()));

    // Read defaults
    QTimer::singleShot(
        100,
        mimeUtils,
        SLOT(generateDefaults()));
}

//---------------------------------------------------------------------------

/**
 * @brief Loads application settings
 */
void MainWindow::loadSettings(
    bool wState,
    bool hState,
    bool tabState,
    bool thumbState)
{
    // first run?
    bool isFirstRun = false;

    if (!settings->value("firstRun").isValid()) {
        isFirstRun = true;
        settings->setValue("firstRun", false);
    }

    setStyleSheet(
        "QToolBar { padding: 0;border:none; }"
        /*"QFrame { border:none; }"
        "QListView::item,QListView::text,QListView::icon"
        "{ border:0px;padding-top:5px;padding-left:5px; }"*/);

    addressToolBar->setContentsMargins(0,0,5,0);

    // Restore window state
    if (wState) {
        qDebug() << "restore window state";

        if (!settings->value("windowState").isValid()) {
            dockTree->hide();
        }

        restoreState(
            settings->value("windowState").toByteArray(),
            1);

        restoreGeometry(
            settings->value("windowGeo").toByteArray());

        if (settings->value("windowMax").toBool()) {
            showMaximized();
        }
    }

    // Load info whether use real mime types
    modelList->setRealMimeTypes(
        settings->value("realMimeTypes", true).toBool());

    // Load information whether hidden files can be displayed
    if (hState) {
        hiddenAct->setChecked(
            settings->value("hiddenMode", 0).toBool());
        toggleHidden();
    }

    // Remove old bookmarks
    modelBookmarks->removeRows(
        0,
        modelBookmarks->rowCount());

    // Load bookmarks
    loadBookmarks();

    // Set bookmarks
    firstRunBookmarks(isFirstRun);

#ifndef NO_UDISKS
    populateMedia();
#endif

    bookmarksList->setModel(modelBookmarks);
    bookmarksList->setResizeMode(QListView::Adjust);
    bookmarksList->setFlow(QListView::TopToBottom);
    bookmarksList->setIconSize(QSize(24,24));

    // Load information whether bookmarks are displayed
    wrapBookmarksAct->setChecked(
        settings->value("wrapBookmarks", 0).toBool());

    bookmarksList->setWrapping(
        wrapBookmarksAct->isChecked());

    // Lock information whether layout is locked or not
    lockLayoutAct->setChecked(
        settings->value("lockLayout", 1).toBool());

    toggleLockLayout();

    // Load zoom settings
    zoom = settings->value("zoom", 48).toInt();
    zoomTree = settings->value("zoomTree", 16).toInt();
    zoomBook = settings->value("zoomBook", 24).toInt();
    zoomList = settings->value("zoomList", 24).toInt();
    zoomDetail = settings->value("zoomDetail", 32).toInt();

    detailTree->setIconSize(QSize(zoomDetail, zoomDetail));
    detailTree2->setIconSize(QSize(zoomDetail, zoomDetail));
    tree->setIconSize(QSize(zoomTree, zoomTree));
    bookmarksList->setIconSize(QSize(zoomBook, zoomBook));

    // Load information whether thumbnails can be shown
    if (thumbState) {
        thumbsAct->setChecked(
            settings->value("showThumbs", 1).toBool());
    }

    // Load view mode
    detailAct->setChecked(
        settings->value("viewMode", 0).toBool());

    iconAct->setChecked(
        settings->value("iconMode", 1).toBool());

    toggleDetails();

    // Restore header of detail tree
    detailTree->header()->restoreState(
        settings->value("header").toByteArray());

    detailTree->setSortingEnabled(1);
    detailTree2->setSortingEnabled(1);

    // Load sorting information and sort
    currentSortColumn =
        settings->value("sortBy", 0).toInt();

    currentSortOrder =
        (Qt::SortOrder)settings->value(
            "sortOrder",
            0).toInt();

    switch (currentSortColumn) {
    case 0:
        setSortColumn(sortNameAct);
        break;

    case 1:
        setSortColumn(sortSizeAct);
        break;

    case 3:
        setSortColumn(sortDateAct);
        break;
    }

    setSortOrder(currentSortOrder);
    modelView->sort(
        currentSortColumn,
        currentSortOrder);

    // Load terminal command
    term = settings->value(
        "term",
        "xterm").toString();

    // custom actions
#ifndef Q_OS_MAC
    firstRunCustomActions(isFirstRun);
#endif

    // Load information whether tabs can be shown on top
    if (tabState) {
        tabsOnTopAct->setChecked(
            settings->value("tabsOnTop", 0).toBool());
        tabsOnTop();
    }

    // show/hide buttons
    homeAct->setVisible(
        settings->value("home_button", true).toBool());

    newTabAct->setVisible(
        settings->value("newtab_button", false).toBool());

    terminalAct->setVisible(
        settings->value("terminal_button", true).toBool());

    // path history
    pathHistory =
        settings->value(
            "pathHistory",
            true).toBool();

    // path in window title
    showPathInWindowTitle =
        settings->value(
            "windowTitlePath",
            true).toBool();

    if (!showPathInWindowTitle) {
        setWindowTitle(APP_NAME);
    }

    // 'copy of' filename
    copyXof =
        settings->value(
            "copyXof",
            COPY_X_OF).toString();

    copyXofTS =
        settings->value(
            "copyXofTS",
            COPY_X_TS).toString();
}

//---------------------------------------------------------------------------

void MainWindow::firstRunBookmarks(bool isFirstRun)
{
    if (!isFirstRun) {
        return;
    }

    modelBookmarks->addBookmark(
        tr("Computer"),
        "/",
        "",
        "computer",
        "",
        false,
        false);

#ifdef Q_OS_MAC
    modelBookmarks->addBookmark(
        tr("Applications"),
        "/Applications",
        "",
        "applications-other",
        "",
        false,
        false);
#endif

    modelBookmarks->addBookmark(
        tr("Home"),
        QDir::homePath(),
        "",
        "user-home",
        "",
        false,
        false);

    modelBookmarks->addBookmark(
        tr("Desktop"),
        QString("%1/Desktop").arg(QDir::homePath()),
        "",
        "user-desktop",
        "",
        false,
        false);

    modelBookmarks->addBookmark(
        tr("Trash"),
        QString("%1/.local/share/Trash").arg(QDir::homePath()),
        "",
        "user-trash",
        "",
        false,
        false);

    modelBookmarks->addBookmark(
        "",
        "",
        "",
        "",
        "",
        false,
        false);

    writeBookmarks();
}

//---------------------------------------------------------------------------

void MainWindow::loadBookmarks()
{
    settings->beginGroup("bookmarks");

    foreach (QString key, settings->childKeys()) {
        QStringList temp(
            settings->value(key).toStringList());

        modelBookmarks->addBookmark(
            temp[0],
            temp[1],
            temp[2],
            temp.last(),
            "",
            false,
            false);
    }

    settings->endGroup();
}

//---------------------------------------------------------------------------

void MainWindow::writeBookmarks()
{
    settings->remove("bookmarks");
    settings->beginGroup("bookmarks");

    for (int i = 0;
         i < modelBookmarks->rowCount();
         ++i) {

        if (modelBookmarks->item(i)
                ->data(MEDIA_MODEL)
                .toBool()) {
            continue;
        }

        QStringList temp;
        temp << modelBookmarks->item(i)->text()
             << modelBookmarks->item(i)
                    ->data(BOOKMARK_PATH)
                    .toString()
             << modelBookmarks->item(i)
                    ->data(BOOKMARKS_AUTO)
                    .toString()
             << modelBookmarks->item(i)
                    ->data(BOOKMARK_ICON)
                    .toString();

        QString number =
            QString("%1").arg(
                i,
                4,
                10,
                QChar('0'));

        settings->setValue(number, temp);
    }

    settings->endGroup();
}

//---------------------------------------------------------------------------

void MainWindow::handleBookmarksChanged()
{
    QTimer::singleShot(
        1000,
        this,
        SLOT(writeBookmarks()));
}

//---------------------------------------------------------------------------

void MainWindow::firstRunCustomActions(bool isFirstRun)
{
    if (!isFirstRun) {
        return;
    }

    settings->beginGroup("customActions");

    int childs =
        settings->childKeys().size();

    if (childs > 0) {
        return;
    }

    QVector<QStringList> defActions =
        Common::getDefaultActions();

    for (int i = 0;
         i < defActions.size();
         ++i) {

        settings->setValue(
            QString::number(i),
            defActions.at(i));
    }

    settings->endGroup();
    settings->sync();
}

//---------------------------------------------------------------------------

/**
 * @brief Close event
 * @param event
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();
    modelList->cacheInfo();
    event->accept();
}

//---------------------------------------------------------------------------

/**
 * @brief Closes main window
 */
void MainWindow::exitAction()
{
    close();
}

//---------------------------------------------------------------------------

void MainWindow::treeSelectionChanged(
    QModelIndex current,
    QModelIndex previous)
{
    qDebug() << "treeSelectionChanged";
    Q_UNUSED(previous)

    QFileInfo name =
        modelList->fileInfo(
            modelTree->mapToSource(current));

    if (!name.exists()) {
        return;
    }

    curIndex = name;

    if (showPathInWindowTitle) {
        if (curIndex.fileName().isEmpty()) {
            setWindowTitle(
                curIndex.absolutePath());
        } else {
            setWindowTitle(
                curIndex.fileName());
        }
    } else {
        setWindowTitle(APP_NAME);
    }

    tabBar *currentTabs =
        activePane == 2 ? tabs2 : tabs;

    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    QListView *currentList =
        activePane == 2 ? list2 : list;

    QTreeView *currentDetail =
        activePane == 2 ?
        detailTree2 :
        detailTree;

    QString *currentPath =
        activePane == 2 ?
        &panePath2 :
        &panePath1;

    if (tree->hasFocus() &&
        QApplication::mouseButtons() == Qt::MiddleButton) {

        listItemPressed(
            modelView->mapFromSource(
                modelList->index(
                    name.filePath())));

        if (currentTabs->count()) {
            currentTabs->setCurrentIndex(
                currentTabs->count() - 1);
        }

        if (currentView == 2) {
            currentDetail->setFocus(
                Qt::TabFocusReason);
        } else {
            currentList->setFocus(
                Qt::TabFocusReason);
        }
    }

    *currentPath = name.filePath();

    if (curIndex.filePath() !=
        pathEdit->itemText(0)) {

        if (currentTabs->count() &&
            pathHistory) {
            currentTabs->addHistory(
                curIndex.filePath());
        }

        if (!pathHistory &&
            pathEdit->count() > 0) {
            pathEdit->clear();
        }

        pathEdit->insertItem(
            0,
            curIndex.filePath());

        pathEdit->setCurrentIndex(0);
    }

    if (!bookmarksList->hasFocus()) {
        bookmarksList->clearSelection();
    }

    if (modelList->setRootPath(
            name.filePath())) {
        modelView->invalidate();
    }

    QModelIndex baseIndex =
        modelView->mapFromSource(
            modelList->index(
                name.filePath()));

    if (currentView == 2) {
        currentDetail->setRootIndex(baseIndex);
    } else {
        currentList->setRootIndex(baseIndex);
    }

    if (currentTabs->count()) {
        QString tabText =
            curIndex.fileName();

        if (tabText.isEmpty()) {
            tabText = "/";
        }

        currentTabs->setTabText(
            currentTabs->currentIndex(),
            tabText);

        currentTabs->setTabData(
            currentTabs->currentIndex(),
            curIndex.filePath());

        currentTabs->setIcon(
            currentTabs->currentIndex());
    }

    if (backIndex.isValid()) {
        QModelIndex backModelIndex =
            modelView->mapFromSource(backIndex);

        selectionModel->setCurrentIndex(
            backModelIndex,
            QItemSelectionModel::ClearAndSelect);

        if (currentView == 2) {
            currentDetail->scrollTo(
                backModelIndex);
        } else {
            currentList->scrollTo(
                backModelIndex);
        }
    } else {
        selectionModel->blockSignals(1);
        selectionModel->clear();
    }

    selectionModel->blockSignals(0);

    pathEdit->setEditText(*currentPath);

    updateGrid();

    qDebug() <<
        "trigger dirloaded on tree selection changed";

    QTimer::singleShot(
        30,
        this,
        SLOT(dirLoaded()));
}

//---------------------------------------------------------------------------

void MainWindow::dirLoaded(bool thumbs)
{
    if (backIndex.isValid()) {
        backIndex = QModelIndex();
        return;
    }

    qDebug() <<
        "dirLoaded triggered, thumbs?"
        << thumbs;

    qint64 bytes = 0;
    QModelIndexList items;
    bool includeHidden =
        hiddenAct->isChecked();

    QString path =
        activePane == 2 ?
        panePath2 :
        panePath1;

    QModelIndex root =
        modelList->index(path);

    for (int x = 0;
         x < modelList->rowCount(root);
         ++x) {

        items.append(
            modelList->index(
                x,
                0,
                root));
    }

    foreach (QModelIndex theItem, items) {
        if (includeHidden ||
            !modelList->fileInfo(theItem).isHidden()) {

            bytes +=
                modelList->size(theItem);
        } else {
            items.removeOne(theItem);
        }
    }

    QString total;

    if (!bytes) {
        total = "";
    } else {
        total = Common::formatSize(bytes);
    }

    statusName->clear();

    statusSize->setText(
        QString("%1 items").arg(items.count()));

    statusDate->setText(
        QString("%1").arg(total));

    if (thumbsAct->isChecked() && thumbs) {
        QThreadPool::globalInstance()->start(
            [this, items]() {
                modelList->loadThumbs(items);
            });
    }

    updateGrid();
}

//---------------------------------------------------------------------------

void MainWindow::updateDir()
{
    dirLoaded(false /* dont refresh thumb*/);
}

//---------------------------------------------------------------------------

void MainWindow::handleReloadDir(
    const QString &path)
{
    if (ignoreReload) {
        qDebug() << "ignore reload";
        return;
    }

    ignoreReload = true;

    qDebug() <<
        "handle reload dir"
        << path
        << modelList->getRootPath();

    if (path != modelList->getRootPath()) {
        return;
    }

    dirLoaded();

    QTimer::singleShot(
        500,
        this,
        SLOT(enableReload()));
}

//---------------------------------------------------------------------------

void MainWindow::thumbUpdate(
    const QString &path)
{
    qDebug() <<
        "thumbupdate"
        << path
        << modelList->getRootPath();

    if (path != modelList->getRootPath()) {
        return;
    }

    refresh(false, false);
}

//---------------------------------------------------------------------------

void MainWindow::listSelectionChanged(
    const QItemSelection selected,
    const QItemSelection deselected)
{
    Q_UNUSED(selected)
    Q_UNUSED(deselected)

    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    QModelIndexList items;

    if (selectionModel->selectedRows(0).count()) {
        items =
            selectionModel->selectedRows(0);
    } else {
        items =
            selectionModel->selectedIndexes();
    }

    statusSize->clear();
    statusDate->clear();
    statusName->clear();

    if (items.count() == 0) {
        curIndex =
            QFileInfo(
                activePane == 2 ?
                panePath2 :
                panePath1);
        return;
    }

    if (QApplication::focusWidget() !=
        bookmarksList) {
        bookmarksList->clearSelection();
    }

    curIndex =
        modelList->fileInfo(
            modelView->mapToSource(
                selectionModel->currentIndex()));

    qint64 bytes = 0;
    int folders = 0;
    int files = 0;

    foreach (QModelIndex theItem, items) {
        if (modelList->isDir(
                modelView->mapToSource(theItem))) {
            folders++;
        } else {
            files++;
        }

        bytes +=
            modelList->size(
                modelView->mapToSource(theItem));
    }

    QString total;
    QString name;

    if (!bytes) {
        total = "";
    } else {
        total =
            Common::formatSize(bytes);
    }

    if (items.count() == 1) {
        QFileInfo file(
            modelList->filePath(
                modelView->mapToSource(
                    items.at(0))));

        name = file.fileName();

        if (file.isSymLink()) {
            name =
                "Link --> " +
                file.symLinkTarget();
        }

        statusName->setText(
            name + "   ");

        statusSize->setText(
            QString("%1   ").arg(total));

        statusDate->setText(
            QString("%1").arg(
                QLocale().toString(
                    file.lastModified().date(),
                    QLocale::ShortFormat)));
    } else {
        statusName->setText(
            total + "   ");

        if (files) {
            statusSize->setText(
                QString("%1 files  ").arg(files));
        }

        if (folders) {
            statusDate->setText(
                QString("%1 folders").arg(folders));
        }
    }
}

//---------------------------------------------------------------------------

void MainWindow::listItemClicked(
    QModelIndex current)
{
    QString currentPath =
        activePane == 2 ?
        panePath2 :
        panePath1;

    if (modelList->filePath(
            modelView->mapToSource(current)) ==
        currentPath) {
        return;
    }

    Qt::KeyboardModifiers mods =
        QApplication::keyboardModifiers();

    if (mods == Qt::ControlModifier ||
        mods == Qt::ShiftModifier) {
        return;
    }

    if (modelList->isDir(
            modelView->mapToSource(current))) {

        tree->setCurrentIndex(
            modelTree->mapFromSource(
                modelView->mapToSource(current)));
    }
}

//---------------------------------------------------------------------------

void MainWindow::listItemPressed(
    QModelIndex current)
{
    // middle-click -> open new tab
    // ctrl+middle-click -> open new instance

    if (QApplication::mouseButtons() !=
        Qt::MiddleButton) {
        return;
    }

    if (modelList->isDir(
            modelView->mapToSource(current))) {

        if (QApplication::keyboardModifiers() ==
            Qt::ControlModifier) {
            openFile();
        } else {
            addTab(
                modelList->filePath(
                    modelView->mapToSource(current)));
        }
    } else {
        openFile();
    }
}

//---------------------------------------------------------------------------

void MainWindow::openTab()
{
    QString path =
        activePane == 2 ?
        panePath2 :
        panePath1;

    if (curIndex.isDir()) {
        addTab(curIndex.filePath());
    } else if (!path.isEmpty()) {
        addTab(path);
    } else {
        addTab(QDir::homePath());
    }
}

//---------------------------------------------------------------------------

void MainWindow::openNewTab()
{
    QFileInfo info(curIndex.filePath());

    if (!info.isDir()) {
        return;
    }

    addTab(curIndex.filePath());
}

//---------------------------------------------------------------------------

int MainWindow::addTab(QString path)
{
    tabBar *currentTabs =
        activePane == 2 ?
        tabs2 :
        tabs;

    QString currentPath =
        activePane == 2 ?
        panePath2 :
        panePath1;

    if (currentTabs->count() == 0) {
        currentTabs->addNewTab(
            currentPath,
            currentView);
    }

    return currentTabs->addNewTab(
        path,
        currentView);
}

//---------------------------------------------------------------------------

void MainWindow::tabsOnTop()
{
    if (tabsOnTopAct->isChecked()) {
        mainLayout->setDirection(
            QBoxLayout::BottomToTop);

        tabs->setShape(
            QTabBar::RoundedNorth);

        tabs2->setShape(
            QTabBar::RoundedNorth);
    } else {
        mainLayout->setDirection(
            QBoxLayout::TopToBottom);

        tabs->setShape(
            QTabBar::RoundedSouth);

        tabs2->setShape(
            QTabBar::RoundedSouth);
    }
}

//---------------------------------------------------------------------------

void MainWindow::tabChanged(int index)
{
    tabBar *currentTabs =
        qobject_cast<tabBar*>(sender());

    if (!currentTabs ||
        currentTabs->count() == 0) {
        return;
    }

    activePane =
        currentTabs == tabs2 ? 2 : 1;

    QString path =
        currentTabs->tabData(index).toString();

    if (path.isEmpty()) {
        return;
    }

    if (activePane == 2) {
        panePath2 = path;
    } else {
        panePath1 = path;
    }

    pathEdit->clear();
    pathEdit->addItems(
        *currentTabs->getHistory(index));

    int type =
        currentTabs->getType(index);

    if (currentView != type) {
        if (type == 2) {
            detailAct->setChecked(1);
        } else {
            detailAct->setChecked(0);
        }

        if (type == 1) {
            iconAct->setChecked(1);
        } else {
            iconAct->setChecked(0);
        }

        toggleDetails();
    }

    if (modelList->setRootPath(path)) {
        modelView->invalidate();
    }

    QModelIndex baseIndex =
        modelView->mapFromSource(
            modelList->index(path));

    if (activePane == 2) {
        if (currentView == 2) {
            detailTree2->setRootIndex(baseIndex);
        } else {
            list2->setRootIndex(baseIndex);
        }
    } else {
        if (currentView == 2) {
            detailTree->setRootIndex(baseIndex);
        } else {
            list->setRootIndex(baseIndex);
        }
    }

    pathEdit->setEditText(path);

    tree->setCurrentIndex(
        modelTree->mapFromSource(
            modelList->index(path)));

    updateGrid();
}

//---------------------------------------------------------------------------

void MainWindow::newWindow()
{
    if (settings->value("clearCache").toBool()) {
        settings->setValue(
            "clearCache",
            false);
    }

    writeSettings();
    QProcess::startDetached(
        qApp->applicationFilePath());
}

//---------------------------------------------------------------------------

/**
 * @brief Doubleclick on icon/launcher
 * @param current
 */
void MainWindow::listDoubleClicked(
    QModelIndex current)
{
    Qt::KeyboardModifiers mods =
        QApplication::keyboardModifiers();

    if (mods == Qt::ControlModifier ||
        mods == Qt::ShiftModifier) {
        return;
    }

#ifdef Q_OS_MAC
    if (modelList->isDir(
            modelView->mapToSource(current)) &&
        !modelList->fileName(
            modelView->mapToSource(current))
            .endsWith(".app")) {
#else
    if (modelList->isDir(
            modelView->mapToSource(current))) {
#endif
        QModelIndex i =
            modelView->mapToSource(current);

        tree->setCurrentIndex(
            modelTree->mapFromSource(i));
    } else {
        executeFile(current, 0);
    }
}

//---------------------------------------------------------------------------

/**
 * @brief Reaction for change of path edit
 * @param path
 */
void MainWindow::pathEditChanged(
    QString path)
{
    QString info = path;

    if (!QFileInfo(path).exists()) {
        return;
    }

    info.replace(
        "~",
        QDir::homePath());

    if (activePane == 2) {
        panePath2 = info;
    } else {
        panePath1 = info;
    }

    tree->setCurrentIndex(
        modelTree->mapFromSource(
            modelList->index(info)));
}

//---------------------------------------------------------------------------

/**
 * @brief Handle clipboard changes
 */
void MainWindow::clipboardChanged()
{
    qDebug() << "clipboard changed";

    if (QApplication::clipboard()->mimeData()) {
        if (QApplication::clipboard()
                ->mimeData()
                ->hasUrls()) {

            qDebug() <<
                "clipboard has data, enable paste";

            pasteAct->setEnabled(true);
            return;
        }
    }

    // clear tmp and disable paste
    modelList->clearCutItems();
    pasteAct->setEnabled(false);
}

//---------------------------------------------------------------------------

/**
 * @brief Pastes from clipboard
 */
void MainWindow::pasteClipboard()
{
    QString newPath;
    QStringList cutList;

    if (curIndex.isDir()) {
        newPath = curIndex.filePath();
    } else {
        newPath =
            activePane == 2 ?
            panePath2 :
            panePath1;
    }

    QFile tempFile(
        QDir::tempPath() +
        "/" +
        APP +
        ".temp");

    if (tempFile.exists()) {
        if (tempFile.open(QIODevice::ReadOnly)) {
            QDataStream out(&tempFile);
            out >> cutList;
            tempFile.close();
        }
    }

    pasteLauncher(
        QApplication::clipboard()->mimeData(),
        newPath,
        cutList);
}

//---------------------------------------------------------------------------

/**
 * @brief Drags data to the new location
 * @param data data to be pasted
 * @param newPath path of new location
 * @param dragMode mode of dragging
 */
void MainWindow::dragLauncher(
    const QMimeData *data,
    const QString &newPath,
    Common::DragMode dragMode)
{
    QList<QUrl> files = data->urls();

    if (files.isEmpty()) {
        return;
    }

    QStringList getOldPath =
        files.at(0)
            .toLocalFile()
            .split(
                "/",
                Qt::SkipEmptyParts);

    QString oldPath;

    for (int i = 0;
         i < getOldPath.size() - 1;
         ++i) {

        oldPath.append(
            QString("/%1")
                .arg(getOldPath.at(i)));
    }

    QString oldDevice =
        Common::getDeviceForDir(oldPath);

    QString newDevice =
        Common::getDeviceForDir(newPath);

    qDebug() <<
        "oldpath:"
        << oldDevice
        << oldPath;

    qDebug() <<
        "newpath:"
        << newDevice
        << newPath;

    QString extraText;
    Common::DragMode currentDragMode =
        dragMode;

    if (oldDevice != newDevice) {
        extraText =
            tr("Source and destination is on a different storage.");

        currentDragMode =
            Common::DM_UNKNOWN;
    }

    if (currentDragMode ==
        Common::DM_UNKNOWN) {

        QMessageBox box;

        box.setWindowTitle(
            tr("Select file action"));

        box.setWindowIcon(
            QIcon::fromTheme(
                "qtfm",
                QIcon(":/images/qtfm.png")));

        box.setIconPixmap(
            QIcon::fromTheme(
                "dialog-information")
                .pixmap(QSize(32,32)));

        box.setText(
            tr("<h3>What do you want to do?</h3>"));

        if (!extraText.isEmpty()) {
            box.setText(
                QString("%1<p>%2</p>")
                    .arg(box.text())
                    .arg(extraText));
        }

        QAbstractButton *move =
            box.addButton(
                tr("Move here"),
                QMessageBox::ActionRole);

        QAbstractButton *copy =
            box.addButton(
                tr("Copy here"),
                QMessageBox::ActionRole);

        QAbstractButton *link =
            box.addButton(
                tr("Link here"),
                QMessageBox::ActionRole);

        QAbstractButton *canc =
            box.addButton(
                QMessageBox::Cancel);

        move->setIcon(
            QIcon::fromTheme("edit-cut"));

        copy->setIcon(
            QIcon::fromTheme("edit-copy"));

        link->setIcon(
            QIcon::fromTheme("insert-link"));

        canc->setIcon(
            QIcon::fromTheme("edit-delete"));

        box.exec();

        if (box.clickedButton() == move) {
            dragMode = Common::DM_MOVE;
        } else if (box.clickedButton() == copy) {
            dragMode = Common::DM_COPY;
        } else if (box.clickedButton() == link) {
            dragMode = Common::DM_LINK;
        } else if (box.clickedButton() == canc) {
            return;
        }

        currentDragMode = dragMode;
    }

    QStringList cutList;

    if (currentDragMode ==
        Common::DM_MOVE) {

        foreach (QUrl item, files) {
            cutList.append(item.path());
        }
    }

    pasteLauncher(
        files,
        newPath,
        cutList,
        dragMode == Common::DM_LINK);
}

//---------------------------------------------------------------------------

/**
 * @brief Pastes data to the new location
 */
void MainWindow::pasteLauncher(
    const QMimeData *data,
    const QString &newPath,
    const QStringList &cutList,
    bool link)
{
    QList<QUrl> files = data->urls();

    if (files.isEmpty()) {
        return;
    }

    pasteLauncher(
        files,
        newPath,
        cutList,
        link);
}

//---------------------------------------------------------------------------

/**
 * @brief Pastes files to the new path
 */
void MainWindow::pasteLauncher(
    const QList<QUrl> &files,
    const QString &newPath,
    const QStringList &cutList,
    bool link)
{
    qDebug() <<
        "pasteLauncher"
        << files
        << newPath
        << cutList
        << link;

    if (files.isEmpty()) {
        return;
    }

    if (!QFile(files.at(0).path()).exists()) {
        QString msg =
            tr("File '%1' no longer exists!")
                .arg(files.at(0).path());

        QMessageBox::information(
            this,
            tr("No paste for you!"),
            msg);

        pasteAct->setEnabled(false);
        return;
    }

    int replace = 0;
    QStringList completeList;
    QString baseName =
        QFileInfo(
            files.at(0).toLocalFile()).path();

    if (newPath != baseName) {
        foreach (QUrl file, files) {
            QFileInfo temp(file.toLocalFile());

            if (temp.isDir() &&
                QFileInfo(
                    newPath +
                    QDir::separator() +
                    temp.fileName()).exists()) {

                QString msg =
                    QString(
                        "<b>%1</b><p>Already exists!</p>"
                        "<p>What do you want to do?</p>")
                        .arg(
                            newPath +
                            QDir::separator() +
                            temp.fileName());

                QMessageBox message(
                    QMessageBox::Question,
                    tr("Existing folder"),
                    msg,
                    QMessageBox::Yes |
                    QMessageBox::No |
                    QMessageBox::Cancel);

                message.button(QMessageBox::Yes)
                    ->setText(tr("Merge"));

                message.button(QMessageBox::No)
                    ->setText(tr("Replace"));

                int merge =
                    message.exec();

                if (merge == QMessageBox::Cancel) {
                    return;
                }

                if (merge == QMessageBox::Yes) {
                    FileUtils::recurseFolder(
                        temp.filePath(),
                        temp.fileName(),
                        &completeList);
                } else {
                    FileUtils::removeRecurse(
                        newPath,
                        temp.fileName());
                }
            } else {
                completeList.append(
                    temp.fileName());
            }
        }

        foreach (QString file, completeList) {
            QFileInfo temp(
                newPath +
                QDir::separator() +
                file);

            if (temp.exists()) {
                QFileInfo orig(
                    baseName +
                    QDir::separator() +
                    file);

                if (replace != QMessageBox::YesToAll &&
                    replace != QMessageBox::NoToAll) {

                    replace =
                        showReplaceMsgBox(
                            temp,
                            orig);
                }

                if (replace ==
                    QMessageBox::Cancel) {
                    return;
                }

                if (replace ==
                        QMessageBox::Yes ||
                    replace ==
                        QMessageBox::YesToAll) {
                    QFile(temp.filePath()).remove();
                }
            }
        }
    }

    if (link) {
        qDebug() <<
            "LINK"
            << files
            << newPath;

        linkFiles(files, newPath);
        return;
    }

    for (int i = 0;
         i < files.size();
         ++i) {

        QString queueFile =
            files.at(i).fileName();

        queueFile.prepend(
            QString("%1/").arg(newPath));

        if (!progressQueue.contains(queueFile)) {
            qDebug() <<
                "add to queue"
                << queueFile;

            progressQueue.append(queueFile);
        }
    }

    QString title =
        cutList.count() == 0 ?
        tr("Copying...") :
        tr("Moving...");

    if (!progress) {
        progress =
            new myProgressDialog(title);

        connect(
            this,
            SIGNAL(updateCopyProgress(
                qint64,
                qint64,
                QString)),
            progress,
            SLOT(update(
                qint64,
                qint64,
                QString)));
    }

    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    selectionModel->clear();

    QThreadPool::globalInstance()->start(
        [this, files, newPath, cutList]() {
            pasteFiles(
                files,
                newPath,
                cutList);
        });
}

//---------------------------------------------------------------------------

/**
 * @brief Asks user whether replace file 'f1' with 'f2'
 */
int MainWindow::showReplaceMsgBox(
    const QFileInfo &f1,
    const QFileInfo &f2)
{
    QString t =
        tr("<h3>Do you want to replace?</h3>"
           "<p><b>%1</p><p>Modified: %2<br>"
           "Size: %3 bytes</p><p>with:<p><b>%4</p>"
           "<p>Modified: %5<br>Size: %6 bytes</p>");

    t = t.arg(f1.filePath())
         .arg(f1.lastModified().toString())
         .arg(f1.size())
         .arg(f2.filePath())
         .arg(f2.lastModified().toString())
         .arg(f2.size());

    return QMessageBox::question(
        Q_NULLPTR,
        tr("Replace"),
        t,
        QMessageBox::Yes |
        QMessageBox::YesToAll |
        QMessageBox::No |
        QMessageBox::NoToAll |
        QMessageBox::Cancel);
}

//---------------------------------------------------------------------------

void MainWindow::progressFinished(
    int ret,
    QStringList newFiles)
{
    qDebug() <<
        "progressFinished"
        << ret
        << newFiles;

    for (int i = 0;
         i < newFiles.size();
         ++i) {

        if (progressQueue.contains(
                newFiles.at(i))) {

            qDebug() <<
                "remove from queue"
                << newFiles.at(i);

            progressQueue.remove(
                progressQueue.indexOf(
                    newFiles.at(i)));
        }
    }

    if (progress != Q_NULLPTR) {
        progress->setResult(0);

        if (progressQueue.isEmpty()) {
            progress->close();
            delete progress;
            progress = Q_NULLPTR;
        }
    }

    if (newFiles.count()) {
        QItemSelectionModel *selectionModel =
            activePane == 2 ?
            listSelectionModel2 :
            listSelectionModel;

        disconnect(
            selectionModel,
            SIGNAL(selectionChanged(
                const QItemSelection,
                const QItemSelection)),
            this,
            SLOT(listSelectionChanged(
                const QItemSelection,
                const QItemSelection)));

        qApp->processEvents();

        QString activePath =
            activePane == 2 ?
            panePath2 :
            panePath1;

        if (QFileInfo(newFiles.first()).path() ==
            activePath) {

            foreach (QString item, newFiles) {
                selectionModel->select(
                    modelView->mapFromSource(
                        modelList->index(item)),
                    QItemSelectionModel::Select);
            }
        }

        connect(
            selectionModel,
            SIGNAL(selectionChanged(
                const QItemSelection,
                const QItemSelection)),
            this,
            SLOT(listSelectionChanged(
                const QItemSelection,
                const QItemSelection)));

        curIndex.setFile(
            newFiles.first());

        QModelIndex newIndex =
            modelView->mapFromSource(
                modelList->index(
                    newFiles.first()));

        if (activePane == 2) {
            if (currentView == 2) {
                detailTree2->scrollTo(
                    newIndex,
                    QAbstractItemView::EnsureVisible);
            } else {
                list2->scrollTo(
                    newIndex,
                    QAbstractItemView::EnsureVisible);
            }
        } else {
            if (currentView == 2) {
                detailTree->scrollTo(
                    newIndex,
                    QAbstractItemView::EnsureVisible);
            } else {
                list->scrollTo(
                    newIndex,
                    QAbstractItemView::EnsureVisible);
            }
        }

        if (QFile(
                QDir::tempPath() +
                QString("/%1.temp").arg(APP)).exists()) {

            QApplication::clipboard()->clear();
        }

        clearCutItems();
    }

    if (ret == 1) {
        QMessageBox::information(
            this,
            tr("Failed"),
            tr("Paste failed...do you have write permissions?"));
    }

    if (ret == 2) {
        QMessageBox::warning(
            this,
            tr("Too big!"),
            tr("There is not enough space on the destination storage!"));
    }
}

//---------------------------------------------------------------------------

void MainWindow::folderPropertiesLauncher()
{
    QModelIndexList selList;

    if (focusWidget() == bookmarksList) {
        selList.append(
            modelView->mapFromSource(
                modelList->index(
                    bookmarksList->currentIndex()
                        .data(BOOKMARK_PATH)
                        .toString())));
    } else if (focusWidget() == list ||
               focusWidget() == detailTree ||
               focusWidget() == list2 ||
               focusWidget() == detailTree2) {

        QItemSelectionModel *selectionModel =
            activePane == 2 ?
            listSelectionModel2 :
            listSelectionModel;

        if (selectionModel->selectedRows(0).count()) {
            selList =
                selectionModel->selectedRows(0);
        } else {
            selList =
                selectionModel->selectedIndexes();
        }
    }

    if (selList.count() == 0) {
        selList <<
            modelView->mapFromSource(
                modelList->index(
                    activePane == 2 ?
                    panePath2 :
                    panePath1));
    }

    QStringList paths;

    foreach (QModelIndex item, selList) {
        paths.append(
            modelList->filePath(
                modelView->mapToSource(item)));
    }

    properties =
        new PropertiesDialog(
            paths,
            modelList);

    connect(
        properties,
        SIGNAL(propertiesUpdated()),
        this,
        SLOT(clearCutItems()));
}

//---------------------------------------------------------------------------

/**
 * @brief Writes settings into config file
 */
void MainWindow::writeSettings()
{
    settings->setValue(
        "viewMode",
        stackWidget->currentIndex());

    settings->setValue(
        "iconMode",
        iconAct->isChecked());

    settings->setValue("zoom", zoom);
    settings->setValue("zoomTree", zoomTree);
    settings->setValue("zoomBook", zoomBook);
    settings->setValue("zoomList", zoomList);
    settings->setValue("zoomDetail", zoomDetail);
    settings->setValue("sortBy", currentSortColumn);
    settings->setValue("sortOrder", currentSortOrder);
    settings->setValue(
        "showThumbs",
        thumbsAct->isChecked());

    settings->setValue(
        "hiddenMode",
        hiddenAct->isChecked());

    settings->setValue(
        "lockLayout",
        lockLayoutAct->isChecked());

    settings->setValue(
        "tabsOnTop",
        tabsOnTopAct->isChecked());

    settings->setValue(
        "windowState",
        saveState(1));

    settings->setValue(
        "windowGeo",
        saveGeometry());

    settings->setValue(
        "windowMax",
        isMaximized());

    settings->setValue(
        "header",
        detailTree->header()->saveState());

    settings->setValue(
        "realMimeTypes",
        modelList->isRealMimeTypes());

    writeBookmarks();
}

//---------------------------------------------------------------------------

/**
 * @brief Display popup menu
 * @param event
 */
void MainWindow::contextMenuEvent(
    QContextMenuEvent *event)
{
    QMenu *popup;
    QWidget *widget =
        childAt(event->pos());

    if (widget == tabs ||
        widget == tabs2) {

        popup = new QMenu(this);
        popup->addAction(closeTabAct);
        popup->exec(event->globalPos());
        return;
    }

    if (widget == status) {
        popup = createPopupMenu();
        popup->addSeparator();
        popup->addAction(lockLayoutAct);
        popup->exec(event->globalPos());
        return;
    }

    if (widget == navToolBar) {
        qDebug() << "TOOLBAR";
        return;
    }

    QToolButton *isToolButton =
        dynamic_cast<QToolButton*>(
            childAt(event->pos()));

    if (isToolButton) {
        qDebug() << "TOOLBUTTON";
        return;
    }

#ifndef Q_OS_MAC
    QMenuBar *isMenuBar =
        dynamic_cast<QMenuBar*>(
            childAt(event->pos()));

    if (isMenuBar) {
        qDebug() << "MENUBAR";
        return;
    }
#endif

    QList<QAction*> actions;
    popup = new QMenu(this);

    bool isMedia = false;

    if (focusWidget() == list ||
        focusWidget() == detailTree ||
        focusWidget() == list2 ||
        focusWidget() == detailTree2) {

        bookmarksList->clearSelection();

        QItemSelectionModel *selectionModel =
            activePane == 2 ?
            listSelectionModel2 :
            listSelectionModel;

        if (selectionModel->hasSelection()) {
            curIndex =
                QFileInfo(
                    modelList->filePath(
                        modelView->mapToSource(
                            selectionModel->currentIndex())));

            if (!curIndex.isDir()) {
                QString type =
                    modelList->getMimeType(
                        modelList->index(
                            curIndex.filePath()));

                QMultiHashIterator<QString,QAction*> i(
                    *customActManager->getActions());

                while (i.hasNext()) {
                    i.next();

                    if (curIndex.completeSuffix()
                            .endsWith(i.key())) {
                        actions.append(i.value());
                    }
                }

                if (curIndex.isExecutable() ||
                    curIndex.isBundle() ||
                    type.endsWith("appimage") ||
                    curIndex.absoluteFilePath()
                        .endsWith(".desktop")) {

                    popup->addAction(runAct);
                } else {
                    popup->addAction(openAct);
                }

#ifndef Q_OS_MAC
                popup->addSeparator();
                popup->addMenu(createOpenWithMenu());
#endif

                if (!actions.isEmpty()) {
                    popup->addSeparator();
                    popup->addActions(actions);
                    popup->addSeparator();
                }

                QMultiHashIterator<QString,QMenu*> m(
                    *customActManager->getMenus());

                while (m.hasNext()) {
                    m.next();

                    if (curIndex.completeSuffix()
                            .endsWith(m.key())) {
                        popup->addMenu(m.value());
                    }
                }

                popup->addSeparator();
                popup->addAction(cutAct);
                popup->addAction(copyAct);
                popup->addAction(pasteAct);
                popup->addSeparator();
                popup->addAction(renameAct);
                popup->addSeparator();

                foreach (QMenu *parent,
                         customActManager
                             ->getMenus()
                             ->values("*")) {
                    popup->addMenu(parent);
                }

                actions =
                    customActManager
                        ->getActions()
                        ->values("*");

                popup->addActions(actions);

                if (customActManager
                        ->getActionList()
                        ->size() > 0) {
                    popup->addSeparator();
                }

                if (modelList->getRootPath() !=
                    trashDir) {
                    popup->addAction(
                        trashAct);
                }

                popup->addAction(deleteAct);
                popup->addSeparator();

                actions =
                    customActManager
                        ->getActions()
                        ->values(
                            curIndex.path());

                if (actions.count()) {
                    popup->addActions(actions);
                    popup->addSeparator();
                }
            } else {
                popup->addAction(openInTabAct);
                popup->addSeparator();
                popup->addAction(addBookmarkAct);
                popup->addSeparator();
                popup->addAction(cutAct);
                popup->addAction(copyAct);
                popup->addAction(pasteAct);
                popup->addSeparator();
                popup->addAction(renameAct);
                popup->addSeparator();

                foreach (QMenu *parent,
                         customActManager
                             ->getMenus()
                             ->values("*")) {
                    popup->addMenu(parent);
                }

                actions =
                    customActManager
                        ->getActions()
                        ->values("*");

                popup->addActions(actions);

                if (modelList->getRootPath() !=
                    trashDir) {
                    popup->addAction(trashAct);
                }

                popup->addAction(deleteAct);
                popup->addSeparator();

                foreach (QMenu *parent,
                         customActManager
                             ->getMenus()
                             ->values("folder")) {
                    popup->addMenu(parent);
                }

                actions =
                    customActManager
                        ->getActions()
                        ->values(
                            curIndex.fileName());

                actions.append(
                    customActManager
                        ->getActions()
                        ->values(
                            curIndex.path()));

                actions.append(
                    customActManager
                        ->getActions()
                        ->values("folder"));

                if (actions.count()) {
                    popup->addActions(actions);
                    popup->addSeparator();
                }
            }

            popup->addAction(folderPropertiesAct);
        } else {
            popup->addAction(backAct);
            popup->addAction(upAct);
            popup->addAction(homeAct);
            popup->addSeparator();
            popup->addAction(newDirAct);
            popup->addAction(newFileAct);
            popup->addSeparator();

            if (pasteAct->isEnabled()) {
                popup->addAction(pasteAct);
                popup->addSeparator();
            }

            popup->addAction(addBookmarkAct);
            popup->addSeparator();

            foreach (QMenu *parent,
                     customActManager
                         ->getMenus()
                         ->values("folder")) {
                popup->addMenu(parent);
            }

            actions =
                customActManager
                    ->getActions()
                    ->values(
                        curIndex.fileName());

            actions.append(
                customActManager
                    ->getActions()
                    ->values("folder"));

            if (actions.count()) {
                foreach (QAction *action,
                         actions) {
                    popup->addAction(action);
                }

                popup->addSeparator();
            }

            popup->addAction(
                folderPropertiesAct);
        }
    } else {
        if (focusWidget() ==
            bookmarksList) {

            QItemSelectionModel *selectionModel =
                activePane == 2 ?
                listSelectionModel2 :
                listSelectionModel;

            selectionModel->clearSelection();

            if (bookmarksList->indexAt(
                    bookmarksList->mapFromGlobal(
                        event->globalPos())).isValid()) {

                curIndex =
                    QFileInfo(
                        bookmarksList->currentIndex()
                            .data(BOOKMARK_PATH)
                            .toString());

                isMedia =
                    bookmarksList->currentIndex()
                        .data(MEDIA_MODEL)
                        .toBool();

                if (!isMedia) {
                    popup->addAction(
                        delBookmarkAct);

                    if (!curIndex.path().isEmpty()) {
                        popup->addAction(
                            editBookmarkAct);
                    }
                } else {
#ifndef NO_UDISKS
                    QString mediaPath =
                        bookmarksList->currentIndex()
                            .data(MEDIA_PATH)
                            .toString();

                    if (!mediaPath.isEmpty()) {
                        if (!disks->devices[mediaPath]
                                ->mountpoint.isEmpty()) {

                            popup->addAction(
                                mediaUnmountAct);
                        } else if (
                            disks->devices[mediaPath]
                                ->isOptical) {

                            popup->addAction(
                                mediaEjectAct);
                        }
                    }
#endif
                }
            } else {
                bookmarksList->clearSelection();
                popup->addAction(
                    addSeparatorAct);
                popup->addAction(
                    wrapBookmarksAct);
            }

            popup->addSeparator();
        } else {
            bookmarksList->clearSelection();
            popup->addAction(newDirAct);
            popup->addAction(newFileAct);
            popup->addAction(newWinAct);
            popup->addAction(openTabAct);
            popup->addSeparator();
            popup->addAction(cutAct);
            popup->addAction(copyAct);
            popup->addAction(pasteAct);
            popup->addSeparator();
            popup->addAction(renameAct);
            popup->addSeparator();

            if (modelList->getRootPath() !=
                trashDir) {
                popup->addAction(
                    trashAct);
            }

            popup->addAction(deleteAct);
        }

        popup->addSeparator();

        foreach (QMenu *parent,
                 customActManager
                     ->getMenus()
                     ->values("folder")) {
            popup->addMenu(parent);
        }

        actions =
            customActManager
                ->getActions()
                ->values(
                    curIndex.fileName());

        actions.append(
            customActManager
                ->getActions()
                ->values(
                    curIndex.path()));

        actions.append(
            customActManager
                ->getActions()
                ->values("folder"));

        if (actions.count()) {
            foreach (QAction *action,
                     actions) {
                popup->addAction(action);
            }

            popup->addSeparator();
        }

        if (!isMedia &&
            !curIndex.path().isEmpty()) {
            popup->addAction(
                folderPropertiesAct);
        }
    }

    popup->exec(event->globalPos());
    delete popup;
}

//---------------------------------------------------------------------------

/**
 * @brief Creates menu for opening file in selected application
 */
QMenu *MainWindow::createOpenWithMenu()
{
    qDebug() << "open with";

    QMenu *openMenu =
        new QMenu(tr("Open with"));

    QAction *selectAppAct =
        new QAction(
            tr("Select..."),
            openMenu);

    selectAppAct->setStatusTip(
        tr("Select application for opening the file"));

    connect(
        selectAppAct,
        SIGNAL(triggered()),
        this,
        SLOT(selectAppForFiles()));

    QString mime =
        mimeUtils->getMimeType(
            curIndex.filePath());

    QStringList appNames =
        mimeUtils->getDefault(mime);

    if (appNames.size() == 1 &&
        appNames.at(0).isEmpty() &&
        mime.startsWith("text/")) {

        qDebug() <<
            "get fallback apps for text/plain";

        appNames =
            mimeUtils->getDefault(
                "text/plain");
    }

    qDebug() << mime << appNames;

    QList<QAction*> defaultApps;

    foreach (QString appName, appNames) {
        if (appName.isEmpty()) {
            continue;
        }

        QString appDesktopFile =
            Common::findApplication(
                qApp->applicationFilePath(),
                appName);

        if (appDesktopFile.isEmpty()) {
            continue;
        }

        DesktopFile df(appDesktopFile);

        QAction *action =
            new QAction(
                df.getName(),
                openMenu);

        action->setData(
            appDesktopFile);

        action->setIcon(
            FileUtils::searchAppIcon(df));

        defaultApps.append(action);

        connect(
            action,
            SIGNAL(triggered()),
            SLOT(openInApp()));

        openMenu->addAction(action);
    }

    if (!defaultApps.isEmpty()) {
        openMenu->addSeparator();
    }

    openMenu->addAction(
        selectAppAct);

    return openMenu;
}

void MainWindow::updateActivePaneFrame()
{
    QWidget *pane1 = folderSplitter->widget(0);
    QWidget *pane2 = folderSplitter->widget(1);

    QWidget *panes[] = { pane1, pane2 };

    for (int i = 0; i < 2; ++i) {
        QWidget *pane = panes[i];
        bool active = (activePane == i + 1);

        pane->setStyleSheet(
            active
                ? QString(
                    "QWidget { border: 1px solid %1; }")
                    .arg(palette().color(
                        QPalette::Highlight).name())
                : QString());

        if (active) {
            QGraphicsDropShadowEffect *glow =
                new QGraphicsDropShadowEffect(pane);

            glow->setBlurRadius(14);
            glow->setOffset(0);
            glow->setColor(
                palette().color(QPalette::Highlight));

            pane->setGraphicsEffect(glow);
        } else {
            pane->setGraphicsEffect(nullptr);
        }
    }
}

bool MainWindow::eventFilter(
    QObject *o,
    QEvent *e)
{
	QWidget *widget = qobject_cast<QWidget*>(o);

	if (widget &&
	    (e->type() == QEvent::FocusIn ||
	     e->type() == QEvent::MouseButtonPress)) {

	    QWidget *pane2Widgets[] = {
	        list2, detailTree2, stackWidget2, tabs2
	    };
	    QWidget *pane1Widgets[] = {
	        list, detailTree, stackWidget, tabs
	    };

	    bool inPane2 = false;
	    bool inPane1 = false;

	    for (QWidget *child : pane2Widgets) {
	        if (widget == child || child->isAncestorOf(widget)) {
	            inPane2 = true;
	            break;
	        }
	    }

	    for (QWidget *child : pane1Widgets) {
	        if (widget == child || child->isAncestorOf(widget)) {
	            inPane1 = true;
	            break;
	        }
	    }
	    
		if (inPane2 && activePane != 2) {
		    activePane = 2;
		    pathEdit->setEditText(panePath2);
		    updateActivePaneFrame();
		} else if (inPane1 && activePane != 1) {
		    activePane = 1;
		    pathEdit->setEditText(panePath1);
		    updateActivePaneFrame();
		}
	}

	    if (e->type() ==
	        QEvent::MouseButtonPress) {

	        QMouseEvent *me =
	            static_cast<QMouseEvent*>(e);

	        switch (me->button()) {
	        case Qt::BackButton:
	            goBackDir();
	            break;

	        default:
	            break;
	        }
	    }

    if (dynamic_cast<QListView*>(o) !=
        Q_NULLPTR) {

        if (e->type() ==
            QEvent::KeyPress) {

            QKeyEvent *key =
                static_cast<QKeyEvent*>(e);

            if (key->key() ==
                Qt::Key_Tab) {

                qDebug() <<
                    "Tab pressed: path completion "
                    << o;

                QListView *completionList =
                    dynamic_cast<QListView*>(o);

                QModelIndex index =
                    completionList->currentIndex();

                QString itemText =
                    index.data(
                        Qt::DisplayRole)
                        .toString();

                QString currentPath =
                    pathEdit->lineEdit()
                        ->text();

                QStringList tempList =
                    currentPath.split("/");

                tempList.takeLast();
                tempList << itemText;

                QString newPath =
                    tempList.join("/");

                pathEdit->lineEdit()
                    ->setText(newPath);

                pathEditChanged(
                    newPath);

                QTimer::singleShot(
                    0,
                    pathEdit->lineEdit(),
                    SLOT(setFocus()));

                pathEdit->lineEdit()
                    ->setText(
                        newPath +
                        QString("/"));
            }
        }
    }

    return QMainWindow::eventFilter(
        o,
        e);
}

//---------------------------------------------------------------------------

void MainWindow::refresh(
    bool modelRefresh,
    bool loadDir)
{
    qDebug() <<
        "refresh"
        << modelRefresh
        << loadDir;

    if (modelRefresh) {
        modelList->refreshItems();
        modelList->forceRefresh();
    }

    QString path =
        activePane == 2 ?
        panePath2 :
        panePath1;

    if (modelList->setRootPath(path)) {
        modelView->invalidate();
    }

    QModelIndex baseIndex =
        modelView->mapFromSource(
            modelList->index(path));

    if (activePane == 2) {
        if (currentView == 2) {
            detailTree2->setRootIndex(
                baseIndex);
        } else {
            list2->setRootIndex(
                baseIndex);
        }
    } else {
        if (currentView == 2) {
            detailTree->setRootIndex(
                baseIndex);
        } else {
            list->setRootIndex(
                baseIndex);
        }
    }

    pathEdit->setEditText(path);

    if (loadDir) {
        qDebug() <<
            "trigger dirloaded on refresh";

        dirLoaded();
    }
}

//---------------------------------------------------------------------------

void MainWindow::enableReload()
{
    qDebug() <<
        "enable reload";

    ignoreReload = false;
}

//---------------------------------------------------------------------------

/**
 * @brief Selects application for opening file
 */
void MainWindow::selectApp()
{
    ApplicationDialog *dialog =
        new ApplicationDialog(true, this);

    if (dialog->exec()) {
        if (dialog->getCurrentLauncher()
                .compare("") != 0) {

            QString appName =
                dialog->getCurrentLauncher() +
                ".desktop";

            QString desktop =
                Common::findApplication(
                    qApp->applicationFilePath(),
                    appName);

            if (desktop.isEmpty()) {
                return;
            }

            DesktopFile df(desktop);

            mimeUtils->openInApp(
                df.getExec(),
                curIndex,
                df.isTerminal() ?
                    term :
                    "");
        }
    }
}

//---------------------------------------------------------------------------

void MainWindow::selectAppForFiles()
{
    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    QModelIndexList items;

    if (selectionModel->selectedRows(0).count()) {
        items =
            selectionModel->selectedRows(0);
    } else {
        items =
            selectionModel->selectedIndexes();
    }

    QStringList files;

    foreach (QModelIndex index, items) {
        QModelIndex srcIndex =
            modelView->mapToSource(index);

        files <<
            modelList->filePath(srcIndex);
    }

    ApplicationDialog *dialog =
        new ApplicationDialog(true, this);

    if (dialog->exec()) {
        if (dialog->getCurrentLauncher()
                .compare("") != 0) {

            QString appName =
                dialog->getCurrentLauncher() +
                ".desktop";

            QString desktop =
                Common::findApplication(
                    qApp->applicationFilePath(),
                    appName);

            if (desktop.isEmpty()) {
                return;
            }

            DesktopFile df(desktop);

            if (df.getExec().contains("%F") ||
                df.getExec().contains("%U")) {

                mimeUtils->openFilesInApp(
                    df.getExec(),
                    files,
                    df.isTerminal() ?
                        term :
                        "");
            } else {
                for (int i = 0;
                     i < files.size();
                     ++i) {

                    QFileInfo fileInfo(
                        files.at(i));

                    mimeUtils->openInApp(
                        df.getExec(),
                        fileInfo,
                        df.isTerminal() ?
                            term :
                            "");
                }
            }
        }
    }
}

//---------------------------------------------------------------------------

/**
 * @brief Opens files in application
 */
void MainWindow::openInApp()
{
    QAction *action =
        dynamic_cast<QAction*>(sender());

    if (!action) {
        return;
    }

    DesktopFile df(
        action->data().toString());

    if (df.getExec().isEmpty()) {
        return;
    }

    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    QModelIndexList items;

    if (selectionModel->selectedRows(0).count()) {
        items =
            selectionModel->selectedRows(0);
    } else {
        items =
            selectionModel->selectedIndexes();
    }

    QStringList fileList;

    foreach (QModelIndex index, items) {
        QModelIndex srcIndex =
            modelView->mapToSource(index);

        fileList <<
            modelList->filePath(srcIndex);
    }

    if (df.getExec().contains("%F") ||
        df.getExec().contains("%U")) {

        mimeUtils->openFilesInApp(
            df.getExec(),
            fileList,
            df.isTerminal() ?
                term :
                "");
    } else {
        for (int i = 0;
             i < fileList.size();
             ++i) {

            QFileInfo fileInfo(
                fileList.at(i));

            mimeUtils->openInApp(
                df.getExec(),
                fileInfo,
                df.isTerminal() ?
                    term :
                    "");
        }
    }
}

//---------------------------------------------------------------------------

void MainWindow::updateGrid()
{
    QListView *activeList =
        activePane == 2 ?
        list2 :
        list;

    if (activeList->viewMode() !=
        QListView::IconMode) {
        return;
    }

    qDebug() << "updateGrid";

    QFontMetrics fm =
        fontMetrics();

    int textWidth =
        fm.averageCharWidth() * 17;

    int realTextWidth =
        fm.averageCharWidth() * 14;

    int textHeight =
        fm.lineSpacing() * 3;

    QSize grid;

    grid.setWidth(
        qMax(zoom, textWidth));

    grid.setHeight(
        zoom + textHeight);

    QString path =
        activePane == 2 ?
        panePath2 :
        panePath1;

    QModelIndex root =
        modelList->index(path);

    QModelIndexList items;

    for (int x = 0;
         x < modelList->rowCount(root);
         ++x) {

        items.append(
            modelList->index(
                x,
                0,
                root));
    }

    foreach (QModelIndex theItem, items) {
        QString filename =
            modelList->fileName(theItem);

        QRect item(
            0,
            0,
            realTextWidth,
            grid.height());

        QSize txtsize =
            fm.boundingRect(
                item,
                Qt::AlignTop |
                Qt::AlignHCenter |
                Qt::TextWordWrap |
                Qt::TextWrapAnywhere,
                filename).size();

        int newHeight =
            txtsize.height() +
            zoom +
            5 +
            8 +
            4;

        if (txtsize.width() >
            grid.width()) {

            grid.setWidth(
                txtsize.width());
        }

        if (newHeight >
            grid.height()) {

            grid.setHeight(
                newHeight);
        }
    }

    if (activeList->gridSize() !=
        grid) {
        activeList->setGridSize(grid);
    }
}

//---------------------------------------------------------------------------

/**
 * @brief media support
 */
#ifndef NO_UDISKS

void MainWindow::populateMedia()
{
    QMapIterator<QString, Device*> device(
        disks->devices);

    while (device.hasNext()) {
        device.next();

        if (mediaBookmarkExists(
                device.value()->path) > -1) {
            continue;
        }

        if ((device.value()->isOptical &&
             !device.value()->hasMedia)

#ifndef __FreeBSD__
            || (!device.value()->isOptical &&
                !device.value()->isRemovable)
#endif
            || (!device.value()->isOptical &&
                !device.value()->hasPartition)) {

            continue;
        }

        modelBookmarks->addBookmark(
            QString("%1 (%2)")
                .arg(device.value()->name)
                .arg(device.value()->dev),
            device.value()->mountpoint,
            "",
            device.value()->isOptical ?
                "drive-optical" :
                "drive-removable-media",
            device.value()->path,
            true,
            false);
    }
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaMountpointChanged(
    QString path,
    QString mountpoint)
{
    Q_UNUSED(mountpoint)

    if (path.isEmpty()) {
        return;
    }

    for (int i = 0;
         i < modelBookmarks->rowCount();
         ++i) {

        if (modelBookmarks->item(i)
                ->data(MEDIA_MODEL)
                .toBool() &&
            modelBookmarks->item(i)
                ->data(MEDIA_PATH)
                .toString() == path) {

            modelBookmarks->item(i)->setData(
                disks->devices[path]->mountpoint,
                BOOKMARK_PATH);
        }
    }
}

//---------------------------------------------------------------------------

int MainWindow::mediaBookmarkExists(
    QString path)
{
    if (path.isEmpty()) {
        return -1;
    }

    for (int i = 0;
         i < modelBookmarks->rowCount();
         ++i) {

        if (modelBookmarks->item(i)
                ->data(MEDIA_MODEL)
                .toBool() &&
            modelBookmarks->item(i)
                ->data(MEDIA_PATH)
                .toString() == path) {

            return i;
        }
    }

    return -1;
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaAdded(
    QString path)
{
    Q_UNUSED(path)
    populateMedia();
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaRemoved(
    QString path)
{
    int bookmark =
        mediaBookmarkExists(path);

    if (bookmark > -1) {
        modelBookmarks->removeRow(bookmark);
    }
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaChanged(
    QString path,
    bool present)
{
    if (path.isEmpty()) {
        return;
    }

    if (disks->devices[path]->isOptical &&
        !present &&
        mediaBookmarkExists(path) > -1) {

        handleMediaRemoved(path);

    } else if (
        disks->devices[path]->isOptical &&
        present &&
        mediaBookmarkExists(path) == -1) {

        handleMediaAdded(path);
    }
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaUnmount()
{
    QStandardItem *item =
        modelBookmarks->itemFromIndex(
            bookmarksList->currentIndex());

    if (item == Q_NULLPTR) {
        return;
    }

    QString path =
        item->data(MEDIA_PATH).toString();

    if (path.isEmpty()) {
        return;
    }

    disks->devices[path]->unmount();
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaEject()
{
    QStandardItem *item =
        modelBookmarks->itemFromIndex(
            bookmarksList->currentIndex());

    if (item == Q_NULLPTR) {
        return;
    }

    QString path =
        item->data(MEDIA_PATH).toString();

    if (path.isEmpty()) {
        return;
    }

    disks->devices[path]->eject();
}

//---------------------------------------------------------------------------

void MainWindow::handleMediaError(
    QString path,
    QString error)
{
    QMessageBox::warning(
        this,
        path,
        error);
}

#endif

//---------------------------------------------------------------------------

void MainWindow::clearCache()
{
    settings->setValue(
        "clearCache",
        true);

    QMessageBox::information(
        this,
        tr("Close window"),
        tr("Please close window to apply action."));
}

//---------------------------------------------------------------------------

void MainWindow::handlePathRequested(
    QString path)
{
    qDebug() <<
        "handle service path requested"
        << path;

    QString currentPath =
        activePane == 2 ?
        panePath2 :
        panePath1;

    if (path == currentPath ||
        path.isEmpty()) {
        return;
    }

    if (path.contains("/.")) {
        modelList->setRootPath(path);
    }

    pathEdit->setItemText(
        0,
        path);

    QTimer::singleShot(
        100,
        this,
        SLOT(slowPathEdit()));
}

//---------------------------------------------------------------------------

void MainWindow::slowPathEdit()
{
    pathEditChanged(
        pathEdit->currentText());

    status->showMessage(
        Common::getDriveInfo(
            curIndex.filePath()));
}

//---------------------------------------------------------------------------

void MainWindow::actionMapper(
    QString cmd)
{
    QModelIndexList selList;
    QStringList temp;

    QItemSelectionModel *selectionModel =
        activePane == 2 ?
        listSelectionModel2 :
        listSelectionModel;

    if (focusWidget() == list ||
        focusWidget() == detailTree ||
        focusWidget() == list2 ||
        focusWidget() == detailTree2) {

        QFileInfo file =
            modelList->fileInfo(
                modelView->mapToSource(
                    selectionModel->currentIndex()));

        if (file.isDir()) {
            cmd.replace(
                "%n",
                file.fileName()
                    .replace(" ", "\\"));
        } else {
            cmd.replace(
                "%n",
                file.baseName()
                    .replace(" ", "\\"));
        }

        if (selectionModel
                ->selectedRows(0)
                .count()) {

            selList =
                selectionModel->selectedRows(0);
        } else {
            selList =
                selectionModel->selectedIndexes();
        }
    } else {
        selList <<
            modelView->mapFromSource(
                modelList->index(
                    curIndex.filePath()));
    }

    cmd.replace(
        "~",
        QDir::homePath());

    int pos = 0;

    while (pos >= 0) {
        pos =
            cmd.indexOf(
                "%i",
                pos);

        if (pos != -1) {
            pos += 2;

            QString var =
                cmd.mid(
                    pos,
                    cmd.indexOf(" ", pos) -
                    pos);

            QString input =
                QInputDialog::getText(
                    this,
                    tr("Input"),
                    var,
                    QLineEdit::Normal);

            if (input.isNull()) {
                return;
            }

            cmd.replace(
                "%i" + var,
                input);
        }
    }

    for (const QModelIndex &index : selList) {
        temp.append(
            modelList->fileName(
                modelView->mapToSource(index))
                .replace(" ", "\\"));
    }

    cmd.replace(
        "%f",
        temp.join(" "));

    temp.clear();

    for (const QModelIndex &index : selList) {
        temp.append(
            modelList->filePath(
                modelView->mapToSource(index))
                .replace(" ", "\\"));
    }

    cmd.replace(
        "%F",
        temp.join(" "));

    customActManager->execAction(
        cmd,
        activePane == 2 ?
            panePath2 :
            panePath1);
}

//---------------------------------------------------------------------------

void MainWindow::clearCutItems()
{
    qDebug() << "clearCutItems";

    modelList->clearCutItems();
    modelList->update();

    QString path =
        activePane == 2 ?
        panePath2 :
        panePath1;

    QModelIndex baseIndex =
        modelView->mapFromSource(
            modelList->index(path));

    if (activePane == 2) {
        if (currentView == 2) {
            detailTree2->setRootIndex(
                baseIndex);
        } else {
            list2->setRootIndex(
                baseIndex);
        }
    } else {
        if (currentView == 2) {
            detailTree->setRootIndex(
                baseIndex);
        } else {
            list->setRootIndex(
                baseIndex);
        }
    }

    qDebug() <<
        "trigger updateDir from clearCutItems";

    QTimer::singleShot(
        50,
        this,
        SLOT(updateDir()));
}