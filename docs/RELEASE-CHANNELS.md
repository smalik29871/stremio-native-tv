# Release channels

Every device family has its own update file, so a release made for one family moves no other.

| Family | Update file | Read by |
|---|---|---|
| TV: LG webOS, Samsung Tizen | `repo.json` | the Homebrew Channel and the app |
| Mobile: Android phones and tablets | `mobile.json` | the app |
| Android TV and Fire TV | `android.json` | the app (the same APK as mobile; apps from before `mobile.json` also read this) |
| Windows | `windows.json` | the app |

Each file is read from one of two streams: **stable** (`releases/latest/download/…`, the release everyone has)
and **beta** (`releases/download/beta/…`, devices with Settings > About > Beta updates on).

## Releasing for all devices (as before)

`0.10.101: headline, headline [tested here]` builds LG, Android and Windows, publishes the pre-release with
every update file (including `mobile.json`, the same as `android.json`) and puts them on the `beta` release.
**Promote** makes it the latest release.

## Releasing for some devices only

Put a flag after the version, one or more of them: the families are the three below, and each flag names
the builds and update files that release makes.

| Flag | For | Builds | Update files written |
|---|---|---|---|
| `[tv]` | LG webOS, Samsung, Android TV, Fire TV | LG package, Android APK | `repo.json`, `android.json` |
| `[mobile]` | Android phones and tablets | Android APK | `mobile.json` |
| `[desktop]` | Windows | Windows installer and zip | `windows.json` |

`0.10.101 [mobile]: headline, headline [tested here]` is a mobile release. `0.10.101 [tv][mobile]: ...` is for
those two together (no Windows). No flag is every family. (`tools/release_families.sh` reads the flags; the
Android APK is one file for phones, tablets and Android TV, so `[tv]` builds it too, but writes only `android.json`.)

- Only those builds are made; the others are skipped. The release is titled *(mobile beta)*, *(tv beta)*,
  *(tv + mobile beta)* ... and its notes say who it is for (and list only that family's and everyone's notes).
- Only those update files are written, so no other device is offered it. The `beta` stream gets the new files;
  the others stay as they were.
- It publishes only when every build it asked for succeeded (an all-device release still leaves out an APK or a
  Windows zip that failed, as before).
- **Promote** with that version marks it released but does not make it the latest release (that would take every
  other device's update address with it). Its update files are put into the release that *is* latest, which is where
  each device reads them. The run's summary lists each release and who it is for.
- The version number is shared: each release, for any family, takes the next number.

## Things to know

- Phones with an app from before `mobile.json` read `android.json`. A `[mobile]` release does not move it, so
  those phones stay on the last release that wrote `android.json` (an all-device or `[tv]` one).
- A phone that finds no `mobile.json` (a stable stream whose latest release has none yet) falls back to
  `android.json`.
- `tests/test_release_channels.py` runs the release steps against a stand-in `gh` and `git` (no GitHub involved)
  and evaluates the workflow's `if:` conditions (which builds each flag runs, when a release publishes) for every
  combination of flags. It cannot test GitHub itself: the first real `[tv]`, `[mobile]` and `[desktop]` releases
  are the real test, so watch their runs.
- Promote in version order. A whole release promoted after a later `[mobile]` one puts that one's older `mobile.json`
  back as the latest.

## Notes per device

An item in CHANGELOG.md can name who it is for with a tag after the dash:

    - [phone] **Scrolling carries further.** A flick leaves faster and glides longer.
    - [phone,tv] **Both.** ...
    - **For everyone.** (no tag)

`[phone]` is Android phones and tablets, `[tv]` the TVs (LG, Samsung, Android TV, Fire TV), `[desktop]` the
computers. The app's What's new page shows each device only its own notes (and skips the page after an update
when none apply), and `tools/release_notes.py` groups the release page on GitHub the same way. Give every
release at least one untagged note, so no device is left with an empty page. A section with no tags at all
(every release before 0.10.100) shows as it always did.
