# Changelog

## 0.2.3 (2026-11-24)

- **Presets no longer depend on Savage Wind honouring a test switch.** With HDR on, Savage Wind 0.6 draws with its own
  HDR version of the game's tone-mapping shader. 0.2.2 asked Savage Wind not to (`SAVAGEWIND_HDR_TONEMAP=0`), a test
  switch it could drop in any update. Now the layer also recognises Savage Wind's HDR version and puts the very same
  Clarity program in it, so the picture is identical either way. The launchers still set the switch, so either one alone
  is enough.
- **With a preset on, Savage Wind's custom HDR tone-mapping isn't used:** its extra-bright HDR highlights and its HDR
  picture settings don't apply, because Clarity is the tone-mapping. Savage Wind still shows Clarity in HDR, at its game
  brightness. Choose **Off** for Savage Wind's own HDR look.
- `clearskies.log` names each replacement with a checksum of the program handed to the driver: the same number means
  the same Clarity.
- The layer test adds a stand-in for Savage Wind's HDR shader and a look-alike that must be left alone, and checks
  that both replacements are one identical program (Windows, and on Linux in GitHub's check).
- Tested on Savage Wind 0.6.0-alpha.6 on Windows (AMD Radeon RX 9070 XT) with HDR on, without the switch (Savage Wind's
  HDR tone-mapping ran and was replaced with the same program as the game's), with HDR off, and on Savage Wind 0.5.3.

## 0.2.2 (2026-11-24)

- **Fixed: presets didn't show with HDR on in Savage Wind 0.6** (issues #1 and #2). With HDR on, Savage Wind 0.6
  draws the picture with its own HDR tone-mapping shader instead of the game's. Clear Skies turns the game's shader
  into Clarity, so the look was loaded (the log said so) but never drawn. Now, with a preset chosen, Clear Skies asks
  Savage Wind to keep the game's own tone-mapping (`SAVAGEWIND_HDR_TONEMAP=0`), and Savage Wind still shows it in HDR.
  With **Off**, Savage Wind's HDR tone-mapping is left alone.
- With a preset and HDR on, the picture is Clarity's at Savage Wind's game brightness, without Savage Wind's extended
  HDR highlights. For those, choose **Off**.
- `clearskies.log` now says whether Savage Wind's HDR tone-mapping was turned off for Clarity.
- The same fix is in the Windows launcher, the Linux AppImage and `clearskies.sh`.
- Tested on Savage Wind 0.6.0-alpha.6 on Windows (AMD Radeon RX 9070 XT) with HDR on and off.

## 0.2.1 (2026-11-11)

- **Linux: one AppImage with the Clear Skies window**, in place of 0.2.0's tarball and script. It opens the same
  window as ClearSkies.exe: choose a look, **Browse** to the Savage Wind AppImage (or drag it onto the window), press
  **Play**. If the Savage Wind AppImage isn't allowed to run yet, Clear Skies offers to allow it.
- Linux settings are saved in `~/.config/ClearSkies/clearskies.ini`, with the layer's log next to it.
- The AppImage also works from the command line (`--play`, `--preset`, `--game`, `--list`, `--no-play`) and as
  Steam's launch options (`ClearSkies-0.2.1-x86_64.AppImage %command%`).
- The Linux window is drawn with SDL3 and stb_truetype in Fira Sans; all three are in LICENSES.txt.
- GitHub's check builds the AppImage on Ubuntu 22.04 and tests it on 24.04: the command line, the window, every
  preset through the AppImage's own layer, and Play with a fake game standing in for Savage Wind.
- Windows is unchanged apart from the version number.

## 0.2.0 (2026-11-11)

Linux and Steam Deck support, by [Danny Rashid](https://github.com/danny-rashd).

- Linux: the layer builds as `libclearskies_layer.so` for Savage Wind's native AppImage. It uses the shaderc the
  AppImage ships, and falls back to the system's.
- Linux: `clearskies.sh`, a launcher script with the same options as ClearSkies.exe. It also works as Steam launch
  options (`clearskies.sh %command%`) for the Steam Deck.
- `build.sh` and `tests/run_tests.sh` build and test the Linux version. All 21 presets pass the layer test on
  Ubuntu 24.04 with Mesa's software Vulkan driver. It hasn't been tried in the game on Linux yet.
- A GitHub check builds the Linux version and runs that test on every push.
- The download page has a Linux download next to the Windows one.

## 0.1.0 (2026-11-07)

The first release.

- Cemu's Clarity for Breath of the Wild on Savage Wind, as a Vulkan layer: the game's tone-mapping shader is
  replaced with Clarity, and nothing in Savage Wind's folder is changed.
- All 20 presets from Cemu's graphic pack, and all 65 of Clarity's settings adjustable in `clearskies.ini`.
- ClearSkies.exe, a themed launcher: choose a look, point it at `savagewind.exe`, press Play. It also has
  command-line options, and warns when Savage Wind isn't set up yet.
- `clearskies.log` for troubleshooting.

Tested in the game on one PC (AMD Radeon RX 9070 XT, Windows 11, HDR off) with Serfrost's preset, and every preset
automatically on that GPU. NVIDIA, Intel, HDR output, Linux and Steam Deck are untested.
