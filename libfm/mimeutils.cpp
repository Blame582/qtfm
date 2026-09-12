#include "mimeutils.h"
#include "fileutils.h"

#include <QProcess>
#include <QDebug>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QMimeType>

#ifdef Q_OS_DARWIN
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>
#endif

#include "common.h"


/**
 * @brief Creates mime utils
 * @param parent
 */
MimeUtils::MimeUtils(QObject *parent) : QObject(parent) {
  defaultsFileName = MIME_APPS;
  defaults = new Properties();
  loadDefaults();
}
//---------------------------------------------------------------------------

/**
 * @brief Loads list of default applications for mimes
 * @return properties with default applications
 */
void MimeUtils::loadDefaults() {
  defaults->load(QDir::homePath() + defaultsFileName, "Default Applications");
  defaultsChanged = false;
}
//---------------------------------------------------------------------------

/**
 * @brief Destructor
 */
MimeUtils::~MimeUtils() {
  delete defaults;
}
//---------------------------------------------------------------------------

/**
 * @brief Returns mime type of given file
 * @note This operation is slow, prevent its mass application
 * @param path path to file
 * @return mime type
 */
QString MimeUtils::getMimeType(const QString &path) {
    QMimeDatabase db;
    QMimeType type = db.mimeTypeForFile(path);
    //qDebug() << "mime type" << type.name() << path;
    return type.name();
}
//---------------------------------------------------------------------------

/**
 * @brief Returns list of mime types
 * @return list of available mimetypes
 */
QStringList MimeUtils::getMimeTypes() const {
    QStringList result = Common::getMimeTypes(qApp->applicationFilePath());
    //qDebug() << "getMimeTypes"  << result;
    return result;
}
//---------------------------------------------------------------------------

/**
 * @brief Opens file in a default application
 * @param file
 * @param processOwner
 */
void MimeUtils::openInApp(const QFileInfo &file, QString termCmd) {
    qDebug() << "openInApp without app";
  QString mime = getMimeType(file.absoluteFilePath());
  QString app = defaults->value(mime).toString().split(";").first();
  if (app.isEmpty() && mime.startsWith("text/") && mime != "text/plain") {
      // fallback for text
      app = defaults->value("text/plain").toString().split(";").first();
  }
  QString desktop = Common::findApplication(qApp->applicationFilePath(), app);
  qDebug() << "openInApp" << file.absoluteFilePath() << termCmd << mime << app << desktop;
  if (!desktop.isEmpty()) {
    DesktopFile df = DesktopFile(desktop);
    if (!df.isTerminal()) { termCmd.clear(); }
    else {
        if (termCmd.isEmpty()) { termCmd = "xterm"; }
    }
    openInApp(df.getExec(), file, termCmd);
  } else {
#ifdef Q_OS_DARWIN
      CFURLRef ref = CFURLCreateWithFileSystemPath(Q_NULLPTR,
                                                   file.absoluteFilePath().toCFString(),
                                                   kCFURLPOSIXPathStyle,
                                                   file.isDir());
      LSOpenCFURLRef(ref, Q_NULLPTR);
#else

     QString title = tr("No default application");
     QString msg = tr("No default application for mime: %1!").arg(mime);
     QMessageBox::warning(Q_NULLPTR, title, msg);
#endif
  }
}
//---------------------------------------------------------------------------

/**
 * @brief Opens file in a given application
 * @param exe name of application to be executed
 * @param file to be opened in executed application
 * @param processOwner process owner (default NULL)
 */
void MimeUtils::openInApp(QString exe, const QFileInfo &file,
                          QString termCmd) {

  qDebug() << "openInApp" << exe << file.absoluteFilePath() << termCmd;

  // This is not the right solution, but qpdfview won't start otherwise
  // TODO: Repair it correctly
  if (exe.contains("qpdfview")) {
    exe = "qpdfview";
  }

  // Separate application name from its arguments
  QStringList split = exe.split(" ", Qt::SkipEmptyParts);
  if (split.isEmpty()) {
    return;
  }

  QString name = split.takeFirst();
  QString args = split.join(" ");

  // Replace desktop-entry field codes
  if (args.contains("%F", Qt::CaseInsensitive)) {
    args.replace("%F", "\"" + file.filePath() + "\"", Qt::CaseInsensitive);
  } else if (args.contains("%f", Qt::CaseInsensitive)) {
    args.replace("%f", "\"" + file.filePath() + "\"", Qt::CaseInsensitive);
  } else if (args.contains("%U", Qt::CaseInsensitive)) {
    args.replace("%U", "\"" + file.filePath() + "\"", Qt::CaseInsensitive);
  } else if (args.contains("%u", Qt::CaseInsensitive)) {
    args.replace("%u", "\"" + file.filePath() + "\"", Qt::CaseInsensitive);
  } else {
    args.append(args.isEmpty() ? "" : " ");
    args.append("\"" + file.filePath() + "\"");
  }

  qDebug() << "application:" << name;
  qDebug() << "arguments:" << args;

  // Start application
  if (termCmd.isEmpty()) {
    QStringList arguments;
    if (!args.isEmpty()) {
      arguments = QProcess::splitCommand(args);
    }

    qDebug() << "starting:" << name << arguments;
    QProcess::startDetached(name, arguments);
  } else {
    QString terminalArgs;
    terminalArgs = "-e";
    terminalArgs += " ";
    terminalArgs += name;
    if (!args.isEmpty()) {
      terminalArgs += " ";
      terminalArgs += args;
    }

    qDebug() << "starting terminal:" << termCmd << terminalArgs;

    QStringList arguments;
    arguments << "-e";
    arguments << name;
    if (!args.isEmpty()) {
      QStringList appArgs = QProcess::splitCommand(args);
      arguments.append(appArgs);
    }

    QProcess::startDetached(termCmd, arguments);
  }
}




void MimeUtils::openFilesInApp(QString exe, const QStringList &files, QString termCmd)
{
    // Separate application name from its arguments
    QStringList split = exe.split(" ");
    QString name = split.takeAt(0);
    QString args = split.join(" ");

    if (args.toLower().contains("%f")) {
        args.replace("%f", "", Qt::CaseInsensitive);
    } else if (args.toLower().contains("%u")) {
        args.replace("%u", "", Qt::CaseInsensitive);
    }
    for (int i=0;i<files.size();++i) {
        args.append("\"" + files.at(i) + "\" ");
    }

    // Start application
    QString cmd = name;
    if (termCmd.isEmpty()) {
      cmd.append(" ");
      cmd.append(args);
    } else {
      cmd = QString("%1 -e \"%2 %3\"").arg(termCmd).arg(name).arg(args);
    }
    qDebug() << "running:" << cmd;
    QProcess::startDetached(cmd);
}
//---------------------------------------------------------------------------

/**
 * @brief Sets defaults file name (name of file where defaults are stored)
 * @param fileName
 */
void MimeUtils::setDefaultsFileName(const QString &fileName) {
  this->defaultsFileName = fileName;
  loadDefaults();
}
//---------------------------------------------------------------------------

/**
 * @brief Returns defaults file name
 * @return name of file where defaults are stored
 */
QString MimeUtils::getDefaultsFileName() const {
    return defaultsFileName;
}

QString MimeUtils::getAppForMimeType(const QString &mime) const
{
    return defaults->value(mime).toString().split(";").first();
}
//---------------------------------------------------------------------------

/**
 * @brief Generates default application-mime associations
 */
void MimeUtils::generateDefaults() {

  // Load list of applications
  QList<DesktopFile> apps = FileUtils::getApplications();
  QStringList names;

  // Find defaults; for each application...
  // ------------------------------------------------------------------------
  foreach (DesktopFile a, apps) {

  // ignore NoDisplay=true
  if (a.noDisplay()) { continue; }

    // For each mime of current application...
    QStringList mimes = a.getMimeType();
    foreach (QString mime, mimes) {

      // Current app name
      QString name = a.getPureFileName() + ".desktop";
      names.append(name);

      // If current mime is not mentioned in the list of defaults, add it
      // together with current application and continue
      if (!defaults->contains(mime)) {
        defaults->set(mime, name);
        defaultsChanged = true;
        continue;
      }

      // Retrieve list of default applications for current mime, if it does
      // not contain current application, add this application to list
      QStringList appNames = defaults->value(mime).toString().split(";");
      if (!appNames.contains(name)) {
        appNames.append(name);
        defaults->set(mime, appNames.join(";"));
        defaultsChanged = true;
      }
    }
  }

  // Delete dead defaults (non existing apps)
  // ------------------------------------------------------------------------
  foreach (QString key, defaults->getKeys()) {
    QStringList tmpNames1 = defaults->value(key).toString().split(";");
    QStringList tmpNames2 = QStringList();
    foreach (QString name, tmpNames1) {
      if (names.contains(name)) {
        tmpNames2.append(name);
      }
    }
    if (tmpNames1.size() != tmpNames2.size()) {
      defaults->set(key, tmpNames2.join(";"));
      defaultsChanged = true;
    }
  }

  // Save defaults if changed
  saveDefaults();
}
//---------------------------------------------------------------------------

/**
 * @brief Sets default mime association
 * @param mime mime name
 * @param apps list of applications (desktop file names)
 */
void MimeUtils::setDefault(const QString &mime, const QStringList &apps) {
  QString value = apps.join(";");
  if (value.compare(defaults->value(mime, "").toString()) != 0) {
    defaults->set(mime, value);
    defaultsChanged = true;
  }
}
//---------------------------------------------------------------------------

/**
 * @brief Returns default applications for given mime
 * @param mime
 * @return list of default applications name
 */
QStringList MimeUtils::getDefault(const QString &mime) const {
  return defaults->value(mime).toString().split(";");
}
//---------------------------------------------------------------------------

/**
 * @brief Saves defaults
 */
void MimeUtils::saveDefaults() {
  if (defaultsChanged) {
    defaults->save(QDir::homePath() + defaultsFileName, "Default Applications");
    defaultsChanged = false;
  }
}
//---------------------------------------------------------------------------
