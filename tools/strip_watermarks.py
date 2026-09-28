#!/usr/bin/env python3
"""Hard-delete DEPTH3D / AstrayFX on-screen watermark drawing code from ReShade shaders.
Keeps functional shader bodies; does not touch zryuyu credits (not in shaders).
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "vendor" / "remastered" / "reshade-shaders"


def find_matching_brace(text: str, open_idx: int) -> int:
    """open_idx points at '{'. Return index of matching '}'."""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i
        elif c == "/" and i + 1 < n:
            if text[i + 1] == "/":
                i = text.find("\n", i)
                if i < 0:
                    break
            elif text[i + 1] == "*":
                j = text.find("*/", i + 2)
                i = j + 1 if j >= 0 else n
        elif c in "\"'":
            q = c
            i += 1
            while i < n and text[i] != q:
                if text[i] == "\\":
                    i += 1
                i += 1
        i += 1
    raise ValueError("unbalanced brace")


WM_IF = re.compile(
    r"(?:\[branch\]\s*)?if\s*\(\s*false\s*\)\s*/\*\s*watermark removed\s*\*/\s*\{",
    re.I,
)

# return false ? /* watermark removed */ <dead> : <keep>;
WM_TERNARY = re.compile(
    r"return\s+false\s*\?\s*/\*\s*watermark removed\s*\*/\s*.*?\s*:\s*(.+?);",
    re.S,
)

LOGO_BANNER = re.compile(
    r"(?m)^[ \t]*(?://)?[\\/\*]*(?:Logo|Watermark)[\\/\*]*[ \t]*\n?",
)


def strip_if_false_blocks(text: str) -> tuple[str, int]:
    count = 0
    while True:
        m = WM_IF.search(text)
        if not m:
            break
        brace_open = text.find("{", m.start())
        brace_close = find_matching_brace(text, brace_open)
        after = text[brace_close + 1 :]
        # Drop optional else { return ... } / else return ... — keep the return body.
        else_m = re.match(
            r"\s*else\s*(\{)|(return\b)",
            after,
            re.S,
        )
        replacement = ""
        consume = brace_close + 1
        if else_m:
            if else_m.group(1):  # else {
                eopen = after.find("{")
                eabs = brace_close + 1 + eopen
                eclose = find_matching_brace(text, eabs)
                body = text[eabs + 1 : eclose].strip()
                replacement = "\n\t" + body + "\n"
                if not body.endswith(";") and "return" in body:
                    pass
                consume = eclose + 1
            else:  # else return ...
                # from 'else' through semicolon
                rest = after
                em = re.match(r"\s*else\s+(return\b[^;]*;)", rest, re.S)
                if em:
                    replacement = "\n\t" + em.group(1).strip() + "\n"
                    consume = brace_close + 1 + em.end()
        text = text[: m.start()] + replacement + text[consume:]
        count += 1
    return text, count


def strip_ternaries(text: str) -> tuple[str, int]:
    count = 0

    def repl(m: re.Match) -> str:
        nonlocal count
        count += 1
        keep = m.group(1).strip()
        return f"return {keep};"

    # Apply repeatedly for nested safety; usually one per function.
    new, n = WM_TERNARY.subn(repl, text)
    return new, n


def cleanup_dead_logo_helpers(text: str) -> str:
    """Remove unused PosX watermark locals left after strip when function only returns Color."""
    # Simplify common Out patterns that still declare dead D,E,P,... after strip.
    text = re.sub(
        r"(?m)^[ \t]*float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y[^;]*;\n",
        "",
        text,
    )
    text = re.sub(
        r"(?m)^[ \t]*float3 (?:D,E,P,T,H,Three,DD,Dot,I,N,F,O|Color = .+?,D,E,P,T,H,Three,DD,Dot,I,N,F,O);\n",
        "",
        text,
    )
    # NFAA / Flair style: Color + PosX + A0/A1 call on one line then return Color
    text = re.sub(
        r"(float3 Color = [^;]+;)\s*float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y,[^;]*;",
        r"\1",
        text,
    )
    text = re.sub(
        r"(float4 Color = [^;]+;)\s*float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y[^;]*;",
        r"\1",
        text,
    )
    # Depth_Cues style leftover Text_Timer locals
    text = re.sub(
        r"(?ms)^[ \t]*float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y, Text_Timer = 12500,[^;]*;\n"
        r"[ \t]*float D,E,P,T,H,Three,DD,Dot,I,N,F,O,R,EE,A,DDD,HH,EEE,L,PP,NN,PPP,C,Not,No;\n"
        r"([ \t]*float3 Color = [^;]+;)\n"
        r"[ \t]*if\(NC \|\| NP\)\s*Text_Timer = 18750;\n",
        r"\1\n",
        text,
    )
    text = re.sub(
        r"(?ms)^[ \t]*float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y, Text_Timer = 12500,[^;]*;\n"
        r"[ \t]*float D,E,P,T,H,Three,DD,Dot,I,N,F,O,R,EE,A,DDD,HH,EEE,L,PP;\n"
        r"([ \t]*float3 Color = [^;]+;)\n"
        r"[ \t]*if\(NC \|\| NP\)\s*Text_Timer = 18750;\n",
        r"\1\n",
        text,
    )
    return text


def simplify_radiant_style_out(text: str) -> str:
    """After if(false) strip, RadiantGI/GloomAO Out still has huge unused float decls — collapse Out."""
    # RadiantGI / GloomAO pattern
    pat = re.compile(
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*"
        r"\{.*?"
        r"(float4 Color = (?:MixOut|SSDOMixing)\(texcoord\);).*?"
        r"(return (?:Helper\(\) \? 0 : Color|Color);)\s*\}",
        re.S,
    )

    def repl(m: re.Match) -> str:
        return (
            "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
            "{\n"
            f"\t{m.group(1)}\n"
            f"\t{m.group(2)}\n"
            "}"
        )

    text, n = pat.subn(repl, text)
    return text


def simplify_superdepth_out(text: str) -> str:
    """SuperDepth3D* Out after strip — keep MixOut/Color return."""
    pat = re.compile(
        r"(float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{)"
        r".*?"
        r"(float4 Color = [^;]+;)"
        r".*?"
        r"(return Color;)\s*\}",
        re.S,
    )
    # Too greedy across techniques — only apply if watermark marker remnants or PosX logo present
    if "0.9525f*BUFFER_WIDTH*pix.x" not in text and "drawChar" not in text:
        return text
    return text


def process_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8", errors="replace")
    text = original
    total = 0

    text, n = strip_ternaries(text)
    total += n
    text, n = strip_if_false_blocks(text)
    total += n

    if total == 0 and "watermark removed" not in text and "Depth3D.Info Logo" not in text:
        # Still may need cleanup of helper A0/A1 only used for watermark
        if "0.9525f*BUFFER_WIDTH*pix.x" not in text and "drawChar( CH_" not in text:
            return False

    text = cleanup_dead_logo_helpers(text)
    text = simplify_radiant_style_out(text)

    # Remove Logo section banners
    text = re.sub(
        r"(?m)^[ \t]*/?[/\*]{2,}.*(?:Logo|Watermark).*$\n?",
        "",
        text,
    )

    # Drop orphaned A0/A1 watermark glyph helpers in NFAA/Flair/Dimension_Plus if Out no longer calls them
    if "A0(texcoord" not in text and "A1(texcoord" not in text:
        text = re.sub(
            r"(?ms)^void A0\(float2 texcoord.*?^\}\n+",
            "",
            text,
        )
        text = re.sub(
            r"(?ms)^void A1\(float2 texcoord.*?^\}\n+",
            "",
            text,
        )

    # Drop unused drawChar / bitmap charset blocks only if drawChar no longer referenced
    # (keep if still used for non-watermark UI — rare). Skip aggressive CH_ removal.

    if text == original:
        return False
    path.write_text(text, encoding="utf-8", newline="\n")
    print(f"stripped {path.relative_to(ROOT.parent)} (+{total} blocks)")
    return True


def collapse_simple_out_files(path: Path) -> bool:
    """Hand-tuned collapses for files where automated brace strip left noise."""
    text = path.read_text(encoding="utf-8", errors="replace")
    orig = text
    name = path.name

    if name in ("NFAA.fx", "Flair.fx", "Dimension_Plus.fx"):
        # Ensure Out / OutF only returns Color
        text = re.sub(
            r"(float4 Out(?:F)?\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{)\s*"
            r"float3 Color = ([^;]+);.*?return float4\(Color,1\.\);",
            r"\1\n\tfloat3 Color = \2;\n\treturn float4(Color,1.);",
            text,
            count=1,
            flags=re.S,
        )

    if name == "Temporal_AA.fx":
        # void Out(... out float4 color) — keep TAA assignment only
        text = re.sub(
            r"void Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD, out float4 color : SV_Target\)\s*\{.*?\}",
            """void Out(float4 position : SV_Position, float2 texcoord : TEXCOORD, out float4 color : SV_Target)
{
\tfloat4 T_A_A = TAA(texcoord);

\t#if App_Sync
\tfloat Scale = 2;
\tif(texcoord.x < pix.x * Scale && 1-texcoord.y < pix.y * Scale)
\t\tT_A_A = Alternate ? 0 : 1;
\t#endif

\tcolor = T_A_A;
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name in ("DLAA_Plus.fx", "Depth_Cues.fx", "Smart_Sharp.fx", "VirtualNose.fx",
                "Polynomial_Barrel_Distortion_for_HMDs.fx", "Depth3D.fx", "BloomingHDR.fx"):
        # Generic: if still has logo PosX after Color assign and only return Color — already handled
        pass

    if name == "DLAA_Plus.fx":
        text = re.sub(
            r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return float4\(Color,1\.\);\s*\}",
            """float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\tfloat3 Color = DLAA(texcoord).rgb;
\treturn float4(Color,1.);
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name == "Depth_Cues.fx":
        text = re.sub(
            r"float3 Out_DC\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return Color;\s*\}",
            """float3 Out_DC(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\treturn ShaderOut_DC(texcoord).rgb;
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name == "Smart_Sharp.fx":
        text = re.sub(
            r"float3 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return Color;\s*\}",
            """float3 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\treturn Smart_Sharp(texcoord).rgb;
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name == "VirtualNose.fx":
        text = re.sub(
            r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return float4\(Color,1\.\);\s*\}",
            """float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\tfloat3 Color = Nose(texcoord).rgb;
\treturn float4(Color,1.);
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name == "Polynomial_Barrel_Distortion_for_HMDs.fx":
        text = re.sub(
            r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{.*?return float4\(Color,1\.\);\s*\}",
            """float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\tfloat3 Color = PBD(texcoord).rgb;
\treturn float4(Color,1.);
}""",
            text,
            count=1,
            flags=re.S,
        )

    if name == "Depth3D.fx":
        # Keep theater mode zoom logic, drop watermark only — already stripped by if(false)
        text = re.sub(
            r"(float2 TM = float2\(\(texcoord\.x\*X\)-midW,\(texcoord\.y\*Y\)-midH\);)\s*"
            r"float PosX = 0\.9525f\*BUFFER_WIDTH\*pix\.x,PosY = 0\.975f\*BUFFER_HEIGHT\*pix\.y;\s*"
            r"float4 Color = float4\(PS_calcLR\(TM\)\.rgb,1\.0\),D,E,P,T,H,Three,DD,Dot,I,N,F,O;\s*"
            r"return Color;",
            r"\1\n\tfloat4 Color = float4(PS_calcLR(TM).rgb,1.0);\n\treturn Color;",
            text,
            count=1,
            flags=re.S,
        )

    if name in ("RadiantGI.fx", "GloomAO.fx"):
        # Already handled by simplify_radiant_style_out; also kill drawChar helpers if unused
        if "drawChar(" not in text:
            text = re.sub(r"(?ms)^float getBit\(.*?^\}\s*", "", text)
            text = re.sub(r"(?ms)^float drawChar\(.*?^\}\s*", "", text)
            text = re.sub(r"(?ms)^.*?float DT_Information\(\).*?\n", "", text)

    if name.startswith("SuperDepth3D"):
        # Collapse Out to return Color after MixOut
        m = re.search(
            r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{",
            text,
        )
        if m and "drawChar" in text[m.start() : m.start() + 8000]:
            # find Color = and final return Color after strip
            pass
        text = re.sub(
            r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*"
            r"\{.*?float4 Color = ([^;]+);.*?return Color;\s*\}",
            """float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target
{
\tfloat4 Color = \\1;
\treturn Color;
}""",
            text,
            count=1,
            flags=re.S,
        )

    if text != orig:
        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"collapsed {path.relative_to(ROOT.parent)}")
        return True
    return False


def main() -> int:
    shaders = list(ROOT.rglob("*.fx"))
    changed = 0
    targets = []
    for p in shaders:
        raw = p.read_text(encoding="utf-8", errors="replace")
        if any(
            s in raw
            for s in (
                "watermark removed",
                "Depth3D.Info Logo",
                "0.9525f*BUFFER_WIDTH*pix.x",
                "////////////////////////////////////////////////////////Logo",
                "Watermark",
            )
        ):
            targets.append(p)

    for p in targets:
        if process_file(p):
            changed += 1
        if collapse_simple_out_files(p):
            changed += 1

    # Second pass verification
    leftovers = []
    for p in ROOT.rglob("*.fx"):
        t = p.read_text(encoding="utf-8", errors="replace")
        hits = []
        if "watermark removed" in t:
            hits.append("watermark-marker")
        if "Depth3D.Info Logo" in t:
            hits.append("Depth3D.Info Logo")
        if re.search(r"drawChar\(\s*CH_", t):
            hits.append("drawChar")
        if "0.9525f*BUFFER_WIDTH*pix.x" in t and "Logo" not in t:
            # may still be leftover locals
            hits.append("logo-PosX")
        if hits:
            leftovers.append((p, hits))

    print(f"\nfiles touched round: {changed}")
    if leftovers:
        print("LEFTOVERS:")
        for p, hits in leftovers:
            print(f"  {p.relative_to(ROOT.parent)}: {', '.join(hits)}")
        return 1
    print("No watermark drawing leftovers.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
