#!/usr/bin/env python3
"""Dry run of the release steps that change with a "[mobile]" release, without GitHub: each step's
shell is taken out of .github/workflows and run in a scratch folder, with a stand-in `gh` that
records what it was asked to do.

  python3 tests/test_release_channels.py        (needs PyYAML; says so and skips without it)

A release for all devices writes every update file; a [mobile] one only mobile.json, never takes the
"latest" slot (every TV's and computer's update address lives there) and puts its phone file into the
release that is latest."""
import json
import os
import shutil
import subprocess
import sys
import tempfile

try:
    import yaml
except ImportError:
    print("PyYAML is not installed: skipped")
    sys.exit(0)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
failures = 0


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


def step(workflow, job, name):
    d = yaml.safe_load(open(os.path.join(ROOT, ".github", "workflows", workflow + ".yml")))
    for s in d["jobs"][job]["steps"]:
        if s.get("name") == name:
            return s
    raise SystemExit("no step %r in %s/%s" % (name, workflow, job))


GH = r"""#!/bin/bash
# a stand-in for gh: every call is logged; what the steps ask about is answered from GH_FAKE_*
echo "gh $*" >> "$GH_LOG"
case "$1 $2" in
  "release view")
    if [ "$3" = "--repo" ]; then   # no tag: the release that is latest
      case "$*" in *tagName*) echo "$GH_FAKE_LATEST"; exit 0;; esac
    fi
    case "$*" in
      *"--json assets"*) tr ',' '\n' <<< "$GH_FAKE_ASSETS"; exit 0;;
    esac
    exit 0;;
  "release download")
    dir=""; while [ $# -gt 0 ]; do [ "$1" = "--dir" ] && dir="$2"; shift; done
    echo '{"version":"0.10.101"}' > "$dir/mobile.json"; exit 0;;
esac
exit 0
"""


GIT = r"""#!/bin/bash
# a stand-in for git: no network, ever. The releases repository "has commits" (so nothing is seeded)
echo "git $*" >> "$GH_LOG"
if [ "$1" = "ls-remote" ]; then echo "0123456789abcdef refs/heads/main"; fi
exit 0
"""


def run(workflow, job, name, env, workdir, extra_env=None):
    s = step(workflow, job, name)
    e = dict(os.environ)
    e.update({k: str(v) for k, v in (s.get("env") or {}).items() if "${{" not in str(v)})
    e.update(env)
    r = subprocess.run(["bash", "-eo", "pipefail", "-c", s["run"]], cwd=workdir, env=e, capture_output=True, text=True)
    return r


def scratch(files):
    d = tempfile.mkdtemp(prefix="relchan_")
    os.makedirs(os.path.join(d, "bin"))
    open(os.path.join(d, "bin", "gh"), "w").write(GH)
    os.chmod(os.path.join(d, "bin", "gh"), 0o755)
    open(os.path.join(d, "bin", "git"), "w").write(GIT)
    os.chmod(os.path.join(d, "bin", "git"), 0o755)
    os.makedirs(os.path.join(d, "dist"))
    for f in files:
        open(os.path.join(d, "dist", f), "w").write("x" if f.endswith((".apk", ".ipk", ".exe")) else "{}")
    return d


def base_env(d):
    return {"PATH": os.path.join(d, "bin") + os.pathsep + os.environ["PATH"], "GH_LOG": os.path.join(d, "gh.log"),
            "RELEASES_REPO": "owner/releases", "VERSION": "0.10.101", "TAG": "v0.10.101", "GH_TOKEN": "t"}


# ---- the Android update notice ------------------------------------------------------------------
for mobile in ("false", "true"):
    d = scratch(["Cuelight-0.10.101.apk", "repo.json", "windows.json"] if mobile == "false" else ["Cuelight-0.10.101.apk"])
    e = base_env(d); e["MOBILE"] = mobile
    r = run("build", "release", "The Android app's update notice", e, d)
    check(r.returncode == 0, "the Android notice step runs (mobile=%s): %s" % (mobile, r.stderr))
    files = sorted(os.listdir(os.path.join(d, "dist")))
    mj = json.load(open(os.path.join(d, "dist", "mobile.json"))) if "mobile.json" in files else {}
    check(mj.get("version") == "0.10.101" and mj.get("apkUrl", "").endswith("/v0.10.101/Cuelight-0.10.101.apk") and len(mj.get("sha256", "")) == 64,
          "mobile.json names this version's APK and its hash (mobile=%s): %s" % (mobile, mj))
    if mobile == "false":
        check("android.json" in files and "mobile.json" in files, "an all-device release writes android.json and mobile.json: %s" % files)
        check(open(os.path.join(d, "dist", "android.json")).read() == open(os.path.join(d, "dist", "mobile.json")).read(), "both say the same")
    else:
        check("android.json" not in files, "a mobile release writes no android.json (Android TV must not be offered it): %s" % files)
        check(not {"repo.json", "windows.json"} & set(files), "and none for LG or Windows: %s" % files)
    shutil.rmtree(d)

# ---- the release notes ----------------------------------------------------------------------------
for mobile in ("false", "true"):
    d = scratch(["Cuelight-0.10.101.apk"])
    shutil.copy(os.path.join(ROOT, "CHANGELOG.md"), d)
    # the newest section of the changelog stands in for this version's
    first = [l for l in open(os.path.join(d, "CHANGELOG.md")) if l.startswith("## ")][0].strip()[3:]
    e = base_env(d); e["MOBILE"] = mobile; e["VERSION"] = first
    r = run("build", "release", "Release notes from CHANGELOG", e, d)
    check(r.returncode == 0, "the notes step runs (mobile=%s): %s" % (mobile, r.stderr))
    notes = open(os.path.join(d, "notes.md")).read()
    check(("Homebrew Channel" in notes) == (mobile == "false"), "the LG install section is there only for an all-device release (mobile=%s)" % mobile)
    check(("A mobile release" in notes) == (mobile == "true"), "a mobile release says so (mobile=%s)" % mobile)
    check("Install on Android" in notes, "the Android section is always there (mobile=%s)" % mobile)
    shutil.rmtree(d)

# ---- the release's title ---------------------------------------------------------------------------
for mobile, title in (("false", "Cuelight v0.10.101 (beta)"), ("true", "Cuelight v0.10.101 (mobile beta)")):
    d = scratch(["Cuelight-0.10.101.apk"])
    open(os.path.join(d, "notes.md"), "w").write("notes")
    e = base_env(d); e["MOBILE"] = mobile; e["GH_FAKE_LATEST"] = "v0.10.100"
    r = run("build", "release", "Create the release in the public repository", e, d)
    log = open(os.path.join(d, "gh.log")).read() if os.path.exists(os.path.join(d, "gh.log")) else ""
    check(r.returncode == 0 and ('--title %s' % title) in log, "the release is titled %r (mobile=%s): %s %s" % (title, mobile, r.stderr, log))
    shutil.rmtree(d)

# ---- Promote -----------------------------------------------------------------------------------------
for assets, kind in (("repo.json,android.json,mobile.json,windows.json,x.ipk", "all"), ("Cuelight-0.10.101.apk,mobile.json", "mobile")):
    d = scratch([])
    e = base_env(d); e["GH_FAKE_ASSETS"] = assets; e["GH_FAKE_LATEST"] = "v0.10.100"
    outf = os.path.join(d, "out"); e["GITHUB_OUTPUT"] = outf
    r = run("promote", "promote", "Make it the latest release", e, d)
    log = open(os.path.join(d, "gh.log")).read()
    check(r.returncode == 0, "Promote runs (%s release): %s" % (kind, r.stderr))
    if kind == "all":
        check("release edit v0.10.101" in log and "--latest " in log + " " and "--latest=false" not in log, "an all-device release becomes latest: %s" % log)
        check("mobile=false" in open(outf).read(), "and says it is not a mobile one")
    else:
        check("--latest=false" in log and "--prerelease=false" in log, "a mobile release is released but never takes the latest slot: %s" % log)
        check("release upload v0.10.100 mobile/mobile.json" in log and "--clobber" in log, "its phone file goes into the latest release: %s" % log)
        check("mobile=true" in open(outf).read(), "and says it is a mobile one (no README refresh)")
    shutil.rmtree(d)

if failures:
    print("%d check(s) failed" % failures)
    sys.exit(1)
print("release channels: ok")
