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

#include "mymodel.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <QApplication>
#include <QBuffer>
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QImageReader>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include "fileutils.h"

//---------------------------------------------------------------------------

static QByteArray imageToThumbnail(const QImage &source,
                                   const QColor &background = Qt::transparent)
{
    if (source.isNull())
        return QByteArray();

    QImage canvas(128, 128, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(background);

    QImage image = source.scaled(120, 120,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);

    QPainter painter(&canvas);
    painter.drawImage((128 - image.width()) / 2,
                      (128 - image.height()) / 2,
                      image);
    painter.end();

    QByteArray result;
    QBuffer buffer(&result);

    if (!buffer.open(QIODevice::WriteOnly) || !canvas.save(&buffer, "PNG"))
        return QByteArray();

    return result;
}

//---------------------------------------------------------------------------

/**
 * @brief Creates file system model
 * @param realMime
 * @param mimeUtils
 */
myModel::myModel(bool realMime, MimeUtils *mimeUtils, QObject *parent)
    : QAbstractItemModel(parent)
{
    mimeUtilsPtr = mimeUtils;

    mimeGeneric = new QHash<QString, QString>;
    mimeGlob = new QHash<QString, QString>;
    mimeIcons = new QHash<QString, QIcon>;
    folderIcons = new QHash<QString, QIcon>;
    thumbs = new QHash<QString, QByteArray>;
    icons = new QCache<QString, QIcon>;
    icons->setMaxCost(500);

    QFile fileIcons(QString("%1/file.cache").arg(Common::configDir()));
    if (fileIcons.open(QIODevice::ReadOnly)) {
        QDataStream out(&fileIcons);
        out >> *mimeIcons;
        fileIcons.close();
    }

    fileIcons.setFileName(QString("%1/folder.cache").arg(Common::configDir()));
    if (fileIcons.open(QIODevice::ReadOnly)) {
        QDataStream out(&fileIcons);
        out.setDevice(&fileIcons);
        out >> *folderIcons;
        fileIcons.close();
    }

    rootItem = new myModelItem(QFileInfo("/"),
                               new myModelItem(QFileInfo(), Q_NULLPTR));
    currentRootPath = "/";

    QDir root("/");
    QFileInfoList drives = root.entryInfoList(
        QDir::AllEntries | QDir::Files | QDir::Hidden |
        QDir::System | QDir::NoDotAndDotDot);

    foreach (QFileInfo drive, drives)
        new myModelItem(drive, rootItem);

    rootItem->walked = true;
    rootItem = rootItem->parent();

    iconFactory = new QFileIconProvider();

    inotifyFD = inotify_init();
    notifier = new QSocketNotifier(inotifyFD, QSocketNotifier::Read, this);
    connect(notifier, SIGNAL(activated(int)), this, SLOT(notifyChange()));
    connect(&eventTimer, SIGNAL(timeout()), this, SLOT(eventTimeout()));

    realMimeTypes = realMime;
}

//---------------------------------------------------------------------------

/**
 * @brief Deletes model of file system
 */
myModel::~myModel()
{
    if (inotifyFD >= 0)
        close(inotifyFD);

    delete mimeGeneric;
    delete mimeGlob;
    delete mimeIcons;
    delete folderIcons;
    delete thumbs;
    delete icons;
    delete rootItem;
    delete iconFactory;
}

//---------------------------------------------------------------------------

/**
 * @brief Deletes icon cache
 */
void myModel::clearIconCache()
{
    folderIcons->clear();
    mimeIcons->clear();
    QFile(QString("%1/folder.cache").arg(Common::configDir())).remove();
    QFile(QString("%1/file.cache").arg(Common::configDir())).remove();
}

//---------------------------------------------------------------------------

void myModel::forceRefresh()
{
    qDebug() << "force refresh model view";
    beginResetModel();
    endResetModel();
}

//---------------------------------------------------------------------------

/**
 * @brief Sets whether use real mime types or not
 * @param realMimeTypes
 */
void myModel::setRealMimeTypes(bool realMimeTypes)
{
    this->realMimeTypes = realMimeTypes;
}

//---------------------------------------------------------------------------

/**
 * @brief Returns true if real mime types are used
 * @return real mime types
 */
bool myModel::isRealMimeTypes() const
{
    return realMimeTypes;
}

//---------------------------------------------------------------------------

/**
 * @brief Returns mime utils
 * @return mime utils
 */
MimeUtils *myModel::getMimeUtils() const
{
    return mimeUtilsPtr;
}

//---------------------------------------------------------------------------

QModelIndex myModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return QModelIndex();

    myModelItem *parentItem =
        static_cast<myModelItem *>(parent.internalPointer());

    if (!parentItem)
        parentItem = rootItem;

    myModelItem *childItem = parentItem->childAt(row);
    if (childItem)
        return createIndex(row, column, childItem);

    return QModelIndex();
}

//---------------------------------------------------------------------------

QModelIndex myModel::index(const QString &path) const
{
    myModelItem *item = rootItem->matchPath(path.split(SEPARATOR));
    if (item)
        return createIndex(item->childNumber(), 0, item);

    return QModelIndex();
}

//---------------------------------------------------------------------------

QModelIndex myModel::parent(const QModelIndex &index) const
{
    if (!index.isValid())
        return QModelIndex();

    myModelItem *childItem =
        static_cast<myModelItem *>(index.internalPointer());

    if (!childItem)
        return QModelIndex();

    myModelItem *parentItem = childItem->parent();

    if (!parentItem || parentItem == rootItem)
        return QModelIndex();

    return createIndex(parentItem->childNumber(), 0, parentItem);
}

//---------------------------------------------------------------------------

bool myModel::isDir(const QModelIndex &index)
{
    if (!index.isValid())
        return false;

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item && item != rootItem)
        return item->fileInfo().isDir();

    return false;
}

//---------------------------------------------------------------------------

QFileInfo myModel::fileInfo(const QModelIndex &index)
{
    if (!index.isValid())
        return QFileInfo();

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item)
        return item->fileInfo();

    return QFileInfo();
}

//---------------------------------------------------------------------------

qint64 myModel::size(const QModelIndex &index)
{
    if (!index.isValid())
        return 0;

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item)
        return item->fileInfo().size();

    return 0;
}

//---------------------------------------------------------------------------

QString myModel::fileName(const QModelIndex &index)
{
    if (!index.isValid())
        return QString();

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item)
        return item->fileName();

    return QString();
}

//---------------------------------------------------------------------------

QString myModel::filePath(const QModelIndex &index)
{
    if (!index.isValid())
        return QString();

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item)
        return item->absoluteFilePath();

    return QString();
}

//---------------------------------------------------------------------------

QString myModel::getMimeType(const QModelIndex &index)
{
    if (!index.isValid())
        return QString();

    qDebug() << "myModel getMimeType";

    myModelItem *item = static_cast<myModelItem *>(index.internalPointer());
    if (item->mMimeType.isNull()) {
        if (realMimeTypes) {
            item->mMimeType =
                mimeUtilsPtr->getMimeType(item->absoluteFilePath());
        } else {
            if (item->fileInfo().isDir())
                item->mMimeType = "folder";
            else
                item->mMimeType = item->fileInfo().suffix();

            if (item->mMimeType.isNull())
                item->mMimeType = "file";
        }
    }

    return item->mMimeType;
}

//---------------------------------------------------------------------------

void myModel::notifyChange()
{
    notifier->setEnabled(0);

    int buffSize = 0;
    ioctl(inotifyFD, FIONREAD, (char *)&buffSize);

    QByteArray buffer;
    buffer.resize(buffSize);
    read(inotifyFD, buffer.data(), buffSize);

    const char *at = buffer.data();
    const char * const end = at + buffSize;

    while (at < end) {
        const inotify_event *event =
            reinterpret_cast<const inotify_event *>(at);

        int w = event->wd;
        lastEventFilename = event->name;

        if (eventTimer.isActive()) {
            if (w == lastEventID) {
                eventTimer.start(40);
            } else {
                eventTimer.stop();
                notifyProcess(lastEventID, lastEventFilename);
                lastEventID = w;
                eventTimer.start(40);
            }
        } else {
            lastEventID = w;
            eventTimer.start(40);
        }

        at += sizeof(inotify_event) + event->len;
    }

    notifier->setEnabled(1);
}

//---------------------------------------------------------------------------

void myModel::eventTimeout()
{
    notifyProcess(lastEventID, lastEventFilename);
    eventTimer.stop();
}

//---------------------------------------------------------------------------

void myModel::notifyProcess(int eventID, QString fileName)
{
    qDebug() << "notifyProcess" << eventID << fileName;

    QString folderChanged;

    if (watchers.contains(eventID)) {
        myModelItem *parent =
            rootItem->matchPath(watchers.value(eventID).split(SEPARATOR));

        if (parent) {
            parent->dirty = 1;

            QDir dir(parent->absoluteFilePath());
            folderChanged = dir.absolutePath();

            QFileInfoList all = dir.entryInfoList(
                QDir::AllEntries | QDir::NoDotAndDotDot |
                QDir::Hidden | QDir::System);

            foreach (myModelItem *child, parent->children()) {
                if (all.contains(child->fileInfo())) {
                    all.removeOne(child->fileInfo());
                } else {
                    if (child->fileInfo().isDir()) {
                        int wd = watchers.key(child->absoluteFilePath());
                        inotify_rm_watch(inotifyFD, wd);
                        watchers.remove(wd);
                    }

                    beginRemoveRows(index(parent->absoluteFilePath()),
                                    child->childNumber(),
                                    child->childNumber());
                    parent->removeChild(child);
                    endRemoveRows();
                }
            }

            foreach (QFileInfo one, all) {
                beginInsertRows(index(parent->absoluteFilePath()),
                                parent->childCount(),
                                parent->childCount());
                new myModelItem(one, parent);
                endInsertRows();
            }
        }
    } else {
        inotify_rm_watch(inotifyFD, eventID);
        watchers.remove(eventID);
    }

    if (!fileName.isEmpty() && showThumbs)
        lastEventFilename = fileName;

    if (!folderChanged.isEmpty()) {
        qDebug() << "folder modified" << folderChanged;
        emit reloadDir(folderChanged);
    }
}

//---------------------------------------------------------------------------

void myModel::addWatcher(myModelItem *item)
{
    qDebug() << "addWatcher" << item->absoluteFilePath();

    while (item != rootItem) {
        watchers.insert(
            inotify_add_watch(inotifyFD,
                              item->absoluteFilePath().toLocal8Bit(),
                              IN_CREATE | IN_MODIFY | IN_MOVE | IN_DELETE),
            item->absoluteFilePath());

        item->watched = 1;
        item = item->parent();
    }
}

//---------------------------------------------------------------------------

bool myModel::setRootPath(const QString &path)
{
    currentRootPath = path;

    myModelItem *item = rootItem->matchPath(path.split(SEPARATOR));

    if (item == Q_NULLPTR) {
        QMessageBox::warning(Q_NULLPTR, tr("No such directory"),
                             tr("Directory requested does not exists."));
        return false;
    }

    if (!item->watched)
        addWatcher(item);

    if (!item->walked || !item->watched) {
        populateItem(item);
        return false;
    } else if (item->dirty) {
        item->dirty = false;
        return true;
    }

    return false;
}

//---------------------------------------------------------------------------

QString myModel::getRootPath()
{
    return currentRootPath;
}

//---------------------------------------------------------------------------

bool myModel::canFetchMore(const QModelIndex &parent) const
{
    myModelItem *item =
        static_cast<myModelItem *>(parent.internalPointer());

    if (item && item->walked)
        return false;

    return true;
}

//---------------------------------------------------------------------------

void myModel::fetchMore(const QModelIndex &parent)
{
    myModelItem *item =
        static_cast<myModelItem *>(parent.internalPointer());

    if (item) {
        populateItem(item);
        emit dataChanged(parent, parent);
    }
}

//---------------------------------------------------------------------------

void myModel::populateItem(myModelItem *item)
{
    if (item == Q_NULLPTR)
        return;

    item->walked = 1;

    QDir dir(item->absoluteFilePath());
    QFileInfoList all = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot |
        QDir::Hidden | QDir::System);

    foreach (QFileInfo one, all)
        new myModelItem(one, item);
}

//---------------------------------------------------------------------------

int myModel::columnCount(const QModelIndex &parent) const
{
    return (parent.column() > 0) ? 0 : 5;
}

//---------------------------------------------------------------------------

int myModel::rowCount(const QModelIndex &parent) const
{
    myModelItem *item =
        static_cast<myModelItem *>(parent.internalPointer());

    if (item)
        return item->childCount();

    return rootItem->childCount();
}

//---------------------------------------------------------------------------

void myModel::refresh()
{
    myModelItem *item = rootItem->matchPath(QStringList("/"));

    foreach (int w, watchers.keys())
        inotify_rm_watch(inotifyFD, w);

    watchers.clear();

    beginResetModel();
    if (item)
        item->clearAll();
    endResetModel();
}

//---------------------------------------------------------------------------

void myModel::update()
{
    myModelItem *item =
        rootItem->matchPath(currentRootPath.split(SEPARATOR));

    if (item == Q_NULLPTR)
        return;

    foreach (myModelItem *child, item->children())
        child->refreshFileInfo();
}

//---------------------------------------------------------------------------

void myModel::refreshItems()
{
    myModelItem *item =
        rootItem->matchPath(currentRootPath.split(SEPARATOR));

    if (item == Q_NULLPTR)
        return;

    qDebug() << "refresh items";
    item->clearAll();
    populateItem(item);
}

//---------------------------------------------------------------------------

QModelIndex myModel::insertFolder(QModelIndex parent)
{
    myModelItem *item =
        static_cast<myModelItem *>(parent.internalPointer());

    int num = 0;
    QString name;

    do {
        num++;
        name = QString("new_folder%1").arg(num);
    } while (item->hasChild(name));

    QDir temp(currentRootPath);
    if (!temp.mkdir(name))
        return QModelIndex();

    beginInsertRows(parent, item->childCount(), item->childCount());
    new myModelItem(QFileInfo(currentRootPath + "/" + name), item);
    endInsertRows();

    return index(item->childCount() - 1, 0, parent);
}

//---------------------------------------------------------------------------

QModelIndex myModel::insertFile(QModelIndex parent)
{
    myModelItem *item =
        static_cast<myModelItem *>(parent.internalPointer());

    int num = 0;
    QString name;

    do {
        num++;
        name = QString("new_file%1").arg(num);
    } while (item->hasChild(name));

    QFile temp(currentRootPath + "/" + name);
    if (!temp.open(QIODevice::WriteOnly))
        return QModelIndex();

    temp.close();

    beginInsertRows(parent, item->childCount(), item->childCount());
    new myModelItem(QFileInfo(temp), item);
    endInsertRows();

    return index(item->childCount() - 1, 0, parent);
}

//---------------------------------------------------------------------------

Qt::DropActions myModel::supportedDropActions() const
{
    return Qt::CopyAction | Qt::MoveAction;
}

//---------------------------------------------------------------------------

QStringList myModel::mimeTypes() const
{
    return QStringList("text/uri-list");
}

//---------------------------------------------------------------------------

QMimeData *myModel::mimeData(const QModelIndexList &indexes) const
{
    QMimeData *data = new QMimeData;
    QList<QUrl> files;

    foreach (QModelIndex index, indexes) {
        myModelItem *item =
            static_cast<myModelItem *>(index.internalPointer());

        if (!item)
            continue;

        QUrl url = QUrl::fromLocalFile(item->absoluteFilePath());
        if (!files.contains(url))
            files.append(url);
    }

    data->setUrls(files);
    return data;
}

//---------------------------------------------------------------------------

void myModel::cacheInfo()
{
    QFile fileIcons(QString("%1/file.cache").arg(Common::configDir()));

    if (fileIcons.open(QIODevice::WriteOnly)) {
        QDataStream out(&fileIcons);
        out << *mimeIcons;
        fileIcons.close();
    }

    fileIcons.setFileName(QString("%1/folder.cache").arg(Common::configDir()));

    if (fileIcons.open(QIODevice::WriteOnly)) {
        QDataStream out(&fileIcons);
        out.setDevice(&fileIcons);
        out << *folderIcons;
        fileIcons.close();
    }

    if (thumbs->count() > thumbCount) {
        fileIcons.setFileName(
            QString("%1/thumbs.cache").arg(Common::configDir()));

        if (fileIcons.size() > 10000000) {
            fileIcons.remove();
        } else if (fileIcons.open(QIODevice::WriteOnly)) {
            QDataStream out(&fileIcons);
            out.setDevice(&fileIcons);
            out << *thumbs;
            fileIcons.close();
        }
    }
}

//---------------------------------------------------------------------------

/**
 * @brief Sets indicator whether to show thumbnails of pictures
 * @param icons
 */
void myModel::setMode(bool icons)
{
    showThumbs = icons;
}

//---------------------------------------------------------------------------

/**
 * @brief Loads mime types
 */
void myModel::loadMimeTypes() const
{
    QMapIterator<QString, QString> globs(
        Common::getMimesGlobs(qApp->applicationFilePath()));

    while (globs.hasNext()) {
        globs.next();
        mimeGlob->insert(globs.value(), globs.key());
    }

    QMapIterator<QString, QString> generic(
        Common::getMimesGeneric(qApp->applicationFilePath()));

    while (generic.hasNext()) {
        generic.next();
        mimeGeneric->insert(generic.key(), generic.value());
    }
}

//---------------------------------------------------------------------------

/**
 * @brief Loads thumbnails
 * @param indexes
 */
void myModel::loadThumbs(QModelIndexList indexes)
{
    QStringList files;

    foreach (QModelIndex index, indexes) {
        if (!index.isValid())
            continue;

        QString filename = filePath(index);
        if (filename.isEmpty())
            continue;

        QString mimetype = mimeUtilsPtr->getMimeType(filename);

        if (mimetype.startsWith("image") ||
            mimetype.startsWith("video") ||
            mimetype.startsWith("audio") ||
            mimetype == "application/pdf" ||
            filename.endsWith(".desktop", Qt::CaseInsensitive)) {
            files.append(filename);
        }
    }

    if (files.isEmpty())
        return;

    QFileInfo pathInfo(files.first());

    if (thumbs->count() == 0) {
        QFile cacheFile(QString("%1/thumbs.cache").arg(Common::configDir()));

        if (cacheFile.open(QIODevice::ReadOnly)) {
            QDataStream in(&cacheFile);
            in >> *thumbs;
            cacheFile.close();
        }

        thumbCount = thumbs->count();
    }

    foreach (const QString &filename, files) {
        const bool changed =
            !lastEventFilename.isEmpty() &&
            QFileInfo(filename).fileName() == lastEventFilename;

        if (!thumbs->contains(filename) || changed) {
            QByteArray thumbnail = getThumb(filename);

            if (!thumbnail.isEmpty())
                thumbs->insert(filename, thumbnail);

            if (changed) {
                lastEventFilename.clear();

                QFile cacheFile(
                    QString("%1/thumbs.cache").arg(Common::configDir()));

                if (cacheFile.size() > 10000000) {
                    cacheFile.remove();
                } else if (cacheFile.open(QIODevice::WriteOnly)) {
                    QDataStream out(&cacheFile);
                    out << *thumbs;
                    cacheFile.close();
                }
            }
        }
    }

    emit thumbUpdate(pathInfo.absolutePath());
}

//---------------------------------------------------------------------------

/**
 * @brief Creates a thumbnail for the given item.
 * @param item
 * @return thumbnail
 */
QByteArray myModel::getThumb(QString item)
{
    if (item.isEmpty())
        return QByteArray();

    if (item.endsWith(".desktop", Qt::CaseInsensitive)) {
        QString iconFile = Common::findIcon(
            "", QIcon::themeName(), Common::getDesktopIcon(item));

        if (iconFile.isEmpty())
            return QByteArray();

        QImageReader reader(iconFile);
        reader.setAutoTransform(true);
        return imageToThumbnail(reader.read());
    }

    QString itemMime = mimeUtilsPtr->getMimeType(item);

    if (itemMime.startsWith("video")) {
        QByteArray cover = getVideoFrame(item, true);
        if (!cover.isEmpty())
            return cover;

        return getVideoFrame(item, false);
    }

    if (itemMime.startsWith("audio"))
        return getVideoFrame(item, true);

    if (itemMime == "application/pdf")
        return QByteArray();

    QImageReader reader(item);
    reader.setAutoTransform(true);

    QImage image = reader.read();
    if (image.isNull())
        return QByteArray();

    return imageToThumbnail(
        image, QApplication::palette().color(QPalette::Base));
}

//---------------------------------------------------------------------------

/**
 * @brief Extracts embedded cover art or a video frame with Qt Multimedia.
 */
QByteArray myModel::getVideoFrame(QString file, bool getEmbedded,
                                  int videoFrame, int pixSize)
{
    if (file.isEmpty())
        return QByteArray();

    QMediaPlayer player;
    QVideoSink videoSink;
    QEventLoop loop;
    QTimer timeout;
    QImage image;
    bool finished = false;

    player.setVideoSink(&videoSink);

    timeout.setSingleShot(true);

    auto finish = [&]() {
        if (finished)
            return;

        finished = true;
        loop.quit();
    };

    auto readCover = [&]() {
        const QImage cover =
            player.metaData().value(QMediaMetaData::CoverArtImage).value<QImage>();

        if (!cover.isNull()) {
            image = cover;
            finish();
        }
    };

    connect(&player, &QMediaPlayer::metaDataChanged, &loop, [&]() {
        if (getEmbedded)
            readCover();
    });

    connect(&videoSink, &QVideoSink::videoFrameChanged, &loop,
            [&](const QVideoFrame &frame) {
        if (getEmbedded || finished || !frame.isValid())
            return;

        QImage frameImage = frame.toImage();
        if (!frameImage.isNull()) {
            image = frameImage;
            finish();
        }
    });

    connect(&player, &QMediaPlayer::mediaStatusChanged, &loop,
            [&](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia ||
            status == QMediaPlayer::NoMedia ||
            status == QMediaPlayer::EndOfMedia) {
            if (getEmbedded)
                readCover();

            finish();
        }
    });

    connect(&player, &QMediaPlayer::errorOccurred, &loop,
            [&](QMediaPlayer::Error, const QString &) {
        finish();
    });

    connect(&timeout, &QTimer::timeout, &loop, [&]() {
        if (getEmbedded)
            readCover();

        finish();
    });

    player.setSource(QUrl::fromLocalFile(file));

    timeout.start(getEmbedded ? 4000 : 5000);

    if (!getEmbedded) {
        if (videoFrame >= 0)
            player.setPosition(videoFrame * 40);

        player.play();
    } else {
        player.play();
    }

    if (!finished)
        loop.exec();

    timeout.stop();
    player.stop();

    if (image.isNull())
        return QByteArray();

    if (pixSize > 0 && (image.width() > pixSize || image.height() > pixSize)) {
        image = image.scaled(pixSize, pixSize,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    return imageToThumbnail(image);
}

//---------------------------------------------------------------------------

void myModel::setTheme(const QString &filename)
{
    colors = Common::loadTheme(filename);

    QSettings settings(filename, QSettings::IniFormat);
    colorFileNames = settings.value(
        "Appearance/ColorFileNames",
        Common::readSetting("fileColor", "false")).toBool();

    hiddenColor = QColor(settings.value(
        "FileColors/Hidden", colors.dark().color()).toString());

    symlinkColor = QColor(settings.value(
        "FileColors/SymbolicLink", colors.link().color()).toString());

    executableColor = QColor(settings.value(
        "FileColors/Executable", QColor(Qt::darkGreen)).toString());

    cutColor = QColor(settings.value(
        "FileColors/Cut", colors.windowText().color()).toString());

    if (!hiddenColor.isValid())
        hiddenColor = colors.dark().color();

    if (!symlinkColor.isValid())
        symlinkColor = colors.link().color();

    if (!executableColor.isValid())
        executableColor = QColor(Qt::darkGreen);

    if (!cutColor.isValid())
        cutColor = colors.windowText().color();

    beginResetModel();
    endResetModel();
}

//---------------------------------------------------------------------------

/**
 * @brief Returns model data (information about directories and files)
 * @param index
 * @param role
 * @return model data
 */
QVariant myModel::data(const QModelIndex &index, int role) const
{
    myModelItem *item =
        static_cast<myModelItem *>(index.internalPointer());

    if (!item)
        return QVariant();

    if (role == Qt::ForegroundRole) {
        if (!colorFileNames)
            return colors.windowText();

        QFileInfo type(item->fileInfo());

        if (cutItems.contains(type.filePath()))
            return cutColor;
        else if (type.isHidden())
            return hiddenColor;
        else if (type.isSymLink())
            return symlinkColor;
        else if (type.isDir())
            return colors.windowText();
        else if (type.isExecutable())
            return executableColor;

        return colors.windowText();
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == 1)
            return QVariant(Qt::AlignRight | Qt::AlignVCenter);
    } else if (role == Qt::DisplayRole) {
        QVariant result;

        switch (index.column()) {
        case 0:
            result = item->fileName();
            break;
        case 1:
            result = item->fileInfo().isDir()
                ? "" : Common::formatSize(item->fileInfo().size());
            break;
        case 2:
            if (item->mMimeType.isNull()) {
                if (realMimeTypes) {
                    item->mMimeType =
                        mimeUtilsPtr->getMimeType(item->absoluteFilePath());
                } else {
                    item->mMimeType = item->fileInfo().isDir()
                        ? "folder" : item->fileInfo().suffix();

                    if (item->mMimeType.isNull())
                        item->mMimeType = "file";
                }
            }

            result = item->mMimeType;
            break;
        case 3:
            result = item->fileInfo().lastModified().toString("yyyy-MM-dd");
            break;
        case 4: {
            if (item->mPermissions.isNull()) {
                QString str;
                QFile::Permissions perms = item->fileInfo().permissions();

                str.append(perms.testFlag(QFileDevice::ReadOwner) ? "r" : "-");
                str.append(perms.testFlag(QFileDevice::WriteOwner) ? "w" : "-");
                str.append(perms.testFlag(QFileDevice::ExeOwner) ? "x" : "-");
                str.append(perms.testFlag(QFileDevice::ReadGroup) ? "r" : "-");
                str.append(perms.testFlag(QFileDevice::WriteGroup) ? "w" : "-");
                str.append(perms.testFlag(QFileDevice::ExeGroup) ? "x" : "-");
                str.append(perms.testFlag(QFileDevice::ReadOther) ? "r" : "-");
                str.append(perms.testFlag(QFileDevice::WriteOther) ? "w" : "-");
                str.append(perms.testFlag(QFileDevice::ExeOther) ? "x" : "-");
                str.append(" " + item->fileInfo().owner() + " " +
                           item->fileInfo().group());

                item->mPermissions = str;
            }

            return item->mPermissions;
        }
        default:
            result = "";
            break;
        }

        return result;
    } else if (role == Qt::DecorationRole) {
        if (index.column() != 0)
            return QVariant();

        return findIcon(item);
    } else if (role == Qt::EditRole) {
        return item->fileName();
    }

    if (role == Qt::StatusTipRole)
        return item->fileName();

    return QVariant();
}

//---------------------------------------------------------------------------

/**
 * @brief Finds icon of a file
 * @param item
 * @return icon
 */
QVariant myModel::findIcon(myModelItem *item) const
{
    if (!item)
        return QIcon();

    QFileInfo type(item->fileInfo());

    if (type.isDir()) {
        if (folderIcons->contains(type.fileName()))
            return folderIcons->value(type.fileName());

#ifdef Q_OS_MAC
        if (type.fileName().endsWith(".app"))
            return iconFactory->icon(type);

        return QIcon::fromTheme("folder", iconFactory->icon(type));
#else
        return iconFactory->icon(type);
#endif
    }

    if (showThumbs && thumbs->contains(item->absoluteFilePath())) {
        QPixmap pic;
        pic.loadFromData(thumbs->value(item->absoluteFilePath()));

        icons->insert(item->absoluteFilePath(), new QIcon(pic), 1);
        return *icons->object(item->absoluteFilePath());
    }

    QString suffix = FileUtils::getRealSuffix(type.fileName());

    if (mimeIcons->contains(suffix))
        return mimeIcons->value(suffix);

    QIcon theIcon;

    if (suffix.isEmpty()) {
        if (!type.isExecutable()) {
            QString mime = mimeUtilsPtr->getMimeType(type.absoluteFilePath());
            return FileUtils::searchMimeIcon(mime);
        }

        suffix = "exec";

        if (mimeIcons->contains(suffix))
            theIcon = mimeIcons->value(suffix);
        else
            theIcon = QIcon::fromTheme("application-x-executable", theIcon);
    } else {
        if (mimeGlob->count() == 0)
            loadMimeTypes();

        QString mimeType = mimeGlob->value(suffix.toLower(), "");

        if (mimeType.isEmpty()) {
            mimeType = mimeUtilsPtr->getMimeType(type.absoluteFilePath());
            mimeGlob->insert(suffix.toLower(), mimeType);
        }

        theIcon = FileUtils::searchMimeIcon(mimeType);
    }

    mimeIcons->insert(suffix, theIcon);
    return theIcon;
}

//---------------------------------------------------------------------------

/**
 * @brief Finds icon of a file, based on file mime type
 * @param item
 * @return icon
 */
QVariant myModel::findMimeIcon(myModelItem *item) const
{
    if (!item)
        return QIcon();

    QString mime = mimeUtilsPtr->getMimeType(item->absoluteFilePath());

    if (mimeIcons->contains(mime))
        return mimeIcons->value(mime);

    QIcon theIcon = FileUtils::searchMimeIcon(mime);
    mimeIcons->insert(mime, theIcon);
    return theIcon;
}

//---------------------------------------------------------------------------

bool myModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    Q_UNUSED(role)

    myModelItem *item =
        static_cast<myModelItem *>(index.internalPointer());

    if (!item)
        return false;

    bool ok = QFile::rename(
        item->absoluteFilePath(),
        item->parent()->absoluteFilePath() + SEPARATOR + value.toString());

    if (ok) {
        item->mMimeType.clear();
        item->changeName(value.toString());
        emit dataChanged(index, index);
    }

    return ok;
}

//---------------------------------------------------------------------------

bool myModel::remove(const QModelIndex &theIndex)
{
    myModelItem *item =
        static_cast<myModelItem *>(theIndex.internalPointer());

    if (!item)
        return false;

    QString path = item->absoluteFilePath();

    QDirIterator it(path,
                    QDir::AllEntries | QDir::System |
                    QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);

    QStringList children;

    while (it.hasNext())
        children.prepend(it.next());

    children.append(path);
    children.removeDuplicates();

    bool error = false;

    for (int i = 0; i < children.count(); i++) {
        QFileInfo info(children.at(i));

        if (info.isDir()) {
            int wd = watchers.key(info.filePath());
            if (wd >= 0)
                inotify_rm_watch(inotifyFD, wd);

            watchers.remove(wd);
            error |= QDir().rmdir(info.filePath());
        } else {
            error |= QFile::remove(info.filePath());
        }
    }

    beginRemoveRows(index(item->parent()->absoluteFilePath()),
                    item->childNumber(), item->childNumber());
    item->parent()->removeChild(item);
    endRemoveRows();

    return error;
}

//---------------------------------------------------------------------------

/**
 * @brief Drag drop to current datamodel
 * @param data
 * @param action
 * @param row
 * @param column
 * @param parent
 * @return true if successful
 */
bool myModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                           int row, int column, const QModelIndex &parent)
{
    Q_UNUSED(action);
    Q_UNUSED(row);
    Q_UNUSED(column);

    if (!isDir(parent))
        return false;

    QList<QUrl> files = data->urls();
    if (files.isEmpty())
        return false;

    if (QFileInfo(files.first().toLocalFile()).canonicalPath() ==
        filePath(parent)) {
        return false;
    }

    Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    Common::DragMode mode = Common::getDefaultDragAndDrop();

    if (mods == Qt::ControlModifier)
        mode = Common::getDADctrlMod();
    else if (mods == Qt::ShiftModifier)
        mode = Common::getDADshiftMod();
    else if (mods == Qt::AltModifier)
        mode = Common::getDADaltMod();

    emit dragDropPaste(data, filePath(parent), mode);
    return true;
}

//---------------------------------------------------------------------------

QVariant myModel::headerData(int section, Qt::Orientation orientation,
                             int role) const
{
    Q_UNUSED(orientation)

    if (role == Qt::DisplayRole) {
        switch (section) {
        case 0: return tr("Name");
        case 1: return tr("Size");
        case 2: return tr("Type");
        case 3: return tr("Date Modified");
        case 4: return tr("Owner");
        default: return QVariant();
        }
    }

    return QVariant();
}

//---------------------------------------------------------------------------

Qt::ItemFlags myModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return {};

    return Qt::ItemIsEnabled | Qt::ItemIsSelectable |
           Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
           Qt::ItemIsDropEnabled;
}

//---------------------------------------------------------------------------

void myModel::addCutItems(QStringList files)
{
    cutItems = files;
}

//---------------------------------------------------------------------------

void myModel::clearCutItems()
{
    cutItems.clear();
    QFile(QDir::tempPath() + QString("/%1.temp").arg(APP)).remove();
}
