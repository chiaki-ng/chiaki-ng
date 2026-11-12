// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include <rogally.h>

#ifdef _WIN32
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>

/* Minimal WinRT ABI declarations, avoiding SDK-generated sensor headers. */
struct IAccelerometer;
struct IAccelerometerReading;
struct IGyrometer;
struct IGyrometerReading;

struct IAccelerometerStatics : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetDefault(IAccelerometer **sensor) = 0;
};

struct IAccelerometer : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetCurrentReading(IAccelerometerReading **reading) = 0;
};

struct IAccelerometerReading : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Timestamp(INT64 *timestamp) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AccelerationX(double *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AccelerationY(double *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AccelerationZ(double *value) = 0;
};

struct IGyrometerStatics : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetDefault(IGyrometer **sensor) = 0;
};

struct IGyrometer : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE GetCurrentReading(IGyrometerReading **reading) = 0;
};

struct IGyrometerReading : IInspectable
{
    virtual HRESULT STDMETHODCALLTYPE get_Timestamp(INT64 *timestamp) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AngularVelocityX(double *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AngularVelocityY(double *value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AngularVelocityZ(double *value) = 0;
};

static const IID IID_IAccelerometerStatics =
    {0xa5e28b74, 0x5a87, 0x4a2d, {0xbe, 0xcc, 0x0f, 0x90, 0x6e, 0xa0, 0x61, 0xdd}};
static const IID IID_IGyrometerStatics =
    {0x83b6e7c9, 0xe49d, 0x4b39, {0x86, 0xe6, 0xcd, 0x55, 0x4b, 0xe4, 0xc5, 0xc1}};

template<typename TStatics, typename TSensor>
static HRESULT get_default_sensor(const wchar_t *runtime_class, REFIID statics_iid, TSensor **sensor)
{
    *sensor = NULL;

    HSTRING class_name = NULL;
    HRESULT hr = WindowsCreateString(runtime_class, (UINT32)wcslen(runtime_class), &class_name);
    if (FAILED(hr))
        return hr;

    TStatics *statics = NULL;
    hr = RoGetActivationFactory(class_name, statics_iid, (void **)&statics);
    HRESULT delete_hr = WindowsDeleteString(class_name);
    if (FAILED(hr)) {
        if (statics)
            statics->Release();
        return hr;
    }
    if (FAILED(delete_hr)) {
        if (statics)
            statics->Release();
        return delete_hr;
    }
    if (!statics)
        return E_POINTER;

    hr = statics->GetDefault(sensor);
    statics->Release();
    if (FAILED(hr) && *sensor) {
        (*sensor)->Release();
        *sensor = NULL;
    }
    return hr;
}

struct rogally_t {
    std::thread worker;
    std::atomic<bool> stop_requested;
    std::mutex event_mutex;
    RogallyEvent latest_event;
    bool event_pending;

    rogally_t() : stop_requested(false), event_pending(false) {}
};

static uint64_t get_monotonic_timestamp_us(void)
{
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter) || freq.QuadPart <= 0)
        return GetTickCount64() * 1000;
    return (uint64_t)((double)counter.QuadPart * 1000000.0 / (double)freq.QuadPart);
}

static void publish_event(Rogally *r, const RogallyEvent *event)
{
    std::lock_guard<std::mutex> lock(r->event_mutex);
    r->latest_event = *event;
    r->event_pending = true;
}

static void publish_error(Rogally *r)
{
    RogallyEvent event = {};
    event.type = ROGALLY_EVENT_ERROR;
    event.error = true;
    publish_event(r, &event);
}

static void rogally_worker(Rogally *r)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        fprintf(stderr, "rogally: RoInitialize failed (0x%08lx)\n", hr);
        publish_error(r);
        return;
    }

    IAccelerometer *accelerometer = NULL;
    IGyrometer *gyrometer = NULL;

    hr = get_default_sensor<IAccelerometerStatics, IAccelerometer>(
        L"Windows.Devices.Sensors.Accelerometer", IID_IAccelerometerStatics, &accelerometer);
    bool has_accel = SUCCEEDED(hr) && accelerometer;

    hr = get_default_sensor<IGyrometerStatics, IGyrometer>(
        L"Windows.Devices.Sensors.Gyrometer", IID_IGyrometerStatics, &gyrometer);
    bool has_gyro = SUCCEEDED(hr) && gyrometer;

    if (!has_accel && !has_gyro) {
        fprintf(stderr, "rogally: No accelerometer or gyrometer sensors found\n");
        publish_error(r);
        if (accelerometer)
            accelerometer->Release();
        if (gyrometer)
            gyrometer->Release();
        RoUninitialize();
        return;
    }

    fprintf(stderr, "rogally: ROG Ally IMU initialized (accel=%d gyro=%d)\n",
            has_accel, has_gyro);

    float last_accel_x = 0.0f, last_accel_y = 1.0f, last_accel_z = 0.0f;
    float last_gyro_x = 0.0f, last_gyro_y = 0.0f, last_gyro_z = 0.0f;
    while (!r->stop_requested.load()) {
        bool motion_dirty = false;

        if (has_accel && accelerometer) {
            IAccelerometerReading *reading = NULL;
            HRESULT reading_hr = accelerometer->GetCurrentReading(&reading);
            if (SUCCEEDED(reading_hr) && reading) {
                double ax, ay, az;
                HRESULT x_hr = reading->get_AccelerationX(&ax);
                HRESULT y_hr = reading->get_AccelerationY(&ay);
                HRESULT z_hr = reading->get_AccelerationZ(&az);
                if (SUCCEEDED(x_hr) && SUCCEEDED(y_hr) && SUCCEEDED(z_hr)) {
                    last_accel_x = (float)ax;
                    last_accel_y = (float)ay;
                    last_accel_z = (float)az;
                    motion_dirty = true;
                }
                reading->Release();
            }
        }

        if (has_gyro && gyrometer) {
            IGyrometerReading *reading = NULL;
            HRESULT reading_hr = gyrometer->GetCurrentReading(&reading);
            if (SUCCEEDED(reading_hr) && reading) {
                double gx, gy, gz;
                HRESULT x_hr = reading->get_AngularVelocityX(&gx);
                HRESULT y_hr = reading->get_AngularVelocityY(&gy);
                HRESULT z_hr = reading->get_AngularVelocityZ(&gz);
                if (SUCCEEDED(x_hr) && SUCCEEDED(y_hr) && SUCCEEDED(z_hr)) {
                    const float degrees_to_radians = 0.017453292519943295f;
                    last_gyro_x = (float)gx * degrees_to_radians;
                    last_gyro_y = (float)gy * degrees_to_radians;
                    last_gyro_z = (float)gz * degrees_to_radians;
                    motion_dirty = true;
                }
                reading->Release();
            }
        }

        if (motion_dirty) {
            RogallyEvent event = {};
            event.type = ROGALLY_EVENT_MOTION;
            event.motion.accel_x = last_accel_x;
            event.motion.accel_y = last_accel_y;
            event.motion.accel_z = last_accel_z;
            event.motion.gyro_x = last_gyro_x;
            event.motion.gyro_y = last_gyro_y;
            event.motion.gyro_z = last_gyro_z;
            event.motion.timestamp_us = get_monotonic_timestamp_us();
            publish_event(r, &event);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }

    if (accelerometer)
        accelerometer->Release();
    if (gyrometer)
        gyrometer->Release();
    RoUninitialize();
}

Rogally *rogally_new(void)
{
    Rogally *r = NULL;
    try {
        r = new Rogally();
        r->worker = std::thread(rogally_worker, r);
    } catch (const std::bad_alloc &) {
        fprintf(stderr, "rogally: Failed to allocate sensor worker\n");
        delete r;
        return NULL;
    } catch (const std::system_error &error) {
        fprintf(stderr, "rogally: Failed to start sensor worker: %s\n", error.what());
        delete r;
        return NULL;
    }

    return r;
}

void rogally_free(Rogally *r)
{
    if (!r)
        return;
    r->stop_requested.store(true);
    if (r->worker.joinable())
        r->worker.join();
    delete r;
}

void rogally_read(Rogally *r, RogallyEventCb cb, void *user)
{
    if (!r || !cb)
        return;

    RogallyEvent event = {};
    {
        std::lock_guard<std::mutex> lock(r->event_mutex);
        if (!r->event_pending)
            return;
        event = r->latest_event;
        r->event_pending = false;
    }

    cb(&event, user);
}

#else

struct rogally_t {
    int dummy;
};

Rogally *rogally_new(void)
{
    return NULL;
}

void rogally_free(Rogally *r)
{
    (void)r;
}

void rogally_read(Rogally *r, RogallyEventCb cb, void *user)
{
    (void)r;
    (void)cb;
    (void)user;
}

#endif
