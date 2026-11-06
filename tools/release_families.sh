#!/bin/bash
# Which device families a release is for, from the commit message in $MSG (source this file).
#
#   0.10.111 [tv]: ...            LG webOS, Samsung, Android TV, Fire TV
#   0.10.111 [mobile]: ...        Android phones and tablets
#   0.10.111 [desktop]: ...       Windows
#   0.10.111 [tv][mobile]: ...    any two (or all three) together
#   0.10.111: ...                 no flag: every family, as before
#
# Sets FAM_TV, FAM_MOBILE, FAM_DESKTOP (1 or 0), FAM_ALL (1 when no flag was given), FAM_FOR (the families for
# tools/release_notes.py: tv,phone,desktop in that order, those chosen), FAM_LABEL ("" for all, else "tv",
# "mobile", "tv + mobile" ...: the release's title) and FAM_PEOPLE ("TVs and Android phones and tablets" ...).
# docs/RELEASE-CHANNELS.md says what each family gets.
FAM_TV=0; FAM_MOBILE=0; FAM_DESKTOP=0; FAM_ALL=0
case "$MSG" in *"[tv]"*) FAM_TV=1;; esac
case "$MSG" in *"[mobile]"*) FAM_MOBILE=1;; esac
case "$MSG" in *"[desktop]"*) FAM_DESKTOP=1;; esac
if [ $((FAM_TV + FAM_MOBILE + FAM_DESKTOP)) -eq 0 ]; then FAM_TV=1; FAM_MOBILE=1; FAM_DESKTOP=1; FAM_ALL=1; fi
FAM_FOR=""; FAM_LABEL=""; FAM_PEOPLE=""
fam_add() { FAM_FOR="${FAM_FOR:+$FAM_FOR,}$1"; FAM_LABEL="${FAM_LABEL:+$FAM_LABEL + }$2"; FAM_PEOPLE="${FAM_PEOPLE:+$FAM_PEOPLE, }$3"; }
if [ "$FAM_TV" = 1 ]; then fam_add tv tv "TVs (LG webOS, Samsung, Android TV, Fire TV)"; fi
if [ "$FAM_MOBILE" = 1 ]; then fam_add phone mobile "Android phones and tablets"; fi
if [ "$FAM_DESKTOP" = 1 ]; then fam_add desktop desktop "Windows"; fi
if [ "$FAM_ALL" = 1 ]; then FAM_LABEL=""; fi
