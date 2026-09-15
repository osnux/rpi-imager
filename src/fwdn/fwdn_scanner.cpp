/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 TOPST SBC Project
 */

#include "fwdn_scanner.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#if defined(Q_OS_WIN) || defined(_WIN32)
#include <windows.h>
#endif

#if defined(__linux__)
#include <filesystem>
#include <fstream>
namespace fs = std::filesystem;
#endif

#include <libusb.h>

namespace fwdn {

static constexpr uint16_t TELECHIPS_VID = 0x140E;

std::vector<Drivelist::DeviceDescriptor> scanFwdnDevices()
{
    std::vector<Drivelist::DeviceDescriptor> result;

    // 1. Check for manual test environment variables
    const char *forceD3 = std::getenv("TOPST_FWDN_FORCE_D3");
    if (forceD3 && std::strcmp(forceD3, "1") == 0) {
        Drivelist::DeviceDescriptor dd;
        dd.device = "fwdn://topst-d3";
        dd.description = "TOPST D3 (eMMC via FWDN) [Emulated]";
        dd.size = 16ULL * 1024 * 1024 * 1024;
        dd.isRemovable = true;
        dd.isUSB = true;
        dd.isReadOnly = false;
        dd.isSystem = false;
        dd.isVirtual = false;
        dd.isFwdn = true;
        dd.fwdnBoardType = "topst-d3";
        dd.fwdnPort = "";
        result.push_back(std::move(dd));
    }

    const char *forceAi = std::getenv("TOPST_FWDN_FORCE_AI");
    if (forceAi && std::strcmp(forceAi, "1") == 0) {
        Drivelist::DeviceDescriptor dd;
        dd.device = "fwdn://topst-ai?port=COM3";
        dd.description = "TOPST AI-G (eMMC via FWDN - COM3) [Emulated]";
        dd.size = 16ULL * 1024 * 1024 * 1024;
        dd.isRemovable = true;
        dd.isUSB = true;
        dd.isReadOnly = false;
        dd.isSystem = false;
        dd.isVirtual = false;
        dd.isFwdn = true;
        dd.fwdnBoardType = "topst-ai";
        dd.fwdnPort = "COM3";
        result.push_back(std::move(dd));
    }

#if defined(Q_OS_WIN) || defined(_WIN32)
    // 2. Windows: Check COM ports in registry for AI-G
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valueName[256];
        DWORD valueNameSize = sizeof(valueName);
        BYTE portName[256];
        DWORD portNameSize = sizeof(portName);
        DWORD type;
        DWORD index = 0;

        while (RegEnumValueA(hKey, index, valueName, &valueNameSize, NULL, &type, portName, &portNameSize) == ERROR_SUCCESS) {
            if (type == REG_SZ) {
                std::string port = reinterpret_cast<char*>(portName);
                // If it is a USB serial device or standard COM port
                Drivelist::DeviceDescriptor dd;
                dd.device = "fwdn://topst-ai?port=" + port;
                dd.description = "TOPST AI-G (eMMC via FWDN - " + port + ")";
                dd.size = 16ULL * 1024 * 1024 * 1024;
                dd.isRemovable = true;
                dd.isUSB = true;
                dd.isReadOnly = false;
                dd.isSystem = false;
                dd.isVirtual = false;
                dd.isFwdn = true;
                dd.fwdnBoardType = "topst-ai";
                dd.fwdnPort = port;
                result.push_back(std::move(dd));
            }
            valueNameSize = sizeof(valueName);
            portNameSize = sizeof(portName);
            index++;
        }
        RegCloseKey(hKey);
    }
#endif

#if defined(__linux__)
    // 2. Linux: Check /dev/ttyUSB* or /dev/ttyACM* for AI-G
    try {
        if (fs::exists("/dev")) {
            for (const auto& entry : fs::directory_iterator("/dev")) {
                std::string filename = entry.path().filename().string();
                if (filename.rfind("ttyUSB", 0) == 0 || filename.rfind("ttyACM", 0) == 0) {
                    std::string portPath = entry.path().string();
                    Drivelist::DeviceDescriptor dd;
                    dd.device = "fwdn://topst-ai?port=" + portPath;
                    dd.description = "TOPST AI-G (eMMC via FWDN - " + filename + ")";
                    dd.size = 16ULL * 1024 * 1024 * 1024;
                    dd.isRemovable = true;
                    dd.isUSB = true;
                    dd.isReadOnly = false;
                    dd.isSystem = false;
                    dd.isVirtual = false;
                    dd.isFwdn = true;
                    dd.fwdnBoardType = "topst-ai";
                    dd.fwdnPort = portPath;
                    result.push_back(std::move(dd));
                }
            }
        }
    } catch (...) {
        // Ignore filesystem error
    }

    // Check Linux sysfs for Telechips USB devices (0x140e)
    try {
        if (fs::exists("/sys/bus/usb/devices")) {
            for (const auto& entry : fs::directory_iterator("/sys/bus/usb/devices")) {
                auto vidPath = entry.path() / "idVendor";
                if (fs::exists(vidPath)) {
                    std::ifstream f(vidPath);
                    std::string vidStr;
                    if (f >> vidStr && vidStr == "140e") {
                        Drivelist::DeviceDescriptor dd;
                        dd.device = "fwdn://topst-d3";
                        dd.description = "TOPST D3 (eMMC via FWDN)";
                        dd.size = 16ULL * 1024 * 1024 * 1024;
                        dd.isRemovable = true;
                        dd.isUSB = true;
                        dd.isReadOnly = false;
                        dd.isSystem = false;
                        dd.isVirtual = false;
                        dd.isFwdn = true;
                        dd.fwdnBoardType = "topst-d3";
                        dd.fwdnPort = "";
                        result.push_back(std::move(dd));
                        break;
                    }
                }
            }
        }
    } catch (...) {
        // Ignore filesystem error
    }
#endif

    // 3. Libusb scan for Telechips VID (0x140e) across all platforms
    try {
        libusb_context *ctx = nullptr;
        if (libusb_init(&ctx) == 0 && ctx) {
            libusb_device **devs = nullptr;
            ssize_t cnt = libusb_get_device_list(ctx, &devs);
            if (cnt > 0 && devs) {
                bool found = false;
                for (ssize_t i = 0; i < cnt; ++i) {
                    libusb_device_descriptor desc;
                    if (libusb_get_device_descriptor(devs[i], &desc) == 0) {
                        if (desc.idVendor == TELECHIPS_VID) {
                            // Check if not already added
                            bool alreadyInList = false;
                            for (const auto& existing : result) {
                                if (existing.fwdnBoardType == "topst-d3") {
                                    alreadyInList = true;
                                    break;
                                }
                            }
                            if (!alreadyInList) {
                                Drivelist::DeviceDescriptor dd;
                                dd.device = "fwdn://topst-d3";
                                dd.description = "TOPST D3 (eMMC via FWDN)";
                                dd.size = 16ULL * 1024 * 1024 * 1024;
                                dd.isRemovable = true;
                                dd.isUSB = true;
                                dd.isReadOnly = false;
                                dd.isSystem = false;
                                dd.isVirtual = false;
                                dd.isFwdn = true;
                                dd.fwdnBoardType = "topst-d3";
                                dd.fwdnPort = "";
                                result.push_back(std::move(dd));
                            }
                            found = true;
                            break;
                        }
                    }
                }
                libusb_free_device_list(devs, 1);
            }
            libusb_exit(ctx);
        }
    } catch (...) {
        // Libusb failure should not crash scanner
    }

    return result;
}

} // namespace fwdn
