# open-lifu-transmitter-bl-updater

One-shot firmware for an Open-LIFU **transmitter** module (STM32L443RC). It replaces the
secure bootloader from inside the application slot, then resets into the new
bootloader's DFU, where the host installs the application:
- USB DFU on the master;
- I2C DFU, reached through the master, on a slave module.

It exists for one migration: research units on bootloader **1.0.x** (shipped with
transmitter app 2.0.8, bootloader `1.0.1`) to bootloader **1.1.x**. Bootloader 1.1.0
moves the end of the slot and adds the preset slots, so those units need it before
transmitter app 2.1.x. Both bootloaders trust the same research key, and this image is
signed with it. Clinical units are built with the new bootloader and never run this.

Host side: [openlifu-sdk](https://github.com/OpenwaterHealth/openlifu-sdk)
`LIFUTransmitterFirmwareUpdate` installs it automatically when it finds a bootloader
older than 1.1.0: `update()` for the master, `update_slave()` for a slave, and
`update_chain()` / `--all` for slaves first, then the master. It then waits for the new
bootloader's DFU and installs the application. See `docs/api/LIFUFirmwareUpdate.md`,
"Replacing a 1.0.x bootloader".

## What it does

It runs as an ordinary application launched by bootloader 1.0.x at `0x08010400`. It is
the same image on a master and on a slave.

1. It masks interrupts, points VTOR at itself and turns the heartbeat LED (LD_HB) on.
2. It refuses, without writing anything, unless the option bytes are open: RDP level 0,
   both write-protection areas off and no PCROP. It then requests DFU and resets, so
   the old bootloader comes back in DFU.
3. It checks the embedded bootloader:
   - its size matches the build;
   - its initial stack pointer is in SRAM1;
   - its reset vector is in the bootloader region;
   - it carries the `OWBI` info block at `0x080001E0`.

   The image is in `.rodata`, so the old bootloader already verified it as part of this
   firmware's signature.
4. If pages 0-31 already hold exactly this bootloader, it goes straight to step 6.
5. It rewrites the bootloader from SRAM-resident code (`src/flash_ram.c`):
   - erases pages 0-31;
   - programs the image double-word by double-word from `.rodata`;
   - flushes the flash caches;
   - verifies by read-back, including that the rest of the region reads erased;
   - feeds the watchdog (about 8 s) throughout and makes up to three attempts.

   **Do not power off** during this step.
6. It writes the bootloader's DFU request (`RTC->BKP7R = 0xB007C0DE`) and clears the
   boot-failure counter (`BKP6R`). From SRAM, it erases pages 32-126 (the old slot,
   itself included, the new preset slots and the anti-rollback floor), then resets.
   Page 127 (runtime config) is kept. The new bootloader starts in DFU with an empty
   slot, blank preset slots and no floor. The first application installed sets the new
   floor.

If programming fails after the erase, the module has no bootloader: LD_HB blinks
forever, and the module needs SWD recovery.

On a slave, the new bootloader comes up in its I2C DFU at 0x72. The master's next
re-enumeration gives it its chain address, and the SDK does that re-enumeration.

## Flash layout

| Pages | Address | Bootloader 1.0.x | Bootloader 1.1.x | Updater |
|---|---|---|---|---|
| 0-31 | `0x08000000` | bootloader | bootloader | rewritten |
| 32-105 | `0x08010000` | slot | slot | erased (runs here) |
| 106-125 | `0x08035000` | slot | presets, 10 × 4 KB | erased |
| 126 | `0x0803F000` | anti-rollback floor | anti-rollback floor | erased |
| 127 | `0x0803F800` | runtime config | runtime config | **kept** |

The image (about 63 KB) stays well below page 106, so it fits both slots.

## Versions

The **release tag** (e.g. `1.0.0`) versions this repository. The image is signed with
**`FW_VERSION` = 2.0.31** (FwVersion 4127), which `build.yml` pins:

- 2.0.31 is above every fielded transmitter application (2.0.x), so the old bootloader
  installs it and its floor check accepts it;
- it is below the first application for the new bootloader (2.1.0, 4128), which then
  installs without a downgrade even if the floor were not erased.

## Build

The build needs the Arm GNU Toolchain, CMake and Ninja (the `stm32-build-env` image),
plus the research bootloader binary from its GitHub release:

```sh
gh release download 1.1.0-rc.1 -R OpenwaterHealth/open-lifu-transmitter-bl -p open-lifu-transmitter-bl.bin -D blimg
cmake --preset Release -DBL_IMAGE=$PWD/blimg/open-lifu-transmitter-bl.bin -DBL_TAG=1.1.0-rc.1
cmake --build build/Release
bash scripts/check_ramfunc.sh build/Release/open-lifu-transmitter-bl-updater.elf
```

The output is `build/Release/open-lifu-transmitter-bl-updater.bin`, an unsigned slot
image. `scripts/check_ramfunc.sh` fails the build if the SRAM-resident flash code
references anything in flash. Signing it:

```sh
python -m openlifu_sdk.io.LIFUCrypto sign --keys <research keys dir> \
    --firmware build/Release/open-lifu-transmitter-bl-updater.bin --version 2.0.31 --output updater-signed.bin
```

## CI

`.github/workflows/build.yml`:
1. Downloads the embedded bootloader (`BL_RELEASE`) from its GitHub release.
2. Checks that `FW_SIGNING_KEY` is the key the fielded bootloader (`FIELDED_BL`) trusts,
   that it is the embedded bootloader's research key, and that it is not its clinical
   key.
3. Builds, signs with the SDK at `SDK_RELEASE`, then verifies the image with the
   research key and refuses it if it also verifies with the clinical key.
4. Generates the SBOM (`scripts/gen_sbom.py`).
5. On a tag, publishes the GitHub release: `open-lifu-transmitter-bl-updater-signed.bin`,
   the SBOM and `SHA256SUMS`.

`safety-security-scan.yml` runs cppcheck, lizard and a grype scan of the SBOM.

## LED

| State | LD_HB (PC12) |
|---|---|
| Running / rewriting the bootloader (do not power off) | on |
| Done or refused, resetting | off |
| Failed after the erase (SWD recovery) | blinking |

## Safety notes

- A power loss between the erase and the end of programming leaves the module with no
  bootloader. The window is a few seconds. Keep the chain powered and do not unplug USB.
- The updater refuses a module whose option bytes are not open, so it never touches a
  protected (clinical) unit.
- This is a one-way migration. Once the slot holds an application above 2.0.31, the new
  bootloader will not install the updater again.

## Licence

Apache-2.0, see `LICENSE`. The vendored CMSIS headers (`cmsis/`) are Apache-2.0
(Arm and STMicroelectronics).
