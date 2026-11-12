// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <softapscaninhibitor.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

SoftapScanInhibitor::SoftapScanInhibitor(QObject *parent, const QString &socket_path)
	: QObject(parent), socket_path(socket_path), socket(this), timer(this)
{
	timer.setSingleShot(true);
	timer.setTimerType(Qt::PreciseTimer);
	connect(&timer, &QTimer::timeout, this, &SoftapScanInhibitor::SendNextInterface);
	connect(&socket, &QLocalSocket::connected, this, &SoftapScanInhibitor::SendNextInterface);
	connect(&socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
		if(!active)
			return;
		qWarning("Wi-Fi scan inhibition unavailable: %s", qPrintable(socket.errorString()));
		Stop();
	});
	connect(&socket, &QLocalSocket::disconnected, this, [this]() {
		if(active)
			qWarning("Wi-Fi scan inhibition socket disconnected");
		Stop();
	});
}

SoftapScanInhibitor::~SoftapScanInhibitor()
{
	Stop();
}

void SoftapScanInhibitor::Start()
{
	// One attempt per stream, including across Chiaki's initial connection retries.
	if(attempted)
		return;
	attempted = true;

	const QString names = qEnvironmentVariable("WIFI_INTERFACES");
	if(!names.isEmpty())
		interfaces = names.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
	else
	{
		const QDir net(QStringLiteral("/sys/class/net"));
		for(const QString &name : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
		{
			if(QFileInfo::exists(net.filePath(name + QStringLiteral("/phy80211"))))
				interfaces.append(name);
		}
	}
	if(interfaces.isEmpty())
		return;

	active = true;
	socket.connectToServer(socket_path);
}

void SoftapScanInhibitor::SendNextInterface()
{
	if(!active || socket.state() != QLocalSocket::ConnectedState || interfaces.isEmpty())
		return;

	const QByteArray command = "streamon\t" + interfaces.takeFirst().toLocal8Bit() + "\n";
	if(socket.write(command) != command.size())
	{
		if(active)
		{
			qWarning("Wi-Fi scan inhibition write failed: %s", qPrintable(socket.errorString()));
			Stop();
		}
		return;
	}
	socket.flush();

	// The daemon parses one command per read and sends no acknowledgements.
	// Pace separate writes; Unix streams do not guarantee read boundaries.
	if(active && !interfaces.isEmpty())
		timer.start(100);
}

void SoftapScanInhibitor::Stop()
{
	// Cancel before abort(), which can emit socket signals synchronously.
	attempted = true;
	active = false;
	timer.stop();
	interfaces.clear();
	socket.abort();
}
