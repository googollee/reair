#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Googol Lee
"""导出 PCB 的嘉立创生产文件,写入 out/pcb/:

  gerber/                      Gerber 和钻孔文件(含中间产物)
  reair-gerber.zip             上传嘉立创打样用的压缩包
  BOM-reair-JLCPCB.csv         贴片 BOM(Comment, Designator, Footprint, LCSC Part #)
  CPL-reair-JLCPCB.csv         贴片坐标(Designator, Mid X, Mid Y, Layer, Rotation)

导出前先跑 DRC(含与原理图一致性检查),有违规或未连接焊盘就中止。
BOM 来自原理图的 LCSC 字段;插件元件(如接线端子)也要在坐标文件里,否则嘉立创提示
"designator don't exist in the CPL file"。
坐标和旋转角是 KiCad 的原始值,嘉立创贴片预览里可能需要按元件库校正旋转角。

只用标准库,依赖 kicad-cli。用法: python3 scripts/gen_pcb_production.py [--skip-drc]
"""
import argparse
import csv
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HW = ROOT / "hardware"
PCB = HW / "reair.kicad_pcb"
SCH = HW / "reair.kicad_sch"
MAC_KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

GERBER_LAYERS = "F.Cu,B.Cu,F.Paste,B.Paste,F.Mask,B.Mask,F.SilkS,B.SilkS,Edge.Cuts"
# 上传嘉立创的文件:Gerber 与钻孔,不含钻孔图和 .gbrjob
UPLOAD_FILES = [
    "reair-F_Cu.gtl", "reair-B_Cu.gbl",
    "reair-F_Mask.gts", "reair-B_Mask.gbs",
    "reair-F_Silkscreen.gto", "reair-B_Silkscreen.gbo",
    "reair-F_Paste.gtp", "reair-B_Paste.gbp",
    "reair-Edge_Cuts.gm1", "reair-PTH.drl", "reair-NPTH.drl",
]


def die(msg):
    print(f"错误: {msg}", file=sys.stderr)
    sys.exit(1)


def find_kicad_cli(arg):
    for cand in (arg, os.environ.get("KICAD_CLI"), shutil.which("kicad-cli"), MAC_KICAD_CLI):
        if cand and shutil.which(cand):
            return cand
    die("找不到 kicad-cli,请安装 KiCad 或用 KICAD_CLI 指定路径")


def run(cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode != 0:
        die(f"命令失败: {' '.join(str(c) for c in cmd)}\n{r.stdout}{r.stderr}")
    return r


def rel(p):
    try:
        return p.relative_to(ROOT)
    except ValueError:
        return p


def check_drc(cli):
    with tempfile.TemporaryDirectory() as tmp:
        report = Path(tmp) / "drc.rpt"
        r = subprocess.run([cli, "pcb", "drc", "--schematic-parity", "--severity-all",
                            "--format", "report", "-o", report, PCB],
                           capture_output=True, text=True)
        if not report.exists():
            die(f"DRC 没有生成报告\n{r.stdout}{r.stderr}")
        text = report.read_text()
    counts = {k: int(n) for n, k in re.findall(r"\*\* Found (\d+) (DRC violations|unconnected pads|Footprint errors)", text)}
    bad = {k: n for k, n in counts.items() if n}
    if bad or len(counts) != 3:
        die(f"DRC 未通过: {bad or '无法解析报告'}。先在 KiCad 里修好,或加 --skip-drc 跳过")
    print("DRC: 0 违规, 0 未连接, 0 封装错误")


def export_gerber(cli, out):
    gdir = out / "gerber"
    gdir.mkdir(parents=True)
    run([cli, "pcb", "export", "gerbers", "--layers", GERBER_LAYERS, "--subtract-soldermask",
         "--no-x2", "--precision", "6", "--check-zones", "-o", f"{gdir}/", PCB])
    run([cli, "pcb", "export", "drill", "--format", "excellon", "--drill-origin", "absolute",
         "--excellon-zeros-format", "decimal", "--excellon-units", "mm", "--excellon-separate-th",
         "--generate-map", "--map-format", "gerberx2", "-o", f"{gdir}/", PCB])
    zpath = out / "reair-gerber.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for name in UPLOAD_FILES:
            f = gdir / name
            if not f.exists():
                die(f"缺少 {name}")
            z.write(f, name)
    print(f"Gerber: {len(UPLOAD_FILES)} 个文件 -> {rel(zpath)}")


def export_bom(cli, out):
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "bom.csv"
        run([cli, "sch", "export", "bom", "--fields", "Value,Reference,Footprint,LCSC",
             "--labels", "Comment,Designator,Footprint,LCSC Part #",
             "--group-by", "Value,Footprint,LCSC", "--ref-delimiter", ",",
             "--ref-range-delimiter", "", "--exclude-dnp", "-o", raw, SCH])
        rows = list(csv.reader(raw.open(encoding="utf-8")))
    header, body = rows[0], rows[1:]
    missing = [r[1] for r in body if not r[3].strip()]
    if missing:
        die(f"这些元件缺少 LCSC 料号: {', '.join(missing)}")
    with (out / "BOM-reair-JLCPCB.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(header)
        for r in body:
            r[2] = r[2].split(":")[-1]  # 去掉封装库前缀
            w.writerow(r)
    print(f"BOM: {len(body)} 行")


def through_hole_centers():
    """插件元件的焊盘包围盒中心(板上坐标)。KiCad 导出的是封装原点(可能在 1 脚),
    嘉立创按元件本体中心贴放,插件需要改用中心,否则整体偏移。"""
    text = PCB.read_text(encoding="utf-8")
    centers = {}
    for blk in text.split("\n\t(footprint ")[1:]:
        if "(attr through_hole)" not in blk:
            continue
        ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
        fx, fy, fr = re.search(r"\n\t\t\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)", blk).groups()
        fx, fy, t = float(fx), float(fy), math.radians(float(fr or 0))
        pts = [(float(x), float(y)) for x, y in
               re.findall(r'\n\t\t\(pad "[^"]*" thru_hole[^\n]*\n\t\t\t\(at ([-\d.]+) ([-\d.]+)', blk)]
        cx = (min(x for x, _ in pts) + max(x for x, _ in pts)) / 2
        cy = (min(y for _, y in pts) + max(y for _, y in pts)) / 2
        centers[ref] = (fx + cx * math.cos(t) + cy * math.sin(t),
                        fy - cx * math.sin(t) + cy * math.cos(t))
    return centers


def export_cpl(cli, out):
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "pos.csv"
        run([cli, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "both",
             "--exclude-dnp", "-o", raw, PCB])
        rows = list(csv.DictReader(raw.open(encoding="utf-8")))
    for ref, (x, y) in through_hole_centers().items():
        for r in rows:
            if r["Ref"] == ref:
                r["PosX"], r["PosY"] = x, -y  # 与 kicad-cli 的 Y 轴方向一致

    def key(ref):
        return (re.sub(r"\d+", "", ref), int(re.sub(r"\D", "", ref) or 0))

    with (out / "CPL-reair-JLCPCB.csv").open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in sorted(rows, key=lambda r: key(r["Ref"])):
            rot = float(r["Rot"]) % 360
            w.writerow([r["Ref"], f'{float(r["PosX"]):.6f}mm', f'{float(r["PosY"]):.6f}mm',
                        "Top" if r["Side"] == "top" else "Bottom", f"{rot:g}"])
    print(f"CPL: {len(rows)} 个元件(含插件)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--kicad-cli", help="kicad-cli 路径(默认从 PATH 或 KICAD_CLI 查找)")
    ap.add_argument("--out", type=Path, default=ROOT / "out/pcb")
    ap.add_argument("--skip-drc", action="store_true", help="跳过导出前的 DRC")
    args = ap.parse_args()
    cli = find_kicad_cli(args.kicad_cli)
    for f in (PCB, SCH):
        if not f.exists():
            die(f"找不到 {f}")
    if not args.skip_drc:
        check_drc(cli)
    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    export_gerber(cli, out)
    export_bom(cli, out)
    export_cpl(cli, out)
    print(f"完成: {rel(out)}")


if __name__ == "__main__":
    main()
