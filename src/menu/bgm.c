/**
 * @file bgm.c
 * @brief Background Music Player implementation
 * @ingroup menu
 */

#include <libdragon.h>
#include <string.h>
#include <stdlib.h>

#include "bgm.h"
#include "mp3_player.h"
#include "sound.h"

static char *bgm_files[BGM_MAX_FILES];
static int bgm_order[BGM_MAX_FILES];
static int bgm_file_count = 0;
static int bgm_current_index = 0;
static int bgm_track_index = -1;   /* -1 = shuffle, >=0 = pinned track */

static bool bgm_enabled = false;
static bool bgm_active = false;    /* mp3player is initialised and playing */
static bool bgm_suspended = false; /* paused while music player view is open */

static void bgm_shuffle(void) {
    for (int i = bgm_file_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = bgm_order[i];
        bgm_order[i] = bgm_order[j];
        bgm_order[j] = tmp;
    }
    bgm_current_index = 0;
}

static void bgm_play_track(void) {
    if (bgm_file_count == 0) {
        return;
    }

    /* Pinned track: play exactly that file, no fallback cycling */
    if (bgm_track_index >= 0 && bgm_track_index < bgm_file_count) {
        if (mp3player_init() == MP3PLAYER_OK) {
            if (mp3player_load(bgm_files[bgm_track_index]) == MP3PLAYER_OK) {
                sound_init_mp3_playback();
                if (mp3player_play() == MP3PLAYER_OK) {
                    bgm_active = true;
                    return;
                }
            }
            mp3player_deinit();
        }
        bgm_active = false;
        return;
    }

    /* Shuffle: try each track in order until one plays */
    for (int tries = 0; tries < bgm_file_count; tries++) {
        char *path = bgm_files[bgm_order[bgm_current_index]];

        if (mp3player_init() == MP3PLAYER_OK) {
            if (mp3player_load(path) == MP3PLAYER_OK) {
                sound_init_mp3_playback();
                if (mp3player_play() == MP3PLAYER_OK) {
                    bgm_active = true;
                    return;
                }
            }
            mp3player_deinit();
        }

        bgm_current_index = (bgm_current_index + 1) % bgm_file_count;
        if (bgm_current_index == 0) {
            bgm_shuffle();
        }
    }

    bgm_active = false;
}

void bgm_init(const char *music_dir) {
    bgm_file_count = 0;

    if (!music_dir) {
        return;
    }

    dir_t dir;
    char path_buf[512];

    if (dir_findfirst(music_dir, &dir) != 0) {
        return;
    }

    do {
        if (dir.d_type != DT_REG) {
            continue;
        }
        if (bgm_file_count >= BGM_MAX_FILES) {
            break;
        }
        char *dot = strrchr(dir.d_name, '.');
        if (!dot || strcasecmp(dot + 1, "mp3") != 0) {
            continue;
        }
        snprintf(path_buf, sizeof(path_buf), "%s/%s", music_dir, dir.d_name);
        bgm_files[bgm_file_count] = strdup(path_buf);
        bgm_order[bgm_file_count] = bgm_file_count;
        bgm_file_count++;
    } while (dir_findnext(music_dir, &dir) == 0);

    if (bgm_file_count > 0) {
        srand((unsigned int) timer_ticks());
        bgm_shuffle();
    }
}

int bgm_get_file_count(void) {
    return bgm_file_count;
}

const char *bgm_get_basename(int index) {
    if (index < 0 || index >= bgm_file_count) return NULL;
    const char *path = bgm_files[index];
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void bgm_set_track(int index) {
    bgm_track_index = (index >= 0 && index < bgm_file_count) ? index : -1;
    if (bgm_enabled && bgm_active) {
        mp3player_deinit();
        bgm_active = false;
        /* bgm_process will restart with new selection */
    }
}

int bgm_get_current_track(void) {
    return bgm_track_index;
}

void bgm_set_track_by_name(const char *name) {
    if (!name || name[0] == '\0') {
        bgm_track_index = -1;
        return;
    }
    for (int i = 0; i < bgm_file_count; i++) {
        const char *base = bgm_get_basename(i);
        if (base && strcasecmp(base, name) == 0) {
            bgm_track_index = i;
            return;
        }
    }
    bgm_track_index = -1;
}

void bgm_deinit(void) {
    if (bgm_active) {
        mp3player_deinit();
        bgm_active = false;
    }
    for (int i = 0; i < bgm_file_count; i++) {
        free(bgm_files[i]);
        bgm_files[i] = NULL;
    }
    bgm_file_count = 0;
}

void bgm_set_enabled(bool enabled) {
    bgm_enabled = enabled;

    if (bgm_enabled && !bgm_active && !bgm_suspended && bgm_file_count > 0) {
        bgm_play_track();
    } else if (!bgm_enabled && bgm_active) {
        mp3player_deinit();
        bgm_active = false;
    }
}

void bgm_suspend(void) {
    if (bgm_active) {
        mp3player_deinit();
        bgm_active = false;
    }
    bgm_suspended = true;
}

void bgm_resume(void) {
    bgm_suspended = false;
    if (bgm_enabled && bgm_file_count > 0) {
        bgm_play_track();
    }
}

void bgm_process(void) {
    if (!bgm_enabled || bgm_suspended || bgm_file_count == 0) {
        return;
    }

    if (bgm_active) {
        mp3player_err_t err = mp3player_process();
        if (err != MP3PLAYER_OK || mp3player_is_finished()) {
            mp3player_deinit();
            bgm_active = false;
            if (bgm_track_index < 0) {
                /* Shuffle: advance to next track */
                bgm_current_index = (bgm_current_index + 1) % bgm_file_count;
                if (bgm_current_index == 0) {
                    bgm_shuffle();
                }
            }
            /* Pinned track: bgm_play_track will restart the same file */
            bgm_play_track();
        }
    } else {
        bgm_play_track();
    }
}
