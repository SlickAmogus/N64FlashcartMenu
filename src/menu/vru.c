/**
 * @file vru.c
 * @brief Voice Recognition Unit detection and initialization
 * @ingroup menu
 *
 * Talks to the VRU over libdragon's joybus_exec_cmd.  Command IDs and the
 * documented init sequence are taken from the public reverse-engineering
 * notes on the Console Protocols / n64brew wiki — none of the protocol is
 * Nintendo-proprietary code; it's all observed-on-the-wire behaviour.
 *
 * Implementation notes:
 *   - 0x0D writes are 3-byte config pokes that the documented init runs
 *     five of in sequence; each returns one byte and that byte is expected
 *     to be 0x00.
 *   - 0x0C writes 4 data bytes and gets a CRC of those bytes back; we
 *     compute the same CRC locally and use *that* as the success check
 *     rather than the docs' hard-coded magic byte (works on both US and JP
 *     ROMs' boot sequence).
 *   - 0x0B reads voice_state + a CRC of the first two response bytes,
 *     same self-checking idea.
 *
 * The CRC is the standard zoinkity-described 8-bit polynomial-tap variant
 * with the 0x85 reduction.  Re-implemented independently from the spec.
 */

#include <libdragon.h>
#include <string.h>

#include "vru.h"

static int            present_port    = -1;
static vru_presence_t presence        = VRU_PRESENCE_ABSENT;
static bool           load_attempted  = false;

/* Frames the VRU has been continuously detected.  Spec says the US version
 * wants ~50 ms of quiet time after the peripheral signals itself before
 * the host starts talking to it.  6 frames at 60 Hz is comfortably over
 * that and short enough that the user won't notice the delay. */
static int detected_frames = 0;
#define INIT_DELAY_FRAMES   (6)

/* Phoneme sequence for "UP", encoded against the Hey-You-Pikachu USA
 * phoneme alphabet (each entry is a 16-bit big-endian code, all codes
 * multiples of 3 per the alphabet's encoding scheme).
 *   0x0432 — start-of-word marker for "uh"-class vowels
 *   0x00A8 — the "uh" sound (as in cup / luck / Pi"ka")
 *   0x03A8 — the "p" stop (as in topaz)
 * This is the first word we try to upload after init, purely to confirm
 * the joybus payload + CRC plumbing works end-to-end. */
static const uint16_t test_word_up[] = { 0x0432, 0x00A8, 0x03A8 };
#define TEST_WORD_UP_LEN  (sizeof(test_word_up) / sizeof(test_word_up[0]))

/* VRU response-CRC: process each input byte MSB-first into an 8-bit shift
 * register, XOR-tapping with 0x85 whenever the bit shifted out was 1.  The
 * loop runs one extra zero-input iteration past the data to flush the
 * register, which is the quirk that makes the algorithm match real VRU
 * silicon. */
static uint8_t vru_crc (const uint8_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i <= len; i++) {
        for (int bit = 0x80; bit; bit >>= 1) {
            uint8_t tap = (crc & 0x80) ? 0x85 : 0x00;
            crc <<= 1;
            if (i < len && (data[i] & bit)) crc |= 1;
            crc ^= tap;
        }
    }
    return crc;
}

/* The five documented audio/gain pokes the boot ROM runs.  Each expects
 * a single 0x00 response. */
static const uint8_t init_config_steps[5][3] = {
    {0x0D, 0x1E, 0x0C},
    {0x0D, 0x6E, 0x07},
    {0x0D, 0x08, 0x0E},
    {0x0D, 0x56, 0x18},
    {0x0D, 0x03, 0x0F},
};

static int find_vru_port (void) {
    for (int p = 0; p < 4; p++) {
        uint8_t status = 0;
        joybus_identifier_t id = joybus_get_identifier(p, &status);
        if (id == JOYBUS_IDENTIFIER_N64_VOICE_RECOGNITION) {
            return p;
        }
    }
    return -1;
}

static bool vru_initialize (int port) {
    uint8_t rx[3] = {0};

    /* Steps 1-5: gain/audio config pokes. */
    for (int i = 0; i < 5; i++) {
        joybus_exec_cmd(port, 3, 1, init_config_steps[i], rx);
        if (rx[0] != 0x00) {
            return false;
        }
    }

    /* Step 6: 0x0C "dictionary setup".  Payload bytes [3..6] are CRC'd by
     * the VRU and returned; we compare against our own computation. */
    const uint8_t cmd_c[7] = {0x0C, 0x00, 0x00,  0x00, 0x00, 0x01, 0x00};
    uint8_t expected_c = vru_crc(&cmd_c[3], 4);
    joybus_exec_cmd(port, 7, 1, cmd_c, rx);
    if (rx[0] != expected_c) {
        return false;
    }

    /* Step 7: 0x0B status read.  Returns {voice_state, 0x00, crc(state,0)}.
     * voice_state value depends on region (READY=0 on JP, START=1 on US),
     * so don't strictly check it — just verify the CRC is consistent, which
     * proves the VRU's response is well-formed. */
    const uint8_t cmd_b[3] = {0x0B, 0x00, 0x00};
    joybus_exec_cmd(port, 3, 3, cmd_b, rx);
    uint8_t expected_b = vru_crc(&rx[0], 2);
    if (rx[2] != expected_b) {
        return false;
    }

    return true;
}

/* Build the 80-byte joybus word buffer for a single dictionary entry.
 *
 * The VRU's read-status parser (see simple64's vru_controller.c) walks the
 * loaded 40-uint16_t buffer like this:
 *   - skip leading 0x0000 entries
 *   - find a 0x0003 marker
 *   - skip 3 more uint16_t (header bytes the game uses for word metadata —
 *     we just write zeros there)
 *   - read one uint16_t LENGTH
 *   - the next LENGTH uint16_t entries are the phoneme stream
 *
 * Everything is big-endian on the wire because that's how the N64
 * marshals uint16_t to joybus. */
static void vru_build_word_buffer (const uint16_t *phonemes, int count,
                                   uint8_t buf[80]) {
    memset(buf, 0, 80);
    /* word[0] = 0x0003 marker */
    buf[0] = 0x00;
    buf[1] = 0x03;
    /* word[1], word[2] = 0  (already zero-padded) */
    /* word[3] = LENGTH (count of phonemes), at byte offset 6 */
    buf[6] = (uint8_t)((count >> 8) & 0xFF);
    buf[7] = (uint8_t)( count       & 0xFF);
    /* word[4..] = phonemes, starting at byte offset 8 */
    int max_phonemes = (80 - 8) / 2;
    if (count > max_phonemes) count = max_phonemes;
    for (int i = 0; i < count; i++) {
        buf[8 + i*2 + 0] = (uint8_t)((phonemes[i] >> 8) & 0xFF);
        buf[8 + i*2 + 1] = (uint8_t)( phonemes[i]       & 0xFF);
    }
}

/* Upload one word to the VRU.
 *
 * Sequence:
 *   1. 0x0C "clear, expect N words" — sub-command 0x02 in payload byte 0.
 *   2. Four back-to-back 0x0A (JCMD_VRU_WRITE) calls, each carrying 20 of
 *      the 80 buffer bytes.  Each one returns the VRU-computed CRC of the
 *      20 payload bytes; we match it against our local CRC.
 *   3. 0x0B (JCMD_VRU_READ_STATUS) — this is what triggers the VRU to
 *      actually parse the accumulated buffer.  Returns {state, 0, crc}.
 *
 * Returns true only if every CRC verification matches. */
static bool vru_upload_word (int port, const uint16_t *phonemes, int count) {
    /* Step 1: clear dictionary, expect 1 word. */
    const uint8_t clear_cmd[7] = { 0x0C, 0x00, 0x00,  0x02, 0x00, 0x01, 0x00 };
    uint8_t rx1[1] = {0};
    joybus_exec_cmd(port, 7, 1, clear_cmd, rx1);
    if (rx1[0] != vru_crc(&clear_cmd[3], 4)) return false;

    /* Step 2: four 20-byte chunks of word buffer. */
    uint8_t buf[80];
    vru_build_word_buffer(phonemes, count, buf);

    for (int chunk = 0; chunk < 4; chunk++) {
        uint8_t tx[23];
        tx[0] = 0x0A;
        tx[1] = 0x00;
        tx[2] = 0x00;
        memcpy(&tx[3], &buf[chunk * 20], 20);
        uint8_t rx[1] = {0};
        joybus_exec_cmd(port, 23, 1, tx, rx);
        if (rx[0] != vru_crc(&tx[3], 20)) return false;
    }

    /* Step 3: trigger parse. */
    const uint8_t parse_cmd[3] = { 0x0B, 0x00, 0x00 };
    uint8_t rx3[3] = {0};
    joybus_exec_cmd(port, 3, 3, parse_cmd, rx3);
    if (rx3[2] != vru_crc(&rx3[0], 2)) return false;

    return true;
}

void vru_poll (void) {
    int port = find_vru_port();
    if (port < 0) {
        /* Lost / never had a VRU — reset to absent. */
        present_port    = -1;
        presence        = VRU_PRESENCE_ABSENT;
        detected_frames = 0;
        load_attempted  = false;
        return;
    }

    /* VRU is on the bus.  Cache the port. */
    present_port = port;

    if (presence == VRU_PRESENCE_ABSENT) {
        presence        = VRU_PRESENCE_DETECTED;
        detected_frames = 0;
        load_attempted  = false;
        return;
    }

    if (presence == VRU_PRESENCE_DETECTED) {
        if (++detected_frames < INIT_DELAY_FRAMES) {
            return;
        }
        /* One-shot init.  Whichever way it goes, latch the result so we
         * don't re-poke the VRU every frame and stomp on its state. */
        presence = vru_initialize(port) ? VRU_PRESENCE_READY
                                        : VRU_PRESENCE_INIT_FAILED;
        return;
    }

    /* Once init succeeded, try a one-shot upload of the test word so we
     * can verify the encoding + CRC plumbing actually round-trips.  Don't
     * retry on failure — the VRU's internal state would get stomped. */
    if (presence == VRU_PRESENCE_READY && !load_attempted) {
        load_attempted = true;
        if (vru_upload_word(port, test_word_up, TEST_WORD_UP_LEN)) {
            presence = VRU_PRESENCE_LOADED;
        }
    }
}

bool vru_is_present (void) {
    return present_port >= 0;
}

int vru_get_port (void) {
    return present_port;
}

vru_presence_t vru_get_presence (void) {
    return presence;
}
