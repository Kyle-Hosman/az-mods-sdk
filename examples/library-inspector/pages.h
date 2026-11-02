/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef LIBRARY_INSPECTOR_PAGES_H
#define LIBRARY_INSPECTOR_PAGES_H
#include "azmod-mod.h"
#define LIBRARY_INSPECTOR_ID "library-inspector"
extern const az_library_v1 *library;
typedef struct library_page {
    int (*init)(const az_core_v1 *);
    void (*tick)(void);
    void (*draw)(az_draw *, az_rect, void *);
    int (*input)(const az_event *, void *);
} library_page;
extern const library_page library_page_cues, library_page_live;
extern const library_page library_page_analysis, library_page_control;
#endif
