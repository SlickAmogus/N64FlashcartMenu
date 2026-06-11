/**
 * @file vru.h
 * @brief Voice Recognition Unit (NUS-020 / Hey You Pikachu microphone) support
 * @ingroup menu
 *
 * Detection + initialization of the N64 VRU peripheral.  Recognition itself
 * (loading vocabulary, polling for matches) isn't wired up yet — that
 * requires the phoneme-encoded dictionary format which is partly
 * reverse-engineered.  For now, this module lets the menu show whether a
 * VRU is plugged in and whether the documented init handshake succeeded.
 *
 * Hot-plug is supported: libdragon's joybus subsystem refreshes peripheral
 * identifiers automatically, and presence is re-evaluated every frame.
 */

#ifndef VRU_H__
#define VRU_H__

#include <stdbool.h>
#include <stdint.h>

/** @brief Lifecycle states for the connected VRU. */
typedef enum {
    VRU_PRESENCE_ABSENT       = 0,  /**< No VRU connected. */
    VRU_PRESENCE_DETECTED     = 1,  /**< Hardware seen; init not yet attempted. */
    VRU_PRESENCE_INIT_FAILED  = 2,  /**< Init handshake didn't get expected response. */
    VRU_PRESENCE_READY        = 3,  /**< Init succeeded; ready to accept vocabulary. */
    VRU_PRESENCE_LOADED       = 4,  /**< Dictionary uploaded; listening cycle active. */
} vru_presence_t;

/** @brief Action a recognized utterance maps to. */
typedef enum {
    VRU_HIT_NONE  = 0,
    VRU_HIT_UP    = 1,
    VRU_HIT_DOWN  = 2,
    VRU_HIT_LEFT  = 3,
    VRU_HIT_RIGHT = 4,
    VRU_HIT_OK    = 5,
} vru_hit_t;

/** @brief Return and clear the most-recently-recognized hit (if any). */
vru_hit_t vru_consume_hit (void);

/** @brief Snapshot of the last 0x09 result block — exposed for debug UI. */
typedef struct {
    bool      has_data;        /**< true once at least one CRC-valid read happened */
    uint16_t  voice_level;     /**< bytes 0x08–0x09 in the result block (mic level) */
    uint16_t  rel_level;       /**< bytes 0x0A–0x0B (voice / noise ratio)            */
    uint16_t  voice_length;    /**< bytes 0x0C–0x0D (utterance length, ~ms)          */
    uint16_t  err_flags;       /**< bytes 0x04–0x05 (recognition error bits)         */
    uint16_t  valid_count;     /**< bytes 0x06–0x07 (# of valid match results)       */
    uint16_t  hit_1_index;     /**< bytes 0x0E–0x0F (best-match dictionary slot)     */
    uint16_t  hit_1_deviance;  /**< bytes 0x10–0x11 (best-match confidence — lower is better) */
    uint16_t  mode_status;     /**< bytes 0x22–0x23 (mode + status flags, normally 0x0040) */
    uint16_t  state_0b;        /**< last 0x0B state word read during the cycle */
    uint8_t   cycle_state;     /**< current recognition-cycle state (diagnostic) */
} vru_debug_info_t;

/** @brief Copy the most-recent 0x09 read into @p out (for debug overlay). */
void vru_get_debug_info (vru_debug_info_t *out);

/** @brief Refresh cached state.  Cheap; call once per frame. */
void vru_poll (void);

/** @brief Is a VRU currently connected (in any state)? */
bool vru_is_present (void);

/** @brief Port (0–3) the VRU is connected to, or −1 if absent. */
int  vru_get_port (void);

/** @brief Current lifecycle state of the connected VRU. */
vru_presence_t vru_get_presence (void);

#endif /* VRU_H__ */
