# Working in this repo

## Commits
- One line, no body and no trailers: `Area: what changed, what else changed`
  (e.g. `Settings: labelled background chips, no text under the swatches on phones`).
- A release commit starts with the version: `0.10.86: headline, headline, headline`.
  A commit message starting `0.x` triggers the release workflow.

## Branches
- `type/short-kebab-name`, e.g. `feat/phone-player`, `fix/subtitle-overlap`.

## Testing
- Fake values in tests come from real devices, never from what the code needs to pass. Frame
  times include the wait for the display: about 17 ms at 60 Hz when a frame keeps up, 33 ms
  when it misses one; a TV is never faster than that. Memory, network speeds and decode limits
  likewise follow what the LG, the Shield and phones report (Diagnostics > Speed, the log).
- Anything that depends on the hardware (frame timing, memory, decoding, remote input) is
  checked against what real devices report before it is called done, not only the test machine.
- The full suite passes before any release.

## Releases
- Wait for the owner's go-ahead before a release commit.
- A release goes out as a beta: devices with Settings > About > Beta updates on get it first.
  It goes to everyone only when the owner runs the Promote workflow with its version.
