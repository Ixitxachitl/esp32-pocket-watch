"""
patch_nimconfig.py – PlatformIO pre-build script

Patches NimBLE-Arduino's nimconfig.h to add #undef guards before
macro definitions that conflict with ESP-IDF's sdkconfig.h.
Runs automatically before each build; safe to re-run (idempotent).
"""
Import("env")

import os
import re

def patch_nimconfig(source, target, env):
    nimconfig = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),
        env.subst("$PIOENV"),
        "NimBLE-Arduino", "src", "nimconfig.h"
    )
    if not os.path.isfile(nimconfig):
        return

    with open(nimconfig, "r") as f:
        content = f.read()

    # Already patched
    if "/* patched by patch_nimconfig.py */" in content:
        return

    # Macros that sdkconfig.h defines (with value) and nimconfig.h
    # redefines (without value or different value), causing warnings.
    macros = [
        "CONFIG_BT_NIMBLE_ROLE_CENTRAL",
        "CONFIG_BT_NIMBLE_ROLE_OBSERVER",
        "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL",
        "CONFIG_BT_NIMBLE_ROLE_BROADCASTER",
        "CONFIG_BT_NIMBLE_ACL_BUF_COUNT",
        "CONFIG_BT_NIMBLE_ACL_BUF_SIZE",
        "CONFIG_BT_NIMBLE_HCI_EVT_BUF_SIZE",
        "CONFIG_BT_NIMBLE_HCI_EVT_HI_BUF_COUNT",
        "CONFIG_BT_NIMBLE_HCI_EVT_LO_BUF_COUNT",
    ]

    for macro in macros:
        # Add #undef before each #define to prevent redefinition warning.
        # Handle both '#define MACRO' and '#  define MACRO' variants.
        # Patch ALL occurrences (a macro may appear in multiple #if branches).
        pattern = r'^(\s*#\s*define\s+' + re.escape(macro) + r')\b'
        replacement = r'#undef ' + macro + r'\n\1'
        content = re.sub(pattern, replacement, content, flags=re.MULTILINE)

    content = "/* patched by patch_nimconfig.py */\n" + content

    with open(nimconfig, "w") as f:
        f.write(content)

    print("  [patch_nimconfig] Patched nimconfig.h to suppress redefinition warnings")

# Run immediately during script load (before library compilation begins)
patch_nimconfig(None, None, env)
