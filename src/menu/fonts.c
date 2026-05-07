#include <libdragon.h>

#include "fonts.h"
#include "utils/fs.h"


static rdpq_font_t *default_font = NULL;
static color_t main_text_color = { 0xFF, 0xFF, 0xFF, 0xFF };

/* Lookup table for the user-selectable main text colour.
 * Order must match main_text_color_t in fonts.h. */
static const color_t main_text_palette[MAIN_TEXT_COLOR_COUNT] = {
    [MAIN_TEXT_COLOR_WHITE]  = { 0xFF, 0xFF, 0xFF, 0xFF },
    [MAIN_TEXT_COLOR_YELLOW] = { 0xFF, 0xFF, 0x70, 0xFF },
    [MAIN_TEXT_COLOR_CYAN]   = { 0x70, 0xE0, 0xFF, 0xFF },
    [MAIN_TEXT_COLOR_GREEN]  = { 0x70, 0xFF, 0x70, 0xFF },
    [MAIN_TEXT_COLOR_RED]    = { 0xFF, 0x70, 0x70, 0xFF },
    [MAIN_TEXT_COLOR_ORANGE] = { 0xFF, 0x99, 0x00, 0xFF },
    [MAIN_TEXT_COLOR_PINK]   = { 0xFF, 0x70, 0xC0, 0xFF },
    [MAIN_TEXT_COLOR_AMBER]  = { 0xFF, 0xBF, 0x00, 0xFF },
};


void fonts_set_main_text_color (int color) {
    if (color < 0 || color >= MAIN_TEXT_COLOR_COUNT) {
        color = MAIN_TEXT_COLOR_WHITE;
    }
    main_text_color = main_text_palette[color];
    if (default_font) {
        rdpq_font_style(default_font, STL_DEFAULT,
                        &((rdpq_fontstyle_t) { .color = main_text_color }));
    }
}


static void load_default_font (char *custom_font_path) {
    char *font_path = "rom:/Firple-Bold.font64";

    if (custom_font_path && file_exists(custom_font_path)) {
        font_path = custom_font_path;
    }

    default_font = rdpq_font_load(font_path);

    rdpq_font_style(default_font, STL_DEFAULT, &((rdpq_fontstyle_t) { .color = main_text_color }));
    rdpq_font_style(default_font, STL_GREEN, &((rdpq_fontstyle_t) { .color = RGBA32(0x70, 0xFF, 0x70, 0xFF) }));
    rdpq_font_style(default_font, STL_BLUE, &((rdpq_fontstyle_t) { .color = RGBA32(0x70, 0xBC, 0xFF, 0xFF) }));
    rdpq_font_style(default_font, STL_YELLOW, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0xFF, 0x70, 0xFF) }));
    rdpq_font_style(default_font, STL_ORANGE, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0x99, 0x00, 0xFF) }));
    rdpq_font_style(default_font, STL_RED, &((rdpq_fontstyle_t) { .color = RGBA32(0xFF, 0x40, 0x40, 0xFF) }));
    rdpq_font_style(default_font, STL_GRAY, &((rdpq_fontstyle_t) { .color = RGBA32(0xA0, 0xA0, 0xA0, 0xFF) }));

    rdpq_text_register_font(FNT_DEFAULT, default_font);
}


void fonts_init (char *custom_font_path) {
    load_default_font(custom_font_path);
}
