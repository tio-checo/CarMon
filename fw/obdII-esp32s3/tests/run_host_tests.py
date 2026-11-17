"""Run software-only port regressions; no board or ESP-IDF installation required."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
compiler = shlex.split(os.environ.get("CC", "cc"))
includes = [root/"components"/name/"include" for name in
            ("platform", "rtl", "communication", "carmon", "devices", "storage")]
with tempfile.TemporaryDirectory(prefix="carmon-host-tests-") as directory:
    for name, sources, extra in (
        ("crc", ["tests/test_crc.c", "components/platform/utils.c"], []),
        ("rtl", ["tests/test_rtl.c", "components/rtl/rt_light.c"],
         ["-I"+str(root/"tests/host_idf"), "-Wno-pointer-to-int-cast"]),
    ):
        exe = Path(directory)/name
        command = compiler + extra + ["-I"+str(p) for p in includes]
        command += [str(root/p) for p in sources] + ["-o", str(exe)]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(exe)], check=True)
