#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <chrono>

/**
 * ===== ADAPTIVE SIGNATURE SCANNER =====
 * 
 * Real-time pattern validation and auto-correction system.
 * Detects when offsets have changed and automatically updates them.
 * 
 * Features:
 * - Signature-based offset discovery
 * - Runtime validation with fallback scanning
 * - Cache management with expiration
 * - Batch offset verification
 * - Pattern drift detection
 */

struct SignaturePattern {
    std::string name;
    std::string pattern;
    std::string mask;
    size_t readOffset = 0;  // Bytes to skip after match
    std::chrono::milliseconds expiryTime = std::chrono::milliseconds(5000);  // 5 second cache
};

struct CachedOffset {
    uintptr_t value;
    std::chrono::high_resolution_clock::time_point lastUpdate;
    bool isValid;
};

class SigScanner {
public:
    static SigScanner& getInstance() {
        static SigScanner instance;
        return instance;
    }

    /**
     * Register a signature pattern for a named offset
     */
    void registerPattern(const std::string& offsetName, const SignaturePattern& pattern);

    /**
     * Scan for a pattern with optional cached result
     */
    uintptr_t scan(const std::string& offsetName, bool useCache = true);

    /**
     * Batch scan multiple patterns (more efficient than individual scans)
     */
    std::map<std::string, uintptr_t> scanBatch(const std::vector<std::string>& offsetNames);

    /**
     * Validate if a cached offset is still correct
     * by re-scanning and comparing
     */
    bool validateOffset(const std::string& offsetName);

    /**
     * Check if any offsets have drifted (changed)
     * Returns vector of names that changed
     */
    std::vector<std::string> detectDrift(const std::vector<std::string>& offsetNames);

    /**
     * Update a cached offset manually
     */
    void updateCache(const std::string& offsetName, uintptr_t value);

    /**
     * Clear cache for specific offset (forces re-scan on next access)
     */
    void invalidateCache(const std::string& offsetName);

    /**
     * Clear all cached offsets
     */
    void clearCache();

    /**
     * Get last scan time for an offset
     */
    std::chrono::high_resolution_clock::time_point getLastUpdateTime(const std::string& offsetName);

    /**
     * Enable/disable auto-validation (periodically checks if offsets are still valid)
     */
    void enableAutoValidation(bool enabled);

    /**
     * Set validation frequency in milliseconds
     */
    void setValidationFrequency(uint32_t msInterval);

    /**
     * Perform auto-validation tick (call from main loop)
     */
    void autoValidationTick();

private:
    SigScanner() = default;
    SigScanner(const SigScanner&) = delete;
    SigScanner& operator=(const SigScanner&) = delete;

    // Low-level pattern matching
    uintptr_t patternScan(const std::string& pattern, const std::string& mask);
    
    // Reads memory chunk and searches for pattern
    bool matchPattern(const uint8_t* data, const std::string& pattern, const std::string& mask);

    // Cache management
    std::map<std::string, SignaturePattern> patterns;
    std::map<std::string, CachedOffset> offsetCache;

    // Auto-validation state
    bool autoValidationEnabled = false;
    uint32_t validationFrequency = 10000;  // 10 seconds
    std::chrono::high_resolution_clock::time_point lastValidationTime;
};

// Global convenience functions
inline uintptr_t getSigOffset(const std::string& name) {
    return SigScanner::getInstance().scan(name);
}

inline bool validateSigOffset(const std::string& name) {
    return SigScanner::getInstance().validateOffset(name);
}

inline std::vector<std::string> checkOffsetDrift(const std::vector<std::string>& names) {
    return SigScanner::getInstance().detectDrift(names);
}
