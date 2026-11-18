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
        if name == "tcp":
            command += ["-DMCOM_TCP_MODULE", "-DI2CHAL_MODULE", "-DPCF8563_MODULE",
                        "-DEE24_MODULE", "-DCANHAL_MODULE", "-DCARMON_MODULE", "-DOBD_MODULE",
                        "-Wno-sign-compare", "-Wno-unused-variable"]
            sources += ["components/communication/master_comm.c",
                        "components/platform/pic_core.c", "components/platform/utils.c",
                        "components/platform/helpers.c", "tests/protocol_runtime.c"]
        command += [str(root/p) for p in sources] + ["-o", str(exe)]
        subprocess.run(command, cwd=root, check=True)
        subprocess.run([str(exe)], check=True)

    # Exercise production network code with POSIX sockets and task/event doubles.
    for name, source in (("wifi", "components/communication/wifi.c"),
                         ("tcp", "components/communication/tcp.c"),
                         ("network", "main/network.c")):
        exe = Path(directory)/name
        command = compiler + ["-std=c11", "-pthread", "-Wall", "-Wextra",
                              "-Wno-unused-parameter", "-Werror",
                              "-I"+str(root/"tests/host_network"),
                              "-I"+str(root/"components/communication/include"),
                              "-I"+str(root/"main")] + ["-I"+str(p) for p in includes]
        sources = [source, "tests/test_"+name+".c"]
        if name != "network":
            sources.append("tests/network_test_runtime.c")
        if name == "tcp":
            command += ["-DMCOM_TCP_MODULE", "-DI2CHAL_MODULE", "-DPCF8563_MODULE",
                        "-DEE24_MODULE", "-DCANHAL_MODULE", "-DCARMON_MODULE", "-DOBD_MODULE",
                        "-Wno-sign-compare", "-Wno-unused-variable"]
            sources += ["components/communication/master_comm.c",
                        "components/platform/pic_core.c", "components/platform/utils.c",
                        "components/platform/helpers.c", "tests/protocol_runtime.c"]
        command += [str(root/p) for p in sources]
        command += ["-o", str(exe)]
        subprocess.run(command, cwd=root, check=True)
        if name == "network":
            for scenario in ("valid", "wifi_error", "tcp_error"):
                result = subprocess.run([str(exe), scenario], capture_output=True,
                                        text=True, timeout=10, check=True)
                assert "host-test-password" not in result.stdout
                assert "Password:" not in result.stdout
                if scenario == "valid":
                    assert "Waiting for WiFi/DHCP" in result.stdout
                    assert "initialization failed" not in result.stdout
                else:
                    assert "initialization failed" in result.stdout
                if scenario == "wifi_error":
                    assert "idf.py menuconfig -> CarMon network" in result.stdout
            print("NETWORK PASS: menuconfig credentials, startup without USB and errors")
        else:
            subprocess.run([str(exe)], timeout=40, check=True)
