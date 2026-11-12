// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SOFTAPSCANINHIBITOR_H
#define CHIAKI_SOFTAPSCANINHIBITOR_H

#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

class SoftapScanInhibitor : public QObject
{
	public:
		explicit SoftapScanInhibitor(QObject *parent = nullptr,
			const QString &socket_path = QStringLiteral("/var/run/softapmanager.streaming.socket"));
		~SoftapScanInhibitor() override;
		void Start();
		void Stop();

	private:
		void SendNextInterface();

		const QString socket_path;
		QLocalSocket socket;
		QTimer timer;
		QStringList interfaces;
		bool attempted = false;
		bool active = false;
};

#endif
