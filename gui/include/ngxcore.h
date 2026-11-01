#ifndef CHIAKI_GUI_NGXCORE_H
#define CHIAKI_GUI_NGXCORE_H

#include <libplacebo/vulkan.h>

// Shared, reference counted NVIDIA NGX initialization on libplacebo's Vulkan
// device. NGX may only be initialized once per device, but both RTX Video
// Super Resolution and DLSS Frame Generation use it. Only functional when
// built with CHIAKI_HAVE_NGX.
namespace NgxCore
{
// Initializes NGX on the first call. Every successful acquire() must be
// balanced by a release() before the device is destroyed.
bool acquire(pl_vulkan vk);
void release();
// Human readable NGX result, for logs.
const char *resultString(unsigned int result);
}

#endif // CHIAKI_GUI_NGXCORE_H
