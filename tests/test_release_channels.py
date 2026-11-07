#!/usr/bin/env python3
"""Dry run of the release steps that change with a "[tv]", "[mobile]" or "[desktop]" release, without GitHub: each
step's shell is taken out of .github/workflows and run in a scratch folder, with a stand-in `gh` that records what it
was asked to do. The workflow's `if:` conditions (which jobs a release runs, when it is published) are evaluated by a
small reading of GitHub's expression syntax, for every combination of flags.

  python3 tests/test_release_channels.py        (needs PyYAML; says so and skips without it)

A release for all devices writes every update file. One for some families (any of [tv], [mobile], [desktop]) builds
and writes only theirs, never takes the "latest" slot (every device's update address lives there) and puts its update
files into the release that is latest."""
import json
import os
import re
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


def workflow(name):
    return yaml.safe_load(open(os.path.join(ROOT, ".github", "workflows", name + ".yml")))


def step(wf, job, name):
    for s in workflow(wf)["jobs"][job]["steps"]:
        if s.get("name") == name:
            return s
    raise SystemExit("no step %r in %s/%s" % (name, wf, job))


# ---- GitHub's `if:` expressions, as far as these workflows use them ---------------------------------------
def gha_if(expr, msg, needs, ref="refs/heads/main", event="push", promoted="false"):
    py = " ".join(str(expr).split())
    py = re.sub(r"contains\(github\.event\.head_commit\.message, '([^']*)'\)", lambda m: "(%r in MSG)" % m.group(1), py)
    py = re.sub(r"startsWith\(github\.event\.head_commit\.message, '([^']*)'\)", lambda m: "MSG.startswith(%r)" % m.group(1), py)
    py = re.sub(r"startsWith\(github\.ref, '([^']*)'\)", lambda m: "REF.startswith(%r)" % m.group(1), py)
    py = re.sub(r"github\.ref == '([^']*)'", lambda m: "(REF == %r)" % m.group(1), py)
    py = re.sub(r"github\.event_name == '([^']*)'", lambda m: "(EVENT == %r)" % m.group(1), py)
    py = re.sub(r"needs\.(\w+)\.result == '(\w+)'", lambda m: "(NEEDS.get(%r, 'skipped') == %r)" % (m.group(1), m.group(2)), py)
    py = re.sub(r"needs\.promote\.outputs\.promoted == '(\w+)'", lambda m: "(PROMOTED == %r)" % m.group(1), py)
    py = py.replace("always()", "True").replace("&&", " and ").replace("||", " or ")
    py = re.sub(r"!(?!=)", " not ", py)
    return bool(eval(py, {"MSG": msg, "REF": ref, "EVENT": event, "NEEDS": needs, "PROMOTED": promoted}))


def runs(job, msg, ref="refs/heads/main", event="push"):
    return gha_if(workflow("build")["jobs"][job]["if"], msg, {}, ref, event)


T = " [tested here]"
FLAG_CASES = [   # message -> the build jobs that run (the test jobs are skipped by [tested here])
    ("0.10.111: headline" + T, {"webos", "android", "windows"}),
    ("0.10.111 [mobile]: headline" + T, {"android"}),
    ("0.10.111 [tv]: headline" + T, {"webos", "android"}),
    ("0.10.111 [desktop]: headline" + T, {"windows"}),
    ("0.10.111 [tv][desktop]: headline" + T, {"webos", "android", "windows"}),
    ("0.10.111 [mobile][desktop]: headline" + T, {"android", "windows"}),
    ("0.10.111 [tv][mobile]: headline" + T, {"webos", "android"}),
]
for msg, want in FLAG_CASES:
    got = {j for j in ("webos", "android", "windows") if runs(j, msg)}
    check(got == want, "%r runs %s, wanted %s" % (msg, sorted(got), sorted(want)))
check(all(runs(j, "Player: a fix") for j in ("webos", "android", "windows")), "an ordinary commit on main builds every package, as it always did")
check(all(runs(j, "", event="workflow_dispatch") for j in ("webos", "android", "windows")), "a run started by hand builds every family")
check(not any(runs(j, "Player: a fix", ref="refs/heads/feat/x") for j in ("webos", "android", "windows")), "an ordinary branch commit builds no package")

# Publish release and Move main: the jobs a release needs, and what a failed one costs
rel = workflow("build")["jobs"]["release"]["if"]
mv = workflow("build")["jobs"]["promote"]["if"]
OK, SK, FA = "success", "skipped", "failure"


def release_runs(msg, webos, android, windows, ui=SK, desktop=SK):
    return gha_if(rel, msg, {"webos": webos, "android": android, "windows": windows, "ui": ui, "desktop": desktop})


check(release_runs("0.10.111: x" + T, OK, OK, OK), "an all-device release publishes when everything built")
check(release_runs("0.10.111: x" + T, OK, FA, FA), "an all-device release does not wait for the APK or the Windows zip (a failure only leaves it out)")
check(not release_runs("0.10.111: x" + T, FA, OK, OK), "an all-device release waits for the LG package")
check(release_runs("0.10.111 [mobile]: x" + T, SK, OK, SK), "a [mobile] release publishes with only the APK")
check(not release_runs("0.10.111 [mobile]: x" + T, SK, FA, SK), "a [mobile] release does not publish without its APK")
check(release_runs("0.10.111 [tv]: x" + T, OK, OK, SK), "a [tv] release publishes with the LG package and the APK")
check(not release_runs("0.10.111 [tv]: x" + T, OK, FA, SK), "a [tv] release does not publish without the APK (Android TV and Fire TV read it)")
check(not release_runs("0.10.111 [tv]: x" + T, FA, OK, SK), "a [tv] release does not publish without the LG package")
check(release_runs("0.10.111 [desktop]: x" + T, SK, SK, OK), "a [desktop] release publishes with only the Windows files")
check(not release_runs("0.10.111 [desktop]: x" + T, SK, SK, FA), "a [desktop] release does not publish without its Windows files")
check(release_runs("0.10.111 [mobile][desktop]: x" + T, SK, OK, OK), "[mobile][desktop] publishes with the APK and the Windows files")
check(not release_runs("0.10.111 [mobile][desktop]: x" + T, SK, OK, FA), "[mobile][desktop] waits for both")
check(not release_runs("0.10.111: x", OK, OK, OK, ui=SK, desktop=SK), "without [tested here] the test jobs must have run")
check(release_runs("0.10.111: x", OK, OK, OK, ui=OK, desktop=OK), "with them green it publishes")
for msg, w, a, wi, want in (("0.10.111 [mobile]: x" + T, SK, OK, SK, True), ("0.10.111 [desktop]: x" + T, SK, SK, OK, True),
                            ("0.10.111 [tv]: x" + T, OK, OK, SK, True), ("0.10.111: x" + T, OK, OK, OK, True), ("0.10.111: x" + T, SK, OK, OK, False)):
    got = gha_if(mv, msg, {"webos": w, "android": a, "windows": wi, "ui": SK, "desktop": SK}, ref="refs/heads/feat/x")
    check(got == want, "Move main for %r with webos %s: %s, wanted %s" % (msg, w, got, want))

# ---- the shell steps ------------------------------------------------------------------------------------------
GH = r"""#!/bin/bash
# a stand-in for gh: every call is logged; what the steps ask about is answered from GH_FAKE_*
echo "gh $*" >> "$GH_LOG"
case "$1 $2" in
  "release view")
    case "$*" in
      *"--json tagName"*) echo "$GH_FAKE_LATEST"; exit 0;;
      *"--json name"*) echo "$GH_FAKE_NAME"; exit 0;;
    esac
    exit 0;;
  "release download")
    dir=""; pat=""; while [ $# -gt 0 ]; do [ "$1" = "--dir" ] && dir="$2"; [ "$1" = "--pattern" ] && pat="$2"; shift; done
    case ",$GH_FAKE_ASSETS," in *",$pat,"*) echo '{"version":"0.10.101"}' > "$dir/$pat"; exit 0;; esac
    exit 1;;
esac
exit 0
"""

GIT = r"""#!/bin/bash
# a stand-in for git: no network, ever. The releases repository "has commits" (so nothing is seeded)
echo "git $*" >> "$GH_LOG"
if [ "$1" = "ls-remote" ]; then echo "0123456789abcdef refs/heads/main"; fi
exit 0
"""


def run(wf, job, name, env, workdir):
    s = step(wf, job, name)
    e = dict(os.environ)
    e.update({k: str(v) for k, v in (s.get("env") or {}).items() if "${{" not in str(v)})
    e.update(env)
    return subprocess.run(["bash", "-eo", "pipefail", "-c", s["run"]], cwd=workdir, env=e, capture_output=True, text=True)


def scratch(files):
    d = tempfile.mkdtemp(prefix="relchan_")
    os.makedirs(os.path.join(d, "bin"))
    for n, body in (("gh", GH), ("git", GIT)):
        open(os.path.join(d, "bin", n), "w").write(body)
        os.chmod(os.path.join(d, "bin", n), 0o755)
    os.makedirs(os.path.join(d, "dist"))
    os.makedirs(os.path.join(d, "tools"))
    for t in ("release_families.sh", "release_notes.py"):
        shutil.copy(os.path.join(ROOT, "tools", t), os.path.join(d, "tools"))
    for f in files:
        open(os.path.join(d, "dist", f), "w").write("x" if f.endswith((".apk", ".ipk", ".exe")) else "{}")
    return d


def base_env(d, msg):
    return {"PATH": os.path.join(d, "bin") + os.pathsep + os.environ["PATH"], "GH_LOG": os.path.join(d, "gh.log"),
            "RELEASES_REPO": "owner/releases", "VERSION": "0.10.101", "TAG": "v0.10.101", "GH_TOKEN": "t", "MSG": msg}


APK = "Cuelight-0.10.101.apk"
# message -> (the files the builds leave in dist, the update files written, the title's family label)
RELEASES = [
    ("0.10.101: x", [APK, "repo.json", "windows.json"], {"android.json", "mobile.json"}, "", "all"),
    ("0.10.101 [mobile]: x", [APK], {"mobile.json"}, "mobile", "mobile"),
    ("0.10.101 [tv]: x", [APK, "repo.json"], {"android.json"}, "tv", "tv"),
    ("0.10.101 [desktop]: x", ["windows.json"], set(), "desktop", "desktop"),
    ("0.10.101 [tv][mobile]: x", [APK, "repo.json"], {"android.json", "mobile.json"}, "tv + mobile", "tv+mobile"),
]

# ---- which families a message names --------------------------------------------------------------------------------
for msg, _, _, label, kind in RELEASES:
    r = subprocess.run(["bash", "-c", '. tools/release_families.sh; echo "$FAM_TV$FAM_MOBILE$FAM_DESKTOP|$FAM_ALL|$FAM_FOR|$FAM_LABEL"'],
                       cwd=ROOT, env=dict(os.environ, MSG=msg), capture_output=True, text=True)
    out = r.stdout.strip()
    want = {"all": "111|1|tv,phone,desktop|", "mobile": "010|0|phone|mobile", "tv": "100|0|tv|tv", "desktop": "001|0|desktop|desktop",
            "tv+mobile": "110|0|tv,phone|tv + mobile"}[kind]
    check(out == want, "families of %r: %r, wanted %r" % (msg, out, want))

# ---- the Android update notice ----------------------------------------------------------------------------------
for msg, files, written, label, kind in RELEASES:
    d = scratch(files)
    r = run("build", "release", "The Android app's update notice", base_env(d, msg), d)
    check(r.returncode == 0, "the Android notice step runs (%s): %s" % (kind, r.stderr))
    got = {f for f in os.listdir(os.path.join(d, "dist")) if f in ("android.json", "mobile.json")}
    check(got == written, "%s release writes %s, got %s" % (kind, sorted(written), sorted(got)))
    if APK in files:
        for f in got:
            j = json.load(open(os.path.join(d, "dist", f)))
            check(j.get("version") == "0.10.101" and j.get("apkUrl", "").endswith("/v0.10.101/" + APK) and len(j.get("sha256", "")) == 64,
                  "%s names this version's APK and its hash (%s): %s" % (f, kind, j))
    if kind == "all":
        check(open(os.path.join(d, "dist", "android.json")).read() == open(os.path.join(d, "dist", "mobile.json")).read(), "both say the same")
    # the files of the other builds are left alone
    check({"repo.json", "windows.json"} & set(files) <= set(os.listdir(os.path.join(d, "dist"))), "the LG and Windows files stay (%s)" % kind)
    shutil.rmtree(d)

# ---- the release notes --------------------------------------------------------------------------------------------------
for msg, files, written, label, kind in RELEASES:
    d = scratch(files + (["Cuelight-0.10.101-windows-setup.exe"] if "windows.json" in files else []))
    # notes of its own, for every family, ahead of the real changelog: what the newest real release happens to say
    # (one phone note, say) must not decide what this checks
    real = open(os.path.join(ROOT, "CHANGELOG.md")).read()
    fixture = "## 0.99.0\n\n- **A note for everyone.** It reads the same on every device.\n- [tv] **A TV note.** Only TVs.\n- [phone] **A phone note.** Only phones.\n- [desktop] **A computer note.** Only computers.\n\n"
    open(os.path.join(d, "CHANGELOG.md"), "w").write(real.replace("# Changelog\n\n", "# Changelog\n\n" + fixture, 1))
    first = "0.99.0"
    e = base_env(d, msg); e["VERSION"] = first
    r = run("build", "release", "Release notes from CHANGELOG", e, d)
    check(r.returncode == 0, "the notes step runs (%s): %s" % (kind, r.stderr))
    notes = open(os.path.join(d, "notes.md")).read()
    tv = kind in ("all", "tv", "tv+mobile")
    check(("Homebrew Channel" in notes) == tv, "the LG install section is there only for a release with TVs (%s)" % kind)
    check(("A release for" in notes) == (kind != "all"), "a release for some families says so (%s)" % kind)
    check(("Install on Android" in notes) == (APK in files), "the Android section follows the APK (%s)" % kind)
    check(("Install on Windows" in notes) == ("windows.json" in files), "the Windows section follows the installer (%s)" % kind)
    shutil.rmtree(d)

# the notes of a section with tags, by family
sample = "## 9.9.9\n\n- [phone] **P.** a\n- [tv] **T.** b\n- [desktop] **D.** c\n- **E.** d\n"
for flags, have, lack in (([], "PTDE", ""), (["--mobile"], "PE", "TD"), (["--for", "tv"], "TE", "PD"), (["--for", "tv,phone"], "PTE", "D"), (["--for", "desktop"], "DE", "PT")):
    d = scratch([])
    open(os.path.join(d, "c.md"), "w").write(sample)
    r = subprocess.run([sys.executable, os.path.join(d, "tools", "release_notes.py"), "9.9.9"] + flags + [os.path.join(d, "c.md")], capture_output=True, text=True)
    out = r.stdout
    check(all(("**%s.**" % c) in out for c in have) and not any(("**%s.**" % c) in out for c in lack), "notes %s: %r" % (flags, out))
    shutil.rmtree(d)

# ---- the release's title ---------------------------------------------------------------------------------------------------
for msg, files, written, label, kind in RELEASES:
    title = "Cuelight v0.10.101 (%sbeta)" % (label + " " if label else "")
    d = scratch([APK])
    open(os.path.join(d, "notes.md"), "w").write("notes")
    e = base_env(d, msg); e["GH_FAKE_LATEST"] = "v0.10.100"
    r = run("build", "release", "Create the release in the public repository", e, d)
    log = open(os.path.join(d, "gh.log")).read() if os.path.exists(os.path.join(d, "gh.log")) else ""
    check(r.returncode == 0 and ("--title %s" % title) in log, "the release is titled %r (%s): %s %s" % (title, kind, r.stderr, log))
    shutil.rmtree(d)

# ---- Promote --------------------------------------------------------------------------------------------------------------
for name, assets, kind, label in (("Cuelight v0.10.101 (beta)", "repo.json,android.json,mobile.json,windows.json", "all", ""),
                                  ("Cuelight v0.10.101 (mobile beta)", "mobile.json", "mobile", "mobile"),
                                  ("Cuelight v0.10.101 (tv beta)", "repo.json,android.json", "tv", "tv"),
                                  ("Cuelight v0.10.101 (desktop beta)", "windows.json", "desktop", "desktop"),
                                  ("Cuelight v0.10.101 (tv + mobile beta)", "repo.json,android.json,mobile.json", "tv+mobile", "tv + mobile")):
    d = scratch([])
    e = base_env(d, ""); e["GH_FAKE_ASSETS"] = assets; e["GH_FAKE_LATEST"] = "v0.10.100"; e["GH_FAKE_NAME"] = name
    outf = os.path.join(d, "out"); e["GITHUB_OUTPUT"] = outf
    r = run("promote", "promote", "Make it the latest release", e, d)
    log = open(os.path.join(d, "gh.log")).read()
    check(r.returncode == 0, "Promote runs (%s release): %s" % (kind, r.stderr))
    if kind == "all":
        check("release edit v0.10.101" in log and "--latest " in log + " " and "--latest=false" not in log, "an all-device release becomes latest: %s" % log)
        check("partial=false" in open(outf).read(), "and says it is a whole one")
    else:
        check("--latest=false" in log and "--prerelease=false" in log, "a %s release is released but never takes the latest slot: %s" % (kind, log))
        check("--title Cuelight v0.10.101 (%s)" % label in log, "and is titled for its families (%s): %s" % (kind, log))
        up = [l for l in log.splitlines() if l.startswith("gh release upload v0.10.100")]
        check(len(up) == 1 and "--clobber" in up[0], "its update files go into the latest release: %s" % log)
        for f in assets.split(","):
            check(("upd/%s" % f) in up[0] or "upd/*.json" in up[0], "%s is handed on (%s)" % (f, kind))
        check(sorted(os.listdir(os.path.join(d, "upd"))) == sorted(assets.split(",")), "exactly its own update files (%s): %s" % (kind, os.listdir(os.path.join(d, "upd"))))
        check("partial=true" in open(outf).read(), "and says it is a partial one (no README refresh)")
    shutil.rmtree(d)

if failures:
    print("%d check(s) failed" % failures)
    sys.exit(1)
print("release channels: ok")
