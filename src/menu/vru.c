/**
 * @file vru.c
 * @brief Voice Recognition Unit detection
 * @ingroup menu
 */

#include <libdragon.h>

#include "vru.h"

/* Currently-connected port (0–3) or -1 if absent. */
static int present_port = -1;

void vru_poll (void) {
    int found = -1;
    for (int p = 0; p < 4; p++) {
        uint8_t status = 0;
        joybus_identifier_t id = joybus_get_identifier(p, &status);
        if (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION) {
            found = p;
            break;
        }
    }
    present_port = found;
}

bool vru_is_present (void) {
    return present_port >= 0;
}

int vru_get_port (void) {
    return present_port;
}
