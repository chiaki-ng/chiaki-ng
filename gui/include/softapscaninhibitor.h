// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_SOFTAPSCANINHIBITOR_H
#define CHIAKI_SOFTAPSCANINHIBITOR_H

#include <QLocalSocket>
#include <QObject>
#include <QStringList>
#include <QTimer>

class SoftapScanInhibitor : public QObject
{
	Q_OBJECT

	public:
		enum class Status { Off, Waiting, Requested, Unavailable, Denied, Error };
		Q_ENUM(Status)

		explicit SoftapScanInhibitor(QObject *parent = nullptr,
			const QString &socket_path = QStringLiteral("/var/run/softapmanager.streaming.socket"));
		~SoftapScanInhibitor() override;
		void Start();
		void Stop();
		Status GetStatus() const { return status; }
		QString GetStatusText() const;

	signals:
		void StatusChanged();

	private:
		void SendNextInterface();
		void UpdateRequested();
		void Close();
		void Fail(Status failure);
		void SetStatus(Status value);

		const QString socket_path;
		QLocalSocket socket;
		QTimer timer;
		QStringList interfaces;
		bool attempted = false;
		bool active = false;
		Status status = Status::Off;
};

#endif
