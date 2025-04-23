#include "error.h"
#include "debug.h"
void recordCriticalError(meshtastic_CriticalErrorCode code, uint32_t address, const char *filename)
{
    // Print error to screen and serial port

    if (filename) {
        LOG_ERROR("NOTE! Record critical error %d at %s:%lu", code, filename, address);
    } else {
        LOG_ERROR("NOTE! Record critical error %d, address=0x%lx", code, address);
    }

    // Record error to DB
    error_code = code;
    error_address = address;

    // Currently portuino is mostly used for simulation.  Make sure the user notices something really bad happened
#ifdef ARCH_PORTDUINO
    LOG_ERROR("A critical failure occurred, portduino is exiting");
    exit(2);
#endif
}