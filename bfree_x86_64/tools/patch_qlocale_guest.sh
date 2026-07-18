#!/usr/bin/env bash
# Guest QLocale: skip readEnvironment and QVariant queries that corrupt ~QVariant on B-Free.
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/text/qlocale.cpp")
text = path.read_text()

# Ensure LocaleChanged guard
old_lc = """    // tell the object that the system locale has changed.
    sys_locale->query(QSystemLocale::LocaleChanged);

    // Populate system locale with fallback as basis"""
new_lc = """    // tell the object that the system locale has changed.
    if (!qEnvironmentVariableIsSet("BFREE_GUEST_FIXED_LOCALE"))
        sys_locale->query(QSystemLocale::LocaleChanged);

    // Populate system locale with fallback as basis"""
if old_lc in text:
    text = text.replace(old_lc, new_lc, 1)

# Full guest early-return after fallback copy
old_body = """    // Populate system locale with fallback as basis
    systemLocaleData = locale_data[sys_locale->fallbackLocaleIndex()];

    QVariant res = sys_locale->query(QSystemLocale::LanguageId);"""
new_body = """    // Populate system locale with fallback as basis
    systemLocaleData = locale_data[sys_locale->fallbackLocaleIndex()];

    if (qEnvironmentVariableIsSet("BFREE_GUEST_FIXED_LOCALE")) {
        if (default_data == &systemLocaleData)
            QLocalePrivate::s_generation.fetchAndAddRelaxed(1);
        return;
    }

    QVariant res = sys_locale->query(QSystemLocale::LanguageId);"""

if old_body not in text:
    if new_body in text:
        print("[patch_qlocale_guest] already applied (full stub)")
    else:
        raise SystemExit("qlocale.cpp: body patch anchor missing")
else:
    text = text.replace(old_body, new_body, 1)
    path.write_text(text)
    print("[patch_qlocale_guest] ok (full stub)")
PY
