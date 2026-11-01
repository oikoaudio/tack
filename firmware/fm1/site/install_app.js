/* SPDX-License-Identifier: GPL-3.0-only */
/* The installer's own step (fm1.toml install_app; web/installer.html APP). Tack has only run on an FM-1 that came from
 * M-VAVE's V15. V16 replaces the bootloader as well, so a unit on V16 gets told so before anything is written. The
 * running firmware names its version. A unit that does not answer is left to Install, which says what is wrong. */
{
  async before(midi, ui) {
    const up = updater(midi);
    const dev = await up.findAny((id) => !IS_OTA(id));
    if (!dev) return null;
    dev.link.close();
    if (!/^FM-1_0\d\d$/.test(dev.id.text) || dev.id.version < 16) return null;
    const answer = await ui.ask(`This FM-1 runs M-VAVE's V${dev.id.version}`,
      "<p>Tack has only been tested on an FM-1 that came from V15. V16 also replaced this FM-1's bootloader, and " +
      "Tack has not run on that bootloader yet. Nothing has been written. If you go ahead and the FM-1 does not " +
      "start, this page's link on recovering an FM-1 explains what to do.</p>",
      [["go", "Install anyway"], ["stop", "Cancel"]]);
    return answer === "go" ? null : false;
  },
}
