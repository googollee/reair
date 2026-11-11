#!/usr/bin/env python3
"""把外壳中层的两个颜色体合并成一个 3MF：

  body    白色，主体（挤出机 1）
  island  黑色，二维码的黑色岛（挤出机 2）

两个体各是 OpenSCAD 单独导出的 3MF（带颜色）。合并后的 3MF 同时带有：
  - 标准的颜色（basematerials），支持 3MF 颜色的切片软件按颜色分配耗材；
  - PrusaSlicer 的体信息（Metadata/Slic3r_PE_model.config），PrusaSlicer 系列（含 OrcaSlicer、
    Bambu Studio）据此把黑色岛分给挤出机 2，自动换料。
PrusaSlicer 不读标准 3MF 的颜色，只认体上的 extruder 元数据，所以两种信息都写。

用法: pack_enclosure_3mf.py OUT.3mf BODY.3mf ISLAND.3mf
"""
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {"m": "http://schemas.microsoft.com/3dmanufacturing/core/2015/02"}


def read_mesh(path: Path):
    with zipfile.ZipFile(path) as z:
        root = ET.fromstring(z.read("3D/3dmodel.model"))
    mesh = root.find(".//m:object/m:mesh", NS)
    verts = [(v.get("x"), v.get("y"), v.get("z")) for v in mesh.find("m:vertices", NS)]
    tris = [(int(t.get("v1")), int(t.get("v2")), int(t.get("v3"))) for t in mesh.find("m:triangles", NS)]
    return verts, tris


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    out, body_p, island_p = (Path(a) for a in sys.argv[1:])
    bv, bt = read_mesh(body_p)
    iv, it = read_mesh(island_p)
    off = len(bv)
    verts = bv + iv
    tris = [(t, 0) for t in bt] + [((a + off, b + off, c + off), 1) for a, b, c in it]

    model = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
             ' <metadata name="Application">ReAir enclosure (OpenSCAD + pack_enclosure_3mf.py)</metadata>',
             ' <resources>',
             '  <basematerials id="1">',
             '   <base name="white" displaycolor="#FFFFFFFF"/>',
             '   <base name="black" displaycolor="#000000FF"/>',
             '  </basematerials>',
             '  <object id="1" type="model" pid="1" pindex="0">',
             '   <mesh>', '    <vertices>']
    model += [f'     <vertex x="{x}" y="{y}" z="{z}"/>' for x, y, z in verts]
    model += ['    </vertices>', '    <triangles>']
    model += [f'     <triangle v1="{a}" v2="{b}" v3="{c}" pid="1" p1="{p}"/>' for (a, b, c), p in tris]
    model += ['    </triangles>', '   </mesh>', '  </object>', ' </resources>',
              ' <build>', '  <item objectid="1"/>', ' </build>', '</model>']

    ident = "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"

    def volume(first, last, name, extruder):
        return [f'  <volume firstid="{first}" lastid="{last}">',
                f'   <metadata type="volume" key="name" value="{name}"/>',
                '   <metadata type="volume" key="volume_type" value="ModelPart"/>',
                f'   <metadata type="volume" key="extruder" value="{extruder}"/>',
                f'   <metadata type="volume" key="matrix" value="{ident}"/>',
                '  </volume>']

    cfg = ['<?xml version="1.0" encoding="UTF-8"?>', '<config>',
           ' <object id="1" instances_count="1">',
           f'  <metadata type="object" key="name" value="{out.stem}"/>']
    cfg += volume(0, len(bt) - 1, "body", 1)
    cfg += volume(len(bt), len(tris) - 1, "island", 2)
    cfg += [' </object>', '</config>']

    content_types = ('<?xml version="1.0" encoding="UTF-8"?>\n'
                     '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                     '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
                     '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>'
                     '<Default Extension="config" ContentType="text/xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", content_types)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", "\n".join(model))
        z.writestr("Metadata/Slic3r_PE_model.config", "\n".join(cfg))
    print(f"{out}: body {len(bt)} 面 + island {len(it)} 面")


if __name__ == "__main__":
    main()
