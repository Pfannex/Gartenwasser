"""PlatformIO Pre-Build-Script: zaehlt build_number.txt bei jedem Build um 1 hoch und
generiert daraus include/BuildNumber.h. Laeuft automatisch (siehe platformio.ini,
extra_scripts). build_number.txt wird versioniert (persistenter Zaehler), BuildNumber.h
ist ein reines Build-Artefakt und wird bei jedem Build neu erzeugt (siehe .gitignore).
"""

import os

Import("env")  # noqa: F821 (von PlatformIO zur Laufzeit injiziert)

project_dir = env.get("PROJECT_DIR")
counter_file = os.path.join(project_dir, "build_number.txt")
header_file = os.path.join(project_dir, "include", "BuildNumber.h")

try:
    with open(counter_file) as f:
        build_number = int(f.read().strip())
except (FileNotFoundError, ValueError):
    build_number = 0

build_number += 1

with open(counter_file, "w") as f:
    f.write(str(build_number))

with open(header_file, "w") as f:
    f.write(
        "// Automatisch generiert von tools/increment_build_number.py bei jedem Build - "
        "nicht manuell bearbeiten.\n"
        "// Persistenter Zaehler liegt in build_number.txt (versioniert), diese Datei ist "
        "reines Build-Artefakt.\n"
        "#pragma once\n\n"
        f'constexpr const char *kBuildNumber = "{build_number:05d}";\n'
    )

print("Build-Nummer: {:05d}".format(build_number))
