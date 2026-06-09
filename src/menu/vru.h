/**
 * @file vru.h
 * @brief Voice Recognition Unit (NUS-020 / Hey You Pikachu microphone) support
 * @ingroup menu
 *
 * The N64 VRU is a peripheral that plugs into a controller port and contains
 * an on-board speech-recognition ASIC.  This module currently only handles
 * detection — call @ref vru_poll once per frame and @ref vru_is_present
 * anywhere to test whether a VRU is currently connected to any port.
 *
 * Hot-plug works: libdragon's joybus subsystem refreshes peripheral
 * identifiers in the background, so the polled state reflects reality
 * within a couple of frames of plugging or unplugging.
 */

#ifndef VRU_H__
#define VRU_H__

#include <stdbool.h>

/** @brief Refresh the cached VRU presence state.  Cheap; call once per frame. */
void vru_poll (void);

/** @brief Is a VRU currently connected to any controller port? */
bool vru_is_present (void);

/** @brief Port (0–3) the VRU is connected to, or −1 if not present. */
int  vru_get_port (void);

#endif /* VRU_H__ */
