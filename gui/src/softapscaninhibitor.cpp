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
	connect(&socket, &QLocalSocket::bytesWritten, this, &SoftapScanInhibitor::UpdateRequested);
	connect(&socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
		if(!active)
			return;
		qWarning("Wi-Fi scan inhibition unavailable: %s", qPrintable(socket.errorString()));
		const Status failure = error == QLocalSocket::ServerNotFoundError ? Status::Unavailable
			: error == QLocalSocket::SocketAccessError ? Status::Denied : Status::Error;
		Fail(failure);
	});
	connect(&socket, &QLocalSocket::disconnected, this, [this]() {
		if(!active)
			return;
		qWarning("Wi-Fi scan inhibition socket disconnected");
		Fail(Status::Error);
	});
}

SoftapScanInhibitor::~SoftapScanInhibitor()
{
	Close();
}

QString SoftapScanInhibitor::GetStatusText() const
{
	switch(status)
	{
		case Status::Off: return QStringLiteral("OFF");
		case Status::Waiting: return QStringLiteral("WAIT");
		case Status::Requested: return QStringLiteral("SENT");
		case Status::Unavailable: return QStringLiteral("N/A");
		case Status::Denied: return QStringLiteral("DENIED");
		case Status::Error: return QStringLiteral("ERROR");
	}
	return QStringLiteral("ERROR");
}

void SoftapScanInhibitor::SetStatus(Status value)
{
	if(status == value)
		return;
	status = value;
	emit StatusChanged();
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
	{
		qWarning("Wi-Fi scan inhibition unavailable: no Wi-Fi interfaces");
		SetStatus(Status::Unavailable);
		return;
	}

	active = true;
	SetStatus(Status::Waiting);
	if(active)
		socket.connectToServer(socket_path);
}

void SoftapScanInhibitor::SendNextInterface()
{
	if(!active || socket.state() != QLocalSocket::ConnectedState || interfaces.isEmpty())
		return;

	const QByteArray command = "streamon\t" + interfaces.first().toLocal8Bit() + "\n";
	if(socket.write(command) != command.size())
	{
		if(active)
		{
			qWarning("Wi-Fi scan inhibition write failed: %s", qPrintable(socket.errorString()));
			Fail(Status::Error);
		}
		return;
	}
	if(!active)
		return;
	interfaces.removeFirst();
	socket.flush();
	UpdateRequested();

	// The daemon parses one command per read and sends no acknowledgements.
	// Pace separate writes; Unix streams do not guarantee read boundaries.
	if(active && !interfaces.isEmpty())
		timer.start(100);
}

void SoftapScanInhibitor::UpdateRequested()
{
	// SENT describes local writes only: the daemon never confirms its scan mask.
	if(active && interfaces.isEmpty() && socket.state() == QLocalSocket::ConnectedState
			&& socket.bytesToWrite() == 0)
		SetStatus(Status::Requested);
}

void SoftapScanInhibitor::Close()
{
	// Cancel before abort(), which can emit socket signals synchronously.
	attempted = true;
	active = false;
	timer.stop();
	interfaces.clear();
	socket.abort();
}

void SoftapScanInhibitor::Fail(Status failure)
{
	Close();
	SetStatus(failure);
}

void SoftapScanInhibitor::Stop()
{
	Close();
	SetStatus(Status::Off);
}
