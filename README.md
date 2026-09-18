# QtFM Qt6

A Qt6 port of **QtFM 6.2.1**.

This repository contains an independent Qt6 fork of QtFM, updated from the 6.2.1 codebase to build and run with Qt6 while retaining QtFM's lightweight, traditional file-manager design.

This is **not an official upstream QtFM release**.

## Features

QtFM provides a lightweight graphical file manager with:

* Qt-based graphical interface
* Directory browsing
* Tabs
* File and directory operations
* Drag and drop
* Custom commands
* Configurable key bindings
* XDG integration
* Removable-media support
* Optional thumbnail support

The Qt6 port preserves the existing QtFM interface and functionality rather than replacing it with a new desktop-oriented design.

## Qt6 Port

The original QtFM 6.2.1 source has been updated for Qt6.

The port includes:

* Qt5 → Qt6 API updates
* Qt6 build and dependency cleanup
* Removal of obsolete Qt5 requirements
* Qt6 compiler compatibility fixes
* Updated source constructs required by modern Qt/C++
* Fixes to command/action handling
* Documentation and icon-theme updates
* ImageMagick and FFmpeg compatibility updates
* General source cleanup

The project is intended to remain **desktop-independent** and usable on lightweight Linux systems.

<<<<<<< HEAD
## Requirements

* Qt6
* A working X11 environment

Optional:

* ImageMagick — image/thumbnail functionality where supported
* FFmpeg — multimedia thumbnail support where supported

No Qt5 installation is required.

## Building

From the source directory:

```sh
qmake6
=======
Doing a normal build:
```
mkdir build && cd build
qmake6 ..
>>>>>>> d73ca53 (Fix file manager actions and application launching)
make
```

Then install using the project's normal installation target:

```sh
make install
```

The exact installation prefix can be supplied through qmake when required.

## Status

This is an actively maintained Qt6 port based on QtFM 6.2.1.

The primary goal is to maintain a clean, usable Qt6 version of QtFM while avoiding unnecessary expansion of the original project's scope.

Testing and bug reports are welcome.

## Source

This project is based on:

**QtFM 6.2.1**

The Qt6 work in this repository is maintained independently from the original upstream project.

## Releases

See the [Releases](../../releases) page for source archives and tagged versions.

### Current release

**QtFM 6.3.0**

QtFM 6.3.0 is the first maintained release of this Qt6 fork.

It contains the Qt6 port and associated compatibility, build, and cleanup work described above.

## Bug Reports

If you find a problem specific to the Qt6 version, please open an issue and include:

* Qt version
* operating system/distribution
* compiler version
* steps to reproduce the problem
* relevant terminal output or error messages

For problems that also occur in the original QtFM 6.2.1 codebase, please indicate that when reporting the issue.

## License

See the files included with the source distribution for the applicable license and copyright information.
