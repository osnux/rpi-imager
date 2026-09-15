#ifndef FWDN_SCANNER_H
#define FWDN_SCANNER_H

/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 TOPST SBC Project
 */

#include "drivelist/drivelist.h"
#include <vector>
#include <string>

namespace fwdn {

/**
 * @brief Scan for TOPST SBC devices connected in FWDN mode
 * 
 * Detects Telechips USB devices (VID 0x140E) for TOPST D3 / Dolphin3,
 * and serial/COM port devices for TOPST AI-G.
 * 
 * @return std::vector<Drivelist::DeviceDescriptor> Synthetic descriptors for FWDN targets
 */
std::vector<Drivelist::DeviceDescriptor> scanFwdnDevices();

} // namespace fwdn

#endif // FWDN_SCANNER_H

