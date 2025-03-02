#include <nvs.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <SPIFFS.h>
#include "main.h"
#include "mesh/RadioLibInterface.h"
#include "mesh/SX1262Interface.h"
#include "PowerStatus.h"
#include "mesh/Router.h"
#include "mesh/ReliableRouter.h"
#include "FSCommon.h"

#undef BUTTON_PIN

RadioInterface *rIf = NULL;
#ifdef ARCH_PORTDUINO
RadioLibHal *RadioLibHAL = NULL;
#endif


void setup() {

    Serial.begin(115200);
    while(!Serial) {};
    Serial.println("Hello");

    // ESP32
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LOG_DEBUG("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI.setFrequency(4000000);




#ifdef ARCH_PORTDUINO
    if (settingsMap[use_sx1262]) {
        if (!rIf) {
            LOG_DEBUG("Activate sx1262 radio on SPI port %s", settingsStrings[spidev].c_str());
            if (settingsStrings[spidev] == "ch341") {
                RadioLibHAL = ch341Hal;
            } else {
                RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            }
            rIf = new SX1262Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No SX1262 radio");
                delete rIf;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1262 init success");
            }
        }
    } else if (settingsMap[use_rf95]) {
        if (!rIf) {
            LOG_DEBUG("Activate rf95 radio on SPI port %s", settingsStrings[spidev].c_str());
            RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new RF95Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                    settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No RF95 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("RF95 init success");
            }
        }
    } else if (settingsMap[use_sx1280]) {
        if (!rIf) {
            LOG_DEBUG("Activate sx1280 radio on SPI port %s", settingsStrings[spidev].c_str());
            RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new SX1280Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No SX1280 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1280 init success");
            }
        }
    } else if (settingsMap[use_lr1110]) {
        if (!rIf) {
            LOG_DEBUG("Activate lr1110 radio on SPI port %s", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new LR1110Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No LR1110 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("LR1110 init success");
            }
        }
    } else if (settingsMap[use_lr1120]) {
        if (!rIf) {
            LOG_DEBUG("Activate lr1120 radio on SPI port %s", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new LR1120Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No LR1120 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("LR1120 init success");
            }
        }
    } else if (settingsMap[use_lr1121]) {
        if (!rIf) {
            LOG_DEBUG("Activate lr1121 radio on SPI port %s", settingsStrings[spidev].c_str());
            LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new LR1121Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No LR1121 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("LR1121 init success");
            }
        }
    } else if (settingsMap[use_sx1268]) {
        if (!rIf) {
            LOG_DEBUG("Activate sx1268 radio on SPI port %s", settingsStrings[spidev].c_str());
            RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
            rIf = new SX1268Interface((LockingArduinoHal *)RadioLibHAL, settingsMap[cs], settingsMap[irq], settingsMap[reset],
                                      settingsMap[busy]);
            if (!rIf->init()) {
                LOG_WARN("No SX1268 radio");
                delete rIf;
                rIf = NULL;
                exit(EXIT_FAILURE);
            } else {
                LOG_INFO("SX1268 init success");
            }
        }
    }

#elif defined(HW_SPI1_DEVICE)
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI1, spiSettings);
#else // HW_SPI1_DEVICE
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
#endif

    LOG_INFO("Setup done")
    return;
    esp32Setup();
    fsInit();

    nodeDB = new NodeDB;
    router = new ReliableRouter();

    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    const char *tmp = "Hello World (lib)";
    memcpy(p->decoded.payload.bytes, tmp, strlen(tmp));
    p->decoded.payload.size = strlen(tmp);

    /*
    p->decoded.payload.size =
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_Compressed_msg, &c);
    */

    p->priority = meshtastic_MeshPacket_Priority_ACK;

    p->hop_limit = 2; // Flood ACK back to original sender
    p->channel = 1;

    router->send(p);
    LOG_INFO("Send Hello Message");
}

void loop()
{
#ifdef ARCH_ESP32
    //esp32Loop();
#endif
#ifdef ARCH_NRF52
    nrf52Loop();
#endif
}


void esp32Setup()
{
    /* We explicitly don't want to do call randomSeed,
    // as that triggers the esp32 core to use a less secure pseudorandom function.
    uint32_t seed = esp_random();
    LOG_DEBUG("Set random seed %u", seed);
    randomSeed(seed);
    */

    LOG_DEBUG("Total heap: %d", ESP.getHeapSize());
    LOG_DEBUG("Free heap: %d", ESP.getFreeHeap());
    LOG_DEBUG("Total PSRAM: %d", ESP.getPsramSize());
    LOG_DEBUG("Free PSRAM: %d", ESP.getFreePsram());

    nvs_stats_t nvs_stats;
    auto res = nvs_get_stats(NULL, &nvs_stats);
    assert(res == ESP_OK);
    LOG_DEBUG("NVS: UsedEntries %d, FreeEntries %d, AllEntries %d, NameSpaces %d", nvs_stats.used_entries, nvs_stats.free_entries,
              nvs_stats.total_entries, nvs_stats.namespace_count);

    LOG_DEBUG("Setup Preferences in Flash Storage");

    // Create object to store our persistent data
    Preferences preferences;
    preferences.begin("meshtastic", false);

    uint32_t rebootCounter = preferences.getUInt("rebootCounter", 0);
    rebootCounter++;
    preferences.putUInt("rebootCounter", rebootCounter);
    // store firmware version and hwrevision for access from OTA firmware
    String fwrev = preferences.getString("firmwareVersion", "");
    if (fwrev.compareTo(optstr(APP_VERSION)) != 0)
        preferences.putString("firmwareVersion", optstr(APP_VERSION));
    uint8_t hwven = preferences.getUInt("hwVendor", 0);
    if (hwven != HW_VENDOR)
        preferences.putUInt("hwVendor", HW_VENDOR);
    preferences.end();
    LOG_DEBUG("Number of Device Reboots: %d", rebootCounter);
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    String BLEOTA = BleOta::getOtaAppVersion();
    if (BLEOTA.isEmpty()) {
        LOG_INFO("No OTA firmware available");
    } else {
        LOG_INFO("OTA firmware version %s", BLEOTA.c_str());
    }
#else
    LOG_INFO("No OTA firmware available");
#endif

    // enableModemSleep();

// Since we are turning on watchdogs rather late in the release schedule, we really don't want to catch any
// false positives.  The wait-to-sleep timeout for shutting down radios is 30 secs, so pick 45 for now.
// #define APP_WATCHDOG_SECS 45
#define APP_WATCHDOG_SECS 90

#ifdef CONFIG_IDF_TARGET_ESP32C6
    esp_task_wdt_config_t *wdt_config = (esp_task_wdt_config_t *)malloc(sizeof(esp_task_wdt_config_t));
    wdt_config->timeout_ms = APP_WATCHDOG_SECS * 1000;
    wdt_config->trigger_panic = true;
    res = esp_task_wdt_init(wdt_config);
    assert(res == ESP_OK);
#else
    res = esp_task_wdt_init(APP_WATCHDOG_SECS, true);
    assert(res == ESP_OK);
#endif
    res = esp_task_wdt_add(NULL);
    assert(res == ESP_OK);

#ifdef HAS_32768HZ
    enableSlowCLK();
#endif
}

#define DEBUG_PORT Serial
CryptoEngine *crypto;
Router *router;
SPISettings spiSettings;
meshtastic::PowerStatus *powerStatus;
void setBluetoothEnable(bool enable) {}
void cpuDeepSleep(uint32_t msecToWake) {}
void getMacAddr(uint8_t *dmac)
{
#if defined(CONFIG_IDF_TARGET_ESP32C6) && defined(CONFIG_SOC_IEEE802154_SUPPORTED)
    assert(esp_base_mac_addr_get(dmac) == ESP_OK);
#else
    assert(esp_efuse_mac_get_default(dmac) == ESP_OK);
#endif
}