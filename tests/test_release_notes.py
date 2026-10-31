#!/usr/bin/env python3
"""tools/release_notes.py: the notes of a release page, grouped by device."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(ROOT, "tools", "release_notes.py")
failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


LOG = """# Changelog

## 0.2.0

- **For all.** Everyone gets this,
  on two lines.
- [phone] **Phone only.** A flick goes further.
- [phone,tv] **Phone and TV.** Both.
- [tv] **TV only.** Calmer Home.
- [desktop] **Computer only.** Wheel.

## 0.1.0

- **Old note.** Nothing tagged then.
- Another old one.
"""


def run(*args):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "release_notes_log.md")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(LOG)
    r = subprocess.run([sys.executable, "-I", SCRIPT, *args, path], capture_output=True, text=True)
    check(r.returncode == 0, "the script runs %s: %s" % (args, r.stderr))
    return r.stdout


out = run("0.2.0")
heads = [l for l in out.splitlines() if l.startswith("### ")]
check(heads == ["### For everyone", "### Android phones and tablets", "### TVs (LG webOS, Samsung, Android TV, Fire TV)", "### Windows and computers"], "the groups, in order: %s" % heads)
check("- **For all.** Everyone gets this,\n  on two lines." in out, "a note keeps its continuation lines")
check(out.count("**Phone and TV.**") == 2, "a note for two devices is under both")
check("[phone]" not in out and "[tv]" not in out and "[desktop]" not in out, "the tags are gone")
phone = out.split("### Android phones and tablets")[1].split("###")[0]
check("**Phone only.**" in phone and "**TV only.**" not in phone, "the phone's group holds the phone's notes")

mob = run("0.2.0", "--mobile")
mheads = [l for l in mob.splitlines() if l.startswith("### ")]
check(mheads == ["### For everyone", "### Android phones and tablets"], "a mobile release: everyone and phones only: %s" % mheads)
check("**TV only.**" not in mob and "**Computer only.**" not in mob, "a mobile release has nothing for TVs or computers")

old = run("0.1.0")
check(old == "- **Old note.** Nothing tagged then.\n- Another old one.\n", "an untagged section prints as it stands: %r" % old)
check(run("9.9.9") == "", "a version with no section prints nothing")

if failures:
    print("%d check(s) failed" % failures)
    sys.exit(1)
print("release notes: ok")
