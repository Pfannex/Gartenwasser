/**
 * @file    Version.h
 * @brief   Firmware-Version - einzige Stelle, die vor jedem Release/OTA-Test manuell
 *          angepasst wird. Wird per MQTT (gartenwasser/diagnostics/version, retained)
 *          veroeffentlicht und im Web-Dashboard angezeigt, um nach einem OTA-Update
 *          bestaetigen zu koennen, dass die neue Firmware tatsaechlich laeuft.
 */

#pragma once

constexpr const char *kFirmwareVersion = "V0.8.0.0";
