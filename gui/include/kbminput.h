// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Qt / platform glue for the keyboard + mouse profile (see kbmprofile.h).

#ifndef CHIAKI_KBMINPUT_H
#define CHIAKI_KBMINPUT_H

#include "kbmprofile.h"

#include <QString>
#include <QtGlobal>
#include <QWindow>

class QKeyEvent;

namespace kbm
{

// Input codes: Qt::Key values for keys, negative values for mouse inputs.
enum MouseInputCode : int
{
	MouseLeft = -1,
	MouseRight = -2,
	MouseMiddle = -3,
	MouseBack = -4,
	MouseForward = -5,
	WheelUp = -6,
	WheelDown = -7,
};

// Returns 0 if the name is unknown.
int ResolveInputName(const QString &name);
// Keyboard-layout independent code for a key event (W stays W on a Cyrillic layout).
int KeyEventCode(const QKeyEvent *event);
// Returns 0 for buttons that cannot be bound.
int MouseButtonCode(Qt::MouseButton button);

QString ProfilePath();
// Reads the profile, writing the default one first if the file does not exist.
Profile LoadProfile(QString *error);
// Changes one value in the profile file, keeping comments.
bool SaveProfileValue(const QString &section, const QString &key, const QString &value, QString *error);

#ifdef Q_OS_WIN
// Windows raw input: relative mouse counts, unaffected by pointer acceleration.
bool RawMouseRegister(WId window);
void RawMouseUnregister();
// message is the MSG* passed to QWindow::nativeEvent.
bool RawMouseParse(void *message, long *dx, long *dy);
void ClipCursorToWindowCenter(WId window);
void ReleaseCursorClip();
#endif

} // namespace kbm

#endif // CHIAKI_KBMINPUT_H
