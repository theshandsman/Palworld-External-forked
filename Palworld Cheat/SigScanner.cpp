#include "SigScanner.h"
#include "Definitions.h"
#include <iostream>
#include <algorithm>
#include <cstring>

void SigScanner::registerPattern(const std::string& offsetName, const SignaturePattern& pattern) {
    patterns[offsetName] = pattern;
    std::cout << "\n  [SigScanner] Registered pattern: " << offsetName;
}

bool SigScanner::matchPattern(const uint8_t* data, const std::string& pattern, const std::string& mask) {
    for (size_t i = 0; i < pattern.length(); ++i) {
        if (mask[i] == 'x' && data[i] != static_cast<uint8_t>(pattern[i])) {
            return false;
        }
    }
    return true;
}

uintptr_t SigScanner::patternScan(const std::string& pattern, const std::string& mask) {
    if (!ProcessHandle || !ProcessBaseAddress) {
        std::cout << "\n  [SigScanner] ERROR: Process not initialized!";
        return 0;
    }

    // Allocate scan buffer
    const size_t SCAN_CHUNK = 0x100000;  // 1MB chunks
    auto buffer = std::make_unique<std::vector<uint8_t>>(SCAN_CHUNK);
    
    // Scan entire process memory
    for (uintptr_t addr = ProcessBaseAddress; addr < ProcessBaseAddress + (2u << 25u); addr += SCAN_CHUNK) {
        // Read chunk from process memory
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(ProcessHandle, reinterpret_cast<LPVOID>(addr), buffer->data(), SCAN_CHUNK, &bytesRead)) {
            continue;
        }

        if (bytesRead < pattern.length()) {
            continue;
        }

        // Search within chunk
        for (size_t i = 0; i < bytesRead - pattern.length(); ++i) {
            if (matchPattern(&buffer->at(i), pattern, mask)) {
                uintptr_t matchAddr = addr + i;
                
                // If there's a readOffset, perform the RIP-relative read
                if (pattern.find('\x48\x8B\x05') != std::string::npos) {  // mov rax, [rip+rel]
                    int32_t relOffset = 0;
                    ReadProcessMemory(ProcessHandle, 
                                    reinterpret_cast<LPVOID>(matchAddr + 3), 
                                    &relOffset, 
                                    sizeof(int32_t), 
                                    nullptr);
                    return matchAddr + relOffset + 7;
                }
                
                return matchAddr;
            }
        }
    }

    std::cout << "\n  [SigScanner] WARNING: Pattern not found!";
    return 0;
}

uintptr_t SigScanner::scan(const std::string& offsetName, bool useCache) {
    auto it = patterns.find(offsetName);
    if (it == patterns.end()) {
        std::cout << "\n  [SigScanner] ERROR: Pattern not registered: " << offsetName;
        return 0;
    }

    const SignaturePattern& sig = it->second;

    // Check cache first
    if (useCache) {
        auto cacheIt = offsetCache.find(offsetName);
        if (cacheIt != offsetCache.end()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - cacheIt->second.lastUpdate);
            
            if (elapsed < sig.expiryTime && cacheIt->second.isValid) {
                return cacheIt->second.value;
            }
        }
    }

    // Perform actual scan
    uintptr_t result = patternScan(sig.pattern, sig.mask);
    
    // Update cache
    CachedOffset cached;
    cached.value = result;
    cached.lastUpdate = std::chrono::high_resolution_clock::now();
    cached.isValid = (result != 0);
    
    offsetCache[offsetName] = cached;

    if (result == 0) {
        std::cout << "\n  [SigScanner] FAILED to find: " << offsetName;
    } else {
        std::cout << "\n  [SigScanner] Found " << offsetName << " at: 0x" << std::hex << result << std::dec;
    }

    return result;
}

std::map<std::string, uintptr_t> SigScanner::scanBatch(const std::vector<std::string>& offsetNames) {
    std::map<std::string, uintptr_t> results;
    
    for (const auto& name : offsetNames) {
        results[name] = scan(name, true);
    }
    
    return results;
}

bool SigScanner::validateOffset(const std::string& offsetName) {
    auto it = patterns.find(offsetName);
    if (it == patterns.end()) {
        return false;
    }

    auto cacheIt = offsetCache.find(offsetName);
    uintptr_t oldValue = (cacheIt != offsetCache.end()) ? cacheIt->second.value : 0;

    // Force re-scan
    uintptr_t newValue = patternScan(it->second.pattern, it->second.mask);

    if (oldValue != newValue && oldValue != 0) {
        std::cout << "\n  [SigScanner] OFFSET DRIFT DETECTED for " << offsetName 
                  << "\n              Old: 0x" << std::hex << oldValue 
                  << " -> New: 0x" << newValue << std::dec;
        
        // Update cache with new value
        CachedOffset cached;
        cached.value = newValue;
        cached.lastUpdate = std::chrono::high_resolution_clock::now();
        cached.isValid = (newValue != 0);
        offsetCache[offsetName] = cached;
        
        return false;  // Offset has changed
    }

    return true;  // Offset is still valid
}

std::vector<std::string> SigScanner::detectDrift(const std::vector<std::string>& offsetNames) {
    std::vector<std::string> drifted;
    
    for (const auto& name : offsetNames) {
        if (!validateOffset(name)) {
            drifted.push_back(name);
        }
    }
    
    return drifted;
}

void SigScanner::updateCache(const std::string& offsetName, uintptr_t value) {
    CachedOffset cached;
    cached.value = value;
    cached.lastUpdate = std::chrono::high_resolution_clock::now();
    cached.isValid = (value != 0);
    
    offsetCache[offsetName] = cached;
    std::cout << "\n  [SigScanner] Updated cache for " << offsetName << " to 0x" << std::hex << value << std::dec;
}

void SigScanner::invalidateCache(const std::string& offsetName) {
    auto it = offsetCache.find(offsetName);
    if (it != offsetCache.end()) {
        it->second.isValid = false;
        std::cout << "\n  [SigScanner] Invalidated cache for " << offsetName;
    }
}

void SigScanner::clearCache() {
    offsetCache.clear();
    std::cout << "\n  [SigScanner] Cleared all cached offsets";
}

std::chrono::high_resolution_clock::time_point SigScanner::getLastUpdateTime(const std::string& offsetName) {
    auto it = offsetCache.find(offsetName);
    if (it != offsetCache.end()) {
        return it->second.lastUpdate;
    }
    return std::chrono::high_resolution_clock::now();
}

void SigScanner::enableAutoValidation(bool enabled) {
    autoValidationEnabled = enabled;
    lastValidationTime = std::chrono::high_resolution_clock::now();
    std::cout << "\n  [SigScanner] Auto-validation " << (enabled ? "ENABLED" : "DISABLED");
}

void SigScanner::setValidationFrequency(uint32_t msInterval) {
    validationFrequency = msInterval;
    std::cout << "\n  [SigScanner] Validation frequency set to " << msInterval << "ms";
}

void SigScanner::autoValidationTick() {
    if (!autoValidationEnabled) {
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastValidationTime);

    if (elapsed.count() < validationFrequency) {
        return;  // Not time to validate yet
    }

    lastValidationTime = now;

    // Check all cached offsets
    std::vector<std::string> offsetsToCheck;
    for (const auto& [name, cached] : offsetCache) {
        if (cached.isValid) {
            offsetsToCheck.push_back(name);
        }
    }

    auto drifted = detectDrift(offsetsToCheck);
    if (!drifted.empty()) {
        std::cout << "\n  [SigScanner] *** IMPORTANT: " << drifted.size() << " offset(s) have changed! ***";
        std::cout << "\n  [SigScanner] Update the Offsets::SetOffsets() or patterns immediately!";
    }
}
