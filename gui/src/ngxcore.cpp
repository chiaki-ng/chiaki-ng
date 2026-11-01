#include "ngxcore.h"

#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(chiakiGui);

#if defined(CHIAKI_HAVE_NGX)

#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QStandardPaths>

#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>

#include <string>

namespace {

// Arbitrary but stable project id; NGX only uses it to tell applications apart.
constexpr const char *kNgxProjectId = "5c9a8e0e-3c0b-4c1f-9b55-6b1f6c0a7c21";

QMutex ngx_mutex;
int ngx_refs = 0;
VkDevice ngx_device = VK_NULL_HANDLE;
std::wstring ngx_feature_path;
std::wstring ngx_data_path;

} // namespace

const char *NgxCore::resultString(unsigned int value)
{
    switch (static_cast<NVSDK_NGX_Result>(value)) {
    case NVSDK_NGX_Result_Success: return "Success";
    case NVSDK_NGX_Result_FAIL_FeatureNotSupported: return "FeatureNotSupported";
    case NVSDK_NGX_Result_FAIL_PlatformError: return "PlatformError";
    case NVSDK_NGX_Result_FAIL_FeatureAlreadyExists: return "FeatureAlreadyExists";
    case NVSDK_NGX_Result_FAIL_FeatureNotFound: return "FeatureNotFound (is the nvngx_*.dll next to chiaki.exe?)";
    case NVSDK_NGX_Result_FAIL_InvalidParameter: return "InvalidParameter";
    case NVSDK_NGX_Result_FAIL_NotInitialized: return "NotInitialized";
    case NVSDK_NGX_Result_FAIL_UnsupportedInputFormat: return "UnsupportedInputFormat";
    case NVSDK_NGX_Result_FAIL_OutOfDate: return "OutOfDate (update the NVIDIA driver)";
    case NVSDK_NGX_Result_FAIL_OutOfGPUMemory: return "OutOfGPUMemory";
    case NVSDK_NGX_Result_FAIL_UnsupportedFormat: return "UnsupportedFormat";
    case NVSDK_NGX_Result_FAIL_UnableToWriteToAppDataPath: return "UnableToWriteToAppDataPath";
    case NVSDK_NGX_Result_FAIL_UnableToInitializeFeature: return "UnableToInitializeFeature";
    default: return "Fail";
    }
}

bool NgxCore::acquire(pl_vulkan vk)
{
    QMutexLocker locker(&ngx_mutex);
    if (ngx_refs > 0) {
        if (ngx_device != vk->device)
            return false;
        ngx_refs++;
        return true;
    }

    auto get_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        vk->get_proc_addr(vk->instance, "vkGetDeviceProcAddr"));
    if (!get_device_proc)
        return false;

    // Feature DLLs (nvngx_vsr.dll, nvngx_dlssg.dll) are searched next to the executable.
    ngx_feature_path = QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).toStdWString();
    QString data_dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (data_dir.isEmpty())
        data_dir = QDir::tempPath();
    QDir().mkpath(data_dir);
    ngx_data_path = QDir::toNativeSeparators(data_dir).toStdWString();

    const wchar_t *paths[] = { ngx_feature_path.c_str() };
    NVSDK_NGX_FeatureCommonInfo common_info = {};
    common_info.PathListInfo.Path = paths;
    common_info.PathListInfo.Length = 1;

    const NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kNgxProjectId, NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", ngx_data_path.c_str(),
        vk->instance, vk->phys_device, vk->device,
        vk->get_proc_addr, get_device_proc, &common_info, NVSDK_NGX_Version_API);
    if (NVSDK_NGX_FAILED(result)) {
        qCWarning(chiakiGui).nospace() << "[ngx] NGX init failed: " << resultString(result)
            << " (0x" << Qt::hex << static_cast<unsigned>(result) << ")";
        return false;
    }
    ngx_device = vk->device;
    ngx_refs = 1;
    qCInfo(chiakiGui) << "[ngx] NGX initialized";
    return true;
}

void NgxCore::release()
{
    QMutexLocker locker(&ngx_mutex);
    if (ngx_refs <= 0)
        return;
    if (--ngx_refs > 0)
        return;
    NVSDK_NGX_VULKAN_Shutdown1(ngx_device);
    ngx_device = VK_NULL_HANDLE;
}

#else // !CHIAKI_HAVE_NGX

bool NgxCore::acquire(pl_vulkan) { return false; }
void NgxCore::release() {}
const char *NgxCore::resultString(unsigned int) { return "NGX not available"; }

#endif // CHIAKI_HAVE_NGX
