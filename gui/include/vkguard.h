#ifndef CHIAKI_GUI_VKGUARD_H
#define CHIAKI_GUI_VKGUARD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ChiakiGuardedFunc)(void *ctx);

// Runs fn(ctx). On Windows MSVC/clang-cl builds a structured exception (access
// violation, ...) raised inside it is caught: the call returns 0 and stores the
// exception code. Elsewhere fn is called directly and *exception_code is 0.
int chiaki_guarded_call(ChiakiGuardedFunc fn, void *ctx, unsigned long *exception_code);

#ifdef __cplusplus
}

#include <libplacebo/vulkan.h>

#include <algorithm>
#include <utility>
#include <vector>

// Calls `f` (returning bool) through chiaki_guarded_call.
template<typename F>
bool guardedCall(F &&f, unsigned long *exception_code)
{
    using Fn = std::remove_reference_t<F>;
    return chiaki_guarded_call([](void *p) -> int {
        return (*static_cast<Fn *>(p))() ? 1 : 0;
    }, &f, exception_code) != 0;
}

// Locks every graphics/compute/transfer queue libplacebo created while it is
// alive. Third party runtimes (NGX, FidelityFX) may submit to these queues
// internally without knowing about libplacebo's queue locks, while other
// threads (presentation, decoding) submit concurrently; Vulkan requires
// queue access to be externally synchronized.
class VkQueuesLock
{
public:
    explicit VkQueuesLock(pl_vulkan vk)
        : vk(vk)
    {
        for (const pl_vulkan_queue &q : { vk->queue_graphics, vk->queue_compute, vk->queue_transfer }) {
            for (uint32_t i = 0; i < q.count; i++) {
                const auto entry = std::make_pair(q.index, i);
                if (std::find(queues.begin(), queues.end(), entry) == queues.end())
                    queues.push_back(entry);
            }
        }
        // A fixed order keeps two lockers from deadlocking each other.
        std::sort(queues.begin(), queues.end());
        for (const auto &q : queues)
            vk->lock_queue(vk, q.first, q.second);
    }

    ~VkQueuesLock()
    {
        for (auto it = queues.rbegin(); it != queues.rend(); ++it)
            vk->unlock_queue(vk, it->first, it->second);
    }

    VkQueuesLock(const VkQueuesLock &) = delete;
    VkQueuesLock &operator=(const VkQueuesLock &) = delete;

private:
    pl_vulkan vk;
    std::vector<std::pair<uint32_t, uint32_t>> queues;
};
#endif // __cplusplus

#endif // CHIAKI_GUI_VKGUARD_H
