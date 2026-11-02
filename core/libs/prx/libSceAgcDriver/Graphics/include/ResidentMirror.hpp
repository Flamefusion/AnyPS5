#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENTMIRROR_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_GRAPHICS_INCLUDE_RESIDENTMIRROR_HPP

#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace AgcDriver::Graphics {

class Buffer;
class ResidentMemory;

using GuestRuns = std::vector<std::pair<std::uint64_t, std::uint64_t>>;

GuestRuns GuestNonzeroBlocks(const GuestRuns& candidates);
bool GuestBytesZero(std::uint64_t begin, std::uint64_t end);
void SetResidentAddressSpaceLimit(std::uint64_t bytes);
std::uint64_t ResidentPendingBytes();
void SetHeapMirrorBudget(std::uint64_t bytes);
std::shared_ptr<void> HoldHeapMirror(std::uint64_t address);

class ResidentBuffer : public std::enable_shared_from_this<ResidentBuffer> {
public:
    static constexpr std::uint64_t BlockBytes = 65536;

    class WriteScope {
    public:
        WriteScope(std::shared_ptr<ResidentBuffer> buffer, const GuestRuns& runs);
        ~WriteScope();
        WriteScope(const WriteScope&) = delete;
        WriteScope& operator=(const WriteScope&) = delete;

    private:
        std::shared_ptr<ResidentBuffer> buffer;
    };

    static std::shared_ptr<ResidentBuffer> Create(const Context& context, std::uint64_t base, std::uint64_t bytes, const GuestRuns& backed);
    ~ResidentBuffer();
    ResidentBuffer(const ResidentBuffer&) = delete;
    ResidentBuffer& operator=(const ResidentBuffer&) = delete;

    VkBuffer Handle() const { return buffer; }
    VkDeviceAddress DeviceAddress() const { return address + lead; }
    std::uint64_t Origin() const { return origin; }
    std::uint64_t BoundBytes() const { return bound * BlockBytes; }
    std::uint32_t MemoryType() const { return type; }
    bool Bound(std::uint64_t guest) const;
    std::byte* Pending(std::uint64_t guest) const;
    std::uint64_t UnboundBytes(const GuestRuns& runs) const;
    void Bind(const GuestRuns& runs);
    void MarkChanged(std::uint64_t begin, std::uint64_t end);
    std::shared_ptr<Buffer> CaptureReference(std::uint64_t guest, std::uint64_t bytes) const;
    std::shared_ptr<Buffer> RecordReadback(VkCommandBuffer commands, std::uint64_t guest, std::uint64_t bytes) const;
    void ReadDevice(std::uint64_t guest, std::span<std::byte> out);
    static void FlushChanged(const Context& context);

private:
    struct Slot {
        void* slab = nullptr;
        std::uint32_t index = 0;
        std::unique_ptr<std::byte[]> pending;
    };
    ResidentBuffer(const Context& context, std::uint64_t base, std::uint64_t bytes, const GuestRuns& backed);
    std::size_t blockIndex(std::uint64_t guest) const;
    void beginWrites(const GuestRuns& runs);
    void endWrites() noexcept;
    void releasePending() noexcept;
    void submit(const std::vector<VkSparseMemoryBind>& binds);
    void flush();
    void upload(GuestRuns& runs);
    std::shared_ptr<Buffer> readbackBatch(std::uint64_t guest, std::uint64_t bytes);
    void release() noexcept;

    Context context;
    std::shared_ptr<ResidentMemory> memory;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    std::uint32_t type = 0;
    std::uint64_t origin = 0;
    std::uint64_t lead = 0;
    std::uint64_t size = 0;
    std::uint64_t bound = 0;
    std::vector<Slot> slots;
    mutable std::mutex pendingMutex;
    std::uint32_t writers = 0;
    std::vector<std::size_t> pendingSlots;
    std::mutex changedMutex;
    GuestRuns changed;
};

}

#endif
