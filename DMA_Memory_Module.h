#pragma once

#include <Windows.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>

/**
 * DMA/FPGA Memory Access Module
 * Replaces traditional ReadProcessMemory/WriteProcessMemory with Direct Memory Access via PCIe
 * Supports: MemProcFS, DMALibrary, LeechCore architectures
 */

class DMAMemoryController {
private:
    // Handle to PCIe DMA device
    HANDLE dmaDeviceHandle;
    bool isInitialized;
    
    // Memory read cache for optimization
    struct CacheEntry {
        uintptr_t address;
        std::vector<uint8_t> data;
        size_t size;
    };
    
    std::vector<CacheEntry> readCache;
    static constexpr size_t MAX_CACHE_SIZE = 50;
    static constexpr size_t CACHE_THRESHOLD = 1024 * 1024; // 1MB

public:
    DMAMemoryController() : dmaDeviceHandle(NULL), isInitialized(false) {}
    ~DMAMemoryController() { Disconnect(); }

    /**
     * Initialize DMA connection via PCIe device
     * Supports multiple DMA device types
     */
    bool Connect(const char* devicePath = R"(\\.\LeechCore)");
    
    /**
     * Disconnect and cleanup DMA resources
     */
    void Disconnect();

    /**
     * High-performance memory read with caching
     * Uses DMA for direct memory access instead of ReadProcessMemory
     */
    template<typename T>
    T ReadDMA(uintptr_t address) {
        T buffer = {};
        ReadDMA(address, &buffer, sizeof(T));
        return buffer;
    }

    /**
     * Read arbitrary memory via DMA
     */
    bool ReadDMA(uintptr_t address, void* buffer, size_t size);

    /**
     * Batch read optimization - read multiple addresses in single PCIe operation
     */
    bool ReadDMABatch(const std::vector<uintptr_t>& addresses, 
                      std::vector<std::vector<uint8_t>>& outputs, 
                      size_t size);

    /**
     * High-performance memory write via DMA
     */
    template<typename T>
    bool WriteDMA(uintptr_t address, const T& value) {
        return WriteDMA(address, &value, sizeof(T));
    }

    /**
     * Write arbitrary memory via DMA
     */
    bool WriteDMA(uintptr_t address, const void* buffer, size_t size);

    /**
     * Batch write optimization
     */
    bool WriteDMABatch(const std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>& writes);

    /**
     * Virtual to physical address translation
     */
    uintptr_t VirtualToPhysical(uintptr_t virtualAddress);

    /**
     * Get DMA device capabilities
     */
    bool GetDeviceCapabilities(uint32_t& maxReadSize, uint32_t& maxWriteSize);

    /**
     * Cache management
     */
    void ClearCache();
    void InvalidateCache(uintptr_t address, size_t size);
    
    bool IsConnected() const { return isInitialized; }

private:
    bool InitializeLeechCore();
    bool InitializeMemProcFS();
    bool InitializeDMALibrary();
    
    std::optional<CacheEntry> FindCached(uintptr_t address, size_t size);
    void AddToCache(uintptr_t address, const void* data, size_t size);
};

// Global DMA controller instance
extern DMAMemoryController g_DMAController;

/**
 * Compatibility wrappers to replace standard Windows API calls
 * These maintain the original function signatures for minimal code changes
 */

// Optimized memory read - replaces ReadProcessMemory when using DMA
template<typename T>
inline T read_dma(uintptr_t address) {
    return g_DMAController.ReadDMA<T>(address);
}

// Overload for variable-size reads - replaces ReadProcessMemory
inline bool read_dma(uintptr_t address, void* buffer, size_t size) {
    return g_DMAController.ReadDMA(address, buffer, size);
}

// Optimized memory write - replaces WriteProcessMemory when using DMA
template<typename T>
inline bool write_dma(uintptr_t address, const T& value) {
    return g_DMAController.WriteDMA(address, value);
}

// Overload for variable-size writes
inline bool write_dma(uintptr_t address, const void* buffer, size_t size) {
    return g_DMAController.WriteDMA(address, buffer, size);
}

// Batch operations for maximum performance
inline bool read_batch_dma(const std::vector<uintptr_t>& addresses,
                           std::vector<std::vector<uint8_t>>& outputs,
                           size_t size) {
    return g_DMAController.ReadDMABatch(addresses, outputs, size);
}

inline bool write_batch_dma(const std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>& writes) {
    return g_DMAController.WriteDMABatch(writes);
}
