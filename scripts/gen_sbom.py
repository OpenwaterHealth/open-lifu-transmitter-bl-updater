#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
gen_sbom.py: build-time CycloneDX 1.6 SBOM for an Open-LIFU bootloader updater image.

The same file is kept in open-lifu-console-bl-updater and open-lifu-transmitter-bl-updater.
Standard library only.

Derived at build time:
  - the root component (name, version, commit, build configuration, signed image version)
  - the CMSIS-Core and CMSIS Device versions, read from the vendored headers under cmsis/
  - the toolchain string and the build container digest
  - the embedded bootloader: repository, release tag, SHA-256 of the binary
  - the signing public-key fingerprint and signing domain
  - the generation timestamp (SOURCE_DATE_EPOCH if set, else now)

From the reviewed manifest (scripts/sbom-manifest.json): everything else (the C runtime
support, licences, evidence strings).

Usage (CI):
  python3 scripts/gen_sbom.py --root . --manifest scripts/sbom-manifest.json \\
      --name open-lifu-console-bl-updater --version 1.0.0 \\
      --toolchain "$(arm-none-eabi-gcc --version | head -1)" \\
      --build-image ghcr.io/openwaterhealth/stm32-build-env@sha256:... \\
      --embedded-bootloader open-lifu-console-bl 1.1.0-rc.3 <sha256> \\
      --signing-domain research --signing-key-fingerprint <sha256> \\
      --property openwater:signed-version=1.2.31 -o sbom.cdx.json
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import subprocess
import sys
import uuid
from pathlib import Path

ORG = {"name": "Openwater (Open Water Internet Inc.)", "url": ["https://www.openwater.health"]}
GH = "https://github.com/OpenwaterHealth/"
TOOL_VERSION = "1.0.0"


def git(root: Path, *args: str, default: str = "") -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=root, text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except Exception:  # noqa: BLE001 - no git: fall back to the default
        return default


def cmsis_core_version(root: Path) -> str | None:
    text = (root / "cmsis/core/cmsis_version.h").read_text(encoding="utf-8", errors="replace")
    main = re.search(r"#define\s+__CM_CMSIS_VERSION_MAIN\s+\(\s*(\d+)U?\)", text)
    sub = re.search(r"#define\s+__CM_CMSIS_VERSION_SUB\s+\(\s*(\d+)U?\)", text)
    return f"{main.group(1)}.{sub.group(1)}" if main and sub else None


def cmsis_device_version(root: Path) -> tuple[str, str] | None:
    """('STM32F0xx', '2.3.8') from cmsis/device/stm32f0xx.h or stm32l4xx.h."""
    for f in sorted((root / "cmsis/device").glob("stm32*xx.h")):
        text = f.read_text(encoding="utf-8", errors="replace")
        parts = {}
        for key in ("MAIN", "SUB1", "SUB2"):
            m = re.search(r"#define\s+__STM32\w+?_(?:DEVICE|CMSIS)_VERSION_" + key
                          + r"\s+\((0x[0-9A-Fa-f]+)\)", text)
            if m:
                parts[key] = int(m.group(1), 16)
        if len(parts) == 3:
            family = f.stem.upper().replace("XX", "xx")
            return family, f"{parts['MAIN']}.{parts['SUB1']}.{parts['SUB2']}"
    return None


def comp(name: str, version: str | None, ctype: str = "library", **kw) -> dict:
    c = {"type": ctype, "name": name}
    if version:
        c["version"] = version
    c["bom-ref"] = kw.pop("bom_ref", None) or re.sub(
        r"[^A-Za-z0-9._-]+", "-", f"{name}@{version or 'unversioned'}").strip("-")
    for k, v in kw.items():
        if v is None:
            continue
        if k == "properties":
            c["properties"] = [{"name": pk, "value": pv} for pk, pv in v.items()]
        else:
            c[k] = v
    return c


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=Path("."))
    ap.add_argument("--manifest", type=Path, required=True)
    ap.add_argument("--name", required=True, help="root component, e.g. open-lifu-console-bl-updater")
    ap.add_argument("--version", help="root version (default: git describe)")
    ap.add_argument("--config", default="Release")
    ap.add_argument("--toolchain", help="arm-none-eabi-gcc --version | head -1, from the build image")
    ap.add_argument("--build-image", help="build container reference, ideally with digest")
    ap.add_argument("--embedded-bootloader", nargs=3, metavar=("REPO", "TAG", "SHA256"))
    ap.add_argument("--signing-domain", help="research or clinical")
    ap.add_argument("--signing-key-fingerprint", help="SHA-256 (DER SPKI) of the signing public key")
    ap.add_argument("--property", action="append", default=[], metavar="NAME=VALUE")
    ap.add_argument("-o", "--output", type=Path, required=True)
    a = ap.parse_args()

    root = a.root.resolve()
    manifest = json.loads(a.manifest.read_text(encoding="utf-8"))
    version = a.version or git(root, "describe", "--tags", "--always", "--dirty", default="unknown")
    commit = git(root, "rev-parse", "HEAD", default="unknown")
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    ts = (dt.datetime.fromtimestamp(int(epoch), dt.timezone.utc) if epoch
          else dt.datetime.now(dt.timezone.utc)).strftime("%Y-%m-%dT%H:%M:%SZ")

    props = {"openwater:build-config": a.config, "openwater:commit": commit,
             "openwater:repository": GH + a.name}
    if a.build_image:
        props["openwater:build-image"] = a.build_image
    if a.signing_domain:
        props["openwater:signing-domain"] = a.signing_domain
    if a.signing_key_fingerprint:
        props["openwater:signing-public-key-sha256"] = a.signing_key_fingerprint
    for kv in a.property:
        k, _, v = kv.partition("=")
        props[k] = v
    ref = f"pkg:github/OpenwaterHealth/{a.name}@{version}"
    rootc = comp(a.name, version, "firmware", bom_ref=ref, supplier=ORG,
                 description=manifest.get("description"), purl=ref, properties=props)

    components: list[dict] = []
    core = cmsis_core_version(root)
    if core:
        components.append(comp("CMSIS-Core(M)", core, supplier={"name": "Arm Limited"},
                               purl=f"pkg:github/ARM-software/CMSIS_5@{core}",
                               licenses=[{"license": {"id": "Apache-2.0"}}],
                               properties={"openwater:evidence": "cmsis/core/cmsis_version.h __CM_CMSIS_VERSION_*"}))
    dev = cmsis_device_version(root)
    if dev:
        family, ver = dev
        components.append(comp(f"CMSIS Device {family}", ver,
                               supplier={"name": "STMicroelectronics", "url": ["https://www.st.com"]},
                               licenses=[{"license": {"id": "Apache-2.0"}}],
                               properties={"openwater:evidence": f"cmsis/device/{family.lower()}.h version macros"}))
    if not core or not dev:
        print("[gen_sbom] cannot read the CMSIS versions under cmsis/", file=sys.stderr)
        return 1
    if a.toolchain:
        m = re.search(r"(\d+\.\d+\.Rel\d+|\d+\.\d+\.\d+)", a.toolchain)
        components.append(comp("Arm GNU Toolchain (arm-none-eabi-gcc)", m.group(1) if m else None,
                               "application", supplier={"name": "Arm Limited"}, scope="excluded",
                               properties={"openwater:evidence": a.toolchain.strip()}))
    if a.build_image:
        img, _, digest = a.build_image.partition("@")
        components.append(comp(img, digest or "latest", "container", scope="excluded",
                               properties={"openwater:evidence": "docker pull in the build workflow"}))
    if a.embedded_bootloader:
        brepo, btag, bsha = a.embedded_bootloader
        bref = f"pkg:github/OpenwaterHealth/{brepo}@{btag}"
        components.append(comp(brepo, btag, "firmware", supplier=ORG, bom_ref=bref, purl=bref,
                               hashes=[{"alg": "SHA-256", "content": bsha}],
                               properties={"openwater:evidence": f"GitHub release {brepo} {btag}, research build",
                                           "openwater:consumers": "embedded in .rodata; written to 0x08000000 by the updater"}))

    derived = {c["name"] for c in components}
    for mc in manifest.get("components", []):
        if mc.get("name") in derived:
            continue
        c = dict(mc)
        c.setdefault("bom-ref", re.sub(r"[^A-Za-z0-9._-]+", "-",
                                       f"{c['name']}@{c.get('version', 'unversioned')}").strip("-"))
        components.append(c)

    bom = {
        "$schema": "http://cyclonedx.org/schema/bom-1.6.schema.json",
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "timestamp": ts,
            "lifecycles": [{"phase": "build"}],
            "tools": {"components": [comp("gen_sbom.py", TOOL_VERSION, "application", supplier=ORG)]},
            "authors": [{"name": "Openwater firmware CI"}],
            "supplier": ORG,
            "component": rootc,
        },
        "components": components,
        "dependencies": [{"ref": rootc["bom-ref"],
                          "dependsOn": [c["bom-ref"] for c in components if c.get("scope") != "excluded"]}]
                        + [{"ref": c["bom-ref"], "dependsOn": []} for c in components],
    }
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(bom, indent=2) + "\n", encoding="utf-8")
    digest = hashlib.sha256(a.output.read_bytes()).hexdigest()
    print(f"[gen_sbom] {a.output}: {a.name} {version}, {len(components)} components, sha256 {digest[:16]}...")
    for c in components:
        print(f"           - {c['type']:<11} {c['name']} {c.get('version', '')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
