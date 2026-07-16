#pragma once

#include <Windows.h>
#include <cstdint>
#include <string>
#include <map>
#include "SigScanner.h"

/**
 * ===== OFFSETS SCHEMA WITH AUTO-UPDATE =====
 * 
 * Version-aware offset definitions with runtime validation.
 * Automatically detects and corrects offset changes.
 */

namespace OffsetsSchema {

    // Version identifiers
    enum class PalworldVersion {
        VERSION_0_1_2_0,
        VERSION_0_1_3_0,
        VERSION_0_1_3_0_ALT,
        VERSION_0_1_4_0,
        VERSION_0_1_4_1,
        VERSION_0_1_4_1_ALT,
        VERSION_0_1_5_0,
        VERSION_0_1_5_1,
        VERSION_0_2_4_0,
        VERSION_UNKNOWN,
        VERSION_AUTO_DETECT  // Runtime auto-detection
    };

    struct OffsetEntry {
        std::string name;
        uintptr_t value;
        SignaturePattern signature;
        bool isStatic;  // true = offset, false = requires signature scanning
    };

    struct VersionProfile {
        PalworldVersion version;
        std::string versionString;
        std::map<std::string, OffsetEntry> offsets;
    };

    class SchemaManager {
    public:
        static SchemaManager& getInstance() {
            static SchemaManager instance;
            return instance;
        }

        /**
         * Initialize schema for a specific version
         */
        bool initializeVersion(PalworldVersion version);

        /**
         * Auto-detect game version from running process
         */
        PalworldVersion autoDetectVersion();

        /**
         * Get offset value (static or scanned)
         */
        uintptr_t getOffset(const std::string& offsetName);

        /**
         * Validate all offsets for current version
         */
        bool validateAllOffsets();

        /**
         * Check for offset drift since last validation
         */
        std::vector<std::string> checkDrift();

        /**
         * Apply version-specific corrections
         */
        void applyVersionCorrections(PalworldVersion version);

        /**
         * Get current version
         */
        PalworldVersion getCurrentVersion() const { return currentVersion; }

        /**
         * Register custom offset pattern
         */
        void registerPattern(const std::string& offsetName, const SignaturePattern& sig);

    private:
        SchemaManager();

        void initializeProfiles();
        void setupPatterns();

        std::map<PalworldVersion, VersionProfile> profiles;
        PalworldVersion currentVersion = PalworldVersion::VERSION_UNKNOWN;
    };

    // Convenience functions
    inline uintptr_t GetOffset(const std::string& name) {
        return SchemaManager::getInstance().getOffset(name);
    }

    inline bool ValidateOffsets() {
        return SchemaManager::getInstance().validateAllOffsets();
    }

    inline std::vector<std::string> CheckDrift() {
        return SchemaManager::getInstance().checkDrift();
    }
}

// Global offset namespace that auto-updates
namespace Offsets {
    extern uintptr_t UWorld;
    extern uintptr_t PersistentLevel;
    extern uintptr_t WorldSettings;
    extern uintptr_t OwningGameInstance;
    extern uintptr_t LocalPlayers;
    extern uintptr_t GameSetting;
    extern uintptr_t PlayerController;
    extern uintptr_t ViewportClient;
    extern uintptr_t PlayerState;
    extern uintptr_t InventoryData;
    extern uintptr_t AcknowledgedPawn;
    extern uintptr_t RootComponent;
    extern uintptr_t Mesh;
    extern uintptr_t CharacterMovement;
    extern uintptr_t CharacterParameterComponent;
    extern uintptr_t OtomoPal;
    extern uintptr_t IndividualParameter;
    extern uintptr_t FollowCamera;
    extern uintptr_t MyHUD;
    extern uintptr_t PlayerCameraManager;

    extern uintptr_t ComponentLocation;
    extern uintptr_t ControlRotation;
    extern uintptr_t MovementMode;
    extern uintptr_t CustomTimeDilation;
    extern uintptr_t bIsEnableMuteki;
    extern uintptr_t SaveParameter;
    extern uintptr_t HungerType;
    extern uintptr_t SP;
    extern uintptr_t bOnlyRelevantToOwner;
    extern uintptr_t TimeDilation;
    extern uintptr_t DefaultFOV;
    extern uintptr_t bLostFocusPaused;
    extern uintptr_t AspectRatio;
    extern uintptr_t bConstrainAspectRatio;
    extern uintptr_t RelativeRotation;
    extern uintptr_t bComponentToWorldUpdated;
    extern uintptr_t RelativeScale3D;
    extern uintptr_t CraftSpeed;
    extern uintptr_t Level;
    extern uintptr_t MaxInventoryWeight;
    extern uintptr_t WorldmapUIMaskClearSize;

    /**
     * Set offsets for specific version with auto-validation
     */
    void SetOffsets(const std::string& version);

    /**
     * Refresh offsets from schema (handles auto-detection)
     */
    void RefreshOffsets();

    /**
     * Validate offsets and print results
     */
    bool ValidateAndPrint();
}
