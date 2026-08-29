"""Inject bench credentials from secrets.ini as build flags.

Keeps the WiFi password and the Stripe key out of the source tree, out of
shell history and out of the build command line. secrets.ini is gitignored;
secrets.ini.example documents the shape.

Absent or incomplete, the build still succeeds -- main.cpp compiles with empty
credentials and the deck stays on "--" rather than inventing numbers. That is
deliberate: a missing key should look obviously unconfigured, not plausible.

This is a bench convenience. Stage 3 moves provisioning to the captive portal
and NVS, where the customer enters the key and it is never compiled in.
"""

import configparser
import os

Import("env")  # noqa: F821  (injected by SCons/PlatformIO)

SECRETS = os.path.join(env.subst("$PROJECT_DIR"), "secrets.ini")  # noqa: F821


def _escape(value):
    """Quote a string so it survives the shell and lands as a C string."""
    return '\\"%s\\"' % value.replace("\\", "\\\\").replace('"', '\\"')


if os.path.isfile(SECRETS):
    parser = configparser.ConfigParser()
    parser.read(SECRETS)

    if parser.has_section("secrets"):
        flags = []
        for key, macro in (
            ("wifi_ssid", "WIFI_SSID"),
            ("wifi_pass", "WIFI_PASS"),
            ("stripe_key", "STRIPE_KEY"),
        ):
            value = parser["secrets"].get(key, "").strip()
            if value:
                flags.append("-D%s=%s" % (macro, _escape(value)))

        if flags:
            env.Append(BUILD_FLAGS=flags)  # noqa: F821
            # Never print the values themselves.
            print("secrets.ini: injected %d credential(s)" % len(flags))
else:
    print("secrets.ini not found -- building without credentials "
          "(deck will show '--'). See secrets.ini.example.")
