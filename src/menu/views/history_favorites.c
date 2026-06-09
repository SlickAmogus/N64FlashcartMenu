#include <stdarg.h>
#include "../bookkeeping.h"
#include "../fonts.h"
#include "../rom_info.h"
#include "../ui_components/constants.h"
#include "../sound.h"
#include "views.h"


typedef enum {
    BOOKKEEPING_TAB_CONTEXT_HISTORY,
    BOOKKEEPING_TAB_CONTEXT_FAVORITE,
    BOOKKEEPING_TAB_CONTEXT_NONE
} bookkeeping_tab_context_t;


static bookkeeping_tab_context_t tab_context = BOOKKEEPING_TAB_CONTEXT_NONE;
static int selected_item = -1;
static bookkeeping_item_t *item_list;
static uint16_t item_max = 0;

/* Spring animation */
#define CURSOR_SPRING_RATE  (7.0f)
#define ROW_HEIGHT          (38)

static float    s_cur_y         = -1.0f;
static uint32_t s_cur_last_ms   = 0;
static int      s_prev_selected = -1;

/* Thumbnail boxart for the selected entry.  Two-slot design:
 *   - thumb_curr is what we're currently displaying
 *   - thumb_next is the in-flight async PNG decode for the new selection
 * When thumb_next finishes loading we promote it to thumb_curr, so the
 * previous boxart stays visible during the brief decode window instead of
 * flashing a black placeholder.  Loading is debounced too: only fires
 * after the cursor has been still for THUMB_DEBOUNCE_MS, so rapid
 * scrolling never triggers a load. */
#define THUMB_DEBOUNCE_MS   (350)

/* rom_info_t cache so we don't re-read each entry's ROM header on every
 * revisit.  Bookkeeping lists are tiny (8 entries), so the cache is too. */
#define MAX_CACHED_INFOS    (16)

static component_boxart_t *thumb_curr       = NULL;
static component_boxart_t *thumb_next       = NULL;
static int                 thumb_loaded_for = -1;
static uint32_t            thumb_request_ms = 0;   /* 0 = no pending request */

static rom_info_t cached_info[MAX_CACHED_INFOS];
static bool       cached_info_valid[MAX_CACHED_INFOS];

static void invalidate_info_cache (void) {
    for (int i = 0; i < MAX_CACHED_INFOS; i++) cached_info_valid[i] = false;
}

static bool fetch_rom_info (int idx, rom_info_t *out) {
    if (idx < 0 || idx >= MAX_CACHED_INFOS) {
        return rom_config_load(item_list[idx].primary_path, out) == ROM_OK;
    }
    if (cached_info_valid[idx]) {
        *out = cached_info[idx];
        return true;
    }
    if (rom_config_load(item_list[idx].primary_path, out) != ROM_OK) {
        return false;
    }
    cached_info[idx]       = *out;
    cached_info_valid[idx] = true;
    return true;
}

static void clear_thumb_curr (void) {
    if (thumb_curr) { ui_components_boxart_free(thumb_curr); thumb_curr = NULL; }
}

static void clear_thumb_next (void) {
    if (thumb_next) { ui_components_boxart_free(thumb_next); thumb_next = NULL; }
}

static void load_thumb_now (menu_t *menu) {
    clear_thumb_next();
    thumb_loaded_for = selected_item;

    if (selected_item < 0 || selected_item >= item_max) { clear_thumb_curr(); return; }
    bookkeeping_item_t *item = &item_list[selected_item];
    if (item->bookkeeping_type == BOOKKEEPING_TYPE_EMPTY) { clear_thumb_curr(); return; }
    if (!path_has_value(item->primary_path))               { clear_thumb_curr(); return; }

    rom_info_t info;
    if (!fetch_rom_info(selected_item, &info)) { clear_thumb_curr(); return; }

    thumb_next = ui_components_boxart_init(menu->storage_prefix, info.game_code, info.title, IMAGE_BOXART_FRONT);
    /* If init returned NULL (no boxart PNG on disk), drop the old one so
     * the empty space communicates "no art for this entry". */
    if (!thumb_next) clear_thumb_curr();
}

/* Promote the freshly-loaded thumb_next into the currently-shown slot.
 * Called from process() each frame so the swap happens as soon as the
 * async decode completes. */
static void promote_thumb_if_ready (void) {
    if (thumb_next && !thumb_next->loading) {
        clear_thumb_curr();
        thumb_curr = thumb_next;
        thumb_next = NULL;
    }
}

/* Mark the current selection as the one we eventually want a thumb for.
 * The actual load happens later in draw() once THUMB_DEBOUNCE_MS has
 * elapsed since the last request — so rapid scrolling never starts a
 * load that will be immediately aborted. */
static void request_thumb (void) {
    thumb_request_ms = get_ticks_ms();
    if (thumb_request_ms == 0) thumb_request_ms = 1;  /* 0 reserved for "no request" */
}

static void item_reset_selected (menu_t *menu) {
    selected_item = -1;

    for (uint16_t i = 0; i < item_max; i++) {
        if (item_list[i].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            selected_item = i;
            break;
        }
    }

    s_cur_y         = -1.0f;
    s_cur_last_ms   = 0;
    s_prev_selected = -1;
    /* Tab switch / favorite removal changes which list item_list points
     * at, so any cached rom_info entries are stale. */
    invalidate_info_cache();
    /* On view (re)entry we load synchronously — there's no rapid scrolling
     * concern, and waiting THUMB_DEBOUNCE_MS for the first thumb feels sluggish. */
    thumb_request_ms = 0;
    load_thumb_now(menu);
}

static void item_move_next (menu_t *menu) {
    int last = selected_item;
    (void)menu;

    do {
        selected_item++;

        if (selected_item >= item_max) {
            selected_item = last;
            break;
        } else if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            request_thumb();
            break;
        }
    } while (true);
}

static void item_move_previous (menu_t *menu) {
    int last = selected_item;
    (void)menu;

    do {
        selected_item--;

        if (selected_item < 0) {
            selected_item = last;
            break;
        } else if (item_list[selected_item].bookkeeping_type != BOOKKEEPING_TYPE_EMPTY) {
            sound_play_effect(SFX_CURSOR);
            request_thumb();
            break;
        }
    } while (true);
}

static void process (menu_t *menu) {
    /* Hand off completed async loads to the displayed slot. */
    promote_thumb_if_ready();

    bool any_input_now =
        menu->actions.go_up    || menu->actions.go_down  ||
        menu->actions.go_left  || menu->actions.go_right ||
        menu->actions.go_fast  || menu->actions.enter    ||
        menu->actions.back     || menu->actions.options  ||
        menu->actions.settings || menu->actions.lz_context;

    /* Fire any pending debounced thumbnail load — but only when the user
     * has truly stopped: debounce window must have elapsed AND there must
     * be no input this frame.  This second check kills the hitch that
     * fired during the brief gap between auto-repeat ticks. */
    if (thumb_request_ms != 0 && selected_item != thumb_loaded_for && !any_input_now) {
        uint32_t now = get_ticks_ms();
        if (now - thumb_request_ms >= THUMB_DEBOUNCE_MS) {
            load_thumb_now(menu);
            thumb_request_ms = 0;
        }
    }

    if (menu->actions.go_down) {
        item_move_next(menu);
    } else if (menu->actions.go_up) {
        item_move_previous(menu);
    } else if (menu->actions.enter && selected_item != -1) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->load.load_favorite_id = selected_item;
            menu->load.load_history_id = -1;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->load.load_history_id = selected_item;
            menu->load.load_favorite_id = -1;
        }

        if (item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_DISK) {
            menu->next_mode = MENU_MODE_LOAD_DISK;
            sound_play_effect(SFX_ENTER);
        } else if (item_list[selected_item].bookkeeping_type == BOOKKEEPING_TYPE_ROM) {
            menu->next_mode = MENU_MODE_LOAD_ROM;
            sound_play_effect(SFX_ENTER);
        }
    } else if (menu->actions.go_left) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_HISTORY;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_BROWSER;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (menu->actions.go_right) {
        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
            menu->next_mode = MENU_MODE_BROWSER;
        } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
            menu->next_mode = MENU_MODE_FAVORITE;
        }
        sound_play_effect(SFX_CURSOR);
    } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && menu->actions.options && selected_item != -1) {
        bookkeeping_favorite_remove(&menu->bookkeeping, selected_item);
        item_reset_selected(menu);
        sound_play_effect(SFX_SETTING);
    }
}

static void draw_list (menu_t *menu, surface_t *display) {
    int list_y0 = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL;
    uint32_t now_ms = get_ticks_ms();

    if (selected_item != -1) {
        int target_y = list_y0 + selected_item * ROW_HEIGHT;

        if (s_cur_y < 0.0f) {
            s_cur_y         = (float)target_y;
            s_cur_last_ms   = now_ms;
            s_prev_selected = selected_item;
        } else if (selected_item != s_prev_selected) {
            s_cur_y         = (float)(list_y0 + s_prev_selected * ROW_HEIGHT);
            s_prev_selected = selected_item;
            s_cur_last_ms   = now_ms;
        }
        {
            float cur_dt = (float)(now_ms - s_cur_last_ms) / 1000.0f;
            if (cur_dt > 0.1f) cur_dt = 0.1f;
            float k = cur_dt * CURSOR_SPRING_RATE;
            if (k > 1.0f) k = 1.0f;
            s_cur_y      += ((float)target_y - s_cur_y) * k;
            s_cur_last_ms = now_ms;
        }
        int highlight_y = (int)(s_cur_y + 0.5f);

        ui_components_box_draw(
            VISIBLE_AREA_X0,
            highlight_y,
            VISIBLE_AREA_X0 + FILE_LIST_HIGHLIGHT_WIDTH + LIST_SCROLLBAR_WIDTH,
            highlight_y + ROW_HEIGHT + 1,
            FILE_LIST_HIGHLIGHT_COLOR
        );
    }

    char buffer[1024];
    buffer[0] = 0;

    for (uint16_t i = 0; i < item_max; i++) {
        if (path_has_value(item_list[i].primary_path)) {
            sprintf(buffer, "%s%d  : %s\n", buffer, (i + 1), path_last_get(item_list[i].primary_path));
        } else {
            sprintf(buffer, "%s%d  : \n", buffer, (i + 1));
        }

        if (path_has_value(item_list[i].secondary_path)) {
            sprintf(buffer, "%s     %s\n", buffer, path_last_get(item_list[i].secondary_path));
        } else {
            sprintf(buffer, "%s\n", buffer);
        }
    }

    int nbytes = strlen(buffer);
    rdpq_text_printn(
        &(rdpq_textparms_t) {
            .width  = BOXART_X - VISIBLE_AREA_X0 - (TEXT_MARGIN_HORIZONTAL * 2) - 8,
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - OVERSCAN_HEIGHT - (TEXT_MARGIN_VERTICAL * 2),
            .align  = ALIGN_LEFT,
            .valign = VALIGN_TOP,
            .wrap   = WRAP_ELLIPSES,
            .line_spacing = TEXT_OFFSET_VERTICAL,
        },
        FNT_DEFAULT,
        VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL,
        list_y0,
        buffer,
        nbytes
    );
}

static void draw (menu_t *menu, surface_t *display) {
    rdpq_attach(display, NULL);

    ui_components_background_draw();

    if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE) {
        ui_components_tabs_common_draw(2);
    } else if (tab_context == BOOKKEEPING_TAB_CONTEXT_HISTORY) {
        ui_components_tabs_common_draw(1);
    }

    ui_components_layout_draw_tabbed();

    draw_list(menu, display);

    if (thumb_curr) {
        ui_components_boxart_draw(thumb_curr);
    }

    if (selected_item != -1) {
        ui_components_actions_bar_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "A: Load Game\n"
            "\n"
        );

        if (tab_context == BOOKKEEPING_TAB_CONTEXT_FAVORITE && selected_item != -1) {
            ui_components_actions_bar_text_draw(
                STL_DEFAULT,
                ALIGN_RIGHT, VALIGN_TOP,
                "R: Remove item\n"
                "\n"
            );
        }
    }

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "◀ Change Tab ▶\n"
        "\n"
    );

    ui_components_mic_indicator_draw();

    rdpq_detach_show();
}

void view_favorite_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_FAVORITE;
    item_list   = menu->bookkeeping.favorite_items;
    item_max    = FAVORITES_COUNT;

    item_reset_selected(menu);
}

void view_favorite_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}

void view_history_init (menu_t *menu) {
    tab_context = BOOKKEEPING_TAB_CONTEXT_HISTORY;
    item_list   = menu->bookkeeping.history_items;
    item_max    = HISTORY_COUNT;

    item_reset_selected(menu);
}

void view_history_display (menu_t *menu, surface_t *display) {
    process(menu);
    draw(menu, display);
}
