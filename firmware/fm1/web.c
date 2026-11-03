/* SPDX-License-Identifier: GPL-3.0-only */
/* The browser build's own exports (app.h APP_WEB), for the try-it page. A pattern keeps its own kit but only the slot of
 * the kit it morphs to, and the browser's bank starts empty: the page puts a demo's kits in, one at a time through
 * kit_buf, before the app boots and reads its patterns. emu_web.c includes this after app.c */
__attribute__((used, visibility("default"))) uint8_t *web_kit_buf(void) { return kit_buf; }

__attribute__((used, visibility("default"))) int web_kit_put(uint32_t slot)
{
    return slot < KIT_SLOTS ? plat_data_write(KIT_BASE + slot * KIT_SLOT, kit_buf, KIT_SLOT) : -1;
}
