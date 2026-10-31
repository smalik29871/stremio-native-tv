/* Which update file a device reads. Each family has its own, so a release made for one of them
 * moves no other: a phone-only release changes mobile.json and nothing a TV or a computer reads.
 *   LG webOS and Samsung Tizen   repo.json      (the Homebrew Channel's)
 *   Android phones and tablets   mobile.json
 *   Android TV and Fire TV       android.json   (the same APK; also what apps from before mobile.json read)
 *   Windows                      windows.json
 * An all-device release writes every one of them; a mobile one only mobile.json. */
#ifndef SNTV_UPDATE_CHANNEL_H
#define SNTV_UPDATE_CHANNEL_H

static inline const char *update_manifest_file(int android, int windows, int handheld)
{
    if (android) return handheld ? "mobile.json" : "android.json";
    if (windows) return "windows.json";
    return "repo.json";
}

#endif
