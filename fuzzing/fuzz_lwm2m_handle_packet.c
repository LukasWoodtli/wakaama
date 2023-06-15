#include "liblwm2m.h"

#include <inttypes.h>
#include <stddef.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    lwm2m_context_t* contextP = lwm2m_init(NULL);
    lwm2m_handle_packet(contextP, (uint8_t *)data, size, NULL);
    lwm2m_close(contextP);

    return 0;
}
