from pathlib import Path

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
needles = [
    b"blur_on__num__t", b"blur_off__t", b"blur_intensity", b"BlurredTexture",
    b"bloomDepthControl", b"gauss5x5", b"BLUR_ITERATIONS", b"MipMapLodBias",
    b"mipmaplodbias", b"LODBias", b"MaxAnisotropy", b"Anisotropic",
    b"distance_blur", b"DistanceBlur", b"far_blur", b"DepthBlur",
    b"blurlevel1", b"blurlevel2", b"CityDetail", b"softness",
    b"radial_blur", b"motion_blur", b"MotionBlur",
]
for n in needles:
    i = data.find(n)
    print(f"{n.decode(errors='replace'):24} -> {hex(i) if i >= 0 else '-'}")

for key in [b"bloomDepthControl", b"blurlevel1offset", b"BlurWeights",
            b"gauss5x5_Y4pixelOffset", b"blur_intensity", b"BlurredTexture"]:
    i = data.find(key)
    if i < 0:
        continue
    ctx = data[max(0, i - 100): i + 140]
    s = "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
    print("---", key.decode(), hex(i), "---")
    print(s)
