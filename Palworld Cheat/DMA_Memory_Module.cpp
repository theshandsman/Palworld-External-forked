#include "DMA_Memory_Module.h"
#include <cstring>
#include <chrono>
#include <iostream>

// Global instance
DMAMemoryController g_DMAController;

bool DMAMemoryController::Connect(const char* devicePath) {
    if (isInitialized) return true;

    std::cout << "\n  [DMA] Attempting to initialize DMA controller...";
    
    // Try LeechCore first (most compatible with FPGA/external devices)
    if (InitializeLeechCore()) {
        activeBackend = DMABackend::LEECHCORE;
        isInitialized = true;
        std::cout << "\n  [DMA] LeechCore initialized successfully!";
        return true;
    }
    
    // Fallback to MemProcFS
    if (InitializeMemProcFS()) {
        activeBackend = DMABackend::MEMPROCFS;
        isInitialized = true;
        std::cout << "\n  [DMA] MemProcFS initialized successfully!";
        return true;
    }
    
    // Last resort: DMALibrary
    if (InitializeDMALibrary()) {
        activeBackend = DMABackend::DMALIBRARY;
        isInitialized = true;
        std::cout << "\n  [DMA] DMALibrary initialized successfully!";
        return true;
    }
    
    std::cout << "\n  [DMA] ERROR: No DMA backend available!";
    return false;
}

void DMAMemoryController::Disconnect() {
    if (dmaDeviceHandle) {
        CloseHandle(dmaDeviceHandle);
        dmaDeviceHandle = NULL;
    }
    
    ClearCache();
    isInitialized = false;
    activeBackend = DMABackend::NONE;
    std::cout << "\n  [DMA] Disconnected.";
}

bool DMAMemoryController::ReadDMA(uintptr_t address, void* buffer, size_t size) {
    if (!isInitialized || !buffer) return false;
    
    // Check cache for performance boost
    if (size >= CACHE_THRESHOLD) {
        auto cached = FindCached(address, size);
        if (cached.has_value()) {
            std::memcpy(buffer, cached->data.data(), size);
            return true;
        }
    }
    
    // Perform actual DMA read
    bool success = false;
    
    switch (activeBackend) {
        case DMABackend::LEECHCORE: {
            // LeechCore implementation
            // Use the device handle for direct memory access
            DWORD bytesRead = 0;
            success = ReadFile(dmaDeviceHandle, buffer, static_cast<DWORD>(size), &bytesRead, NULL)
                     && bytesRead == size;
            break;
        }
        case DMABackend::MEMPROCFS: {
            // MemProcFS implementation
            success = ReadFile(dmaDeviceHandle, buffer, static_cast<DWORD>(size), NULL, NULL) != FALSE;
            break;
        }
        case DMABackend::DMALIBRARY: {
            // DMALibrary implementation
            success = ReadFile(dmaDeviceHandle, buffer, static_cast<DWORD>(size), NULL, NULL) != FALSE;
            break;
        }
        default:
            return false;
    }
    
    // Cache successful reads for frequently accessed data
    if (success && size >= CACHE_THRESHOLD) {
        AddToCache(address, buffer, size);
    }
    
    return success;
}

bool DMAMemoryController::WriteDMA(uintptr_t address, const void* buffer, size_t size) {
    if (!isInitialized || !buffer) return false;
    
    // Invalidate relevant cache entries
    InvalidateCache(address, size);
    
    bool success = false;
    
    switch (activeBackend) {
        case DMABackend::LEECHCORE:
        case DMABackend::MEMPROCFS:
        case DMABackend::DMALIBRARY: {
            DWORD bytesWritten = 0;
            success = WriteFile(dmaDeviceHandle, buffer, static_cast<DWORD>(size), &bytesWritten, NULL)
                     && bytesWritten == size;
            break;
        }
        default:
            return false;
    }
    
    return success;
}

bool DMAMemoryController::ReadDMABatch(const std::vector<uintptr_t>& addresses,
                                       std::vector<std::vector<uint8_t>>& outputs,
                                       size_t elementSize) {
    if (!isInitialized || addresses.empty()) return false;
    
    outputs.resize(addresses.size());
    
    // Optimize by sorting addresses for better memory access patterns
    std::vector<std::pair<uintptr_t, size_t>> sortedAddrs;
    for (size_t i = 0; i < addresses.size(); ++i) {
        sortedAddrs.emplace_back(addresses[i], i);
    }
    std::sort(sortedAddrs.begin(), sortedAddrs.end());
    
    // Read in sorted order for better PCIe throughput
    for (auto& [addr, originalIdx] : sortedAddrs) {
        outputs[originalIdx].resize(elementSize);
        if (!ReadDMA(addr, outputs[originalIdx].data(), elementSize)) {
            return false;
        }
    }
    
    return true;
}

bool DMAMemoryController::WriteDMABatch(const std::vector<std::pair<uintptr_t, std::vector<uint8_t>>>& writes) {
    if (!isInitialized || writes.empty()) return false;
    
    // Invalidate cache for all written addresses
    for (const auto& [addr, data] : writes) {
        InvalidateCache(addr, data.size());
    }
    
    // Write in order, or batched if backend supports it
    for (const auto& [addr, data] : writes) {
        if (!WriteDMA(addr, data.data(), data.size())) {
            return false;
        }
    }
    
    return true;
}

uintptr_t DMAMemoryController::VirtualToPhysical(uintptr_t virtualAddress) {
    // Implementation depends on DMA backend
    // For now, assume 1:1 mapping (may need IOMMU translation on some systems)
    return virtualAddress;
}

bool DMAMemoryController::GetDeviceCapabilities(uint32_t& maxReadSize, uint32_t& maxWriteSize) {
    if (!isInitialized) return false;
    
    // Query device capabilities based on backend
    maxReadSize = 64 * 1024 * 1024;   // 64MB typical for PCIe
    maxWriteSize = 64 * 1024 * 1024;
    
    return true;
}

void DMAMemoryController::ClearCache() {
    readCache.clear();
}

void DMAMemoryController::InvalidateCache(uintptr_t address, size_t size) {
    auto it = readCache.begin();
    while (it != readCache.end()) {
        uintptr_t cacheAddr = it->first;
        size_t cacheSize = it->second.size;
        
        // Check for overlap
        if (!(address + size <= cacheAddr || cacheAddr + cacheSize <= address)) {
            it = readCache.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<DMAMemoryController::CacheEntry> DMAMemoryController::FindCached(
    uintptr_t address, size_t size) {
    
    auto it = readCache.find(address);
    if (it != readCache.end()) {
        const CacheEntry& entry = it->second;
        
        // Check if cache is still valid (within TTL)
        auto now = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() - entry.timestamp;
        
        if (elapsed < CACHE_TTL_MS && entry.size >= size) {
            return entry;
        }
    }
    
    return std::nullopt;
}

void DMAMemoryController::AddToCache(uintptr_t address, const void* data, size_t size) {
    if (readCache.size() >= MAX_CACHE_ENTRIES) {
        EvictOldestCache();
    }
    
    CacheEntry entry;
    entry.address = address;
    entry.size = size;
    entry.data.resize(size);
    entry.timestamp = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    
    std::memcpy(entry.data.data(), data, size);
    readCache[address] = entry;
}

void DMAMemoryController::EvictOldestCache() {
    if (readCache.empty()) return;
    
    auto oldest = readCache.begin();
    for (auto it = readCache.begin(); it != readCache.end(); ++it) {
        if (it->second.timestamp < oldest->second.timestamp) {
            oldest = it;
        }
    }
    
    readCache.erase(oldest);
}

const char* DMAMemoryController::GetBackendName() const {
    switch (activeBackend) {
        case DMABackend::LEECHCORE:
            return "LeechCore";
        case DMABackend::MEMPROCFS:
            return "MemProcFS";
        case DMABackend::DMALIBRARY:
            return "DMALibrary";
        case DMABackend::NONE:
        default:
            return "None";
    }
}

// Backend initialization stubs - implement based on your DMA library APIs
bool DMAMemoryController::InitializeLeechCore() {
    // TODO: Implement LeechCore-specific initialization
    // Example:
    // dmaDeviceHandle = CreateFile(L"\\.\LeechCore", ...)
    return false;
}

bool DMAMemoryController::InitializeMemProcFS() {
    // TODO: Implement MemProcFS-specific initialization
    return false;
}

bool DMAMemoryController::InitializeDMALibrary() {
    // TODO: Implement DMALibrary-specific initialization
    return false;
}
