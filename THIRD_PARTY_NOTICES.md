# Third-party notices

These components are not relicensed by the Viewer's Apache-2.0 license.
Retain their license texts and attributions when redistributing them.

## Prism SDK

The Viewer uses the separately distributed [Prism SDK](https://github.com/DIBULI/Prism-SDK).
The core libraries are proprietary, even when statically linked into the Viewer.
Public headers and examples are distinct from the closed-source implementation.
The [SDK binary license](licenses/Prism-SDK-Binary-LICENSE.txt) permits bundled
redistribution of unmodified SDK binaries with applications, with notices retained.
The SDK's notices identify the OpenSSL, libusb and miniz components shipped
with the relevant SDK variants.

This product includes software developed by the OpenSSL Project for use in the
OpenSSL Toolkit (https://www.openssl.org/). This product includes cryptographic
software written by Eric Young (eay@cryptsoft.com).

## Qt

The Viewer dynamically links Qt Core, Gui, Widgets, Network and Sql, plus the
selected platform/image/SQLite plugins. Qt Charts is not used.
Qt is copyright The Qt Company Ltd. and other contributors.
The open-source LGPL option for the applicable Qt libraries remains separate
from the Viewer license. LGPLv3 and its incorporated GPLv3 text are included
under `licenses/third_party/`.

- [Qt licensing](https://doc.qt.io/qt-6/licensing.html)
- [LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations)
- [Qt source and third-party notices](https://code.qt.io/cgit/qt/qtbase.git/)
- [Qt source releases](https://download.qt.io/official_releases/qt/)

Use the exact source/version matching the binaries in a distribution. Linux
packages use the Ubuntu 20.04 Qt packages; Windows/macOS CI uses Qt 6.8.3.
Qt's embedded third-party components have their own notices. Redistributors
must also supply applicable corresponding source/access and relinking
information; including these license texts alone is not a complete compliance
audit. Nothing in the Prism licensing terms restricts recipients' rights
under an applicable third-party license.

## Other runtime dependencies

Release packages statically link Zstandard 1.5.7 under its BSD-3-Clause option.
Source: https://github.com/facebook/zstd/releases/tag/v1.5.7
The pinned source archive SHA256 is
`37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3`.
Its copyright and license are included in `licenses/third_party/zstd-LICENSE.txt`.

Depending on the platform, packages also include libusb (LGPL-2.1-or-later),
OpenSSL (its applicable OpenSSL/SSLeay license for the 1.1.1 SDK runtime),
SQLite and Qt's image/font/compression dependencies. Microsoft runtime
redistributables remain under Microsoft's applicable terms.
System libraries and any separately attributed files keep their own licenses.
The accompanying third-party texts supplement, and do not replace, the
notices for the exact dependencies distributed.
