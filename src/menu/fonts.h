/**
 * @file fonts.h
 * @brief Menu fonts
 * @ingroup menu 
 */

#ifndef FONTS_H__
#define FONTS_H__

/**
 * @brief Font type enumeration.
 * 
 * This enumeration defines the different types of fonts that can be used
 * in the menu system.
 */
typedef enum {
    FNT_DEFAULT = 1, /**< Default font type */
} menu_font_type_t;

/**
 * @brief Font style enumeration.
 * 
 * This enumeration defines the different styles of fonts that can be used
 * in the menu system.
 */
typedef enum {
    STL_DEFAULT = 0, /**< Default font style */
    STL_GREEN,       /**< Green font style */
    STL_BLUE,        /**< Blue font style */
    STL_YELLOW,      /**< Yellow font style */
    STL_ORANGE,      /**< Orange font style */
    STL_RED,         /**< Red font style */
    STL_GRAY,        /**< Gray font style */
} menu_font_style_t;

/**
 * @brief User-selectable colour for the default (main) text style.
 *
 * Only STL_DEFAULT is repainted; semantic styles (STL_RED for errors,
 * STL_GREEN for confirmations, etc.) keep their fixed colours so warnings
 * remain visually distinct regardless of the user's choice.
 */
typedef enum {
    MAIN_TEXT_COLOR_WHITE = 0,
    MAIN_TEXT_COLOR_YELLOW,
    MAIN_TEXT_COLOR_CYAN,
    MAIN_TEXT_COLOR_GREEN,
    MAIN_TEXT_COLOR_RED,
    MAIN_TEXT_COLOR_ORANGE,
    MAIN_TEXT_COLOR_PINK,
    MAIN_TEXT_COLOR_AMBER,
    MAIN_TEXT_COLOR_COUNT,
} main_text_color_t;

/**
 * @brief Initialize fonts.
 *
 * Loads the default font (custom path if provided, otherwise the ROM-baked
 * one) and registers all built-in styles.  Honours the most recent value
 * passed to fonts_set_main_text_color() for STL_DEFAULT.
 */
void fonts_init(char *custom_font_path);

/**
 * @brief Choose the user-facing colour for the main text style (STL_DEFAULT).
 *
 * Safe to call before or after fonts_init().  Out-of-range values fall back
 * to white.  Takes effect on subsequent text draws.
 */
void fonts_set_main_text_color(int color);

#endif /* FONTS_H__ */
