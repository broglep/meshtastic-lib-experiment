#if RADIOLIB_EXCLUDE_SX126X != 1
#include "SX1262Interface.h"

SX1262Interface::SX1262Interface(LockingArduinoHal *hal, RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst,
                                 RADIOLIB_PIN_TYPE busy, NodeNum *nodeId, meshtastic_Config_DeviceConfig *deviceConfig)
    : SX126xInterface(hal, cs, irq, rst, busy, nodeId, deviceConfig)
{
}
#endif