// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "automationframes.h"

#include <QCoreApplication>
#include <QDebug>
#include <QLoggingCategory>
#include <QMutexLocker>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>

#if defined(Q_OS_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

Q_DECLARE_LOGGING_CATEGORY(chiakiGui)

// Shared-memory layout per docs/diy/automationbridge.md (little-endian).
// All supported targets are little-endian, so fields are stored natively.
static constexpr char kShmMagic[8] = {'C', 'H', 'I', 'A', 'F', 'R', 'M', '2'};
static constexpr uint32_t kShmVersion = 2;
static constexpr uint32_t kSlotCount = 4;
static constexpr uint32_t kMaxFrameWidth = 1920;
static constexpr uint32_t kMaxFrameHeight = 1080;
static constexpr uint32_t kPayloadSize = kMaxFrameWidth * kMaxFrameHeight * 3 / 2; // 3110400 (NV12)

// Header offsets (64 bytes total): magic[8], u32 version, u32 slot_count,
// u32 slot_size, u32 width, u32 height, u32 reserved, u32 write_index,
// u32 reserved, u32 creator_pid, padding.
// Interprocess counters are u32: aligned 32-bit accesses are atomic on every
// supported target, while 64-bit ones can tear on 32-bit builds.
static constexpr size_t kHeaderSize = 64;
static constexpr size_t kHeaderOffVersion = 8;
static constexpr size_t kHeaderOffSlotCount = 12;
static constexpr size_t kHeaderOffSlotSize = 16;
static constexpr size_t kHeaderOffWidth = 20;
static constexpr size_t kHeaderOffHeight = 24;
static constexpr size_t kHeaderOffReserved = 28;
static constexpr size_t kHeaderOffWriteIndex = 32;
static constexpr size_t kHeaderOffCreatorPid = 40;

static_assert((kHeaderOffWriteIndex % alignof(uint32_t)) == 0, "write_index must stay 4-byte aligned");

// Slot: u32 seq (seqlock, odd while writing), u16 width, u16 height, u64 frame_index,
// double pts, then the NV12 payload. The 64-bit fields can tear on 32-bit
// targets; the u32 seqlock detects that, so readers must re-check seq.
static constexpr size_t kSlotHeaderSize = 24;
static constexpr size_t kSlotOffDimensions = 4;
static constexpr size_t kSlotOffFrameIndex = 8;
static constexpr size_t kSlotOffPts = 16;
static constexpr uint32_t kSlotSize = kSlotHeaderSize + kPayloadSize;

static_assert((kSlotSize % 8) == 0, "slot size must stay 8-byte aligned");

static inline void shm_store32(uint8_t *p, uint32_t v)
{
	*reinterpret_cast<volatile uint32_t *>(p) = v;
}

static inline void shm_store64(uint8_t *p, uint64_t v)
{
	*reinterpret_cast<volatile uint64_t *>(p) = v;
}

static void CopyPlaneRows(uint8_t *dst, const uint8_t *src, int src_linesize, int row_bytes, int rows)
{
	for(int y = 0; y < rows; y++)
		memcpy(dst + static_cast<size_t>(y) * row_bytes, src + static_cast<ptrdiff_t>(y) * src_linesize, row_bytes);
}

// Converts a software frame into NV12 at dst (capacity kPayloadSize).
// All validation happens up front so a started conversion cannot fail.
static bool ConvertToNv12(const AVFrame *src, uint8_t *dst, uint32_t *out_width, uint32_t *out_height)
{
	if(!src || !src->data[0] || src->width <= 0 || src->height <= 0)
		return false;
	const int width = src->width;
	const int height = src->height;
	if(width > static_cast<int>(kMaxFrameWidth) || height > static_cast<int>(kMaxFrameHeight) || (width & 1) || (height & 1))
		return false;
	if(static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2 > kPayloadSize)
		return false;
	if(src->linesize[0] < width)
		return false;

	uint8_t *dst_y = dst;
	uint8_t *dst_uv = dst + static_cast<size_t>(width) * height;

	switch(src->format)
	{
	case AV_PIX_FMT_NV12:
	{
		if(!src->data[1] || src->linesize[1] < width)
			return false;
		CopyPlaneRows(dst_y, src->data[0], src->linesize[0], width, height);
		CopyPlaneRows(dst_uv, src->data[1], src->linesize[1], width, height / 2);
		break;
	}
	case AV_PIX_FMT_YUV420P: // YUV420P and I420 share the same enum value
	{
		if(!src->data[1] || !src->data[2])
			return false;
		const int chroma_width = width / 2;
		if(src->linesize[1] < chroma_width || src->linesize[2] < chroma_width)
			return false;
		CopyPlaneRows(dst_y, src->data[0], src->linesize[0], width, height);
		for(int y = 0; y < height / 2; y++)
		{
			const uint8_t *row_u = src->data[1] + static_cast<ptrdiff_t>(y) * src->linesize[1];
			const uint8_t *row_v = src->data[2] + static_cast<ptrdiff_t>(y) * src->linesize[2];
			uint8_t *row_uv = dst_uv + static_cast<size_t>(y) * width;
			for(int x = 0; x < chroma_width; x++)
			{
				row_uv[2 * x] = row_u[x];
				row_uv[2 * x + 1] = row_v[x];
			}
		}
		break;
	}
	case AV_PIX_FMT_P010LE: // 10-bit in high bits of 16-bit LE words; >>8 keeps the top 8 bits
	{
		if(!src->data[1] || src->linesize[0] < width * 2 || src->linesize[1] < width * 2)
			return false;
		for(int y = 0; y < height; y++)
		{
			const uint8_t *row = src->data[0] + static_cast<ptrdiff_t>(y) * src->linesize[0];
			uint8_t *out = dst_y + static_cast<size_t>(y) * width;
			for(int x = 0; x < width; x++)
				out[x] = static_cast<uint8_t>(AV_RL16(row + 2 * x) >> 8);
		}
		for(int y = 0; y < height / 2; y++)
		{
			const uint8_t *row = src->data[1] + static_cast<ptrdiff_t>(y) * src->linesize[1];
			uint8_t *out = dst_uv + static_cast<size_t>(y) * width;
			for(int x = 0; x < width; x++)
				out[x] = static_cast<uint8_t>(AV_RL16(row + 2 * x) >> 8);
		}
		break;
	}
	case AV_PIX_FMT_YUV420P10LE: // planar 10-bit in low bits of 16-bit LE words; >>2 keeps the top 8 bits
	{
		if(!src->data[1] || !src->data[2])
			return false;
		const int chroma_width = width / 2;
		if(src->linesize[0] < width * 2 || src->linesize[1] < chroma_width * 2 || src->linesize[2] < chroma_width * 2)
			return false;
		for(int y = 0; y < height; y++)
		{
			const uint8_t *row = src->data[0] + static_cast<ptrdiff_t>(y) * src->linesize[0];
			uint8_t *out = dst_y + static_cast<size_t>(y) * width;
			for(int x = 0; x < width; x++)
				out[x] = static_cast<uint8_t>(AV_RL16(row + 2 * x) >> 2);
		}
		for(int y = 0; y < height / 2; y++)
		{
			const uint8_t *row_u = src->data[1] + static_cast<ptrdiff_t>(y) * src->linesize[1];
			const uint8_t *row_v = src->data[2] + static_cast<ptrdiff_t>(y) * src->linesize[2];
			uint8_t *row_uv = dst_uv + static_cast<size_t>(y) * width;
			for(int x = 0; x < chroma_width; x++)
			{
				row_uv[2 * x] = static_cast<uint8_t>(AV_RL16(row_u + 2 * x) >> 2);
				row_uv[2 * x + 1] = static_cast<uint8_t>(AV_RL16(row_v + 2 * x) >> 2);
			}
		}
		break;
	}
	default:
		return false;
	}

	*out_width = static_cast<uint32_t>(width);
	*out_height = static_cast<uint32_t>(height);
	return true;
}

// Object name is advertised to clients via the bridge socket, never hardcoded.
// Short on purpose: macOS limits shm_open names to about 30 bytes.
static QString ShmBaseName()
{
	return QStringLiteral("chiaki-ab-f%1").arg(QCoreApplication::applicationPid());
}

#if defined(Q_OS_WIN32)

// A stale client can keep the base name alive by holding a mapping view,
// so name collisions are retried with a monotonic "-n" suffix.
static constexpr int kShmNameAttempts = 8;

static HANDLE ShmCreate(const QString &base_name, size_t size, void **base_out, QString *name_out, QString *error_out)
{
	for(int attempt = 0; attempt < kShmNameAttempts; attempt++)
	{
		const QString candidate = attempt == 0 ? base_name : QStringLiteral("%1-%2").arg(base_name).arg(attempt);
		const QString full_name = QStringLiteral("Local\\") + candidate;
		HANDLE handle = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
			static_cast<DWORD>(static_cast<uint64_t>(size) >> 32), static_cast<DWORD>(size & 0xffffffffu),
			reinterpret_cast<LPCWSTR>(full_name.utf16()));
		const DWORD create_error = GetLastError();
		if(!handle)
		{
			if(error_out)
				*error_out = QStringLiteral("CreateFileMapping(%1) failed: %2").arg(full_name).arg(create_error);
			return nullptr;
		}
		if(create_error == ERROR_ALREADY_EXISTS)
		{
			CloseHandle(handle);
			continue;
		}
		void *base = MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
		if(!base)
		{
			const DWORD map_error = GetLastError();
			if(error_out)
				*error_out = QStringLiteral("MapViewOfFile(%1) failed: %2").arg(full_name).arg(map_error);
			CloseHandle(handle);
			return nullptr;
		}
		*base_out = base;
		if(name_out)
			*name_out = candidate;
		return handle;
	}
	if(error_out)
		*error_out = QStringLiteral("shared memory names based on %1 are all in use").arg(base_name);
	return nullptr;
}

static void ShmDestroy(void *handle, void *base)
{
	if(base)
		UnmapViewOfFile(base);
	if(handle)
		CloseHandle(handle);
}

#else

static int ShmCreate(const QString &base_name, size_t size, void **base_out, QString *error_out)
{
	const QString name = QStringLiteral("/") + base_name;
	const QByteArray name_utf8 = name.toUtf8();
	int fd = shm_open(name_utf8.constData(), O_RDWR | O_CREAT | O_EXCL, 0600);
	if(fd < 0 && errno == EEXIST)
	{
		// The name embeds our pid, so a collision can only be a stale object
		// left by a crashed process: remove and retry once.
		shm_unlink(name_utf8.constData());
		fd = shm_open(name_utf8.constData(), O_RDWR | O_CREAT | O_EXCL, 0600);
	}
	if(fd < 0)
	{
		const int err = errno;
		if(error_out)
			*error_out = QStringLiteral("shm_open(%1) failed: %2").arg(name, QString::fromUtf8(strerror(err)));
		return -1;
	}
	if(ftruncate(fd, static_cast<off_t>(size)) < 0)
	{
		const int err = errno;
		if(error_out)
			*error_out = QStringLiteral("ftruncate(%1) failed: %2").arg(name, QString::fromUtf8(strerror(err)));
		close(fd);
		shm_unlink(name_utf8.constData());
		return -1;
	}
	void *base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if(base == MAP_FAILED)
	{
		const int err = errno;
		if(error_out)
			*error_out = QStringLiteral("mmap(%1) failed: %2").arg(name, QString::fromUtf8(strerror(err)));
		close(fd);
		shm_unlink(name_utf8.constData());
		return -1;
	}
	*base_out = base;
	return fd;
}

static void ShmDestroy(const QString &base_name, int fd, void *base, size_t size)
{
	if(base)
		munmap(base, size);
	if(fd >= 0)
		close(fd);
	if(!base_name.isEmpty())
	{
		const QByteArray name_utf8 = (QStringLiteral("/") + base_name).toUtf8();
		if(shm_unlink(name_utf8.constData()) < 0)
			qCWarning(chiakiGui) << "shm_unlink(" << base_name << ") failed:" << strerror(errno);
	}
}

#endif

AutomationFrames::AutomationFrames(QObject *parent)
	: QObject(parent)
{
}

AutomationFrames::~AutomationFrames()
{
	Unsubscribe();
}

bool AutomationFrames::Subscribe(const QString &format, uint32_t max_fps, QString *error_out)
{
	QMutexLocker locker(&write_mutex);

	if(format != QLatin1String("nv12"))
	{
		if(error_out)
			*error_out = QStringLiteral("unsupported frame format \"%1\", only \"nv12\" is available").arg(format);
		return false;
	}

	UnsubscribeLocked();

	if(max_fps == 0) // 0 = omitted by the client
		max_fps = 30;
	max_fps = std::max<uint32_t>(1, std::min<uint32_t>(max_fps, 60));

	const QString base_name = ShmBaseName();
	const size_t total_size = kHeaderSize + static_cast<size_t>(kSlotCount) * kSlotSize;
	void *base = nullptr;
	QString actual_name = base_name;
#if defined(Q_OS_WIN32)
	HANDLE handle = ShmCreate(base_name, total_size, &base, &actual_name, error_out);
	if(!handle)
		return false;
#else
	int fd = ShmCreate(base_name, total_size, &base, error_out);
	if(fd < 0)
		return false;
#endif

	uint8_t *bytes = static_cast<uint8_t *>(base);
	memcpy(bytes, kShmMagic, sizeof(kShmMagic));
	shm_store32(bytes + kHeaderOffVersion, kShmVersion);
	shm_store32(bytes + kHeaderOffSlotCount, kSlotCount);
	shm_store32(bytes + kHeaderOffSlotSize, kSlotSize);
	shm_store32(bytes + kHeaderOffWidth, 0);
	shm_store32(bytes + kHeaderOffHeight, 0);
	shm_store32(bytes + kHeaderOffReserved, 0);
	shm_store32(bytes + kHeaderOffWriteIndex, 0);
	shm_store32(bytes + kHeaderOffCreatorPid, static_cast<uint32_t>(QCoreApplication::applicationPid()));
	// remaining header padding and slot storage stay zeroed from ftruncate
	std::atomic_thread_fence(std::memory_order_release);

#if defined(Q_OS_WIN32)
	shm_handle = handle;
#else
	shm_fd = fd;
#endif
	shm_base = base;
	shm_size = total_size;
	shm_name = actual_name;
#if defined(Q_OS_WIN32)
	shm_path = QStringLiteral("Local\\") + actual_name;
#elif defined(Q_OS_LINUX)
	shm_path = QStringLiteral("/dev/shm/") + actual_name;
#else
	shm_path = QStringLiteral("/") + actual_name;
#endif
	this->max_fps = max_fps;
	frames_published = 0;
	have_last_write = false;
	warned_formats.clear();
	current_width.store(0, std::memory_order_relaxed);
	current_height.store(0, std::memory_order_relaxed);
	subscribed.store(true, std::memory_order_release);
	publish_token.fetch_add(1, std::memory_order_relaxed);

	qCInfo(chiakiGui) << "Automation frame ring published at" << shm_path
		<< "slots" << kSlotCount << "slot_size" << kSlotSize << "max_fps" << max_fps;
	return true;
}

void AutomationFrames::UnsubscribeLocked()
{
	subscribed.store(false, std::memory_order_release);
#if defined(Q_OS_WIN32)
	if(shm_handle || shm_base)
	{
		ShmDestroy(shm_handle, shm_base);
		shm_handle = nullptr;
		shm_base = nullptr;
		shm_size = 0;
	}
#else
	if(shm_base || shm_fd >= 0)
	{
		ShmDestroy(shm_name, shm_fd, shm_base, shm_size);
		shm_base = nullptr;
		shm_fd = -1;
		shm_size = 0;
	}
#endif
	shm_name.clear();
	shm_path.clear();
	max_fps = 0;
	frames_published = 0;
	have_last_write = false;
	current_width.store(0, std::memory_order_relaxed);
	current_height.store(0, std::memory_order_relaxed);
}

void AutomationFrames::Unsubscribe()
{
	QMutexLocker locker(&write_mutex);
	UnsubscribeLocked();
}

bool AutomationFrames::IsSubscribed() const
{
	return subscribed.load(std::memory_order_acquire);
}

AutomationFramesShmInfo AutomationFrames::GetShmInfo() const
{
	AutomationFramesShmInfo info;
	if(!subscribed.load(std::memory_order_acquire))
		return info;
	info.path = shm_path;
	info.slot_count = kSlotCount;
	info.slot_size = kSlotSize;
	info.width = current_width.load(std::memory_order_acquire);
	info.height = current_height.load(std::memory_order_acquire);
	info.format = QStringLiteral("nv12");
	return info;
}

void AutomationFrames::SetEventCallback(EventCallback callback)
{
	QMutexLocker locker(&write_mutex);
	event_callback = std::move(callback);
}

void AutomationFrames::WriteFrame(const AVFrame *frame, double pts)
{
	if(!subscribed.load(std::memory_order_acquire) || !frame)
		return;

	// Never block the frame thread: if Subscribe/Unsubscribe holds the lock,
	// drop this frame.
	if(!write_mutex.tryLock())
		return;

	bool published = false;
	uint64_t published_index = 0;
	uint32_t published_slot = 0;
	uint32_t published_width = 0;
	uint32_t published_height = 0;
	uint64_t published_token = 0;
	EventCallback callback;
	AVFrame *sw_frame = nullptr;

	do {
		if(!shm_base)
			break;

		// Throttle to max_fps on a monotonic clock.
		const auto now = std::chrono::steady_clock::now();
		if(have_last_write)
		{
			const auto min_interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<double>(1.0 / static_cast<double>(max_fps)));
			if(now - last_write_time < min_interval)
				break;
		}

		const AVFrame *src = frame;
		if(frame->hw_frames_ctx)
		{
			sw_frame = av_frame_alloc();
			if(!sw_frame)
				break;
			if(av_hwframe_transfer_data(sw_frame, frame, 0) < 0)
			{
				if(!warned_formats.contains(frame->format))
				{
					warned_formats.insert(frame->format);
					const char *name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format));
					qCWarning(chiakiGui) << "Automation frames: failed to download hardware frame of format"
						<< (name ? name : "unknown") << ", dropping frame";
				}
				break;
			}
			src = sw_frame;
		}

		uint8_t *bytes = static_cast<uint8_t *>(shm_base);
		const uint64_t index = frames_published;
		const uint32_t slot_index = static_cast<uint32_t>(index % kSlotCount);
		uint8_t *slot = bytes + kHeaderSize + static_cast<size_t>(slot_index) * kSlotSize;
		uint8_t *payload = slot + kSlotHeaderSize;

		uint32_t width = 0;
		uint32_t height = 0;
		volatile uint32_t *seq = reinterpret_cast<volatile uint32_t *>(slot);
		const uint32_t seq_begin = *seq + 1; // odd: write in progress
		*seq = seq_begin;
		std::atomic_thread_fence(std::memory_order_release);

		if(!ConvertToNv12(src, payload, &width, &height))
		{
			if(!warned_formats.contains(src->format))
			{
				warned_formats.insert(src->format);
				const char *name = av_get_pix_fmt_name(static_cast<AVPixelFormat>(src->format));
				qCWarning(chiakiGui) << "Automation frames: unsupported pixel format"
					<< (name ? name : "unknown") << ", dropping frame";
			}
			std::atomic_thread_fence(std::memory_order_release);
			*seq = seq_begin - 1; // validation failed before touching the payload
			break;
		}

		shm_store32(slot + kSlotOffDimensions, width | (height << 16));
		shm_store64(slot + kSlotOffFrameIndex, index);
		static_assert(sizeof(double) == sizeof(uint64_t), "pts storage");
		double pts_value = pts;
		uint64_t pts_bits;
		memcpy(&pts_bits, &pts_value, sizeof(pts_bits));
		shm_store64(slot + kSlotOffPts, pts_bits);

		std::atomic_thread_fence(std::memory_order_release);
		*seq = seq_begin + 1; // even: payload complete

		if(current_width.load(std::memory_order_relaxed) != width || current_height.load(std::memory_order_relaxed) != height)
		{
			current_width.store(width, std::memory_order_relaxed);
			current_height.store(height, std::memory_order_relaxed);
			shm_store32(bytes + kHeaderOffWidth, width);
			shm_store32(bytes + kHeaderOffHeight, height);
		}
		// write_index is the publication marker: fence before the store
		std::atomic_thread_fence(std::memory_order_release);
		shm_store32(bytes + kHeaderOffWriteIndex, static_cast<uint32_t>(index + 1));

		frames_published = index + 1;
		last_write_time = now;
		have_last_write = true;

		callback = event_callback;
		published = true;
		published_index = index;
		published_slot = slot_index;
		published_width = width;
		published_height = height;
		published_token = publish_token.load(std::memory_order_relaxed);
	} while(false);

	if(sw_frame)
		av_frame_free(&sw_frame);

	write_mutex.unlock();

	// Invoke after unlocking so a callback that (indirectly) re-enters
	// WriteFrame cannot deadlock.
	if(published && callback)
		callback(published_index, published_slot, published_width, published_height, pts, published_token);
}
