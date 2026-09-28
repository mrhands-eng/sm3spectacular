from pathlib import Path
import re

SRC = Path(r"D:/Projects/IdeaProjects/Spider Man 3 Remastered/reshade-shaders/Shaders/AstrayFX")
DST = Path(r"D:/Projects/IdeaProjects/sm3spectacular/vendor/remastered/reshade-shaders/Shaders/AstrayFX")
GAME = Path(r"D:/Projects/IdeaProjects/Spider-Man 3 - The Game/reshade-shaders/Shaders/AstrayFX")


def find_func_end(text: str, brace_open: int) -> int:
    depth = 0
    i = brace_open
    n = len(text)
    while i < n:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        elif c == "/" and i + 1 < n and text[i + 1] == "/":
            i = text.find("\n", i)
            if i < 0:
                break
        i += 1
    raise RuntimeError("unbalanced")


def replace_out(text: str, color_expr: str) -> str:
    m = re.search(
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target\s*\{",
        text,
    )
    if not m:
        raise RuntimeError("Out not found")
    brace = text.find("{", m.end() - 1)
    end = find_func_end(text, brace)
    body = text[m.start() : end]
    markers = ("drawChar", "watermark", "Depth3D.Info", "0.9525f*BUFFER_WIDTH", "Minimize_Web_Info")
    if not any(x in body for x in markers):
        raise RuntimeError("Out has no watermark markers; abort")
    clean = (
        "float4 Out(float4 position : SV_Position, float2 texcoord : TEXCOORD) : SV_Target\n"
        "{\n"
        f"\treturn {color_expr};\n"
        "}\n"
    )
    return text[: m.start()] + clean + text[end:]


def also_drop_charset_before_out(text: str) -> str:
    """Remove drawChar/charset block immediately before Out, if present."""
    m = re.search(
        r"float4 Out\(float4 position : SV_Position, float2 texcoord : TEXCOORD\) : SV_Target",
        text,
    )
    if not m:
        return text
    before = text[: m.start()]
    # Drop from Text_Switch / charset / Logo section if drawChar exists before Out
    if "float drawChar(" not in before and "static const float  CH_A" not in before:
        return text
    # Prefer cutting at Overwatch watermark section marker or Text_Switch
    cut = None
    for pat in (
        r"(?m)^////////////////////////////////////////////////////////////////Overwatch",
        r"(?m)^float Text_Switch\(\)",
        r"(?m)^static const float\s+CH_A",
        r"(?m)^#define _f float // Text rendering",
    ):
        mm = list(re.finditer(pat, before))
        if mm:
            cut = mm[-1].start()
            break
    if cut is None:
        return text
    return before[:cut] + text[m.start() :]


def process(name: str, color: str) -> None:
    src = (SRC / name).read_text(encoding="utf-8", errors="replace")
    if "#if exists" not in src:
        raise RuntimeError(f"{name}: source missing #if exists")
    out = replace_out(src, color)
    out = also_drop_charset_before_out(out)
    if "#if exists" not in out:
        raise RuntimeError(f"{name}: broke Overwatch guard")
    # sanity: Overwatch else/endif present
    if out.count("#else") < 1 or out.count("#endif") < 1:
        raise RuntimeError(f"{name}: preprocessor guards damaged")
    (DST / name).write_text(out, encoding="utf-8", newline="\n")
    (GAME / name).write_text(out, encoding="utf-8", newline="\n")
    print(f"OK {name}")
    idx = out.find("#if exists")
    print(out[idx : idx + 90].replace("\n", " | "))


process("RadiantGI.fx", "MixOut(texcoord)")
process("GloomAO.fx", "SSDOMixing(texcoord)")

# clear reshade cache so they recompile
import shutil

cache = Path(r"D:/Projects/IdeaProjects/Spider-Man 3 - The Game/reshade-cache")
if cache.exists():
    shutil.rmtree(cache)
cache.mkdir(parents=True, exist_ok=True)
print("cache cleared")
