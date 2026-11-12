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
    IAccelerometer *accelerometer;
    IGyrometer *gyrometer;
    bool has_accel;
    bool has_gyro;
    bool initialized;
    float last_accel_x, last_accel_y, last_accel_z;
    float last_gyro_x, last_gyro_y, last_gyro_z;
    uint64_t last_timestamp_us;
    bool motion_dirty;
};

static uint64_t get_monotonic_timestamp_us(void)
{
    LARGE_INTEGER freq, counter;
    if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter) || freq.QuadPart <= 0)
        return GetTickCount64() * 1000;
    return (uint64_t)((double)counter.QuadPart * 1000000.0 / (double)freq.QuadPart);
}

Rogally *rogally_new(void)
{
    Rogally *r = (Rogally *)calloc(1, sizeof(Rogally));
    if (!r)
        return NULL;

    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {
        fprintf(stderr, "rogally: RoInitialize failed (0x%08lx)\n", hr);
        free(r);
        return NULL;
    }

    hr = get_default_sensor<IAccelerometerStatics, IAccelerometer>(
        L"Windows.Devices.Sensors.Accelerometer", IID_IAccelerometerStatics, &r->accelerometer);
    r->has_accel = SUCCEEDED(hr) && r->accelerometer;

    hr = get_default_sensor<IGyrometerStatics, IGyrometer>(
        L"Windows.Devices.Sensors.Gyrometer", IID_IGyrometerStatics, &r->gyrometer);
    r->has_gyro = SUCCEEDED(hr) && r->gyrometer;

    if (!r->has_accel && !r->has_gyro) {
        fprintf(stderr, "rogally: No accelerometer or gyrometer sensors found\n");
        rogally_free(r);
        return NULL;
    }

    r->initialized = true;
    r->last_accel_y = 1.0f; /* gravity points down on Y */
    r->motion_dirty = false;

    fprintf(stderr, "rogally: ROG Ally IMU initialized (accel=%d gyro=%d)\n",
            r->has_accel, r->has_gyro);
    return r;
}

void rogally_free(Rogally *r)
{
    if (!r)
        return;
    if (r->accelerometer)
        r->accelerometer->Release();
    if (r->gyrometer)
        r->gyrometer->Release();
    RoUninitialize();
    free(r);
}

void rogally_read(Rogally *r, RogallyEventCb cb, void *user)
{
    if (!r || !r->initialized || !cb)
        return;

    r->motion_dirty = false;

    /* Read accelerometer */
    if (r->has_accel && r->accelerometer) {
        IAccelerometerReading *reading = NULL;
        HRESULT hr = r->accelerometer->GetCurrentReading(&reading);
        if (SUCCEEDED(hr) && reading) {
            double ax, ay, az;
            HRESULT x_hr = reading->get_AccelerationX(&ax);
            HRESULT y_hr = reading->get_AccelerationY(&ay);
            HRESULT z_hr = reading->get_AccelerationZ(&az);
            if (SUCCEEDED(x_hr) && SUCCEEDED(y_hr) && SUCCEEDED(z_hr)) {
                r->last_accel_x = (float)ax;
                r->last_accel_y = (float)ay;
                r->last_accel_z = (float)az;
                r->motion_dirty = true;
            }
            reading->Release();
        }
    }

    /* Read gyrometer */
    if (r->has_gyro && r->gyrometer) {
        IGyrometerReading *reading = NULL;
        HRESULT hr = r->gyrometer->GetCurrentReading(&reading);
        if (SUCCEEDED(hr) && reading) {
            double gx, gy, gz;
            HRESULT x_hr = reading->get_AngularVelocityX(&gx);
            HRESULT y_hr = reading->get_AngularVelocityY(&gy);
            HRESULT z_hr = reading->get_AngularVelocityZ(&gz);
            if (SUCCEEDED(x_hr) && SUCCEEDED(y_hr) && SUCCEEDED(z_hr)) {
                /* Windows gyrometer is in deg/s, convert to rad/s. */
                const float degrees_to_radians = 0.017453292519943295f;
                r->last_gyro_x = (float)gx * degrees_to_radians;
                r->last_gyro_y = (float)gy * degrees_to_radians;
                r->last_gyro_z = (float)gz * degrees_to_radians;
                r->motion_dirty = true;
            }
            reading->Release();
        }
    }

    if (!r->motion_dirty)
        return;

    RogallyEvent event;
    memset(&event, 0, sizeof(event));
    event.type = ROGALLY_EVENT_MOTION;
    event.motion.accel_x = r->last_accel_x;
    event.motion.accel_y = r->last_accel_y;
    event.motion.accel_z = r->last_accel_z;
    event.motion.gyro_x = r->last_gyro_x;
    event.motion.gyro_y = r->last_gyro_y;
    event.motion.gyro_z = r->last_gyro_z;
    event.motion.timestamp_us = get_monotonic_timestamp_us();
    r->last_timestamp_us = event.motion.timestamp_us;

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
