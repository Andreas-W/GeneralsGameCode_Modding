"""Compiles the built-in material shaders of the Direct3D 12 renderer with fxc, without building or running the game.

The shaders live as raw string literals in W3DMaterials.cpp (s_waterShader, s_oceanShader, ...). This script
puts Material.hlsli together the way MaterialSystem does (the vertex constants of ShaderGen.cpp plus s_header of
Materials.cpp), compiles each shader's pixel stage, and, for shaders with a MaterialVertex function, also the
vertex stage the way the generated vertex shaders include it. It also checks that no raw literal piece is near
the MSVC limit of about 16 KB.

Usage (Windows, from anywhere):
    python scripts/d3d8on12/check_material_shaders.py                 # every built-in shader
    python scripts/d3d8on12/check_material_shaders.py s_oceanShader   # only these
    python scripts/d3d8on12/check_material_shaders.py --hlsl Data/Shaders/Material/MAT_Ocean.hlsl

fxc is looked up in the FXC environment variable, then in the newest Windows 10/11 SDK.
Exit code 0 when everything compiles.
"""
import glob
import os
import re
import subprocess
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SHADER_GEN = os.path.join(REPO, "Core", "Libraries", "Source", "D3D8on12", "ShaderGen.cpp")
MATERIALS = os.path.join(REPO, "Core", "Libraries", "Source", "D3D8on12", "Materials.cpp")
W3D_MATERIALS = os.path.join(REPO, "Core", "GameEngineDevice", "Source", "W3DDevice", "GameClient", "W3DMaterials.cpp")
LITERAL_LIMIT = 16380  # MSVC error C2026 above this many bytes in one raw string literal piece

# The generated vertex shader around a material's vertex function, for the water's vertex format
# (see Generate_Vertex_Shader in ShaderGen.cpp).
VERTEX_MAIN = r'''
struct VSInput { float4 position : POSITION; float3 normal : NORMAL; float4 color0 : COLOR0; float2 tex0 : TEXCOORD0; float2 tex1 : TEXCOORD1; };
struct VSOutput { float4 position : SV_Position; float4 diffuse : COLOR0; float4 specular : COLOR1; float fog : FOG;
	float4 tex0 : TEXCOORD0; float4 tex1 : TEXCOORD1; float3 viewPosition : VIEWPOS; float3 viewNormal : VIEWNORMAL; float4 materialData : MATERIALDATA; };
VSOutput main(VSInput input)
{
	VSOutput output;
	MaterialVertexData materialVertex;
	materialVertex.worldPosition = mul(float4(input.position.xyz, 1), World).xyz;
	materialVertex.worldNormal = normalize(mul(input.normal, (float3x3)World));
	materialVertex.tex0 = input.tex0.xy;
	materialVertex.data = float4(0, 0, 0, 0);
	MaterialVertex(materialVertex);
	float4 viewPosition4 = mul(float4(materialVertex.worldPosition, 1), View);
	output.position = mul(viewPosition4, Projection);
	output.viewPosition = viewPosition4.xyz;
	output.viewNormal = normalize(mul(materialVertex.worldNormal, (float3x3)View));
	output.materialData = materialVertex.data;
	output.diffuse = input.color0; output.specular = 0; output.fog = 1;
	output.tex0 = float4(input.tex0, 0, 1); output.tex1 = float4(input.tex1, 0, 1);
	return output;
}
'''


def find_fxc():
    if os.environ.get("FXC") and os.path.isfile(os.environ["FXC"]):
        return os.environ["FXC"]
    found = glob.glob(r"C:\Program Files (x86)\Windows Kits\10\bin\10.*\x64\fxc.exe")
    if not found:
        sys.exit("fxc.exe not found: install the Windows SDK or set FXC")
    return sorted(found)[-1]


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def material_header():
    gen = read(SHADER_GEN)
    m = re.search(r'static const char\* s_vertexShaderHeader =(.*?);\n', gen, re.S)
    vertex_constants = "".join(eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', m.group(1)))  # C string pieces
    header = re.search(r'static const char s_header\[\] = R"\((.*?)\)";', read(MATERIALS), re.S).group(1)
    return vertex_constants + header


def builtin_shaders():
    """name -> list of raw literal pieces."""
    shaders = {}
    for m in re.finditer(r'static const char (s_\w+Shader)\[\] = (R"\(.*?\)");\n', read(W3D_MATERIALS), re.S):
        shaders[m.group(1)] = re.findall(r'R"\((.*?)\)"', m.group(2), re.S)
    return shaders


def run(fxc, args):
    result = subprocess.run([fxc, "/nologo"] + args, capture_output=True, text=True)
    output = (result.stdout + result.stderr).strip()
    lines = [line for line in output.splitlines() if "compilation object save succeeded" not in line]
    return result.returncode == 0, "\n".join(lines)


def check(fxc, work, name, source):
    ok = True
    path = os.path.join(work, name + ".hlsl")
    with open(path, "w", encoding="utf-8") as f:
        f.write(source)
    good, messages = run(fxc, ["/T", "ps_5_0", "/E", "main", "/O3", "/Fo", os.path.join(work, name + ".ps.cso"), path])
    print(f"{name}: pixel stage {'ok' if good else 'FAILED'}")
    if messages:
        print("  " + messages.replace("\n", "\n  "))
    ok = ok and good

    if re.search(r'\bMaterialVertex\s*\(', source):
        # As MaterialSystem::Prepare_Vertex_Function: preprocessed with MATERIAL_VERTEX_STAGE (main becomes
        # MaterialPixelMain), then placed in front of the generated vertex shader.
        flat = os.path.join(work, name + ".vs.pre.hlsl")
        good, messages = run(fxc, ["/P", flat, "/D", "MATERIAL_VERTEX_STAGE=1", path])
        if good:
            with open(flat, "rb") as f:
                text = f.read().replace(b"\0", b"").decode("utf-8", "replace")
            vs = os.path.join(work, name + ".vs.hlsl")
            with open(vs, "w", encoding="utf-8") as f:
                f.write(text + VERTEX_MAIN)
            good, messages = run(fxc, ["/T", "vs_5_0", "/E", "main", "/O3", "/Fo", os.path.join(work, name + ".vs.cso"), vs])
        print(f"{name}: vertex stage {'ok' if good else 'FAILED'}")
        if messages:
            print("  " + messages.replace("\n", "\n  "))
        ok = ok and good
    return ok


def main():
    args = sys.argv[1:]
    fxc = find_fxc()
    work = tempfile.mkdtemp(prefix="material_shaders_")
    with open(os.path.join(work, "Material.hlsli"), "w", encoding="utf-8") as f:
        f.write(material_header())

    ok = True
    if args[:1] == ["--hlsl"]:
        for path in args[1:]:
            ok = check(fxc, work, os.path.splitext(os.path.basename(path))[0], read(path)) and ok
    else:
        shaders = builtin_shaders()
        names = args or sorted(shaders)
        for name in names:
            if name not in shaders:
                print(f"{name}: not found; built-in shaders: {', '.join(sorted(shaders))}")
                ok = False
                continue
            pieces = shaders[name]
            sizes = [len(p.encode("utf-8")) for p in pieces]
            if max(sizes) > LITERAL_LIMIT:
                print(f"{name}: a raw literal piece has {max(sizes)} bytes, over the MSVC limit; split it with )\" R\"(")
                ok = False
            elif max(sizes) > LITERAL_LIMIT - 1500:
                print(f"{name}: raw literal pieces {sizes} bytes, close to the limit of {LITERAL_LIMIT}")
            ok = check(fxc, work, name, "".join(pieces)) and ok
    print("work files:", work)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
