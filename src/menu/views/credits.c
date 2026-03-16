#include "views.h"
#include "../sound.h"

#ifndef MENU_VERSION
#define MENU_VERSION "Unknown"
#endif

#ifndef BUILD_TIMESTAMP
#define BUILD_TIMESTAMP "Unknown"
#endif

static sys_version_t sdk_version = {0};

static void process (menu_t *menu) {
    if (menu->actions.back) {
        sound_play_effect(SFX_EXIT);
        menu->next_mode = MENU_MODE_BROWSER; 
    }
}

static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach(d, NULL);

    ui_components_background_draw();

    ui_components_layout_draw();

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_CENTER, VALIGN_TOP,
        "MENU INFORMATION"
    );

    ui_components_main_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "\n"
        "Menu version: %s\n"
        "libdragon SDK version: %s%s (%s, %.7s)\n"
        "Build timestamp: %s\n"
        "\n"
        "Github - Website:\n"
        "\thttps://github.com/Polprzewodnikowy/N64FlashcartMenu\n"
        "Authors:\n"
        "\tRobin Jones / NetworkFusion\n"
        "\tMateusz Faderewski / Polprzewodnikowy\n"
        "Contributors:\n"
        "\tThank you to ALL project contributors,\n"
        "\tno matter how small the commit.\n"
        "OSS software used:\n"
        "\tlibdragon (UNLICENSE License)\n"
        "\tlibspng (BSD 2-Clause License)\n"
        "\tmini.c (BSD 2-Clause License)\n"
        "\tminimp3 (CC0 1.0 Universal)\n"
        "\tminiz (MIT License)",
        MENU_VERSION,
        sdk_version.branch, sdk_version.dirty ? "*" : "",
        sdk_version.commit_date,
        sdk_version.hash,
        BUILD_TIMESTAMP
    );

    ui_components_actions_bar_text_draw(
        STL_DEFAULT,
        ALIGN_LEFT, VALIGN_TOP,
        "\n"
        "B: Exit"
    );

    rdpq_detach_show();
}


void view_credits_init (menu_t *menu) {
    sys_get_version(&sdk_version);
}

void view_credits_display (menu_t *menu, surface_t *display) {
    process(menu);

    draw(menu, display);
}
