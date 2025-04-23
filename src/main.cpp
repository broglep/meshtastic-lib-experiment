



//RadioInterface *rIf = NULL;



#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "variant.h"
#include "mesh/Channels.h"
#include "mesh/MeshRadio.h"
#include "debug.h"
#include "meshtastic/deviceonly.pb.h"
#include "mesh/Default.h"
#include "mesh/MeshInterface.h"
#include "mesh/SX1262Interface.h"
#include "airtime.h"
#include "error.h"
#include "detect/LoRaRadioType.h"
#include "architecture.h"

meshtastic_Config_LoRaConfig loraConfig;
meshtastic_Config_DeviceConfig deviceConfig = {.role = meshtastic_Config_DeviceConfig_Role_CLIENT};
meshtastic_Config_LoRaConfig_RegionCode region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;
meshtastic_CriticalErrorCode error_code =
        meshtastic_CriticalErrorCode_NONE; // For the error code, only show values from this boot (discard value from flash)
uint32_t error_address = 0;


RadioInterface *rIf = NULL;
// Global LoRa radio type
LoRaRadioType radioType = NO_RADIO;
/*
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);
LockingArduinoHal hal(SPI, spiSettings);
SX1262Interface radio(&hal, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
NodeNum node_id = 1111111;
MeshInterface meshInterface(&loraConfig, &node_id, &radio);
*/

void esp32Setup()
{

    LOG_DEBUG("Total heap: %d", ESP.getHeapSize());
    LOG_DEBUG("Free heap: %d", ESP.getFreeHeap());
    LOG_DEBUG("Total PSRAM: %d", ESP.getPsramSize());
    LOG_DEBUG("Free PSRAM: %d", ESP.getFreePsram());





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
    auto res = esp_task_wdt_init(APP_WATCHDOG_SECS, true);
    assert(res == ESP_OK);
#endif
    res = esp_task_wdt_add(NULL);
    assert(res == ESP_OK);

#ifdef HAS_32768HZ
    enableSlowCLK();
#endif

    gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
}

#include "driver/gpio.h"
#include "PowerMon.h"

static void irq_init()
{
    /*
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = PMU_INPUT_PIN_SEL;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);
    gpio_set_intr_type(PMU_INPUT_PIN, GPIO_INTR_NEGEDGE);
    //install gpio isr service
    gpio_install_isr_service(0);
    //hook isr handler for specific gpio pin
    //gpio_isr_handler_add(PMU_INPUT_PIN, pmu_irq_handler, (void *) PMU_INPUT_PIN);
    */
}


void setup() {


    Serial.begin(115200);
    while(!Serial) {};
    delay(1000);
    Serial.println("Hello");

#ifdef ARCH_ESP32
    esp32Setup();
#endif

    // ESP32


    //SX1262 sx1262 = new Module(SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
    //sx1262.begin();
    //return;

    concurrency::OSThread::setup();
    concurrency::hasBeenSetup = true;
    powerMonInit();

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    LOG_DEBUG("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    SPI.setFrequency(4000000);

    //auto *testingChannel = new meshtastic_Channel();

    auto testingName = "Testing";

#error "Redacted: add your own testing channel secret"
    auto testingSecretBase64 = "";
    auto testingSecretHex = "";
    uint8_t testingSecretData[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};


    //channels.setChannel(*testingChannel);
    //channels.setActiveByIndex(testingChannel->index);

    loraConfig.tx_enabled = true;
    loraConfig.region = meshtastic_Config_LoRaConfig_RegionCode_EU_868;

    channels.initDefaults(&loraConfig);
    initRegion(&region);
    //initRegion();


    auto testingChannel = channels.getByIndex(1);
    strncpy(testingChannel.settings.name, testingName, strlen(testingName));
    testingChannel.role = meshtastic_Channel_Role_SECONDARY;
    testingChannel.has_settings = true;
    memcpy(testingChannel.settings.psk.bytes, testingSecretData, sizeof(testingSecretData));
    testingChannel.settings.psk.size = sizeof(testingSecretData);

    channels.setChannel(testingChannel);

    channels.onConfigChanged(&loraConfig);

    /*
    auto hash = channels.setActiveByIndex(testingChannel.index);
    LOG_INFO("Set channel: %d", hash);
    */

    loraConfig.override_duty_cycle = true;
    NodeNum node_id = 1111111;

    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);
    auto *hal = new LockingArduinoHal(SPI, spiSettings);
    auto sxIf = new SX1262Interface(hal, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY, &node_id, &deviceConfig);
#ifdef SX126X_DIO3_TCXO_VOLTAGE
    sxIf->setTCXOVoltage(SX126X_DIO3_TCXO_VOLTAGE);
#endif
    if (!sxIf->init(&loraConfig)) {
        LOG_WARN("No SX1262 radio");
        delete sxIf;
        rIf = NULL;
    } else {
        LOG_INFO("SX1262 init success");
        rIf = sxIf;
        radioType = SX1262_RADIO;
    }

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_RADIO);
    else {

        // Log bit rate to debug output
        LOG_DEBUG("LoRA bitrate = %f bytes / sec", (float(meshtastic_Constants_DATA_PAYLOAD_LEN) /
                                                    (float(rIf->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN)))) *
                                                   1000);
    }


    MeshInterface meshInterface(&loraConfig, &node_id, rIf);
    meshtastic_MeshPacket *p = meshInterface.allocForSending();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    const char *tmp = "Hello World (lib)";
    memcpy(p->decoded.payload.bytes, tmp, strlen(tmp));
    p->decoded.payload.size = strlen(tmp);
    p->priority = meshtastic_MeshPacket_Priority_ACK;

    p->hop_limit = 2; // Flood ACK back to original sender
    //p->to = 3771721320;
    p->channel = testingChannel.index;

    auto ret = meshInterface.send(p);

    //router->send(p);
    LOG_INFO("Send Hello Message: %d", ret);


}

bool runASAP;
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

void esp32Loop()
{
    esp_task_wdt_reset(); // service our app level watchdog

    // for debug printing
    // radio.radioIf.canSleep();
}

void loop()
{
    runASAP = false;

#ifdef ARCH_ESP32
    esp32Loop();
#endif
#ifdef ARCH_NRF52
    nrf52Loop();
#endif
    //powerCommandsCheck();

#ifdef DEBUG_STACK
    static uint32_t lastPrint = 0;
    if (!Throttle::isWithinTimespanMs(lastPrint, 10 * 1000L)) {
        lastPrint = millis();
        meshtastic::printThreadInfo("main");
    }
#endif


    long delayMsec = concurrency::mainController.runOrDelay();

    // We want to sleep as long as possible here - because it saves power
    if (!runASAP && loopCanSleep()) {
        concurrency::mainDelay.delay(delayMsec);
    }
}


#include <string>
static const int B64index[256] = { 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
                                   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
                                   56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  6,
                                   7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,
                                   0,  0,  0, 63,  0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                                   41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51 };

std::string b64decode(const void* data, const size_t len)
{
    unsigned char* p = (unsigned char*)data;
    int pad = len > 0 && (len % 4 || p[len - 1] == '=');
    const size_t L = ((len + 3) / 4 - pad) * 4;
    std::string str(L / 4 * 3 + pad, '\0');

    for (size_t i = 0, j = 0; i < L; i += 4)
    {
        int n = B64index[p[i]] << 18 | B64index[p[i + 1]] << 12 | B64index[p[i + 2]] << 6 | B64index[p[i + 3]];
        str[j++] = n >> 16;
        str[j++] = n >> 8 & 0xFF;
        str[j++] = n & 0xFF;
    }
    if (pad)
    {
        int n = B64index[p[L]] << 18 | B64index[p[L + 1]] << 12;
        str[str.size() - 1] = n >> 16;

        if (len > L + 2 && p[L + 2] != '=')
        {
            n |= B64index[p[L + 2]] << 6;
            str.push_back(n >> 8 & 0xFF);
        }
    }
    return str;
}