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

/* Recognition cycle, modelled on the SDK's listen-and-fetch flow as
 * reverse-engineered (zoinkity).  The original game traffic per cycle is:
 *
 *   0x0B  state read         — mode (low 3 bits) must be 0 before starting;
 *                              modes 1/3/5 (and error mode 7) get kicked
 *                              back to 0 with a 0x0C {00 00 07 00} reset
 *   0x0C  {00 00 06 00}      — start listening
 *   0x09  result read        — ONE read, only once a completed result
 *                              exists.  Reading mid-capture returns a
 *                              "busy" filler block (00 80 repeated, with
 *                              valid CRC) which must not be parsed as a
 *                              result.
 *   0x0C  {00 00 07 00}      — post-result state reset (cmd 7)
 *   0x0C  {05 00 00 00}      — stop listening
 *   …repeat
 *
 * Each vru_poll call performs at most one joybus transaction. */
typedef enum {
    CYC_IDLE              = 0,
    CYC_CHECK             = 1,  /* read 0x0B; decide START vs CMD7 reset    */
    CYC_CMD7              = 2,  /* send 0x0C {00 00 07 00}                  */
    CYC_CMD7_POLL         = 3,  /* poll 0x0B until mode returns to 0        */
    CYC_START             = 4,  /* send 0x0C {00 00 06 00}                  */
    CYC_WAIT              = 5,  /* poll 0x09 until a completed result shows */
    CYC_CMD7_AFTER_RESULT = 6,  /* post-result reset before stopping        */
    CYC_STOP              = 7,  /* send 0x0C {05 00 00 00}                  */
} cycle_state_t;

static cycle_state_t cyc_state       = CYC_IDLE;
static int           cyc_timer       = 0;
static int           cmd7_retries    = 0;
static int           junk_reads      = 0;
static vru_hit_t     pending_hit     = VRU_HIT_NONE;
static uint32_t      last_dispatch_ms = 0;

/* Frames between polled joybus reads (0x0B in CHECK/CMD7_POLL, 0x09 in
 * WAIT).  Joybus exec blocks ~hundreds of µs but flooding it competes
 * with libdragon's background joypad polling, so keep it modest. */
#define CYCLE_POLL_INTERVAL    (5)

/* After this many consecutive busy/junk 0x09 reads (~30 s of silence),
 * stop and restart the session to un-wedge any stuck state. */
#define MAX_JUNK_READS         (60)

/* Reject matches whose deviance exceeds the SDK's default acceptance
 * threshold — higher deviance = worse match. */
#define DEVIANCE_THRESHOLD     (0x0640)

/* Minimum gap between dispatched voice actions.  Acts as a safety
 * valve: even if the VRU misbehaves and reports phantom matches, the
 * menu stays navigable because at most one synthetic input fires every
 * couple of seconds. */
#define DISPATCH_COOLDOWN_MS   (2000)

/* Navigation vocabulary, encoded against the Hey-You-Pikachu USA phoneme
 * alphabet (each entry is a 16-bit code, all codes are multiples of 3
 * per the alphabet's encoding scheme — see USA VRU Sound Indices doc).
 *
 * The dictionary index assigned by the VRU is the order we upload, so
 * once recognition wakes up, hit_index == VRU_WORD_* tells us which
 * navigation action to take.
 *
 * Encodings are educated guesses from the alphabet's example notes —
 * may need tuning once we observe what the VRU actually matches against.
 *
 *   "UP"     — vowel-start marker for /uh/, /uh/, /p/-stop
 *   "DOWN"   — /d/, /aow/-diphthong, /n/
 *   "LEFT"   — /l/, /e/-as-in-let, /f/, /t/-final
 *   "RIGHT"  — /r/-initial, /eye/-diphthong, /t/-final
 *   "OK"     — vowel-start marker for /o/, /o/-as-in-okay, /k/, long /ay/ */
static const uint16_t vru_word_up[]    = { 0x0432, 0x00A8, 0x03A8 };
static const uint16_t vru_word_down[]  = { 0x0384, 0x01EF, 0x0348 };
static const uint16_t vru_word_left[]  = { 0x02E2, 0x0069, 0x03ED, 0x0003, 0x03C6 };
static const uint16_t vru_word_right[] = { 0x02FD, 0x01B0, 0x0003, 0x03C6 };
static const uint16_t vru_word_ok[]    = { 0x043B, 0x0213, 0x03CC, 0x018F };

typedef struct {
    const uint16_t *phonemes;
    uint8_t         count;
} vru_word_t;

static const vru_word_t vru_dictionary[] = {
    { vru_word_up,    sizeof(vru_word_up)    / sizeof(uint16_t) },
    { vru_word_down,  sizeof(vru_word_down)  / sizeof(uint16_t) },
    { vru_word_left,  sizeof(vru_word_left)  / sizeof(uint16_t) },
    { vru_word_right, sizeof(vru_word_right) / sizeof(uint16_t) },
    { vru_word_ok,    sizeof(vru_word_ok)    / sizeof(uint16_t) },
};
#define VRU_DICT_SIZE   (sizeof(vru_dictionary) / sizeof(vru_dictionary[0]))

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
 * Per the reverse-engineered SDK pseudo-code (zoinkity's notes) the buffer
 * is RIGHT-aligned and the on-wire format is 16-bit LITTLE-ENDIAN:
 *
 *   [zeros padding to start of marker]      bytes 0..(K-1)
 *   [marker 0x0003 LE = bytes 03 00]        bytes K..K+1
 *   [padding word 0x0000]                   bytes K+2..K+3
 *   [phoneme words, each 2 bytes LE]        bytes K+4..(78-1)
 *   [terminator word 0x0000]                bytes 78..79
 *
 * K is chosen so the data ends exactly at byte 77 (just before the
 * trailing 0x0000 terminator at bytes 78-79).  For N phonemes, that
 * means data starts at byte 78 - 2*N and the marker word at
 * byte 78 - 2*N - 4.
 *
 * (My earlier left-aligned big-endian layout matched the simple64
 *  *emulator*'s parser but not the actual VRU silicon — the CRC echo
 *  comes back fine either way because the VRU computes the CRC over
 *  whatever bytes we send, regardless of whether they're meaningful.) */
static void vru_build_word_buffer (const uint16_t *phonemes, int count,
                                   uint8_t buf[80]) {
    memset(buf, 0, 80);

    int data_end   = 78;                    /* exclusive — leaves bytes 78-79 as terminator */
    int data_start = data_end - 2 * count;
    int marker_at  = data_start - 4;        /* marker + 1 padding word */

    if (marker_at < 0) return;              /* word too long to fit */

    /* Marker 0x0003, little-endian on wire. */
    buf[marker_at + 0] = 0x03;
    buf[marker_at + 1] = 0x00;
    /* Padding word at marker_at+2..+3 stays zero. */

    /* Phoneme stream, each code little-endian on wire. */
    for (int i = 0; i < count; i++) {
        buf[data_start + 2*i + 0] = (uint8_t)( phonemes[i]       & 0xFF);
        buf[data_start + 2*i + 1] = (uint8_t)((phonemes[i] >> 8) & 0xFF);
    }
    /* Trailing terminator at bytes 78-79 stays zero. */
}

/* Read the 0x0B (status) reply and return the 16-bit state value, or
 * UINT16_MAX on CRC mismatch.  Wire layout is {state_hi, state_lo, crc}
 * — i.e. the state byte order on the wire is big-endian. */
static uint16_t vru_read_state (int port) {
    const uint8_t cmd[3] = { 0x0B, 0x00, 0x00 };
    uint8_t rx[3] = {0};
    joybus_exec_cmd(port, 3, 3, cmd, rx);
    if (rx[2] != vru_crc(&rx[0], 2)) return UINT16_MAX;
    return ((uint16_t)rx[0] << 8) | rx[1];
}

/* Send a single 0x0C "config" command with a 4-byte payload and validate
 * the VRU's CRC echo.  Common payloads (per the protocol notes):
 *   { 0x02, 0x00, N,    0x00 }  clear dictionary, expect N words
 *   { 0x00, 0x00, 0x07, 0x00 }  pre-write state transition
 *   { 0x00, 0x00, 0x06, 0x00 }  start listening
 *   { 0x05, 0x00, 0x00, 0x00 }  stop listening */
static bool vru_send_config (int port, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    const uint8_t tx[7] = { 0x0C, 0x00, 0x00, b0, b1, b2, b3 };
    uint8_t rx[1] = {0};
    joybus_exec_cmd(port, 7, 1, tx, rx);
    return rx[0] == vru_crc(&tx[3], 4);
}

/* Push the 80-byte word buffer as four back-to-back 0x0A writes.  Each
 * one's response is a CRC of the 20 payload bytes we sent. */
static bool vru_write_word_buffer (int port, const uint8_t buf[80]) {
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
    return true;
}

/* Upload the full navigation dictionary and verify semantically.
 *
 * Sequence (matches the SDK's strings-send-for-E path):
 *   1. 0x0C { 0x02, 0x00, N, 0x00 } — clear, expect N words.
 *   2. 0x0B status read — sanity check, must not have high-byte error bits.
 *   3. For each word in dictionary:
 *        - build a right-aligned LE word buffer
 *        - push it as four 0x0A writes
 *        - read state via 0x0B; reject if error bits 0x0100 / 0x0200 /
 *          0xFE00 are set (buffer overflow / invalid word / etc.).
 *
 * The post-write state check is the part the previous version was
 * missing — without it a malformed word would silently slip through
 * because the per-chunk CRCs would still validate. */
static bool vru_upload_dictionary (int port) {
    /* Step 1: clear & advertise the word count. */
    if (!vru_send_config(port, 0x02, 0x00, (uint8_t)VRU_DICT_SIZE, 0x00)) return false;

    /* Step 2: status read after clear; the state high byte must show no
     * error bits.  0x0100 (USA-region flag) is allowed because the doc
     * says it's set after init on US ROMs and isn't an error here. */
    uint16_t state = vru_read_state(port);
    if (state == UINT16_MAX)        return false;
    if (state & 0xFE40)             return false;  /* error bits or "slot empty" */

    /* Step 3: upload each word. */
    for (int i = 0; i < (int)VRU_DICT_SIZE; i++) {
        uint8_t buf[80];
        vru_build_word_buffer(vru_dictionary[i].phonemes,
                              vru_dictionary[i].count, buf);
        if (!vru_write_word_buffer(port, buf)) return false;

        state = vru_read_state(port);
        if (state == UINT16_MAX)    return false;
        if (state & 0x0100)         return false;  /* buffer overflow */
        if (state & 0x0200)         return false;  /* invalid word */
        if (state & 0xFE00)         return false;  /* other high-byte error */
    }

    return true;
}

/* Read the 0x09 result block (37 bytes: 4 header + 30 result + 2 mode + 1
 * CRC) and return the hit_1 index, or 0x7FFF on "no match" / error.
 *
 * Result block layout (little-endian on wire per protocol notes):
 *   0x00..0x03  header (US: 80 00 0F 00 typically)
 *   0x04..0x05  error flags
 *   0x06..0x07  # valid results
 *   0x08..0x09  voice level from mic
 *   0x0A..0x0B  relative voice level
 *   0x0C..0x0D  voice length (~ms)
 *   0x0E..0x0F  hit 1 index (or 0x7FFF)
 *   0x10..0x11  hit 1 deviance
 *   0x12..0x21  hits 2-5 (index/deviance pairs)
 *   0x22..0x23  mode + status flags
 *   0x24        data CRC */
static vru_debug_info_t last_debug = {0};

typedef enum {
    RESULT_NOT_READY,   /* CRC fail, busy filler, or incomplete (mode != 0) */
    RESULT_COMPLETE,    /* structurally valid, completed result block       */
} result_status_t;

/* Read the 0x09 result block once.  Distinguishes "structure" (is this a
 * real, completed result block at all?) from "quality" (did the speech
 * actually match a word?) — the caller decides what to do with quality.
 * Mid-capture, the VRU answers with a CRC-valid filler pattern of
 * "00 80" repeated; that and any block whose mode bits aren't 0 count
 * as NOT_READY. */
static result_status_t vru_read_result (int port) {
    const uint8_t cmd[3] = { 0x09, 0x00, 0x00 };
    uint8_t rx[37] = {0};
    joybus_exec_cmd(port, 3, 37, cmd, rx);

    /* CRC over the 36 data bytes. */
    if (rx[36] != vru_crc(&rx[0], 36)) return RESULT_NOT_READY;

    /* Capture every field BEFORE structural rejects, so the debug
     * overlay shows what the VRU actually said even when it's filler. */
    last_debug.has_data       = true;
    last_debug.err_flags      = (uint16_t)rx[0x04] | ((uint16_t)rx[0x05] << 8);
    last_debug.valid_count    = (uint16_t)rx[0x06] | ((uint16_t)rx[0x07] << 8);
    last_debug.voice_level    = (uint16_t)rx[0x08] | ((uint16_t)rx[0x09] << 8);
    last_debug.rel_level      = (uint16_t)rx[0x0A] | ((uint16_t)rx[0x0B] << 8);
    last_debug.voice_length   = (uint16_t)rx[0x0C] | ((uint16_t)rx[0x0D] << 8);
    last_debug.hit_1_index    = (uint16_t)rx[0x0E] | ((uint16_t)rx[0x0F] << 8);
    last_debug.hit_1_deviance = (uint16_t)rx[0x10] | ((uint16_t)rx[0x11] << 8);
    last_debug.mode_status    = (uint16_t)rx[0x22] | ((uint16_t)rx[0x23] << 8);

    /* Real result blocks start with header byte 0x80 (US: 80 00 0F 00).
     * Anything else — all zeros, the 00 80 busy filler, etc. — is not a
     * completed result. */
    if (rx[0] != 0x80) return RESULT_NOT_READY;

    /* Result is only complete when the mode bits (low 3 of the status
     * word at 0x22) have returned to 0; the 0x40 flag should be set on
     * a normal block. */
    if (last_debug.mode_status == 0)       return RESULT_NOT_READY;
    if (last_debug.mode_status & 0x0007)   return RESULT_NOT_READY;

    return RESULT_COMPLETE;
}

/* Decide whether a COMPLETE result is a confident match worth acting
 * on, and if so latch it into pending_hit (subject to a cooldown so a
 * misbehaving VRU can never lock the user out of the menu). */
static void vru_maybe_dispatch (void) {
    if (last_debug.err_flags & 0xCC00) return;   /* too low/high, no match, noisy */
    if (last_debug.valid_count == 0)   return;
    if (last_debug.hit_1_index >= VRU_DICT_SIZE) return;
    if (last_debug.hit_1_deviance > DEVIANCE_THRESHOLD) return;

    uint32_t now = get_ticks_ms();
    if (last_dispatch_ms != 0 && (now - last_dispatch_ms) < DISPATCH_COOLDOWN_MS) return;
    last_dispatch_ms = now;

    static const vru_hit_t map[VRU_DICT_SIZE] = {
        VRU_HIT_UP, VRU_HIT_DOWN, VRU_HIT_LEFT, VRU_HIT_RIGHT, VRU_HIT_OK,
    };
    pending_hit = map[last_debug.hit_1_index];
}

/* One step of the recognition cycle.  At most one joybus transaction
 * per call. */
static void vru_cycle_step (int port) {
    last_debug.cycle_state = (uint8_t)cyc_state;

    switch (cyc_state) {
        case CYC_IDLE:
            break;

        case CYC_CHECK: {
            if (++cyc_timer < CYCLE_POLL_INTERVAL) break;
            cyc_timer = 0;
            uint16_t state = vru_read_state(port);
            last_debug.state_0b = state;
            if (state == UINT16_MAX) break;          /* CRC bad — retry */
            switch (state & 0x0007) {
                case 0:  cyc_state = CYC_START; break;
                default: cyc_state = CYC_CMD7;  break;  /* 1/3/5/7 need a reset */
            }
            break;
        }

        case CYC_CMD7:
            if (vru_send_config(port, 0x00, 0x00, 0x07, 0x00)) {
                cyc_state    = CYC_CMD7_POLL;
                cmd7_retries = 0;
                cyc_timer    = 0;
            }
            break;

        case CYC_CMD7_POLL: {
            if (++cyc_timer < CYCLE_POLL_INTERVAL) break;
            cyc_timer = 0;
            uint16_t state = vru_read_state(port);
            last_debug.state_0b = state;
            if (state != UINT16_MAX && (state & 0x0007) == 0) {
                cyc_state = CYC_START;
            } else if (++cmd7_retries > 20) {
                cyc_state = CYC_CHECK;               /* give up; retry loop */
            }
            break;
        }

        case CYC_START:
            if (vru_send_config(port, 0x00, 0x00, 0x06, 0x00)) {
                cyc_state  = CYC_WAIT;
                cyc_timer  = 0;
                junk_reads = 0;
            } else {
                cyc_state = CYC_CHECK;
            }
            break;

        case CYC_WAIT:
            if (++cyc_timer < CYCLE_POLL_INTERVAL) break;
            cyc_timer = 0;
            if (vru_read_result(port) == RESULT_COMPLETE) {
                vru_maybe_dispatch();
                cyc_state = CYC_CMD7_AFTER_RESULT;
            } else if (++junk_reads > MAX_JUNK_READS) {
                cyc_state = CYC_STOP;                /* un-wedge stuck session */
            }
            break;

        case CYC_CMD7_AFTER_RESULT:
            /* Post-result reset, mirroring the SDK's cmd7-then-stop order. */
            vru_send_config(port, 0x00, 0x00, 0x07, 0x00);
            cyc_state = CYC_STOP;
            break;

        case CYC_STOP:
            if (vru_send_config(port, 0x05, 0x00, 0x00, 0x00)) {
                cyc_state = CYC_CHECK;
                cyc_timer = 0;
            }
            break;
    }
}

vru_hit_t vru_consume_hit (void) {
    vru_hit_t h = pending_hit;
    pending_hit = VRU_HIT_NONE;
    return h;
}

void vru_get_debug_info (vru_debug_info_t *out) {
    if (out) *out = last_debug;
}

void vru_poll (void) {
    int port = find_vru_port();
    if (port < 0) {
        /* Lost / never had a VRU — reset to absent. */
        present_port    = -1;
        presence        = VRU_PRESENCE_ABSENT;
        detected_frames = 0;
        load_attempted  = false;
        cyc_state       = CYC_IDLE;
        cyc_timer       = 0;
        pending_hit     = VRU_HIT_NONE;
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
        if (vru_upload_dictionary(port)) {
            presence  = VRU_PRESENCE_LOADED;
            cyc_state = CYC_CHECK;
        }
        return;
    }

    /* Drive the recognition cycle once per frame after the dictionary
     * is loaded.  Each call sends at most one joybus command so the
     * frame budget stays predictable. */
    if (presence == VRU_PRESENCE_LOADED) {
        vru_cycle_step(port);
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
