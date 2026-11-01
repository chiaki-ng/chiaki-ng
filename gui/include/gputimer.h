#ifndef CHIAKI_GUI_GPUTIMER_H
#define CHIAKI_GUI_GPUTIMER_H

#include <libplacebo/vulkan.h>

#include <atomic>
#include <cstdint>
#include <vector>

// Measures the GPU time of work recorded into our own Vulkan command buffers
// with timestamp queries, one query pair per command buffer slot. Results of a
// slot are collected when the slot is reused, i.e. after its work completed.
// averageMs() may be read from any thread.
class VkGpuTimer
{
public:
    bool init(pl_vulkan vk, PFN_vkGetDeviceProcAddr get_device_proc, uint32_t slots)
    {
        destroy();
        auto get_instance_proc = vk->get_proc_addr;
        auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            get_instance_proc(vk->instance, "vkGetPhysicalDeviceProperties"));
        create_pool = reinterpret_cast<PFN_vkCreateQueryPool>(get_device_proc(vk->device, "vkCreateQueryPool"));
        destroy_pool = reinterpret_cast<PFN_vkDestroyQueryPool>(get_device_proc(vk->device, "vkDestroyQueryPool"));
        cmd_reset = reinterpret_cast<PFN_vkCmdResetQueryPool>(get_device_proc(vk->device, "vkCmdResetQueryPool"));
        cmd_write = reinterpret_cast<PFN_vkCmdWriteTimestamp>(get_device_proc(vk->device, "vkCmdWriteTimestamp"));
        get_results = reinterpret_cast<PFN_vkGetQueryPoolResults>(get_device_proc(vk->device, "vkGetQueryPoolResults"));
        if (!get_props || !create_pool || !destroy_pool || !cmd_reset || !cmd_write || !get_results)
            return false;

        VkPhysicalDeviceProperties props = {};
        get_props(vk->phys_device, &props);
        if (props.limits.timestampPeriod <= 0.0f)
            return false;
        period_ns = props.limits.timestampPeriod;

        VkQueryPoolCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = slots * 2;
        if (create_pool(vk->device, &info, nullptr, &pool) != VK_SUCCESS) {
            pool = VK_NULL_HANDLE;
            return false;
        }
        device = vk->device;
        written.assign(slots, false);
        return true;
    }

    void destroy()
    {
        if (pool && destroy_pool)
            destroy_pool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
        written.clear();
        average_ms.store(0.0f, std::memory_order_relaxed);
    }

    void begin(VkCommandBuffer cmd, uint32_t slot)
    {
        if (!pool || slot >= written.size())
            return;
        cmd_reset(cmd, pool, slot * 2, 2);
        cmd_write(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, slot * 2);
    }

    void end(VkCommandBuffer cmd, uint32_t slot)
    {
        if (!pool || slot >= written.size())
            return;
        cmd_write(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, slot * 2 + 1);
        written[slot] = true;
    }

    // Call once the work submitted for `slot` has completed.
    void collect(uint32_t slot)
    {
        if (!pool || slot >= written.size() || !written[slot])
            return;
        written[slot] = false;
        uint64_t stamps[2] = {};
        if (get_results(device, pool, slot * 2, 2, sizeof(stamps), stamps, sizeof(uint64_t),
                        VK_QUERY_RESULT_64_BIT) != VK_SUCCESS || stamps[1] < stamps[0])
            return;
        const float ms = static_cast<float>(static_cast<double>(stamps[1] - stamps[0]) * period_ns / 1e6);
        const float prev = average_ms.load(std::memory_order_relaxed);
        average_ms.store(prev > 0.0f ? prev * 0.9f + ms * 0.1f : ms, std::memory_order_relaxed);
    }

    float averageMs() const { return average_ms.load(std::memory_order_relaxed); }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkQueryPool pool = VK_NULL_HANDLE;
    float period_ns = 1.0f;
    std::vector<bool> written;
    std::atomic<float> average_ms{0.0f};
    PFN_vkCreateQueryPool create_pool = nullptr;
    PFN_vkDestroyQueryPool destroy_pool = nullptr;
    PFN_vkCmdResetQueryPool cmd_reset = nullptr;
    PFN_vkCmdWriteTimestamp cmd_write = nullptr;
    PFN_vkGetQueryPoolResults get_results = nullptr;
};

#endif // CHIAKI_GUI_GPUTIMER_H
