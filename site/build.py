#!/usr/bin/env python3
"""Builds the product site into dist/.

    python3 build.py            # writes dist/
    python3 build.py --publish  # and publishes it with the Spacefast CLI (sf)

Publishing needs the Space and the sf profile to publish with, from the
environment (STICKY_SITE_SPACE, STICKY_SITE_PROFILE) or from a git-ignored
site/publish.local with the same KEY=value lines. Building needs neither.
dist/.spacefast (the link to the Space) is kept between builds.

The product name lives in NAME below and nowhere else: the pages say {{NAME}}.

Screenshots: drop a PNG into src/shots/ with one of these names and rebuild.
Until a file is there, its frame shows a "screenshot coming" placeholder.
  home-portrait.png  home-landscape.png  quick.png
  calendar-day.png   calendar-week.png   notes.png   keyboard.png
(home-landscape.png also replaces the Timer picture in the hero.)
Captures must hold demo data only: no names, WiFi names, addresses or places.

The setup app is src/setup/ (served at setup/; setup/?demo runs it against a
simulated board). Its tests: node test/run.mjs, from this folder.
"""
import os
import pathlib
import shutil
import subprocess
import sys

NAME = "Sticky OS"   # working name: change it here only

HERE = pathlib.Path(__file__).resolve().parent
SRC, DIST = HERE / "src", HERE / "dist"


def main():
    keep = {".spacefast", ".gitignore"}   # sf's link to the Space survives a rebuild
    DIST.mkdir(exist_ok=True)
    for old in DIST.iterdir():
        if old.name in keep:
            continue
        shutil.rmtree(old) if old.is_dir() else old.unlink()
    shutil.copytree(SRC, DIST, dirs_exist_ok=True)
    for page in DIST.rglob("*.html"):
        page.write_text(page.read_text().replace("{{NAME}}", NAME))
    shots = sorted(p.name for p in (DIST / "shots").glob("*.png"))
    print(f"built {DIST} as {NAME!r}; screenshots: {', '.join(shots)}")


def publish_settings():
    """STICKY_SITE_SPACE and STICKY_SITE_PROFILE: the environment, else publish.local."""
    found = {}
    local = HERE / "publish.local"
    if local.exists():
        for line in local.read_text().splitlines():
            key, sep, value = line.strip().partition("=")
            if sep and not key.startswith("#"):
                found[key.strip()] = value.strip()
    found.update({k: v for k, v in os.environ.items() if k.startswith("STICKY_SITE_")})
    missing = [k for k in ("STICKY_SITE_SPACE", "STICKY_SITE_PROFILE") if not found.get(k)]
    if missing:
        sys.exit("can't publish: set " + " and ".join(missing) +
                 " in the environment or in site/publish.local (KEY=value lines; git-ignored)")
    return found["STICKY_SITE_SPACE"], found["STICKY_SITE_PROFILE"]


if __name__ == "__main__":
    if "--publish" in sys.argv[1:]:
        space, profile = publish_settings()   # checked before building: a clear error first
        main()
        subprocess.run(["sf", "publish", str(DIST), "--profile", profile, "--space", space,
                        "--mode", "website", "-y", "--wait"], check=True)
    else:
        main()
