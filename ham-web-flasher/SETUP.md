# Piglet Web Flasher — setup & deploy

A browser-based firmware installer for Piglet, powered by
[ESP Web Tools](https://esphome.github.io/esp-web-tools/). People open a web
page, pick a board from a dropdown, click **Install**, and flash it over USB —
no Arduino IDE, no esptool, no drivers to think about.

It serves firmware straight from **this repo's latest GitHub release**, so there
is nothing to compile when you deploy. Cut a release with your merged `.bin`
files and the flasher automatically points at the newest one.

---

## What's in this folder

```
web-flasher/                     ← the site (copy to your repo root)
├── index.html                   the flasher page + board dropdown
└── manifests/                   one ESP Web Tools manifest per board
    ├── xiao_c5.json             → Piglet.ino.merged.bin
    ├── pigletnode_c5.json       → PigletNode.ino.merged.bin
    └── tdongle_c5.json          → TDongleC5_Piglet.ino.merged.bin

.github/workflows/
└── deploy-web-flasher.yml       ← the GitHub Actions deploy (copy to your repo root)
```

The three boards match the flashable images your release already ships:
`Piglet.ino.merged.bin` (Piglet Core / XIAO C5), `PigletNode.ino.merged.bin`
(PigletNode C5 mesh node), and `TDongleC5_Piglet.ino.merged.bin` (LilyGo
T-Dongle C5) — all ESP32-C5.

---

## 1. Add the files to your repo

From the root of your `piglet` checkout, drop both top-level folders in:

```bash
cp -r web-flasher .github <your-piglet-repo>/
cd <your-piglet-repo>
git add web-flasher .github/workflows/deploy-web-flasher.yml
git commit -m "Add web flasher"
git push
```

If you already have a `.github/workflows/` folder, only the single
`deploy-web-flasher.yml` file needs to go into it — don't overwrite the folder.

---

## 2. First deploy

The workflow runs automatically on a push that touches `web-flasher/`, whenever
you publish a release, and from the Actions tab (**Run workflow**). It turns
GitHub Pages on for you on the first run — you shouldn't need to touch Settings.

1. Push the files (step 1), or go to **Actions → Deploy web flasher → Run
   workflow**.
2. Watch the run finish in the **Actions** tab. The **deploy** job prints the
   live URL, which will be:

   ```
   https://hamspiced.github.io/piglet/
   ```

3. Open that URL in Chrome or Edge on a desktop and flash a board to confirm.

> **If the first run fails to enable Pages** (some org settings block
> auto-enable): go to **Settings → Pages**, set **Source** to **GitHub
> Actions**, then re-run the workflow. One-time only.

---

## 3. How firmware gets to the flasher

You do **not** rebuild firmware here. Each manifest in `manifests/` points at:

```
https://github.com/Hamspiced/piglet/releases/latest/download/<name>.ino.merged.bin
```

`releases/latest/download/...` always resolves to your newest published
release, so the flasher tracks it with no version pinning. Your normal workflow
is unchanged:

1. Build the merged images (the `*.ino.merged.bin` files your Arduino build
   already produces).
2. Create a GitHub **Release** and attach them as assets, keeping the exact
   file names above.
3. Publish. The flasher serves the new firmware immediately — the deploy
   workflow also re-runs on release to refresh the build stamp in the footer.

**The asset names must match the manifests exactly.** If you rename a `.bin`,
update the matching `manifests/*.json` `path` (and the `bin:` field in
`index.html`) to match.

---

## 4. Test locally (optional)

Any static server works — ESP Web Tools needs `https://` or `localhost` and a
Chromium browser:

```bash
cd web-flasher
python3 -m http.server 8000
# then open http://localhost:8000 in Chrome or Edge
```

The board dropdown and manifests work locally; **Install** fetches the `.bin`
from your live GitHub release, so you need a published release for flashing to
succeed end to end.

---

## 5. Adding or changing boards

Each board is one dropdown entry wired to one manifest and one release asset.
To add a board:

1. Ship its merged image in the release (e.g. `MyBoard.ino.merged.bin`).
2. Copy a file in `manifests/` to `manifests/myboard.json`, set its `name`,
   `chipFamily` (e.g. `ESP32-C5`, `ESP32-S3`, `ESP32-C3`, `ESP32-C6`), and the
   release `path` to the new asset.
3. Add one entry to the `BOARDS` array near the bottom of `index.html` —
   `value`, `label`, `manifest`, `bin`, `chips`, `desc`.

To remove a board, delete its `BOARDS` entry (and optionally its manifest).

---

## 6. Note on ESP32-C5 flashing

The native USB-Serial/JTAG on some C5 boards re-enumerates when the browser
loads the flash stub, so browser flashing can fail to start. The page shows a
fallback command for exactly this — flash from a terminal with `--no-stub`:

```bash
esptool --chip esp32c5 --no-stub write-flash 0x0 Piglet.ino.merged.bin
```

---

Built with [ESP Web Tools](https://esphome.github.io/esp-web-tools/).
