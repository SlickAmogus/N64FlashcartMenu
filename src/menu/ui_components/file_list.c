/**
 * @file file_list.c
 * @brief Implementation of the file list UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <string.h>

#include <libdragon.h>

#include "../ui_components.h"
#include "../fonts.h"
#include "constants.h"

static const char *directory_icon = "[DIR] ";

/* Pixel width available for file names in the paragraph layout */
#define TEXT_AREA_WIDTH     (FILE_LIST_MAX_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2))
/* Marquee scroll speed (pixels per second) */
#define MARQUEE_SPEED_PX_S  (80.0f)
/* Pause at each end of the marquee before reversing direction (milliseconds) */
#define MARQUEE_PAUSE_MS    (1500)

/* Marquee state — persists across draw calls so the scroll is continuous */
static float    s_mq_offset      = 0.0f;
static float    s_mq_max         = 0.0f;
static bool     s_mq_fwd         = true;
static uint32_t s_mq_pause_until = 0;
static uint32_t s_mq_last_ms     = 0;
static int      s_mq_idx         = -1;

static int format_file_size (char *buffer, int64_t size) {
    if (size < 0) {
        return sprintf(buffer, "unknown");
    } else if (size == 0) {
        return sprintf(buffer, "empty");
    } else if (size < 8 * 1024) {
        return sprintf(buffer, "%lld B", size);
    } else if (size < 4 * 1024 * 1024) {
        return sprintf(buffer, "%lld kB", size / 1024);
    } else if (size < 1 * 1024 * 1024 * 1024) {
        return sprintf(buffer, "%lld MB", size / 1024 / 1024);
    } else {
        return sprintf(buffer, "%lld GB", size / 1024 / 1024 / 1024);
    }
}

static menu_font_style_t style_for_entry (entry_type_t type) {
    switch (type) {
        case ENTRY_TYPE_DIR:    return STL_YELLOW;
        case ENTRY_TYPE_SAVE:   return STL_GREEN;
        case ENTRY_TYPE_IMAGE:
        case ENTRY_TYPE_MUSIC:  return STL_BLUE;
        case ENTRY_TYPE_TEXT:
        case ENTRY_TYPE_ARCHIVE: return STL_ORANGE;
        case ENTRY_TYPE_OTHER:  return STL_GRAY;
        default:                return STL_DEFAULT;
    }
}

void ui_components_file_list_draw (entry_t *list, int entries, int selected) {
    int starting_position = 0;

    if (entries > LIST_ENTRIES && selected >= (LIST_ENTRIES / 2)) {
        starting_position = selected - (LIST_ENTRIES / 2);
        if (starting_position >= entries - LIST_ENTRIES) {
            starting_position = entries - LIST_ENTRIES;
        }
    }

    ui_components_list_scrollbar_draw(selected, entries, LIST_ENTRIES);

    if (entries == 0) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_LEFT, VALIGN_TOP,
            "\n"
            "^%02X** empty directory **",
            STL_GRAY
        );
        return;
    }

    int sel_vis = selected - starting_position;
    entry_t *sel_entry = &list[selected];
    menu_font_style_t sel_style = style_for_entry(sel_entry->type);

    /* ------------------------------------------------------------------
     * Build a single-line no-wrap layout for the selected entry's name.
     * height=4096 forces initial builder.y = font->ascent (same as the
     * main paragraph which also has height != 0), so rendering at
     * highlight_y places the glyph top exactly at highlight_y — aligned
     * with every other row in the main list.  X offset drives the scroll.
     * ------------------------------------------------------------------ */
    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t){ .width = 4096, .wrap = WRAP_NONE, .height = 4096 },
        FNT_DEFAULT, NULL
    );
    rdpq_paragraph_builder_style(sel_style);
    rdpq_paragraph_builder_span(sel_entry->name, strlen(sel_entry->name));
    rdpq_paragraph_t *marquee_layout = rdpq_paragraph_builder_end();

    float text_w = (float)(marquee_layout->bbox.x1 - marquee_layout->bbox.x0);
    bool needs_marquee = (text_w > (float)TEXT_AREA_WIDTH);

    /* Update marquee scroll state */
    uint32_t now_ms = get_ticks_ms();
    if (selected != s_mq_idx) {
        s_mq_idx         = selected;
        s_mq_offset      = 0.0f;
        s_mq_fwd         = true;
        s_mq_max         = needs_marquee ? (text_w - (float)TEXT_AREA_WIDTH) : 0.0f;
        s_mq_pause_until = now_ms + MARQUEE_PAUSE_MS;
        s_mq_last_ms     = now_ms;
    } else if (needs_marquee) {
        s_mq_max = text_w - (float)TEXT_AREA_WIDTH;
        if (now_ms >= s_mq_pause_until) {
            float dt = (float)(now_ms - s_mq_last_ms) / 1000.0f;
            if (dt > 0.1f) dt = 0.1f;
            if (s_mq_fwd) {
                s_mq_offset += MARQUEE_SPEED_PX_S * dt;
                if (s_mq_offset >= s_mq_max) {
                    s_mq_offset      = s_mq_max;
                    s_mq_fwd         = false;
                    s_mq_pause_until = now_ms + MARQUEE_PAUSE_MS;
                }
            } else {
                s_mq_offset -= MARQUEE_SPEED_PX_S * dt;
                if (s_mq_offset <= 0.0f) {
                    s_mq_offset      = 0.0f;
                    s_mq_fwd         = true;
                    s_mq_pause_until = now_ms + MARQUEE_PAUSE_MS;
                }
            }
        }
        s_mq_last_ms = now_ms;
    }

    /* ------------------------------------------------------------------
     * Main file list paragraph.  For the selected entry, substitute a
     * single space when marquee is active — this preserves line height
     * while rendering nothing visible over the highlight box.
     * ------------------------------------------------------------------ */
    size_t name_lengths[LIST_ENTRIES];
    size_t total_length = 1;

    for (int i = 0; i < LIST_ENTRIES; i++) {
        int entry_index = starting_position + i;
        if (entry_index >= entries) {
            name_lengths[i] = 0;
        } else if (entry_index == selected && needs_marquee) {
            name_lengths[i] = 1;
            total_length += 1;
        } else {
            size_t length = strlen(list[entry_index].name);
            name_lengths[i] = length;
            total_length += length;
        }
    }

    rdpq_paragraph_t *file_list_layout = malloc(
        sizeof(rdpq_paragraph_t) + sizeof(rdpq_paragraph_char_t) * total_length
    );
    memset(file_list_layout, 0, sizeof(rdpq_paragraph_t));
    file_list_layout->capacity = total_length;

    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t) {
            .width = FILE_LIST_MAX_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - VISIBLE_AREA_Y0 - (TEXT_MARGIN_VERTICAL * 2),
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        file_list_layout
    );

    for (int i = 0; i < LIST_ENTRIES; i++) {
        int entry_index = starting_position + i;
        entry_t *entry = &list[entry_index];

        rdpq_paragraph_builder_style(style_for_entry(entry->type));

        if (entry_index == selected && needs_marquee) {
            rdpq_paragraph_builder_span(" ", 1);
        } else {
            rdpq_paragraph_builder_span(entry->name, name_lengths[i]);
        }

        if ((entry_index + 1) >= entries) {
            break;
        }

        rdpq_paragraph_builder_newline();
    }

    rdpq_paragraph_t *layout = rdpq_paragraph_builder_end();

    int highlight_height = (layout->bbox.y1 - layout->bbox.y0) / layout->nlines;
    int highlight_y = VISIBLE_AREA_Y0 + TAB_HEIGHT + TEXT_MARGIN_VERTICAL + TEXT_OFFSET_VERTICAL
                    + (sel_vis * highlight_height);

    ui_components_box_draw(
        FILE_LIST_HIGHLIGHT_X,
        highlight_y,
        FILE_LIST_HIGHLIGHT_X + FILE_LIST_HIGHLIGHT_WIDTH,
        highlight_y + highlight_height,
        FILE_LIST_HIGHLIGHT_COLOR
    );

    int para_base_y = VISIBLE_AREA_Y0 + TEXT_MARGIN_VERTICAL + TAB_HEIGHT + TEXT_OFFSET_VERTICAL;
    int text_x = VISIBLE_AREA_X0 + TEXT_MARGIN_HORIZONTAL;

    rdpq_paragraph_render(layout, text_x, para_base_y);
    rdpq_paragraph_free(layout);

    /* ------------------------------------------------------------------
     * Marquee: scissor-clip the selected row and render the scrolling text.
     * ------------------------------------------------------------------ */
    if (needs_marquee) {
        int clip_x1 = VISIBLE_AREA_X0 + FILE_LIST_MAX_WIDTH - TEXT_MARGIN_HORIZONTAL;
        rdpq_set_scissor(text_x, highlight_y, clip_x1, highlight_y + highlight_height);
        rdpq_paragraph_render(
            marquee_layout,
            text_x - (int)s_mq_offset,
            highlight_y
        );
        rdpq_set_scissor(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    }

    rdpq_paragraph_free(marquee_layout);

    /* ------------------------------------------------------------------
     * Right-aligned file sizes / directory icons.
     * ------------------------------------------------------------------ */
    rdpq_paragraph_builder_begin(
        &(rdpq_textparms_t) {
            .width = VISIBLE_AREA_WIDTH - LIST_SCROLLBAR_WIDTH - (TEXT_MARGIN_HORIZONTAL * 2),
            .height = LAYOUT_ACTIONS_SEPARATOR_Y - VISIBLE_AREA_Y0 - (TEXT_MARGIN_VERTICAL * 2),
            .align = ALIGN_RIGHT,
            .wrap = WRAP_ELLIPSES,
            .line_spacing = TEXT_LINE_SPACING_ADJUST,
        },
        FNT_DEFAULT,
        NULL
    );

    char file_size[16];

    for (int i = starting_position; i < entries; i++) {
        entry_t *entry = &list[i];

        if (entry->type != ENTRY_TYPE_DIR) {
            rdpq_paragraph_builder_span(file_size, format_file_size(file_size, entry->size));
        } else {
            rdpq_paragraph_builder_span(directory_icon, 5);
        }

        if ((i + 1) == (starting_position + LIST_ENTRIES)) {
            break;
        }

        rdpq_paragraph_builder_newline();
    }

    layout = rdpq_paragraph_builder_end();
    rdpq_paragraph_render(layout, text_x, para_base_y);
    rdpq_paragraph_free(layout);
}
