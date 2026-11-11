#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Googol Lee
"""OTA 自动回退的板上测试(ADR 0007)。

会覆盖 PORT 上开发板的引导程序、otadata 和两个应用分区,保留 nvs 和 fctry。
不经过 Matter OTA:直接把测试镜像写进应用分区,再按 esp_ota_set_boot_partition 的方式改写 otadata
(状态 NEW),引导程序据此把新固件标为待验证,和真实 OTA 之后的第一次启动一样。

用 `make test-ota-rollback PORT=...` 运行,镜像由 Makefile 先编译好(REAIR_OTA_TEST_MODE 各取值)。
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

import serial

OTADATA = 0x1D000
SLOT_OFFSET = {0: 0x20000, 1: 0x200000}
NEW, PENDING, VALID, INVALID, ABORTED = 0, 1, 2, 3, 4

# 各镜像的版本字符串。故意取很小的数,测试在板子 nvs 里留下的回退记录不会挡住以后真正的升级
VERSIONS = {"good_base": 100, "bad_selftest": 101, "bad_crash": 102, "bad_timeout": 103, "good_new": 104}

failures = []


def esptool(port, *args):
    cmd = [sys.executable, "-m", "esptool", "--port", port, "--after", "no-reset", *args]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"esptool 失败: {' '.join(cmd)}\n{r.stdout}{r.stderr}")
    return r.stdout


def capture(port, seconds, reset=True):
    """复位板子并抓取串口日志"""
    ser = serial.Serial(port, 115200, timeout=0.2)
    if reset:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        buf += ser.read(4096)
    ser.close()
    text = buf.decode("utf-8", "replace")
    return re.sub(r"\x1b\[[0-9;]*m", "", text)


def switch_to(port, image, slot):
    """模拟一次 OTA 应用:镜像写进 slot,otadata 指向它,状态 NEW。和 IDF 一样不动当前有效的那一项。"""
    with tempfile.TemporaryDirectory() as tmp:
        od = Path(tmp) / "otadata.bin"
        esptool(port, "read-flash", hex(OTADATA), "0x2000", str(od))
        data = od.read_bytes()
        entries = []
        for i in range(2):
            e = data[i * 0x1000:i * 0x1000 + 32]
            seq, = struct.unpack("<I", e[:4])
            state, = struct.unpack("<I", e[24:28])
            crc, = struct.unpack("<I", e[28:32])
            ok = (seq != 0xFFFFFFFF and crc == (zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF)
                  and state not in (INVALID, ABORTED))
            entries.append((seq, ok))
        valid = [(seq, i) for i, (seq, ok) in enumerate(entries) if ok]
        if not valid:
            sys.exit("otadata 里没有有效项,先烧录基线固件")
        base, active = max(valid)
        new = slot + 1
        while new <= base:
            new += 2
        entry = (struct.pack("<I", new) + b"\xff" * 20 + struct.pack("<I", NEW) +
                 struct.pack("<I", zlib.crc32(struct.pack("<I", new), 0xFFFFFFFF) & 0xFFFFFFFF))
        sector = Path(tmp) / "sector.bin"
        sector.write_bytes(entry + b"\xff" * (0x1000 - 32))
        esptool(port, "write-flash", "--flash-mode", "dio", "--flash-size", "4MB", "--flash-freq", "80m",
                hex(SLOT_OFFSET[slot]), str(image), hex(OTADATA + (1 - active) * 0x1000), str(sector))


def expect(name, cond, detail=""):
    print(("  ok   " if cond else "  FAIL ") + name + (f"  ({detail})" if detail and not cond else ""))
    if not cond:
        failures.append(name)


def booted(log, address, version):
    """日志里引导程序从 address 加载,并且应用报告的版本是 version"""
    return f"Loaded app from partition at offset 0x{address:x}" in log and f"Firmware version {version}," in log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True)
    ap.add_argument("--images", type=Path, required=True, help="含 good_base.bin 等测试镜像的目录")
    ap.add_argument("--build", type=Path, required=True, help="固件 build 目录(取引导程序、分区表、初始 otadata)")
    ap.add_argument("--timeout-s", type=int, default=60, help="固件的 CONFIG_REAIR_OTA_SELFTEST_TIMEOUT_S")
    args = ap.parse_args()
    port = args.port
    img = {k: args.images / f"{k}.bin" for k in VERSIONS}
    for f in img.values():
        if not f.exists():
            sys.exit(f"缺少测试镜像 {f}")
    v = VERSIONS

    print("烧录基线:引导程序、分区表、初始 otadata、基线固件(slot 0)")
    esptool(port, "write-flash", "--flash-mode", "dio", "--flash-size", "4MB", "--flash-freq", "80m",
            "0x0", str(args.build / "bootloader/bootloader.bin"),
            "0xc000", str(args.build / "partition_table/partition-table.bin"),
            hex(OTADATA), str(args.build / "ota_data_initial.bin"),
            hex(SLOT_OFFSET[0]), str(img["good_base"]))
    log = capture(port, 8)
    print("基线启动")
    expect("基线固件正常启动并视为已确认", booted(log, 0x20000, v["good_base"]) and "confirmed" in log, log[-300:])

    print("T1 自检失败,立即回退")
    switch_to(port, img["bad_selftest"], 1)
    log = capture(port, 25)
    expect("新固件处于待验证状态", f"Firmware version {v['bad_selftest']}," in log and "pending verification" in log)
    expect("自检失败触发回退", "Self test failed (hardware init)" in log)
    expect("回退到旧固件", booted(log, 0x20000, v["good_base"]))
    expect("记录被回退固件的版本号", f"Firmware version {v['bad_selftest']} was rolled back, recorded" in log)

    print("T2 启动即崩溃,引导程序回退")
    switch_to(port, img["bad_crash"], 1)
    log = capture(port, 25)
    expect("新固件待验证后崩溃", "pending verification" in log and "abort() was called" in log)
    expect("回退到旧固件", booted(log, 0x20000, v["good_base"]))
    expect("记录被回退固件的版本号", f"Firmware version {v['bad_crash']} was rolled back, recorded" in log)

    print(f"T3 自检永不完成,{args.timeout_s} 秒超时后回退")
    switch_to(port, img["bad_timeout"], 1)
    log = capture(port, args.timeout_s + 20)
    expect("新固件待验证", "pending verification" in log)
    expect("自检超时触发回退", "Self test timed out" in log)
    expect("回退到旧固件", booted(log, 0x20000, v["good_base"]))
    expect("记录被回退固件的版本号", f"Firmware version {v['bad_timeout']} was rolled back, recorded" in log)

    print("T4 正常的新固件:自检通过并确认,重启后保持")
    switch_to(port, img["good_new"], 1)
    log = capture(port, 15)
    expect("新固件待验证后自检通过", "pending verification" in log and "Self test passed, firmware confirmed" in log)
    log = capture(port, 8)
    expect("重启后仍运行新固件且已确认", booted(log, 0x200000, v["good_new"]) and "confirmed" in log)
    expect("没有再次回退记录", "was rolled back, recorded" not in log)

    print("T5 反方向:当前在 slot 1,坏固件写进 slot 0,回退到 slot 1")
    switch_to(port, img["bad_selftest"], 0)
    log = capture(port, 25)
    expect("新固件在 slot 0 待验证后回退", "Self test failed (hardware init)" in log)
    expect("回退到 slot 1 的旧固件", booted(log, 0x200000, v["good_new"]))
    expect("记录被回退固件的版本号", f"Firmware version {v['bad_selftest']} was rolled back, recorded" in log)

    if failures:
        print(f"\n{len(failures)} 项失败: {failures}")
        sys.exit(1)
    print("\nOTA 回退板上测试全部通过")


if __name__ == "__main__":
    main()
