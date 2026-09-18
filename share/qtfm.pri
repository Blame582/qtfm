QTFM_TARGET = qtfm
QTFM_TARGET_NAME = "QtFM"

QTFM_MAJOR = 6
QTFM_MINOR = 3
QTFM_PATCH = 0

QMAKE_TARGET_COMPANY = "$${QTFM_TARGET_NAME}"
QMAKE_TARGET_PRODUCT = "$${QTFM_TARGET_NAME}"
QMAKE_TARGET_DESCRIPTION = "$${QTFM_TARGET_NAME}"
QMAKE_TARGET_COPYRIGHT = "Copyright $${QTFM_TARGET_NAME} developers"

# Qt 6 is required.
lessThan(QT_MAJOR_VERSION, 6): error("Qt6 is required.")

unix:!macx {
    # Installation prefix.
    isEmpty(PREFIX): PREFIX = /usr

    # XDG configuration directory.
    isEmpty(XDGDIR): XDGDIR = $${PREFIX}/etc/xdg

    # Library installation directory.
    isEmpty(LIBDIR): LIBDIR = $$PREFIX/lib$${LIBSUFFIX}

    # Documentation installation directory.
    isEmpty(DOCDIR): DOCDIR = $$PREFIX/share/doc

    # Manual page installation directory.
    isEmpty(MANDIR): MANDIR = $$PREFIX/share/man
}

QT += widgets

CONFIG += link_pkgconfig

macx {
    QTFM_TARGET = QtFM

    LIBS += -framework CoreFoundation
    LIBS += -framework CoreServices

    QT_CONFIG -= no-pkg-config

    PKGCONFIG += libinotify

    QMAKE_MACOSX_DEPLOYMENT_TARGET = 10.7

    CONFIG += staticlib
}

CONFIG(deploy) {
    DEFINES += DEPLOY
}

CONFIG(release, debug|release) {
    # Disable Qt debug output in release builds.
    #DEFINES += QT_NO_DEBUG_OUTPUT

    # Build the library statically unless shared libraries are requested.
    !CONFIG(sharedlib): CONFIG += staticlib
}

freebsd {
    LIBS += -linotify
}

netbsd-g++ {
    PKGCONFIG += libinotify
}
