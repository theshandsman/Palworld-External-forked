#pragma once

#include <Windows.h>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include <algorithm>
#include <unordered_map>

/**
 * DMA/FPGA Memory Access Module
 * Replaces traditional ReadProcessMemory/WriteProcessMemory with Direct Memory Access via PCIe
 * Supports: MemProcFS, DMALibrary, LeechCore architectures
 * 
 * PERFORMANCE IMPROVEMENTS:
 * - Eliminates kernel context switches (ReadProcessMemory overhead)
 * - Direct PCIe access bypasses Windows API marshalling
 * - Batch operations reduce round-trip latency
 * - Smart caching for frequently accessed addresses
 * - ~10-100x faster than traditional process injection methods
 */

class DMAMemoryController {
private:
    HANDLE dmaDeviceHandle;
    bool isInitialized;
    
    // Optimized cache structure
    struct CacheEntry {
        uintptr_t address;
        uint32_t timestamp;
        std::vector<uint8_t> data;
        size_t size;
    };
    
    std::unordered_map<uintptr_t, CacheEntry> readCache;
    static constexpr size_t MAX_CACHE_ENTRIES = 256;
    static constexpr size_t CACHE_THRESHOLD = 4096;  // Cache reads >= 4KB
    static constexpr uint32_t CACHE_TTL_MS = 50;     // 50ms cache validity

public:
    DMAMemoryController() : dmaDeviceHandle(NULL), isInitialized(false) {}
    ~DMAMemoryController() { Disconnect(); }

    /**
     * Initialize DMA connection via PCIe device
     * Tries multiple DMA backends in order of preference
     */
    bool Connect(const char* devicePath = nullptr);
    
    /**
     * Disconnect and cleanup DMA resources
     */
    void Disconnect();

    /**
     * HIGH-PERFORMANCE memory read with intelligent caching
     * Automatically uses cache for repeated reads within TTL window
     */
    template<typename T>
    T ReadDMA(uintptr_t address) {
        T buffer = {};
        ReadDMA(address, &buffer, sizeof(T));
        return buffer;
    }

    /**
     * Read arbitrary memory via DMA with caching
     * Returns false on error, true on success
     */
    bool ReadDMA(uintptr_t address, void* buffer, size_t size);

    /**
     * BATCH READ OPTIMIZATION - read multiple addresses in single PCIe transaction
     * Dramatically faster than individual reads for unrelated addresses
     * Use for reading scattered game object data
     */
    bool ReadDMABatch(const std::vector<uintptr_t>& addresses, 
                      std::vector<std::vector<uint8_t>>& outputs, 
                      size_t elementSize);

    /**
     * HIGH-PERFORMANCE memory write via DMA
     */
    template<typename T>
    bool WriteDMA(uintptr_t address, const T& value) {
        return WriteDMA(address, &value, sizeof(T));
    }

    /**
     * Write arbitrary memory via DMA
     * Invalidates relevant cache entries
     */
    bool WriteDMA(uintptr_t address, const void* buffer, size_t size);

    /**
     * BATCH WRITE OPTIMIZATION - write multiple locations in single PCIe transaction
     * Perfect for updating multiple game state values atomically
     */
    bool WriteDMABatch(const std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>& writes);

    /**
     * Virtual to physical address translation via IOMMU
     * Required for certain FPGA/DMA configurations
     */
    uintptr_t VirtualToPhysical(uintptr_t virtualAddress);

    /**
     * Query DMA device capabilities
     * Returns max read/write block sizes supported
     */
    bool GetDeviceCapabilities(uint32_t& maxReadSize, uint32_t& maxWriteSize);

    /**
     * Cache management for memory coherency
     */
    void ClearCache();
    void InvalidateCache(uintptr_t address, size_t size);
    
    bool IsConnected() const { return isInitialized; }
    const char* GetBackendName() const;

private:
    // DMA backend implementations
    bool InitializeLeechCore();
    bool InitializeMemProcFS();
    bool InitializeDMALibrary();
    
    // Cache helpers
    std::optional<CacheEntry> FindCached(uintptr_t address, size_t size);
    void AddToCache(uintptr_t address, const void* data, size_t size);
    void EvictOldestCache();
    
    enum class DMABackend {
        LEECHCORE,
        MEMPROCFS,
        DMALIBRARY,
        NONE
    };
    DMABackend activeBackend = DMABackend::NONE;
};

// Global DMA controller instance
extern DMAMemoryController g_DMAController;

/**
 * ===== COMPATIBILITY LAYER =====
 * Drop-in replacements for Windows API memory functions
 * Minimal code changes required for migration from ReadProcessMemory/WriteProcessMemory
 */

// Optimized template read - replaces ReadProcessMemory<T>
template<typename T>
inline T read_dma(uintptr_t address) {
    return g_DMAController.ReadDMA<T>(address);
}

// Variable-size read overload
inline bool read_dma(uintptr_t address, void* buffer, size_t size) {
    return g_DMAController.ReadDMA(address, buffer, size);
}

// Optimized template write - replaces WriteProcessMemory<T>
template<typename T>
inline bool write_dma(uintptr_t address, const T& value) {
    return g_DMAController.WriteDMA(address, value);
}

// Variable-size write overload
inline bool write_dma(uintptr_t address, const void* buffer, size_t size) {
    return g_DMAController.WriteDMA(address, buffer, size);
}

/**
 * ===== BATCH OPERATIONS =====
 * Use these for maximum performance when reading/writing multiple unrelated addresses
 */

inline bool read_batch_dma(const std::vector<uintptr_t>& addresses,
                           std::vector<std::vector<uint8_t>>& outputs,
                           size_t elementSize) {
    return g_DMAController.ReadDMABatch(addresses, outputs, elementSize);
}

inline bool write_batch_dma(const std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>& writes) {
    return g_DMAController.WriteDMABatch(writes);
}

/**
 * ===== MIGRATION MACROS =====
 * Use to quickly convert existing code:
 * 
 * #define read(addr)        read_dma(addr)        // Old-style calls
 * #define write(addr, val)  write_dma(addr, val)
 * 
 * Or selectively replace critical performance paths:
 * - Main loop reads: use batch operations
 * - Pointer chains: use smart caching
 * - Synchronized writes: use batch writes
 */
