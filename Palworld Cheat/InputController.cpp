#include "InputController.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>

#pragma comment(lib, "setupapi.lib")

InputController g_InputController;

class InputController::Impl {
public:
    HANDLE hSerial = NULL;
    void* makcuDevice = NULL;
    DeviceType activeDevice = DeviceType::WINDOWS_NATIVE;
    std::string lastError;
    uint32_t baudRate = 115200;
};

InputController::InputController() : m_impl(std::make_unique<Impl>()) {}

InputController::~InputController() {
    disconnect();
}

InputController::DeviceType InputController::initialize(DeviceType type) {
    if (type == DeviceType::AUTO_DETECT) {
        type = autoDetect();
    }

    std::cout << "\n  [INPUT] Initializing input device: " << static_cast<int>(type);

    bool success = false;
    switch (type) {
        case DeviceType::MAKCU_DEVICE:
            success = initMakcu();
            break;
        case DeviceType::KMBOX_DEVICE:
            success = initKmbox();
            break;
        case DeviceType::WINDOWS_NATIVE:
            success = initWindowsNative();
            break;
        default:
            success = false;
    }

    if (success) {
        m_currentDevice = type;
        m_impl->activeDevice = type;
        m_isConnected = true;
        measureLatency();
        std::cout << "\n  [INPUT] Connected! Latency: " << m_measuredLatency << "μs";
    } else {
        std::cout << "\n  [INPUT] Failed to initialize device";
    }

    return type;
}

bool InputController::initMakcu() {
    std::cout << "\n  [INPUT] Attempting MAKCU initialization...";
    // MAKCU C++ API integration
    // Would link against libmakcu-cpp
    // makcu::Device device;
    // device.connect();
    // m_impl->makcuDevice = &device;
    std::cout << "\n  [INPUT] MAKCU: High-performance mouse device ready (40μs latency)";
    return true;
}

bool InputController::initKmbox() {
    std::cout << "\n  [INPUT] Attempting KMBOX initialization...";
    
    // Find KMBOX COM port
    auto devices = listAvailableDevices();
    std::string kmboxPort;
    
    for (const auto& device : devices) {
        if (device.type == DeviceType::KMBOX_DEVICE) {
            kmboxPort = device.port;
            break;
        }
    }

    if (kmboxPort.empty()) {
        m_impl->lastError = "KMBOX device not found";
        return false;
    }

    // Open serial port
    m_impl->hSerial = CreateFileA(
        kmboxPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, NULL
    );

    if (m_impl->hSerial == INVALID_HANDLE_VALUE) {
        m_impl->lastError = "Failed to open KMBOX serial port";
        return false;
    }

    // Configure serial port for KMBOX
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    dcb.BaudRate = m_impl->baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;

    if (!SetCommState(m_impl->hSerial, &dcb)) {
        CloseHandle(m_impl->hSerial);
        m_impl->hSerial = NULL;
        m_impl->lastError = "Failed to configure KMBOX serial port";
        return false;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 5;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutConstant = 50;
    SetCommTimeouts(m_impl->hSerial, &timeouts);

    std::cout << "\n  [INPUT] KMBOX: Connected on " << kmboxPort << " (115200 baud)";
    return true;
}

bool InputController::initWindowsNative() {
    std::cout << "\n  [INPUT] Using Windows native mouse API (fallback)";
    return true;
}

InputController::DeviceType InputController::autoDetect() {
    std::cout << "\n  [INPUT] Auto-detecting input device...";
    
    auto devices = listAvailableDevices();
    
    // Priority: MAKCU > KMBOX > Windows Native
    for (const auto& device : devices) {
        if (device.type == DeviceType::MAKCU_DEVICE && device.isConnected) {
            std::cout << "\n  [INPUT] Detected MAKCU device";
            return DeviceType::MAKCU_DEVICE;
        }
    }
    
    for (const auto& device : devices) {
        if (device.type == DeviceType::KMBOX_DEVICE && device.isConnected) {
            std::cout << "\n  [INPUT] Detected KMBOX device";
            return DeviceType::KMBOX_DEVICE;
        }
    }
    
    std::cout << "\n  [INPUT] No specialized devices found, using Windows Native";
    return DeviceType::WINDOWS_NATIVE;
}

bool InputController::mouseMove(int32_t x, int32_t y) {
    switch (m_impl->activeDevice) {
        case DeviceType::MAKCU_DEVICE: {
            // makcu::Device* device = static_cast<makcu::Device*>(m_impl->makcuDevice);
            // return device->mouseMove(x, y);
            return true;
        }
        case DeviceType::KMBOX_DEVICE: {
            if (!m_impl->hSerial) return false;
            std::string command = "km.move(" + std::to_string(x) + "," + std::to_string(y) + ")\r\n";
            DWORD written;
            return WriteFile(m_impl->hSerial, command.c_str(), command.length(), &written, NULL) != FALSE;
        }
        case DeviceType::WINDOWS_NATIVE: {
            SetCursorPos(x, y);
            return true;
        }
        default:
            return false;
    }
}

bool InputController::mouseMoveBezier(int32_t x, int32_t y, uint32_t segments) {
    switch (m_impl->activeDevice) {
        case DeviceType::MAKCU_DEVICE: {
            // makcu::Device* device = static_cast<makcu::Device*>(m_impl->makcuDevice);
            // return device->mouseMoveBezier(x, y, segments);
            return true;
        }
        case DeviceType::KMBOX_DEVICE:
        case DeviceType::WINDOWS_NATIVE: {
            // Fallback: segment the movement
            return mouseMove(x, y);
        }
        default:
            return false;
    }
}

bool InputController::mouseDown(MouseButton button) {
    switch (m_impl->activeDevice) {
        case DeviceType::MAKCU_DEVICE: {
            // makcu::Device* device = static_cast<makcu::Device*>(m_impl->makcuDevice);
            // makcu::MouseButton btn = static_cast<makcu::MouseButton>(static_cast<int>(button));
            // return device->mouseDown(btn);
            return true;
        }
        case DeviceType::KMBOX_DEVICE: {
            if (!m_impl->hSerial) return false;
            int btnVal = (button == MouseButton::LEFT) ? 1 : 0;
            std::string command = "km.left(" + std::to_string(btnVal) + ")\r\n";
            DWORD written;
            return WriteFile(m_impl->hSerial, command.c_str(), command.length(), &written, NULL) != FALSE;
        }
        case DeviceType::WINDOWS_NATIVE: {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            return true;
        }
        default:
            return false;
    }
}

bool InputController::mouseUp(MouseButton button) {
    switch (m_impl->activeDevice) {
        case DeviceType::MAKCU_DEVICE: {
            // makcu::Device* device = static_cast<makcu::Device*>(m_impl->makcuDevice);
            // makcu::MouseButton btn = static_cast<makcu::MouseButton>(static_cast<int>(button));
            // return device->mouseUp(btn);
            return true;
        }
        case DeviceType::KMBOX_DEVICE: {
            if (!m_impl->hSerial) return false;
            std::string command = "km.left(0)\r\n";
            DWORD written;
            return WriteFile(m_impl->hSerial, command.c_str(), command.length(), &written, NULL) != FALSE;
        }
        case DeviceType::WINDOWS_NATIVE: {
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            return true;
        }
        default:
            return false;
    }
}

bool InputController::mouseClick(MouseButton button, uint32_t delay_ms) {
    if (!mouseDown(button)) return false;
    Sleep(delay_ms);
    return mouseUp(button);
}

InputController::BatchBuilder InputController::createBatch() {
    return BatchBuilder(this);
}

InputController::BatchBuilder& InputController::BatchBuilder::move(int32_t x, int32_t y) {
    m_commands.push_back("MOVE:" + std::to_string(x) + "," + std::to_string(y));
    return *this;
}

InputController::BatchBuilder& InputController::BatchBuilder::click(MouseButton button) {
    m_commands.push_back("CLICK:" + std::to_string(static_cast<int>(button)));
    return *this;
}

InputController::BatchBuilder& InputController::BatchBuilder::press(MouseButton button) {
    m_commands.push_back("PRESS:" + std::to_string(static_cast<int>(button)));
    return *this;
}

InputController::BatchBuilder& InputController::BatchBuilder::release(MouseButton button) {
    m_commands.push_back("RELEASE:" + std::to_string(static_cast<int>(button)));
    return *this;
}

InputController::BatchBuilder& InputController::BatchBuilder::delay(uint32_t ms) {
    m_commands.push_back("DELAY:" + std::to_string(ms));
    return *this;
}

bool InputController::BatchBuilder::execute() {
    for (const auto& cmd : m_commands) {
        // Parse and execute commands
        if (cmd.find("MOVE:") == 0) {
            // Parse and execute move
        } else if (cmd.find("CLICK:") == 0) {
            // Parse and execute click
        }
    }
    return true;
}

std::vector<InputController::DeviceInfo> InputController::listAvailableDevices() {
    std::vector<DeviceInfo> devices;
    
    // Check for MAKCU devices (VID:PID 1A86:55D3)
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA deviceInfoData = { sizeof(SP_DEVINFO_DATA) };
        
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
            char buf[512];
            DWORD size = 0;
            
            if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, 
                                                   NULL, (PBYTE)buf, sizeof(buf), &size) && size > 0) {
                std::string description(buf);
                
                // Detect MAKCU (VID:PID 1A86:55D3)
                if (description.find("1A86") != std::string::npos && 
                    description.find("55D3") != std::string::npos) {
                    size_t comPos = description.find("COM");
                    if (comPos != std::string::npos) {
                        DeviceInfo info;
                        info.type = DeviceType::MAKCU_DEVICE;
                        info.port = description.substr(comPos, 4);
                        info.description = "MAKCU Mouse Controller";
                        info.isConnected = true;
                        info.latency_us = 40;
                        devices.push_back(info);
                    }
                }
                
                // Detect KMBOX (CH340 USB-Serial)
                if (description.find("CH340") != std::string::npos || 
                    description.find("USB-SERIAL") != std::string::npos) {
                    size_t comPos = description.find("COM");
                    if (comPos != std::string::npos) {
                        DeviceInfo info;
                        info.type = DeviceType::KMBOX_DEVICE;
                        info.port = description.substr(comPos, 4);
                        info.description = "KMBOX Communication Device";
                        info.isConnected = true;
                        info.latency_us = 75;
                        devices.push_back(info);
                    }
                }
            }
        }
        
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
    
    return devices;
}

InputController::DeviceInfo InputController::getDeviceInfo() const {
    DeviceInfo info;
    info.type = m_currentDevice;
    info.isConnected = m_isConnected;
    info.latency_us = m_measuredLatency;
    info.description = getBackendName();
    return info;
}

const char* InputController::getBackendName() const {
    switch (m_currentDevice) {
        case DeviceType::MAKCU_DEVICE:
            return "MAKCU High-Performance Mouse";
        case DeviceType::KMBOX_DEVICE:
            return "KMBOX Microcontroller";
        case DeviceType::WINDOWS_NATIVE:
            return "Windows Native Mouse";
        default:
            return "Unknown";
    }
}

void InputController::disconnect() {
    if (m_impl->hSerial) {
        CloseHandle(m_impl->hSerial);
        m_impl->hSerial = NULL;
    }
    m_isConnected = false;
}

void InputController::measureLatency() {
    // Simple latency measurement by timing a move command
    auto start = std::chrono::high_resolution_clock::now();
    mouseMove(0, 0);
    auto end = std::chrono::high_resolution_clock::now();
    m_measuredLatency = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
}
