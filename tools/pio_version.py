Import("env")

import re
from pathlib import Path


VERSION_PATTERN = re.compile(
    r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
)
version_file = Path(env.subst("$PROJECT_DIR")) / "version.txt"
version_lines = version_file.read_text(encoding="ascii").splitlines()

if len(version_lines) != 1 or not VERSION_PATTERN.fullmatch(version_lines[0]):
    raise ValueError(f"Invalid semantic version in {version_file}")

version = version_lines[0]
if len(version) >= 24 or any(int(component) > 65535
                             for component in version.split(".")):
    raise ValueError(f"Unsupported semantic version in {version_file}")
env.Append(CPPDEFINES=[("MATEJA_CLOCK_VERSION", env.StringifyMacro(version))])
