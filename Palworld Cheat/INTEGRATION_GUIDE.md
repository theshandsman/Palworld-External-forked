# Palworld External Cheat - Integration & Offset System Guide

## Overview

This guide covers the **three critical systems** that have been integrated into the Palworld External cheat to fix process detection, DMA integration, and implement runtime offset validation.

---

## System 1: Fixed Process Detection (Utils.h)

### Problem
The original `GetProcessInfo()` function had a critical bug: it attempted to create a module snapshot BEFORE successfully finding the process ID, resulting in `ProcessID = NULL`.

### Solution
Rewritten with proper sequencing and validation:

```cpp
bool GetProcessInfo(const wchar_t* FileName) {
    // Step 1: Find process by name
    HANDLE PSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
    PROCESSENTRY32 Process = {};
    Process.dwSize = sizeof(Process);
    bool processFound = false;

    while (Process32Next(PSnapshot, &Process)) {
        if (!_wcsicmp(FileName, Process.szExeFile)) {
            ProcessID = Process.th32ProcessID;
            ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
            processFound = true;
            break;
        }
    }
    CloseHandle(PSnapshot);
    
    if (!processFound) return false;  // Exit if process not found

    // Step 2: NOW get module info with valid ProcessID
    HANDLE MSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, ProcessID);
    MODULEENTRY32 Module = {};
    Module.dwSize = sizeof(Module);
    bool moduleFound = false;

    while (Module32Next(MSnapshot, &Module)) {
        if (!_wcsicmp(FileName, Module.szModule)) {
            ProcessBaseAddress = (uintptr_t)Module.modBaseAddr;
            moduleFound = true;
            break;
        }
    }
    
    CloseHandle(MSnapshot);
    return moduleFound;
}
```

### Key Changes
- **Proper Sequencing**: Find process → Get ProcessID → Get module info
- **Error Handling**: Return false at each critical step
- **Handle Cleanup**: CloseHandle called after snapshots
- **Validation**: Each step validates before proceeding

---

## System 2: DMA Memory Module (DMA_Memory_Module.h/cpp)

### Problem
All DMA backend initialization stubs returned `false`, causing the system to fail silently with no fallback.

### Solution
Implemented fallback to `ReadProcessMemory` when DMA unavailable:

```cpp
bool DMAMemoryController::Connect(const char* devicePath) {
    if (isInitialized) return true;

    std::cout << "\n  [DMA] Attempting to initialize DMA controller...";
    
    // Try LeechCore first
    if (InitializeLeechCore()) {
        activeBackend = DMABackend::LEECHCORE;
        isInitialized = true;
        std::cout << "\n  [DMA] LeechCore initialized!";
        return true;
    }
    
    // Fallback to MemProcFS
    if (InitializeMemProcFS()) {
        activeBackend = DMABackend::MEMPROCFS;
        isInitialized = true;
        std::cout << "\n  [DMA] MemProcFS initialized!";
        return true;
    }
    
    // Last resort: Standard Windows API
    std::cout << "\n  [DMA] WARNING: No hardware DMA available. Using ReadProcessMemory fallback.";
    activeBackend = DMABackend::WINDOWS_API;
    isInitialized = true;
    return true;  // Still succeed, just use Windows API
}

// Fallback implementation for standard memory reads
bool DMAMemoryController::ReadDMA(uintptr_t address, void* buffer, size_t size) {
    if (!isInitialized || !buffer) return false;
    
    switch (activeBackend) {
        case DMABackend::WINDOWS_API: {
            SIZE_T bytesRead = 0;
            return ReadProcessMemory(ProcessHandle, 
                                    reinterpret_cast<LPVOID>(address), 
                                    buffer, 
                                    size, 
                                    &bytesRead) && bytesRead == size;
        }
        // ... other backends ...
    }
    return false;
}
```

### Integration Steps

1. **Include in main_integrated.cpp**:
```cpp
#include "DMA_Memory_Module.h"
```

2. **Initialize in main()** (before loop):
```cpp
if (!g_DMAController.Connect()) {
    std::cout << "\n  [WARNING] DMA init failed, using standard memory API";
}
```

3. **Use in game loop**:
```cpp
// Automatically uses best available method (DMA or Windows API)
uintptr_t UWorld = read_dma<uintptr_t>(Offsets::UWorld);
```

---

## System 3: Adaptive Offset System (SigScanner + OffsetsSchema)

### Architecture

The system consists of three layers:

```
┌─────────────────────────────────────────┐
│   Offsets::GetOffset() (user API)       │
└────────────┬────────────────────────────┘
             │
┌────────────▼────────────────────────────┐
│   SigScanner (pattern validation)       │
│   - Runtime pattern scanning            │
│   - Cache with TTL (time-to-live)       │
│   - Drift detection & auto-correction   │
└────────────┬────────────────────────────┘
             │
┌────────────▼────────────────────────────┐
│   OffsetsSchema (version management)    │
│   - Version profiles                    │
│   - Offset validation                   │
│   - Auto-detection                      │
└─────────────────────────────────────────┘
```

### File Structure

**SigScanner.h** - Signature scanning interface
**SigScanner.cpp** - Pattern matching & caching implementation
**OffsetsSchema.h** - Version-aware offset definitions
**OffsetsSchema.cpp** - Schema initialization & validation

### Key Features

#### 1. **Signature Patterns with TTL**
```cpp
struct SignaturePattern {
    std::string name;
    std::string pattern;      // IDA-style: "\x48\x8B\x05\x00\x00\x00\x00"
    std::string mask;         // Match mask: "xxx????xx"
    size_t readOffset = 0;    // Bytes to skip after match
    std::chrono::milliseconds expiryTime = 5000ms;  // Cache validity
};
```

#### 2. **Caching with Expiration**
```cpp
uintptr_t SigScanner::scan(const std::string& offsetName, bool useCache = true) {
    // Check if cached & still valid
    if (useCache && cached.isValid && !expired) {
        return cached.value;
    }
    
    // Perform re-scan
    uintptr_t result = patternScan(signature.pattern, signature.mask);
    
    // Update cache with timestamp
    offsetCache[offsetName] = {result, now, true};
    return result;
}
```

#### 3. **Automatic Drift Detection**
```cpp
bool SigScanner::validateOffset(const std::string& offsetName) {
    uintptr_t oldValue = offsetCache[offsetName].value;
    uintptr_t newValue = patternScan(pattern, mask);  // Force re-scan
    
    if (oldValue != newValue && oldValue != 0) {
        std::cout << "\n  [ALERT] OFFSET CHANGED: " << offsetName 
                  << " 0x" << std::hex << oldValue << " -> 0x" << newValue;
        
        // Auto-update
        offsetCache[offsetName].value = newValue;
        return false;  // Signal that offset changed
    }
    return true;
}
```

#### 4. **Auto-Validation Loop**
```cpp
// Call once per frame in game loop
void SigScanner::autoValidationTick() {
    if (!autoValidationEnabled) return;
    
    if (elapsed < validationFrequency) return;  // Only check every N ms
    
    // Check all active offsets
    auto drifted = detectDrift(offsetList);
    if (!drifted.empty()) {
        std::cout << "\n  *** " << drifted.size() << " OFFSETS CHANGED ***";
        // Auto-correct cached values
    }
}
```

---

## Integration Checklist

### Phase 1: Fix Process Detection
- [x] Rewrite `GetProcessInfo()` in Utils.h
- [x] Add error handling at each step
- [x] Validate return values in main()

### Phase 2: Implement DMA with Fallback
- [x] Create `DMA_Memory_Module.h/cpp`
- [x] Add Windows API fallback
- [x] Implement `read_dma<T>()` and `write_dma<T>()` wrappers
- [x] Use in main_integrated.cpp

### Phase 3: Offset Validation System
- [x] Create `SigScanner.h/cpp` for pattern matching
- [x] Create `OffsetsSchema.h/cpp` for version management
- [x] Register patterns for all offsets
- [x] Enable auto-validation in main loop

### Phase 4: Update Main Loop
```cpp
// In LoopIntegrated() or Loop():

// Initialize auto-validation
SigScanner::getInstance().enableAutoValidation(true);
SigScanner::getInstance().setValidationFrequency(10000);  // Every 10 seconds

while (true) {
    // ... existing code ...
    
    // Periodic offset validation (call once per frame)
    SigScanner::getInstance().autoValidationTick();
    
    // ... rest of loop ...
}
```

---

## Pattern Registration Example

Register offset patterns at startup (in `main()` after `GetProcessInfo()`):

```cpp
// Register UWorld pattern
SignaturePattern uworldPattern;
uworldPattern.name = "UWorld";
uworldPattern.pattern = "\x48\x8B\x05\x00\x00\x00\x00\xEB\x05";
uworldPattern.mask = "xxx????xx";
uworldPattern.readOffset = 3;  // Read from RIP+3
uworldPattern.expiryTime = std::chrono::milliseconds(5000);  // 5 sec cache

SigScanner::getInstance().registerPattern("UWorld", uworldPattern);

// Similar for other critical offsets
// ... more patterns ...
```

---

## Offset Validation Report

Call this after setting offsets to verify all are valid:

```cpp
if (!Offsets::ValidateAndPrint()) {
    std::cout << "\n  [ERROR] Some offsets failed validation!";
    std::cout << "\n  Please check the offset values for your game version.";
    return;
}

// Check for drift
auto drifted = Offsets::CheckDrift();
if (!drifted.empty()) {
    std::cout << "\n  [WARNING] " << drifted.size() << " offsets have changed!";
    for (const auto& name : drifted) {
        std::cout << "\n    - " << name;
    }
}
```

---

## Version Auto-Detection

The system can auto-detect the game version from running process:

```cpp
// In main() after GetProcessInfo():
OffsetsSchema::PalworldVersion version = 
    OffsetsSchema::SchemaManager::getInstance().autoDetectVersion();

if (version == PalworldVersion::VERSION_UNKNOWN) {
    std::cout << "\n  [WARNING] Could not auto-detect version, using manual selection...";
    // Fall back to manual selection
} else {
    std::cout << "\n  [SUCCESS] Detected version: " << versionString;
    Offsets::SetOffsets(versionString);
}
```

---

## Troubleshooting

### Issue: "Process not found" error
**Cause**: `GetProcessInfo()` failing to find process window or module
**Solution**:
1. Verify Palworld is running: `tasklist | findstr Palworld`
2. Check window name: Should be "UnrealWindow" with title "Pal  "
3. Run as admin for module enumeration
4. Add debug output in `GetProcessInfo()` to see where it fails

### Issue: "No DMA backend available"
**Expected**: Falls back to `ReadProcessMemory` (slower but works)
**Solution**: This is normal if you don't have DMA hardware. System will still function.

### Issue: Offsets changing at runtime
**Cause**: Game version mismatch or memory ASLR
**Solution**:
1. Verify you selected correct game version
2. Enable auto-validation to detect changes
3. System will auto-update cached offsets when drift detected
4. Check console output for "OFFSET CHANGED" warnings

### Issue: Signature scan failing
**Cause**: Pattern doesn't match current game version
**Solution**:
1. Verify pattern string is correct (use IDA to extract)
2. Check mask string matches pattern length
3. Enable debug output: See scan results in console
4. Re-scan pattern for current version

---

## Performance Tips

1. **Cache Frequently Used Offsets**
   - Set `expiryTime` to 5000ms+ for stable offsets
   - Reduces re-scan overhead

2. **Batch Offset Scanning**
   ```cpp
   auto results = SigScanner::getInstance().scanBatch({
       "UWorld", "PlayerController", "AcknowledgedPawn"
   });
   ```

3. **Auto-Validation Frequency**
   - Default: 10000ms (10 seconds)
   - Adjust based on stability needs
   - Less frequent = better performance

4. **Use DMA When Available**
   - DMA: 5-50μs per read
   - Windows API: 100-500μs per read
   - ~10x speedup with hardware DMA

---

## Next Steps

1. **Compile & Test**
   - Include new files in Visual Studio project
   - Compile with `/std:c++17` or later
   - Run and check console output

2. **Verify Offsets**
   - Use `Offsets::ValidateAndPrint()` to confirm all offsets load
   - Check for drift detection warnings

3. **Enable Auto-Validation**
   - Call `autoValidationTick()` in main loop
   - Monitor console for offset changes

4. **Monitor Performance**
   - Check memory read times in ImGui overlay
   - Verify DMA is being used if available

---

## Files Changed/Added

```
Palworld Cheat/
├── Utils.h                    [MODIFIED] - Fixed GetProcessInfo()
├── DMA_Memory_Module.h        [ADDED]    - DMA interface with fallback
├── DMA_Memory_Module.cpp      [ADDED]    - DMA implementation
├── SigScanner.h               [ADDED]    - Signature scanning interface
├── SigScanner.cpp             [ADDED]    - Pattern matching & caching
├── OffsetsSchema.h            [ADDED]    - Version-aware offset schema
├── OffsetsSchema.cpp          [ADDED]    - Schema implementation
└── main_integrated.cpp        [MODIFIED] - Integrated DMA + signature system
```

---

## Related Documentation

- **DMA Theory**: See DMA_Memory_Module.h comments
- **Signature Format**: Standard IDA pattern format (hex + wildcards)
- **Offset Validation**: See SigScanner validation methods
- **Performance**: Check DMA vs Windows API comparison in header comments
