# Take breaks: how the schedules are derived

Settings > Take breaks pauses playback at moments that matter to the viewer. A banner
warns a few minutes ahead, the picture pauses, a calm page names the moment with a
chime (or a recording the viewer provides), and playback continues by itself. This
page records where each schedule comes from and what the app actually computes.

## Sun-based times are computed on the TV

Nothing is fetched for the times themselves. `src/suntimes.c` is a port of the
PrayTimes.org algorithm (sun declination and equation of time from the Julian date,
hour angle for a given depression angle), which also yields plain sunrise, solar
noon, sunset and civil twilight for the other traditions. Tests
(`tests/test_sun.c`) hold the results to the published sunrise/sunset for Makkah and
New York within a few minutes and to the Umm al-Qura Fajr. The TV's own time zone
(`tm_gmtoff`) is used, so daylight-saving changes need no setting.

The location is needed for every sun-based schedule. The viewer types latitude and
longitude (the phone page makes that painless) or presses *Find my location*, which
asks ip-api.com once and keeps the answer. ip-api is free for non-commercial use and
is usually right to the city; nothing else is sent to it.

## Schedules

| Schedule | Events | Source of the rule |
| --- | --- | --- |
| Islamic prayer times | Fajr, Dhuhr, Asr, Maghrib, Isha | Fajr/Isha depression angles per method: MWL 18/17, ISNA 15/15, Egypt 19.5/17.5, Umm al-Qura 18.5 and Isha = Maghrib + 90 min, Karachi 18/18, Tehran 17.7/14 with Maghrib 4.5, Jafari 16/14 with Maghrib 4; Asr shadow factor 1 (standard) or 2 (Hanafi); above 47 degrees twilight is capped at the middle of the night ([PrayTimes methods](https://praytimes.org/wiki/Calculation_Methods), [praytimes.org/docs/methods](https://praytimes.org/docs/methods)). The same conventions are what the [Aladhan API](https://aladhanpy.readthedocs.io/en/latest/api.html) serves, which the app does not need to call. |
| Christian (Angelus) | 6:00, 12:00, 18:00 | The Angelus is traditionally prayed at six, noon and six, echoing the monastic hours ([Ave Maria Press](https://www.avemariapress.com/pages/angelus), [Liturgy Office](https://www.liturgyoffice.org/Prayer/Traditional/Mary.shtml)). Lauds, Vespers and Compline have no fixed clock times, so they are left to the custom slots. |
| Jewish | Shacharit from sunrise, Mincha from Mincha Gedola, Maariv from nightfall | Mincha Gedola is sunrise plus 6.5 halachic hours (a twelfth of the day); nightfall uses 8.5 degrees below the horizon, as Hebcal's zmanim do ([Hebcal zmanim](https://www.hebcal.com/home/1663/zmanim-halachic-times), [KosherJava](https://kosherjava.com/zmanim-project/how-to-use-the-zmanim-api/)). These are the earliest times of each window, not deadlines. |
| Hindu (Sandhya) | sunrise, solar noon, sunset | Sandhyavandanam is performed at dawn, noon and dusk ([Wikipedia](https://en.wikipedia.org/wiki/Sandhyavandanam)). |
| Sikh (Nitnem) | Amrit Vela 4:00, Rehras Sahib at sunset, Kirtan Sohila 22:00 | Morning prayers in the Amrit Vela window, Rehras after sunset, Sohila at bedtime ([Nitnem](https://en.wikipedia.com/wiki/Nitnem), [Sikh prayers](https://www.2019wsj.org/wp-content/uploads/2019/07/Sikh-Prayers.pdf)). The two clock times are the common practice, not scripture; the custom slots override them. |
| Baha'i | sunrise, solar noon, sunset | The medium obligatory prayer is said between sunrise and noon, noon and sunset, and sunset to two hours after; the breaks mark the start of each window ([Obligatory Baha'i prayers](https://en.wikipedia.org/wiki/Obligatory_Bah%C3%A1%CA%BC%C3%AD_prayers), [Baha'i Library](https://bahai-library.com/warwick_daily_devotions)). |
| Buddhist | 6:00, 18:00 | Morning and evening chanting are customary in most traditions but set by the community, so these are defaults to adjust. |
| My own times only | up to three times | Any schedule can add the same three custom times, a stretch reminder every N minutes of watching, and a bedtime that stops playback. |

## Audio

A soft chime plays through SDL audio (silent where the platform gives no device). For
an adhan, a bell or a chant the viewer provides a URL of a recording they have the
right to use; it plays through the media pipeline at the start of the break, and a
second URL (a dua, a closing bell) plays at the end before playback continues. No
recording is bundled: the adhan's words are religious text, but each recording is
someone's performance and most downloads carry no licence. Openly licensed options
exist (for example a CC0 recording on [Freesound](https://freesound.org/people/sonically_sound/sounds/639494/)
and items on the [Internet Archive](https://archive.org/details/adhan.notifications)); check the licence of
whatever you point the app at.

## What the app does not do

- It does not claim religious authority: all times are the standard calculations with
  the method the viewer picks, and the viewer can shift any of them with the custom slots.
- It does not fetch times from a server, so there is no account, key or daily request.
- It does not override the viewer: Back or OK ends a break at once, and *Skip breaks
  today* silences the rest of the day.
