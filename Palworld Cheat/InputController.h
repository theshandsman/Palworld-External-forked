#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <Windows.h>

/**
 * ===== UNIFIED INPUT CONTROLLER =====
 * Supports multiple input devices:
 * - Standard Windows mouse API (fallback)
 * - MAKCU high-performance mouse controllers (40μs latency)
 * - KMBOX communication protocol for microcontroller devices
 * 
 * Seamless switching between devices with identical API
 */

class InputController {
public:
    enum class DeviceType {
        WINDOWS_NATIVE,      // Standard Windows API
        MAKCU_DEVICE,        // MAKCU controller (40μs response time)
        KMBOX_DEVICE,        // KMBOX microcontroller device
        AUTO_DETECT          // Automatically find available device
    };

    enum class MouseButton {
        LEFT = 0x01,
        RIGHT = 0x02,
        MIDDLE = 0x04,
        SIDE1 = 0x08,
        SIDE2 = 0x10
    };

    struct DeviceInfo {
        DeviceType type;
        std::string port;
        std::string description;
        bool isConnected;
        uint32_t latency_us;  // Measured latency in microseconds
    };

    InputController();
    ~InputController();

    /**
     * Initialize input device
     * Auto-detects if type is AUTO_DETECT
     * Returns detected device type
     */
    DeviceType initialize(DeviceType type = DeviceType::AUTO_DETECT);

    /**
     * List available input devices
     */
    static std::vector<DeviceInfo> listAvailableDevices();

    /**
     * Mouse movement - optimized for gaming
     * MAKCU: ~40μs
     * KMBOX: ~50-100μs depending on baud rate
     * Windows: ~1-5ms
     */
    bool mouseMove(int32_t x, int32_t y);

    /**
     * Smooth mouse movement with bezier curves
     * Useful for avoiding detection in game clients
     */
    bool mouseMoveBezier(int32_t x, int32_t y, uint32_t segments = 20);

    /**
     * Mouse button control
     */
    bool mouseDown(MouseButton button);
    bool mouseUp(MouseButton button);
    bool mouseClick(MouseButton button, uint32_t delay_ms = 10);

    /**
     * Batch operations for maximum performance
     * Groups multiple commands into single transmission
     */
    class BatchBuilder {
    public:
        BatchBuilder& move(int32_t x, int32_t y);
        BatchBuilder& click(MouseButton button);
        BatchBuilder& press(MouseButton button);
        BatchBuilder& release(MouseButton button);
        BatchBuilder& delay(uint32_t ms);
        bool execute();

    private:
        friend class InputController;
        BatchBuilder(InputController* controller) : m_controller(controller) {}
        InputController* m_controller;
        std::vector<std::string> m_commands;
    };

    BatchBuilder createBatch();

    /**
     * Get current device info
     */
    DeviceInfo getDeviceInfo() const;

    /**
     * Check if device is connected
     */
    bool isConnected() const { return m_isConnected; }

    /**
     * Disconnect device
     */
    void disconnect();

    /**
     * Get measured device latency (microseconds)
     */
    uint32_t getMeasuredLatency() const { return m_measuredLatency; }

    /**
     * Get device backend name
     */
    const char* getBackendName() const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    DeviceType m_currentDevice = DeviceType::WINDOWS_NATIVE;
    bool m_isConnected = false;
    uint32_t m_measuredLatency = 0;

    // Device-specific initialization
    bool initMakcu();
    bool initKmbox();
    bool initWindowsNative();
    DeviceType autoDetect();
    void measureLatency();
};

/**
 * Global input controller instance
 * Thread-safe singleton
 */
extern InputController g_InputController;
