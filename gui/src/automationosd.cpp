// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "automationosd.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSet>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>

static constexpr int MAX_IMAGES = 8;
static constexpr int MAX_IMAGE_BYTES = 256 * 1024;
static constexpr int MAX_BATCH_BYTES = 512 * 1024;
static constexpr int MAX_SCENE_BYTES = 1024 * 1024;
static constexpr int MAX_METADATA = 4096;

AutomationOsd::AutomationOsd(QObject *parent) : QObject(parent), worker(new QObject)
{
	worker->moveToThread(&thread);
	connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
}

AutomationOsd::~AutomationOsd()
{
	if(!thread.isRunning())
	{
		delete worker;
		return;
	}
	QMetaObject::invokeMethod(worker, [this]() { Close(); }, Qt::BlockingQueuedConnection);
	thread.quit();
	thread.wait();
}

void AutomationOsd::Close()
{
	if(socket)
	{
		delete socket;
		socket = nullptr;
	}
	delete server;
	server = nullptr;
	input.clear();
}

void AutomationOsd::Clear()
{
	++epoch;
	if(!thread.isRunning() || clear_pending.exchange(true))
		return;
	QMetaObject::invokeMethod(worker, [this]() {
		Close();
		scene.reset();
		QMutexLocker lock(&mutex);
		pending.reset();
		clear_pending = false;
	});
	emit cleared();
}

std::shared_ptr<const AutomationOsdScene> AutomationOsd::Latest()
{
	if(!mutex.tryLock())
		return {};
	auto result = std::move(pending);
	mutex.unlock();
	return result;
}

void AutomationOsd::Command(const QJsonObject &message, const QString &path,
	std::function<void(QJsonObject)> reply)
{
	if(!supported || command_pending.exchange(true))
	{
		reply({{"event", "error"}, {"message", supported ? "OSD command busy" : "asynchronous OSD unavailable"}});
		return;
	}
	if(!thread.isRunning())
		thread.start(QThread::LowPriority);
	const uint64_t generation = epoch.load();
	QMetaObject::invokeMethod(worker, [this, message, path, reply, generation]() {
		QJsonObject result{{"event", "ok"}};
		QString error;
		if(generation != epoch)
			error = QStringLiteral("OSD was cleared");
		else if(message["cmd"] == QLatin1String("osd_open"))
		{
			Close();
			server = new QLocalServer(worker);
			server->setSocketOptions(QLocalServer::UserAccessOption);
			server->setMaxPendingConnections(1);
			connect(server, &QLocalServer::newConnection, worker, [this, generation]() {
				while(server->hasPendingConnections())
				{
					auto incoming = server->nextPendingConnection();
					if(socket || generation != epoch)
					{
						delete incoming;
						continue;
					}
					socket = incoming;
					input.clear();
					incoming->setReadBufferSize(MAX_METADATA + MAX_BATCH_BYTES + 8);
					connect(incoming, &QLocalSocket::readyRead, worker, [this, incoming, generation]() { Read(incoming, generation); });
					connect(incoming, &QLocalSocket::disconnected, worker, [this, incoming]() {
						if(socket == incoming)
							socket = nullptr;
						incoming->deleteLater();
					});
				}
			});
			const QString endpoint = path + ".osd-" + QUuid::createUuid().toString(QUuid::Id128);
			if(!server->listen(endpoint))
				error = server->errorString();
			else
				result = {{"event", "ok"}, {"path", endpoint}, {"max_images", MAX_IMAGES},
					{"max_image_bytes", MAX_IMAGE_BYTES}, {"max_batch_bytes", MAX_BATCH_BYTES}};
		}
		else
			error = Apply(message, {}, generation);
		if(!error.isEmpty())
			result = {{"event", "error"}, {"message", error}};
		QMetaObject::invokeMethod(this, [this, reply, result]() {
			command_pending = false;
			reply(result);
		});
	});
}

void AutomationOsd::Read(QLocalSocket *client, uint64_t generation)
{
	input += client->read(MAX_METADATA + MAX_BATCH_BYTES + 8 - input.size());
	if(input.size() < 8)
		return;
	const auto metadata_size = qFromLittleEndian<quint32>(input.constData());
	const auto pixel_size = qFromLittleEndian<quint32>(input.constData() + 4);
	const quint64 size = 8ULL + metadata_size + pixel_size;
	if(metadata_size > MAX_METADATA || pixel_size > MAX_BATCH_BYTES || generation != epoch || quint64(input.size()) > size)
	{
		client->abort();
		return;
	}
	if(quint64(input.size()) < size)
		return;
	const auto doc = QJsonDocument::fromJson(input.mid(8, metadata_size));
	const QString error = doc.isObject() ? Apply(doc.object(), input.mid(8 + metadata_size), generation) : QStringLiteral("invalid metadata");
	input.clear();
	// One byte of credit per batch, on the image channel only.
	if(client->bytesToWrite() != 0)
		client->abort();
	else
		client->write(error.isEmpty() ? "\0" : "\1", 1);
}

QString AutomationOsd::Apply(const QJsonObject &message, const QByteArray &raw, uint64_t generation)
{
	if(generation != epoch)
		return QStringLiteral("OSD was cleared");
	const auto images_value = message["images"];
	const auto remove_value = message["remove"];
	if((!images_value.isUndefined() && !images_value.isArray()) || (!remove_value.isUndefined() && !remove_value.isArray()))
		return QStringLiteral("images and remove must be arrays");
	const auto images = images_value.toArray();
	const auto removed = remove_value.toArray();
	if(images.size() > MAX_IMAGES || removed.size() > MAX_IMAGES)
		return QStringLiteral("too many image operations");
	auto update = std::make_shared<AutomationOsdScene>();
	if(scene && scene->epoch == generation)
		*update = *scene;
	update->epoch = generation;
	QSet<QString> ids;
	for(const auto &value : removed)
	{
		if(!value.isString() || ids.contains(value.toString()))
			return QStringLiteral("invalid or duplicate id");
		const auto id = value.toString();
		ids.insert(id);
		update->images.removeIf([&id](const auto &image) { return image.id == id; });
	}
	int offset = 0, changed_bytes = 0;
	for(const auto &value : images)
	{
		const auto object = value.toObject();
		const QString id = object["id"].toString();
		if(id.isEmpty() || id.size() > 64 || ids.contains(id))
			return QStringLiteral("invalid or duplicate id");
		ids.insert(id);
		auto it = std::find_if(update->images.begin(), update->images.end(), [&id](const auto &image) { return image.id == id; });
		AutomationOsdImage image;
		if(it != update->images.end())
			image = *it;
		image.id = id;
		if(object.contains("rect"))
		{
			const auto rect = object["rect"].toArray();
			if(rect.size() != 4)
				return QStringLiteral("rect must contain x, y, width, height");
			for(int i = 0; i < 4; ++i)
			{
				if(!rect[i].isDouble() || !std::isfinite(rect[i].toDouble()) || rect[i].toDouble() < 0 || rect[i].toDouble() > 1)
					return QStringLiteral("rect must be normalized to 0..1");
				image.rect[i] = rect[i].toDouble();
			}
			if(image.rect[2] <= 0 || image.rect[3] <= 0 || image.rect[0] + image.rect[2] > 1.000001f || image.rect[1] + image.rect[3] > 1.000001f)
				return QStringLiteral("rect must fit inside the video");
		}
		else if(it == update->images.end())
			return QStringLiteral("new image requires rect");
		if(object.contains("pixel_size"))
		{
			const auto size = object["pixel_size"].toArray();
			if(size.size() != 2)
				return QStringLiteral("pixel_size must contain width and height");
			image.width = size[0].toInt();
			image.height = size[1].toInt();
			if(image.width < 1 || image.height < 1 || image.width > 512 || image.height > 512 || image.width * image.height * 4 > MAX_IMAGE_BYTES)
				return QStringLiteral("image exceeds pixel limits");
			const int bytes = image.width * image.height * 4;
			if(object.contains("data"))
			{
				const auto decoded = QByteArray::fromBase64Encoding(object["data"].toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
				if(!decoded)
					return QStringLiteral("invalid base64");
				image.pixels = decoded.decoded;
			}
			else
			{
				image.pixels = raw.mid(offset, bytes);
				offset += bytes;
			}
			if(image.pixels.size() != bytes)
				return QStringLiteral("pixel byte count mismatch");
			const auto format = object["format"].toString();
			const auto alpha = object["alpha_mode"].toString("straight");
			if((format != "rgba8888" && format != "bgra8888") || (alpha != "straight" && alpha != "premultiplied"))
				return QStringLiteral("unsupported pixel format or alpha mode");
			if(format == "bgra8888" || alpha == "straight")
			{
				auto p = reinterpret_cast<unsigned char *>(image.pixels.data());
				for(int n = 0; n < bytes; n += 4)
				{
					if(format == "bgra8888")
						std::swap(p[n], p[n + 2]);
					if(alpha == "straight")
						for(int c = 0; c < 3; ++c)
							p[n + c] = (unsigned(p[n + c]) * p[n + 3] + 127) / 255;
				}
			}
			changed_bytes += bytes;
		}
		else if(image.pixels.isEmpty() || object.contains("data"))
			return QStringLiteral("pixels require pixel_size");
		if(it == update->images.end())
			update->images.append(std::move(image));
		else
			*it = std::move(image);
	}
	int scene_bytes = 0;
	double area = 0;
	for(const auto &image : update->images)
	{
		scene_bytes += image.pixels.size();
		area += image.rect[2] * image.rect[3];
	}
	if(offset != raw.size() || changed_bytes > MAX_BATCH_BYTES || scene_bytes > MAX_SCENE_BYTES || update->images.size() > MAX_IMAGES || area > 0.25)
		return QStringLiteral("image batch exceeds byte, count or coverage limits");
	if(!rate_timer.isValid() || rate_timer.elapsed() >= 1000)
	{
		rate_timer.start();
		rate_bytes = 0;
	}
	if(rate_bytes + changed_bytes > 8 * 1024 * 1024)
		return QStringLiteral("upload rate exceeded");
	rate_bytes += changed_bytes;
	QMutexLocker lock(&mutex);
	if(generation != epoch)
		return QStringLiteral("OSD was cleared");
	if(pending)
		++replaced;
	scene = update;
	pending = std::move(update);
	++accepted;
	return {};
}

AutomationOsdRenderer::AutomationOsdRenderer(pl_gpu gpu, AutomationOsd *osd) : gpu(gpu), osd(osd)
{
	if(!gpu->limits.callbacks)
		return;
	const auto format = pl_find_named_fmt(gpu, "rgba8");
	if(!format)
		return;
	pl_tex_params params{};
	params.w = params.h = 512;
	params.format = format;
	params.sampleable = params.host_writable = true;
	for(auto &texture : textures)
	{
		texture.tex = pl_tex_create(gpu, &params);
		if(!texture.tex)
			return;
	}
	osd->supported = true;
}

AutomationOsdRenderer::~AutomationOsdRenderer()
{
	osd->supported = false;
	// Shutdown only: callbacks must finish before their storage is destroyed.
	pl_gpu_finish(gpu);
	for(auto &texture : textures)
		pl_tex_destroy(gpu, &texture.tex);
}

void AutomationOsdRenderer::Advance()
{
	if(front && front->epoch != osd->epoch)
		front.reset();
	if(next && next->epoch != osd->epoch)
		next.reset();
	if(!next)
	{
		next = osd->Latest();
		uploaded = 0;
		next_slots.fill(-1);
	}
	if(!next || next->epoch != osd->epoch)
		return;
	int bytes = 0;
	for(; uploaded < next->images.size(); ++uploaded)
	{
		const auto &image = next->images[uploaded];
		int slot = -1;
		for(int i = 0; front && i < front->images.size(); ++i)
			if(front->images[i].pixels.constData() == image.pixels.constData())
				slot = front_slots[i];
		if(slot < 0)
		{
			if(bytes + image.pixels.size() > MAX_IMAGE_BYTES)
				return;
			for(int i = 0; i < int(textures.size()); ++i)
			{
				bool used = textures[i].uploading;
				for(int n = 0; front && n < front->images.size(); ++n)
					used |= front_slots[n] == i;
				for(int n = 0; n < uploaded; ++n)
					used |= next_slots[n] == i;
				if(!used && !pl_tex_poll(gpu, textures[i].tex, 0))
				{
					slot = i;
					break;
				}
			}
			if(slot < 0)
				return;
			auto &texture = textures[slot];
			texture.pixels = image.pixels;
			texture.uploading = true;
			pl_tex_transfer_params transfer{};
			transfer.tex = texture.tex;
			transfer.rc = {0, 0, 0, image.width, image.height, 1};
			transfer.row_pitch = image.width * 4;
			transfer.ptr = const_cast<char *>(texture.pixels.constData());
			transfer.callback = [](void *priv) { static_cast<Texture *>(priv)->uploading = false; };
			transfer.priv = &texture;
			if(!pl_tex_upload(gpu, &transfer))
			{
				texture.uploading = false;
				next.reset();
				return;
			}
			bytes += image.pixels.size();
		}
		next_slots[uploaded] = slot;
	}
	for(int i = 0; i < uploaded; ++i)
		if(textures[next_slots[i]].uploading)
			return;
	front = std::move(next);
	front_slots = next_slots;
	++osd->displayed;
}

const pl_rect2df *AutomationOsdRenderer::VideoCrop(const pl_rect2df *source)
{
	if(reset_crop.exchange(false))
		video_crop = {};
	if(source)
		video_crop = *source;
	return pl_rect_w(video_crop) && pl_rect_h(video_crop) ? &video_crop : nullptr;
}

void AutomationOsdRenderer::Compose(pl_frame &target, const pl_overlay &qml)
{
	if(!osd->supported)
		return;
	Advance();
	int count = 0;
	if(front && front->epoch == osd->epoch)
	{
		for(int i = 0; i < front->images.size(); ++i)
		{
			const auto &image = front->images[i];
			const auto &r = image.rect;
			auto &part = parts[count];
			part = {};
			part.src = {0, 0, float(image.width), float(image.height)};
			part.dst = {target.crop.x0 + r[0] * pl_rect_w(target.crop), target.crop.y0 + r[1] * pl_rect_h(target.crop),
				target.crop.x0 + (r[0] + r[2]) * pl_rect_w(target.crop), target.crop.y0 + (r[1] + r[3]) * pl_rect_h(target.crop)};
			auto &overlay = overlays[count++];
			overlay = {};
			overlay.tex = textures[front_slots[i]].tex;
			overlay.repr = pl_color_repr_rgb;
			overlay.repr.alpha = PL_ALPHA_PREMULTIPLIED;
			overlay.color = pl_color_space_srgb;
			overlay.parts = &part;
			overlay.num_parts = 1;
		}
	}
	overlays[count++] = qml;
	target.overlays = overlays.data();
	target.num_overlays = count;
}
