// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <kbminput.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QSaveFile>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace kbm
{

int ResolveInputName(const QString &name)
{
	static const QHash<QString, int> names = {
		{ QStringLiteral("mouseleft"), MouseLeft },
		{ QStringLiteral("mouseright"), MouseRight },
		{ QStringLiteral("mousemiddle"), MouseMiddle },
		{ QStringLiteral("mouseback"), MouseBack },
		{ QStringLiteral("mouseforward"), MouseForward },
		{ QStringLiteral("wheelup"), WheelUp },
		{ QStringLiteral("wheeldown"), WheelDown },
		{ QStringLiteral("space"), Qt::Key_Space },
		{ QStringLiteral("tab"), Qt::Key_Tab },
		{ QStringLiteral("enter"), Qt::Key_Return },
		{ QStringLiteral("return"), Qt::Key_Return },
		{ QStringLiteral("esc"), Qt::Key_Escape },
		{ QStringLiteral("escape"), Qt::Key_Escape },
		{ QStringLiteral("backspace"), Qt::Key_Backspace },
		{ QStringLiteral("shift"), Qt::Key_Shift },
		{ QStringLiteral("ctrl"), Qt::Key_Control },
		{ QStringLiteral("control"), Qt::Key_Control },
		{ QStringLiteral("alt"), Qt::Key_Alt },
		{ QStringLiteral("capslock"), Qt::Key_CapsLock },
		{ QStringLiteral("up"), Qt::Key_Up },
		{ QStringLiteral("down"), Qt::Key_Down },
		{ QStringLiteral("left"), Qt::Key_Left },
		{ QStringLiteral("right"), Qt::Key_Right },
		{ QStringLiteral("insert"), Qt::Key_Insert },
		{ QStringLiteral("ins"), Qt::Key_Insert },
		{ QStringLiteral("delete"), Qt::Key_Delete },
		{ QStringLiteral("del"), Qt::Key_Delete },
		{ QStringLiteral("home"), Qt::Key_Home },
		{ QStringLiteral("end"), Qt::Key_End },
		{ QStringLiteral("pageup"), Qt::Key_PageUp },
		{ QStringLiteral("pgup"), Qt::Key_PageUp },
		{ QStringLiteral("pagedown"), Qt::Key_PageDown },
		{ QStringLiteral("pgdown"), Qt::Key_PageDown },
	};

	QString n = name.trimmed();
	if(n.isEmpty())
		return 0;
	QString lower = n.toLower();
	auto it = names.constFind(lower);
	if(it != names.constEnd())
		return it.value();

	if(n.size() == 1)
	{
		QChar c = n.at(0).toUpper();
		if(c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
			return Qt::Key_A + (c.unicode() - 'A');
		if(c >= QLatin1Char('0') && c <= QLatin1Char('9'))
			return Qt::Key_0 + (c.unicode() - '0');
	}

	if(lower.startsWith(QLatin1Char('f')))
	{
		bool ok = false;
		int f = lower.mid(1).toInt(&ok);
		if(ok && f >= 1 && f <= 35)
			return Qt::Key_F1 + (f - 1);
	}

	QKeySequence seq = QKeySequence::fromString(n, QKeySequence::PortableText);
	if(seq.count() == 1)
	{
		int key = seq[0].key();
		if(key != 0 && key != Qt::Key_unknown && seq[0].keyboardModifiers() == Qt::NoModifier)
			return key;
	}
	return 0;
}

int KeyEventCode(const QKeyEvent *event)
{
	quint32 vk = event->nativeVirtualKey();
#if defined(Q_OS_WIN)
	// Virtual key codes of letters and digits equal their Qt::Key values and do not depend on the layout.
	if((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
		return static_cast<int>(vk);
#elif defined(Q_OS_LINUX)
	// X11 / Wayland: nativeVirtualKey is the keysym
	if(vk >= 'a' && vk <= 'z')
		return static_cast<int>(vk - 'a' + 'A');
	if((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
		return static_cast<int>(vk);
#else
	Q_UNUSED(vk);
#endif
	return event->key();
}

int MouseButtonCode(Qt::MouseButton button)
{
	switch(button)
	{
		case Qt::LeftButton: return MouseLeft;
		case Qt::RightButton: return MouseRight;
		case Qt::MiddleButton: return MouseMiddle;
		case Qt::BackButton: return MouseBack;
		case Qt::ForwardButton: return MouseForward;
		default: return 0;
	}
}

QString ProfilePath()
{
	QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
	return QDir(dir).filePath(QStringLiteral("kbm_profile.ini"));
}

static bool ReadProfileText(QString *text, QString *error)
{
	QString path = ProfilePath();
	if(!QFile::exists(path))
	{
		QDir().mkpath(QFileInfo(path).absolutePath());
		QSaveFile out(path);
		if(!out.open(QIODevice::WriteOnly | QIODevice::Text))
		{
			if(error)
				*error = QStringLiteral("Cannot create %1: %2").arg(path, out.errorString());
			*text = QString::fromUtf8(DefaultProfileText());
			return true;
		}
		out.write(DefaultProfileText());
		if(!out.commit() && error)
			*error = QStringLiteral("Cannot write %1: %2").arg(path, out.errorString());
	}

	QFile in(path);
	if(!in.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		if(error)
			*error = QStringLiteral("Cannot read %1: %2").arg(path, in.errorString());
		return false;
	}
	*text = QString::fromUtf8(in.readAll());
	return true;
}

Profile LoadProfile(QString *error)
{
	QString text;
	if(!ReadProfileText(&text, error))
		return ParseProfile(DefaultProfileText());
	return ParseProfile(text.toStdString());
}

bool SaveProfileValue(const QString &section, const QString &key, const QString &value, QString *error)
{
	QString text;
	if(!ReadProfileText(&text, error))
		return false;
	IniDocument doc = IniDocument::Parse(text.toStdString());
	doc.Set(section.toStdString(), key.toStdString(), value.toStdString());

	QSaveFile out(ProfilePath());
	if(!out.open(QIODevice::WriteOnly | QIODevice::Text))
	{
		if(error)
			*error = out.errorString();
		return false;
	}
	out.write(QByteArray::fromStdString(doc.ToString()));
	if(!out.commit())
	{
		if(error)
			*error = out.errorString();
		return false;
	}
	return true;
}

#ifdef Q_OS_WIN

bool RawMouseRegister(WId window)
{
	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01; // generic desktop
	rid.usUsage = 0x02;     // mouse
	rid.dwFlags = 0;        // only while the window is in the foreground
	rid.hwndTarget = reinterpret_cast<HWND>(window);
	return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

void RawMouseUnregister()
{
	RAWINPUTDEVICE rid = {};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02;
	rid.dwFlags = RIDEV_REMOVE;
	rid.hwndTarget = nullptr;
	RegisterRawInputDevices(&rid, 1, sizeof(rid));
}

bool RawMouseParse(void *message, long *dx, long *dy)
{
	const MSG *msg = static_cast<const MSG *>(message);
	if(!msg || msg->message != WM_INPUT)
		return false;
	RAWINPUT raw = {};
	UINT size = sizeof(raw);
	if(GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
		return false;
	if(raw.header.dwType != RIM_TYPEMOUSE)
		return false;
	// absolute positions come from tablets / remote desktop, not usable as deltas
	if(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
		return false;
	*dx = raw.data.mouse.lLastX;
	*dy = raw.data.mouse.lLastY;
	return *dx != 0 || *dy != 0;
}

void ClipCursorToWindowCenter(WId window)
{
	HWND hwnd = reinterpret_cast<HWND>(window);
	RECT rc;
	if(!GetClientRect(hwnd, &rc))
		return;
	POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
	if(!ClientToScreen(hwnd, &center))
		return;
	RECT clip = { center.x, center.y, center.x + 1, center.y + 1 };
	SetCursorPos(center.x, center.y);
	ClipCursor(&clip);
}

void ReleaseCursorClip()
{
	ClipCursor(nullptr);
}

#endif

} // namespace kbm
