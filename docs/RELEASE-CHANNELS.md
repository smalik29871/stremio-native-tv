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

## Releasing for mobile only

Put `[mobile]` in the commit message: `0.10.101 [mobile]: headline, headline [tested here]`.

- Only the Android build is made (the LG and Windows jobs are skipped); the release is titled *(mobile beta)*.
- Only `mobile.json` is written: no `repo.json`, `android.json` or `windows.json`, so no TV, Android TV or
  computer is offered it. The beta stream gets the new `mobile.json`; the others stay as they were.
- **Promote** with that version marks it released but does not make it the latest release (that would take
  every TV's and computer's update address with it). Its `mobile.json` is put into the release that *is*
  latest, which is where phones look. The run's summary lists each release and who it is for.
- The version number is shared: a mobile release uses the next number, and the next all-device release the
  one after it.

## Things to know

- Phones with an app from before `mobile.json` read `android.json`. The first release with this change must
  be an **all-device** one; mobile-only releases work from the one after it.
- A phone that finds no `mobile.json` (a stable stream whose latest release has none yet) falls back to
  `android.json`.
- `tests/test_release_channels.py` runs the changed workflow steps against a stand-in `gh` and `git` (no
  GitHub involved). It cannot test the workflow's `if:` conditions or GitHub itself: the first real
  `[mobile]` release is the real test, so watch its run.

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
