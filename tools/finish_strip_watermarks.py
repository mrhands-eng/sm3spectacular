#!/usr/bin/env python3
"""Finish hard-delete of on-screen watermarks; repair broken Out() after strip."""
from __future__ import annotations

import re
from pathlib import Path

BASE = Path(__file__).resolve().parents[1] / "vendor" / "remastered" / "reshade-shaders" / "Shaders"


def replace_out(path: Path, pattern: str, repl: str) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    new, n = re.subn(pattern, repl, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f"FAIL {path.name}: replacements={n}")
    new = re.sub(r"(?ms)^void A0\(float2 texcoord.*?^\}\n+", "", new)
    new = re.sub(r"(?ms)^void A1\(float2 texcoord.*?^\}\n+", "", new)
    if "drawChar(" not in new:
        new = re.sub(r"(?ms)^float getBit\(.*?^\}\n+", "", new)
        new = re.sub(r"(?ms)^float drawChar\(.*?^\}\n+", "", new)
    path.write_text(new, encoding="utf-8", newline="\n")
    print("OK", path.relative_to(BASE.parent))


def main() -> None:
    replace_out(
        BASE / "AstrayFX" / "Smart_Sharp.fx",
        r"float3 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float3 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn ShaderOut(texcoord).rgb;\n}",
    )
    replace_out(
        BASE / "AstrayFX" / "NFAA.fx",
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn float4(NFAA(texcoord).rgb, 1.0);\n}",
    )
    replace_out(
        BASE / "AstrayFX" / "Flair.fx",
        r"float4 OutF\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float4 OutF(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn float4(ShaderGlammor(texcoord).rgb, 1.0);\n}",
    )
    replace_out(
        BASE / "Depth3D" / "Others" / "Dimension_Plus.fx",
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn float4(tex2D(SamplerDiplus, texcoord).rgb, 1.0);\n}",
    )
    replace_out(
        BASE / "Depth3D" / "Others" / "VirtualNose.fx",
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn VNose(texcoord);\n}",
    )
    replace_out(
        BASE / "Depth3D" / "Others" / "Polynomial_Barrel_Distortion_for_HMDs.fx",
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n\treturn PBD(texcoord);\n}",
    )
    for rel in (
        BASE / "Depth3D" / "SuperDepth3D.fx",
        BASE / "Depth3D" / "SuperDepth3D_VR+.fx",
        BASE / "Depth3D" / "Others" / "SuperDepth3D_WoWvx.fx",
    ):
        replace_out(
            rel,
            r"float3 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?\}",
            "float3 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
            "{\n\treturn PS_calcLR(texcoord).rgb;\n}",
        )

    # Depth3D.fx keep theater crop logic
    p = BASE / "Depth3D.fx"
    t = p.read_text(encoding="utf-8", errors="replace")
    t, n = re.subn(
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return Color;\s*\}",
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n"
        "\tfloat2 Z_A = float2(1.0,0.5); //Theater Mode\n"
        "\tif(Theater_Mode && Stereoscopic_Mode == 0)\n"
        "\t{\n"
        "\t\tZ_A = float2(1.0,1.0); //Full Screen Mode\n"
        "\t}\n"
        "\tfloat X = Z_A.x;\n"
        "\tfloat Y = Z_A.y * Z_A.x * 2;\n"
        "\tfloat midW = (X - 1)*(BUFFER_WIDTH*0.5)*pix.x;\n"
        "\tfloat midH = (Y - 1)*(BUFFER_HEIGHT*0.5)*pix.y;\n"
        "\tfloat2 TM = float2((texcoord.x*X)-midW,(texcoord.y*Y)-midH);\n"
        "\treturn float4(PS_calcLR(TM).rgb,1.0);\n"
        "}",
        t,
        count=1,
        flags=re.S,
    )
    if n != 1:
        raise SystemExit(f"Depth3D.fx Out fail n={n}")
    t = re.sub(
        r"For more help you can always contact me at DEPTH3D\.info or my Github\.",
        "Stereo depth helper.",
        t,
    )
    p.write_text(t, encoding="utf-8", newline="\n")
    print("OK Depth3D.fx")

    p = BASE / "Depth3D" / "Others" / "Dimension_Plus.fx"
    t = p.read_text(encoding="utf-8", errors="replace")
    t = re.sub(
        r"For more help you can always contact me at DEPTH3D\.info\.",
        "Dimension Plus stereo helper.",
        t,
    )
    p.write_text(t, encoding="utf-8", newline="\n")

    p = BASE / "AstrayFX" / "BloomingHDR.fx"
    t = p.read_text(encoding="utf-8", errors="replace")
    t = t.replace(
        "// Final HDR composite — DEPTH3D.info watermark permanently removed.",
        "// Final HDR composite.",
    )
    p.write_text(t, encoding="utf-8", newline="\n")

    # Scrub RadiantGI / GloomAO help strings that mention Depth3D.info (UI only, not on-screen)
    for rel in ("AstrayFX/RadiantGI.fx", "AstrayFX/GloomAO.fx"):
        p = BASE / Path(rel)
        t = p.read_text(encoding="utf-8", errors="replace")
        t = re.sub(r"http://www\.Depth3D\.info[^\n\"]*", "", t)
        t = re.sub(r"https://blueskydefender\.github\.io/AstrayFX[^\n\"]*", "", t)
        t = re.sub(r"#line 4 \"[^\"]*Depth3D\.info[^\"]*\"", '#line 4 "AstrayFX"', t)
        p.write_text(t, encoding="utf-8", newline="\n")
        print("scrubbed URLs", rel)

    # Final verification
    keys = [
        "watermark removed",
        "Depth3D.Info Logo",
        "drawChar( CH_",
        "0.9525f*BUFFER_WIDTH",
        "return float4(Color,1.) :",
        "Website = D+E+P",
        "A0(texcoord",
        "A1(texcoord",
    ]
    bad = []
    for fx in BASE.rglob("*.fx"):
        text = fx.read_text(encoding="utf-8", errors="replace")
        hits = [k for k in keys if k in text]
        if hits:
            bad.append((fx, hits))
    if bad:
        print("LEFTOVERS:")
        for fx, hits in bad:
            print(" ", fx, hits)
        raise SystemExit(1)
    print("Clean: no on-screen watermark drawing code left.")


if __name__ == "__main__":
    main()
