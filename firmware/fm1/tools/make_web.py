#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes Tack's install, backup, kit bank and try-it pages into a checkout of the oikoaudio site, from a release package.

    make_web.py VERSION WEB_DIR        # first: build.py firmware/fm1 --release VERSION, and --web

The update and backup logic stays fm1-base's: web/installer.html and web/backup/index.html, with the libraries inlined
as make_site.py does. This takes those pages apart so the site can give them its own header, theme and footer. The kit
bank page is Tack's own, from site/kits_body.html, kits_core.js and kits_app.js:

    WEB_DIR/public/tack/fm1/install.js, backup.js, kits.js    the pages' scripts
    WEB_DIR/public/tack/fm1/firmware/                the package, and the licences that go with it
    WEB_DIR/public/tack/fm1/try/                     the firmware in the browser, with site/demo_pattern.bin in pattern 1
    WEB_DIR/app/tack/fm1/tool-pages.ts               the pages' markup, and the release's version and checksum

Writes nothing to an FM-1 and publishes nothing. m-bootloader stays out of it: it is the one thing the installer can
write below 0x4000, and Tack has not been run on it. make_site.py is not used because it needs Emscripten's build.
"""
import base64
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
APP = HERE.parent
ROOT = APP.parents[1]
BASE = ROOT / "third_party" / "fm1-base"
sys.path.insert(0, str(BASE / "tools"))
from build import read_conf                                       # noqa: E402
from make_site import ports_js, product_of, strip_module, tool_pages  # noqa: E402

NAME = "Tack"
V15 = "https://web.archive.org/web/20261009203224id_/https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc"
V16 = "https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc"


# Tack plays with one hand on a button and the other on a dial, and a mouse is one hand. fm1-base's page says so only to a
# screen reader
DEMO_HINT = ('<p style="margin: 14px 0 0;">Press Switch on, then PLAY/STOP. To hold a button or key down, right-click or '
             "Shift-click it, and click it again to let go. Then drag a dial, or use the scroll wheel on it. For the shuffle, "
             "hold SEL and turn SELECT.</p>")


def once(text, old, new):
    """fm1-base changing under these edits should stop the build, not leave a page half edited"""
    if text.count(old) != 1:
        raise SystemExit(f"make_web: expected one of {old[:60]!r}, found {text.count(old)}")
    return text.replace(old, new)


def parts(html, what):
    """a page -> its markup and its one script"""
    if len(re.findall(r"<script\b", html)) != 1:
        raise SystemExit(f"make_web: {what} must have one script")
    body = html[html.index("<body>") + 6:html.index("</body>")]
    at = body.index("<script")
    script = body[body.index(">", at) + 1:body.rindex("</script>")]
    markup = re.sub(r"<!--.*?-->", "", body[:at], flags=re.S)
    return once(once(markup, "<main>", '<div class="tool-column">'), "</main>", "</div>").strip(), script


def installer(conf, version, product, package):
    html = (BASE / "web" / "installer.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((BASE / "web" / f).read_text(encoding="utf-8"))
                    for f in ("fm1ports.js", "fm1pkg.js", "fm1ota.js"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + package, "backup": True,
                       "res": None, "boot": None})
    repo = conf["repo"]
    for key, value in (("NAME", NAME), ("REPO", repo), ("REPO_SHORT", re.sub(r"^https?://", "", repo)),
                       ("LICENSE", conf["licence"]), ("CREDITS", " " + conf["credits"])):
        html = html.replace("{{" + key + "}}", value)
    # fm1-base refuses every real M-VAVE package as damaged: their parts 50, 52, 161 and 251 use another checksum. Stock
    # is checked here on the two parts the FM-1 reads, flash.bin and the loader. Move this into fm1-base's fm1pkg.js
    lib = once(lib, "function checkUfw(image) {", "function checkUfw(image, only) {")
    lib = once(lib, "    const off = rd32(e, 8), len = rd32(e, 12);\n",
               "    const off = rd32(e, 8), len = rd32(e, 12);\n    if (only && !only.includes(rd16(e, 0))) continue;\n")
    html = once(html, 'try { checkUfw(img); } catch (_) { sayK("stockdamaged"); return; }',
                'try { checkUfw(img, [0, 100]); } catch (_) { sayK("stockdamaged"); return; }')
    html = once(html, "/*LIB*/", lib)
    html = once(html, "/*META*/", meta)
    html = once(html, "/*APP*/", (APP / conf["install_app"]).read_text(encoding="utf-8").strip())
    html = once(html, "<footer>", '<div class="tool-credits">')
    html = once(html, "</footer>", "</div>")
    # the stock section: which file is which, as the two differ in what they do to the FM-1
    html = once(html, '<li><span data-t="stock1"></span> <a id="stocklink" '
                      'href="https://yms-file-store.oss-cn-hongkong.aliyuncs.com/software/firmware/FM-1.fwsc">FM-1.fwsc</a></li>',
                f'<li><span data-t="stock1"></span> <a href="{V15}">V15</a>, kept by the Internet Archive, or '
                f'<a id="stocklink" href="{V16}">V16</a>, M-VAVE\'s current download. V16 also replaces the FM-1\'s '
                "bootloader, and Tack has not run on that bootloader, so use V15 if you plan to come back.</li>")
    html = once(html, 'stock1: "Download M-VAVE\'s FM-1 firmware:"', 'stock1: "Download M-VAVE\'s firmware:"')
    html = once(html, 'stockok: "That is M-VAVE\'s FM-1 firmware. Press the button to install it."',
                'stockok: "That is M-VAVE\'s FM-1 firmware,"')
    html = once(html, 'sayK("stockok");',
                'sayK("stockok", ` version ${parseInt(product.slice(-2), 10)}. Press the button to install it.`);')
    html = once(html, 'brick: "If the FM-1 no longer starts, a Mac can put the stock firmware back:"',
                'brick: "If the FM-1 no longer starts, a Mac or a Linux computer can put the stock firmware back:"')
    return parts(html, "web/installer.html")


def backup(conf):
    extra = '<li>The kit bank is not in this file. The <a href="../kits/">kit bank page</a> saves the kits.</li>'
    terminal = "From a terminal, <code>fm1 backup NAME</code> and <code>fm1 restore NAME</code> make and read the same file."
    html = tool_pages(APP, conf, {"EXTRA": extra, "TERMINAL": terminal})["backup/index.html"]
    return parts(html, "web/backup/index.html")


def kits(conf):
    """the kit bank page: its markup, and its script on fm1-base's port finding"""
    lib = "\n".join(strip_module((BASE / "web" / f).read_text(encoding="utf-8")) for f in ("fm1ports.js", "fm1ota.js"))
    core = strip_module((APP / "site" / "kits_core.js").read_text(encoding="utf-8"))
    page = once((APP / "site" / "kits_app.js").read_text(encoding="utf-8"), "{{ID}}", conf["id_prefix"])
    return (APP / "site" / "kits_body.html").read_text(encoding="utf-8").strip(), "\n".join((lib, core, page))


def demo(out):
    """the browser build, which starts with the demo pattern. It is one saved object, as what else a backup holds is not all
    ours to publish, and a page keeps a visitor's own saves"""
    emu = APP / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit("make_web: no build/emu: build.py firmware/fm1 --web first")
    pattern = (APP / "site" / "demo_pattern.bin").read_bytes()
    magic = re.search(r'PATTERN_MAGIC = b"(\w+)"', (HERE / "tack_check.py").read_text(encoding="utf-8")).group(1)
    if pattern[:4] != magic.encode():
        raise SystemExit(f"make_web: site/demo_pattern.bin is not a {magic} pattern: save it again from this build")
    store = json.dumps({"2": base64.b64encode(pattern).decode()})         # 2: app.h's OBJ_PAT0
    seed = ('<script>try { if (!localStorage.getItem("tack-emu-store")) '
            f'localStorage.setItem("tack-emu-store", {json.dumps(store)}); }} catch (e) {{ /* a blank FM-1 */ }}</script>')
    html = (emu / "index.html").read_text(encoding="utf-8")
    html = once(html, "<head>", "<head>" + seed)
    html = once(html, "<h1>TACK</h1>", '<h1>TACK</h1>\n  <p><a href="../">Back to Tack</a></p>')
    html = once(html, '<div class="bar">', f"{DEMO_HINT}\n<div class=\"bar\">")
    target = out / "try"
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    (target / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(emu / "app.wasm", target / "app.wasm")
    shutil.copy(emu / "worklet.js", target / "worklet.js")


def main(version, web):
    web = Path(web).resolve()
    if not (web / "app").is_dir() or not (web / "public").is_dir():
        raise SystemExit(f"make_web: {web} is not the site")
    conf = read_conf(APP / "fm1.toml")
    stem = conf["name"].lower()
    source = APP / "build" / f"{stem}-{version}.fwsc"
    raw = source.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(re.escape(conf["id_prefix"]) + r"\d{6}", product):
        raise SystemExit(f"{source}: identity {product!r} is not a release build")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{source}: no update loader in it")
    package = f"{stem}-{version}.fwsc"

    out = web / "public" / "tack" / "fm1"
    firmware = out / "firmware"
    if firmware.exists():
        shutil.rmtree(firmware)
    firmware.mkdir(parents=True)
    shutil.copy(source, firmware / package)
    shutil.copy(ROOT / "LICENSE", firmware / "LICENSE.txt")
    shutil.copy(ROOT / "NOTICE.md", firmware / "NOTICE.md")
    for text in sorted((ROOT / "LICENSES").glob("*.txt")) + sorted((BASE / "LICENSES").glob("*.txt")):
        shutil.copy(text, firmware / text.name)      # the original LXR licence wants its notice with every copy too
    (firmware / "SOURCE.txt").write_text(            # GPL section 6 and the original LXR licence: where the source is
        f"The complete source of {package}, including fm1-base and felucca-bsp, is the release's source archive:\n"
        f"{conf['repo']}/releases/tag/v{version}\n", encoding="utf-8")

    install_body, install_js = installer(conf, version, product, package)
    backup_body, backup_js = backup(conf)
    backup_body = backup_body.replace(conf["name"], NAME)
    kits_body, kits_js = kits(conf)
    (out / "install.js").write_text(install_js, encoding="utf-8")
    (out / "backup.js").write_text(backup_js, encoding="utf-8")
    (out / "kits.js").write_text(kits_js, encoding="utf-8")
    demo(out)

    release = {"version": version, "file": package, "identity": product, "bytes": len(raw),
               "sha256": hashlib.sha256(raw).hexdigest()}
    (web / "app" / "tack" / "fm1").mkdir(parents=True, exist_ok=True)
    (web / "app" / "tack" / "fm1" / "tool-pages.ts").write_text(
        "// Generated by firmware/fm1/tools/make_web.py in the Tack repository. Edit that, not this.\n"
        f"export const release = {json.dumps(release, indent=2)};\n"
        f"export const installBody = {json.dumps(install_body)};\n"
        f"export const backupBody = {json.dumps(backup_body)};\n"
        f"export const kitsBody = {json.dumps(kits_body)};\n", encoding="utf-8")
    print(f"{web}: {package} {product}, {len(raw)} B, sha256 {release['sha256']}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
