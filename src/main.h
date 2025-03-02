#pragma once
#include <SPI.h>
#include "meshtastic/config.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "meshtastic/mesh.pb.h"

#if ARCH_PORTDUINO
extern HardwareSPI *DisplaySPI;
extern HardwareSPI *LoraSPI;
#endif

// Return a human readable string of the form "Meshtastic_ab13"
const char *getDeviceName();

void nrf52Setup(), esp32Setup(), nrf52Loop(), esp32Loop(), rp2040Setup(), clearBonds(), enterDfuMode();

meshtastic_DeviceMetadata getDeviceMetadata();


// We default to 4MHz SPI, SPI mode 0
extern SPISettings spiSettings;