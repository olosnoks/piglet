# Piglet — Guition JC8048W550 firmware

Pre-built firmware for the **Guition JC8048W550** (ESP32-S3 N16R8, 5" 800x480 RGB).
Plug the board into USB-C before flashing.

## Easiest: web flasher (no software to install)

1. Open **https://espressif.github.io/esptool-js/** in Chrome or Edge.
2. Set **Baudrate** to `921600`, click **Connect**, pick the board's COM port.
   (If no port appears, install the CH340/USB-serial driver and replug.)
3. In the file row, set **Flash Address** to `0x0` and choose
   **`piglet-jc8048w550-full.bin`**.
4. Click **Program**. When it finishes, press the board's **RESET** button.

That single file contains the bootloader, partition table, and app — nothing
else to flash.

## Alternative: esptool (command line)

Install once: `pip install esptool`

Single-file (same as the web flasher):

```
esptool --chip esp32s3 --baud 921600 write_flash 0x0 piglet-jc8048w550-full.bin
```

Or the individual parts:

```
esptool --chip esp32s3 --baud 921600 write_flash \
  0x0     bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 firmware.bin
```

## If flashing fails to start

Hold **BOOT**, tap **RESET**, release **BOOT** to force download mode, then
retry. Drop the baud to `115200` if the connection is unstable.

## Notes / things to check on first boot

This is an initial port from the Freenove board; it builds clean but hasn't been
run on real W550 hardware yet. If something's off:

- **Touch doesn't respond:** the GT911 I2C address differs between units
  (`0x14` vs `0x5D`). Needs a one-line source change + rebuild.
- **Screen tears or is shifted:** RGB timing (porch / pixel-clock polarity) may
  need a tweak.
- **The UI sits in a centered strip with black bars on the sides:** that's
  expected for now — the UI renders at the original resolution scaled up; a
  full-width 800x480 layout is a later step.

Send any of these back and they're quick fixes.
