// SPDX-License-Identifier: AGPL-3.0-only
// Mouse look glue for chiaki-ng (PadBridge patch). The logic lives in mouselook_core.h.

#include "streamsession.h"
#include "mouselook_core.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QWindow>

static_assert(mouselook::BTN_CROSS == CHIAKI_CONTROLLER_BUTTON_CROSS, "button mismatch");
static_assert(mouselook::BTN_CIRCLE == CHIAKI_CONTROLLER_BUTTON_MOON, "button mismatch");
static_assert(mouselook::BTN_SQUARE == CHIAKI_CONTROLLER_BUTTON_BOX, "button mismatch");
static_assert(mouselook::BTN_TRIANGLE == CHIAKI_CONTROLLER_BUTTON_PYRAMID, "button mismatch");
static_assert(mouselook::BTN_L1 == CHIAKI_CONTROLLER_BUTTON_L1, "button mismatch");
static_assert(mouselook::BTN_R1 == CHIAKI_CONTROLLER_BUTTON_R1, "button mismatch");
static_assert(mouselook::BTN_L3 == CHIAKI_CONTROLLER_BUTTON_L3, "button mismatch");
static_assert(mouselook::BTN_R3 == CHIAKI_CONTROLLER_BUTTON_R3, "button mismatch");
static_assert(mouselook::BTN_OPTIONS == CHIAKI_CONTROLLER_BUTTON_OPTIONS, "button mismatch");
static_assert(mouselook::BTN_SHARE == CHIAKI_CONTROLLER_BUTTON_SHARE, "button mismatch");
static_assert(mouselook::BTN_TOUCHPAD == CHIAKI_CONTROLLER_BUTTON_TOUCHPAD, "button mismatch");
static_assert(mouselook::BTN_PS == CHIAKI_CONTROLLER_BUTTON_PS, "button mismatch");
static_assert(mouselook::BTN_DPAD_UP == CHIAKI_CONTROLLER_BUTTON_DPAD_UP, "button mismatch");
static_assert(mouselook::BTN_DPAD_DOWN == CHIAKI_CONTROLLER_BUTTON_DPAD_DOWN, "button mismatch");
static_assert(mouselook::BTN_DPAD_LEFT == CHIAKI_CONTROLLER_BUTTON_DPAD_LEFT, "button mismatch");
static_assert(mouselook::BTN_DPAD_RIGHT == CHIAKI_CONTROLLER_BUTTON_DPAD_RIGHT, "button mismatch");

namespace
{
constexpr int MOUSE_ID_BASE = 0x10000000;     // above every Qt::Key value

int ResolveKeyName(const std::string &name)
{
	const QString n = QString::fromStdString(name).trimmed();
	const QString l = n.toLower();
	if(l == "lmb") return MOUSE_ID_BASE + 1;
	if(l == "rmb") return MOUSE_ID_BASE + 2;
	if(l == "mmb") return MOUSE_ID_BASE + 3;
	if(l == "x1") return MOUSE_ID_BASE + 4;
	if(l == "x2") return MOUSE_ID_BASE + 5;
	if(l == "wheelup") return MOUSE_ID_BASE + 10;
	if(l == "wheeldown") return MOUSE_ID_BASE + 11;
	if(l == "shift") return Qt::Key_Shift;
	if(l == "ctrl" || l == "control") return Qt::Key_Control;
	if(l == "alt") return Qt::Key_Alt;
	if(l == "space") return Qt::Key_Space;
	if(l == "tab") return Qt::Key_Tab;
	if(l == "enter" || l == "return") return Qt::Key_Return;
	if(l == "esc" || l == "escape") return Qt::Key_Escape;
	if(l == "backspace") return Qt::Key_Backspace;
	if(l == "capslock") return Qt::Key_CapsLock;
	if(l == "up" || l == "arrow_up") return Qt::Key_Up;
	if(l == "down" || l == "arrow_down") return Qt::Key_Down;
	if(l == "left" || l == "arrow_left") return Qt::Key_Left;
	if(l == "right" || l == "arrow_right") return Qt::Key_Right;
	if(n.size() == 1)
		return n.toUpper().at(0).unicode();
	const QKeySequence seq(n);
	if(seq.count() > 0)
		return static_cast<int>(seq[0].key());
	return -1;
}

int MouseButtonId(Qt::MouseButton b)
{
	switch(b)
	{
		case Qt::LeftButton: return MOUSE_ID_BASE + 1;
		case Qt::RightButton: return MOUSE_ID_BASE + 2;
		case Qt::MiddleButton: return MOUSE_ID_BASE + 3;
		case Qt::BackButton: return MOUSE_ID_BASE + 4;
		case Qt::ForwardButton: return MOUSE_ID_BASE + 5;
		default: return -1;
	}
}
}

QString StreamSession::MouseLookIniPath() const
{
	return QCoreApplication::applicationDirPath() + "/mouselook.ini";
}

void StreamSession::MouseLookInit()
{
	ml_clock.start();
	ml_timer = new QTimer(this);
	ml_timer->setTimerType(Qt::PreciseTimer);
	ml_timer->setInterval(4);
	connect(ml_timer, &QTimer::timeout, this, &StreamSession::MouseLookTick);
	MouseLookReload();
}

void StreamSession::MouseLookReload()
{
	QFileInfo fi(MouseLookIniPath());
	if(!fi.exists())
	{
		ml_ini_time = QDateTime();
		return;
	}
	ml_ini_time = fi.lastModified();
	QFile f(fi.filePath());
	if(!f.open(QIODevice::ReadOnly))
		return;
	const std::string text = f.readAll().toStdString();
	mouselook::Config cfg = mouselook::ParseIni(text);
	if(!ml_mapper)
		ml_mapper = new mouselook::Mapper(cfg, ResolveKeyName);
	else
		ml_mapper->Configure(cfg, ResolveKeyName);
	const bool want = ml_mapper->Enabled();
	if(!ml_first_load_done)
	{
		ml_first_load_done = true;
		ml_on = want;
	}
	else if(!want)
		ml_on = false;
	if(ml_on)
		ml_timer->start();
	CHIAKI_LOGI(GetChiakiLog(), "Mouse look: loaded %s (%s, profile \"%s\")", qPrintable(fi.filePath()),
			ml_on ? "ON" : "off", ml_mapper->ProfileName().c_str());
}

bool StreamSession::MouseLookActive() const
{
	return ml_on && ml_mapper && ml_mapper->Enabled();
}

void StreamSession::MouseLookSetOn(bool on)
{
	if(!ml_mapper || !ml_mapper->Enabled())
		return;
	if(on == ml_on)
		return;
	ml_on = on;
	ml_mapper->ReleaseAll();
	ml_have_center = false;
	if(on)
		ml_timer->start();
	else
	{
		ml_timer->stop();
		chiaki_controller_state_set_idle(&ml_state);
		SendFeedbackState();
	}
	CHIAKI_LOGI(GetChiakiLog(), "Mouse look: %s", on ? "ON" : "OFF");
}

void StreamSession::MouseLookReleaseAll()
{
	if(!ml_mapper)
		return;
	ml_mapper->ReleaseAll();
	ml_have_center = false;
}

// returns true when the key was used by mouse look (and must not go anywhere else)
bool StreamSession::MouseLookKey(QKeyEvent *event)
{
	if(!ml_mapper)
		return false;
	const int key = event->key();
	const bool press = event->type() == QEvent::KeyPress;
	if(key == ml_mapper->ToggleKey())
	{
		if(press && !event->isAutoRepeat())
		{
			if(ml_mapper->Enabled())
				MouseLookSetOn(!ml_on);
			else
				MouseLookReload();
		}
		return true;
	}
	if(!MouseLookActive())
		return false;
	if(key == ml_mapper->ProfileKey())
	{
		if(press && !event->isAutoRepeat())
		{
			ml_mapper->NextProfile();
			CHIAKI_LOGI(GetChiakiLog(), "Mouse look: profile \"%s\"", ml_mapper->ProfileName().c_str());
		}
		return true;
	}
	if(!ml_mapper->IsMapped(key))
		return false;
	if(!event->isAutoRepeat())
		ml_mapper->SetKey(key, press);
	return true;
}

bool StreamSession::MouseLookMouse(QMouseEvent *event, QWindow *window)
{
	if(!MouseLookActive())
		return false;
	if(event->type() == QEvent::MouseMove)
	{
		const QPoint center = window->mapToGlobal(QPoint(window->width() / 2, window->height() / 2));
		const QPointF pos = event->globalPosition();
		if(ml_have_center)
			ml_mapper->AddMouse(pos.x() - center.x(), pos.y() - center.y());
		ml_have_center = true;
		if(pos.toPoint() != center)
			QCursor::setPos(center);
		return true;
	}
	const int id = MouseButtonId(event->button());
	if(id >= 0)
		ml_mapper->SetKey(id, event->type() == QEvent::MouseButtonPress);
	return true;
}

bool StreamSession::MouseLookWheel(QWheelEvent *event)
{
	if(!MouseLookActive())
		return false;
	const int dy = event->angleDelta().y();
	if(dy != 0)
		ml_mapper->Pulse(MOUSE_ID_BASE + (dy > 0 ? 10 : 11), ml_clock.nsecsElapsed() / 1e9);
	return true;
}

void StreamSession::MouseLookTick()
{
	if(!ml_mapper)
		return;
	if(++ml_tick_count % 125 == 0)
	{
		const QFileInfo fi(MouseLookIniPath());
		if(fi.exists() && fi.lastModified() != ml_ini_time)
			MouseLookReload();
	}
	if(!MouseLookActive())
	{
		ml_timer->stop();
		return;
	}
	const mouselook::Mapper::Output o = ml_mapper->Update(ml_clock.nsecsElapsed() / 1e9);
	if(ml_have_last && o.buttons == ml_last.buttons && o.l2 == ml_last.l2 && o.r2 == ml_last.r2 &&
			o.left_x == ml_last.left_x && o.left_y == ml_last.left_y &&
			o.right_x == ml_last.right_x && o.right_y == ml_last.right_y)
		return;
	ml_last = o;
	ml_have_last = true;
	chiaki_controller_state_set_idle(&ml_state);
	ml_state.buttons = o.buttons;
	ml_state.l2_state = o.l2;
	ml_state.r2_state = o.r2;
	ml_state.left_x = o.left_x;
	ml_state.left_y = o.left_y;
	ml_state.right_x = o.right_x;
	ml_state.right_y = o.right_y;
	SendFeedbackState();
}
