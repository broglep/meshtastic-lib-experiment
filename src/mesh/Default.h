#pragma once
#include <cstdint>
#include "meshtastic/config.pb.h"
#include "meshUtils.h"

#define ONE_DAY 24 * 60 * 60
#define ONE_MINUTE_MS 60 * 1000
#define THIRTY_SECONDS_MS 30 * 1000
#define FIVE_SECONDS_MS 5 * 1000

#define min_default_telemetry_interval_secs 30 * 60
#define default_gps_update_interval IF_ROUTER(ONE_DAY, 2 * 60)
#define default_telemetry_broadcast_interval_secs(deviceConfig) IF_ROUTER(deviceConfig, ONE_DAY / 2, 60 * 60)
#define default_broadcast_interval_secs(deviceConfig) IF_ROUTER(deviceConfig, ONE_DAY / 2, 15 * 60)
#define default_wait_bluetooth_secs(deviceConfig) IF_ROUTER(deviceConfig, 1, 60)
#define default_sds_secs(deviceConfig) IF_ROUTER(deviceConfig, ONE_DAY, UINT32_MAX) // Default to forever super deep sleep
#define default_ls_secs(deviceConfig) IF_ROUTER(deviceConfig, ONE_DAY, 5 * 60)
#define default_min_wake_secs 10
#define default_screen_on_secs(deviceConfig) IF_ROUTER(deviceConfig, 1, 60 * 10)
#define default_node_info_broadcast_secs 3 * 60 * 60
#define default_neighbor_info_broadcast_secs 6 * 60 * 60
#define min_node_info_broadcast_secs 60 * 60 // No regular broadcasts of more than once an hour
#define min_neighbor_info_broadcast_secs 4 * 60 * 60

#define default_mqtt_address "mqtt.meshtastic.org"
#define default_mqtt_username "meshdev"
#define default_mqtt_password "large4cats"
#define default_mqtt_root "msh"

#define IF_ROUTER(deviceConfig, routerVal, normalVal)                                                                                          \
    ((deviceConfig->role == meshtastic_Config_DeviceConfig_Role_ROUTER) ? (routerVal) : (normalVal))

class Default
{
  public:
    static uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval, meshtastic_Config_DeviceConfig *deviceConfig);
    static uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval);
    static uint32_t getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue);
    static uint32_t getConfiguredOrDefaultMsScaled(meshtastic_Config_DeviceConfig *deviceConfig,
                                                   meshtastic_Config_LoRaConfig *loraConfig, uint32_t configured,
                                                   uint32_t defaultValue, uint32_t numOnlineNodes);
    static uint8_t getConfiguredOrDefaultHopLimit(meshtastic_Config_LoRaConfig *loraConfig, uint8_t configured);
    static uint32_t getConfiguredOrMinimumValue(uint32_t configured, uint32_t minValue);

  private:
    static float congestionScalingCoefficient(meshtastic_Config_LoRaConfig *loraConfig, int numOnlineNodes)
    {
        // Increase frequency of broadcasts for small networks regardless of preset
        if (numOnlineNodes <= 10) {
            return 0.6;
        } else if (numOnlineNodes <= 20) {
            return 0.7;
        } else if (numOnlineNodes <= 30) {
            return 0.8;
        } else if (numOnlineNodes <= 40) {
            return 1.0;
        } else {
            float throttlingFactor = 0.075;
            if (loraConfig->use_preset && loraConfig->modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW)
                throttlingFactor = 0.04;
            else if (loraConfig->use_preset && loraConfig->modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST)
                throttlingFactor = 0.02;
            else if (loraConfig->use_preset && loraConfig->modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW)
                throttlingFactor = 0.01;
            else if (loraConfig->use_preset &&
                     IS_ONE_OF(loraConfig->modem_preset, meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST,
                               meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO))
                return 1.0; // Don't bother throttling for highest bandwidth presets
            // Scaling up traffic based on number of nodes over 40
            int nodesOverForty = (numOnlineNodes - 40);
            return 1.0 + (nodesOverForty * throttlingFactor); // Each number of online node scales by 0.075 (default)
        }
    }
};