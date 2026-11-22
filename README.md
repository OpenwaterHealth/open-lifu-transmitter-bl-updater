# open-lifu-transmitter-bl-updater

One-shot firmware for an Open-LIFU **transmitter** module (STM32L443RC). It replaces the
secure bootloader from inside the application slot, then resets into the new
bootloader's DFU, where the host installs the application:
- USB DFU on the master;
- I2C DFU, reached through the master, on a slave module.

It moves a research unit to the research bootloader it embeds. The first use is units on
bootloader **1.0.x** (shipped with transmitter app 2.0.8, bootloader `1.0.1`) to bootloader **1.1.x**; since it is signed at
2.1.0 (see "Versions"), it also moves a unit on an older 1.1.x research bootloader to the
embedded one, e.g. 1.1.0-rc.N to the 1.1.0 release. Bootloader 1.1.0
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

It runs as an ordinary application launched by the research bootloader (1.0.x or 1.1.x) at `0x08010400`. It is
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
| 32-95 | `0x08010000` | slot | slot | erased (runs here) |
| 96-125 | `0x08030000` | slot | presets, 15 × 4 KB | erased |
| 126 | `0x0803F000` | anti-rollback floor | anti-rollback floor | erased |
| 127 | `0x0803F800` | runtime config | runtime config | **kept** |

The image (about 63 KB) stays well below page 96, so it fits both slots.

## Versions

The **release tag** (e.g. `1.0.0`) versions this repository. The image is signed with
**`FW_VERSION` = 2.1.0** (FwVersion 4128), the version of the transmitter application released
with it. `build.yml` pins it:

- A bootloader refuses an image below the installed application or its anti-rollback floor.
  2.1.0 is at or above every application the updater runs over: transmitter 2.0.x on a 1.0.x
  bootloader, and 2.1.0 on a 1.1.x bootloader (every 2.1.0-rc.N signs as 2.1.0). So it moves
  both to the embedded bootloader.
- It is no higher than the application installed after it. On success the floor is erased
  anyway; if the updater refuses (option bytes not open), nothing is written, the floor is at
  most 2.1.0, and the release application still installs.
- Raise it with the application. When the transmitter application moves to 2.1.1 or later, sign the
  updater at that version, or units on the newer application refuse it. The SDK refuses
  before DFU when the updater is signed below the installed application.

Before this, the updater was signed 2.0.31 (4127): above every fielded 2.0.x application
and below 2.1.0. That only ran on 1.0.x bootloaders; a 1.1.x bootloader with a 2.1.0
application refused it.

## Build

The build needs the Arm GNU Toolchain, CMake and Ninja (the `stm32-build-env` image),
plus the research bootloader binary from its GitHub release:

```sh
gh release download 1.1.0 -R OpenwaterHealth/open-lifu-transmitter-bl -p open-lifu-transmitter-bl.bin -D blimg
cmake --preset Release -DBL_IMAGE=$PWD/blimg/open-lifu-transmitter-bl.bin -DBL_TAG=1.1.0
cmake --build build/Release
bash scripts/check_ramfunc.sh build/Release/open-lifu-transmitter-bl-updater.elf
```

The output is `build/Release/open-lifu-transmitter-bl-updater.bin`, an unsigned slot
image. `scripts/check_ramfunc.sh` fails the build if the SRAM-resident flash code
references anything in flash. Signing it:

```sh
python -m openlifu_sdk.io.LIFUCrypto sign --keys <research keys dir> \
    --firmware build/Release/open-lifu-transmitter-bl-updater.bin --version 2.1.0 --output updater-signed.bin
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

## Clinical variant (research unit to clinical)

`clinical-build.yml` builds the same code around the **clinical** bootloader: the clinical
build of `open-lifu-transmitter-bl` from the private artifact bucket, which trusts the Cloud KMS key and locks
the unit down (RDP level 1, write protection) on its first boot. It is still signed with the
**research** key, so a research unit's bootloader (1.0.x or 1.1.x research) launches it.
openlifu-fw-updater runs it to convert a calibrated research unit to clinical: the updater, then
the clinical application, then the clinical presets. With openlifu-sdk that is
`update(signed_app=<clinical app>, bl_updater=<this image>, replace_bootloader=True)`.

- **Version**: signed as **31.63.30** (FwVersion 65534), the top of the range, so the research
  bootloader takes it whatever research application or floor the unit has. Not 31.63.31: that is
  0xFFFF, what erased flash reads as. The updater erases the slot and the floor; the clinical
  application (2.1.0.x, Cloud KMS signed) installs next.
- **Approval and storage**: run by hand on a release tag in the `clinical` environment
  (`gh workflow run clinical-build.yml --ref <tag>`). The outputs (`<repo>-<tag>-clinical-signed.bin`,
  SBOM, `SHA256SUMS`, `clinical-build-manifest.json`) go to
  `gs://openwater-firmware-artifacts/<repo>/<tag>/` only (write-once), never to the GitHub release.
- **Keys**: the run fails unless `FW_SIGNING_KEY` is the research key the fielded and the current
  research bootloaders trust, and not the clinical key the embedded bootloader trusts. The
  manifest records both fingerprints.
- **One way**: once the clinical bootloader has locked the unit, its bootloader pages are write
  protected and this updater cannot run on it again; it refuses on a unit whose option bytes are
  not open in any case.

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
- A bootloader refuses this updater once the slot holds an application above `FW_VERSION`.
  The updater released with that application, signed at its version, covers those units.

## Licence

Apache-2.0, see `LICENSE`. The vendored CMSIS headers (`cmsis/`) are Apache-2.0
(Arm and STMicroelectronics).
