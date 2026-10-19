#!/usr/bin/env sh
# Converts FBX → glTF through Blender in background mode. The engine reads ONLY glTF
# (`.gltf` + an external `.bin` + external textures) — purchased and downloaded FBX models
# are converted to it here, once per asset, instead of supporting a second format in the editor.
# Binary FBX is read (Blender does not open the ASCII variant — such a file must first be re-saved
# as binary in the original package). Textures with foreign absolute paths are looked up next to
# the model.
#
#   scripts/fbx_to_gltf.sh <model.fbx|folder> [output folder]
#
# Without an output folder the result goes next to the source. Each model gets its own folder
# (`<name>/<name>.gltf` + `.bin` + textures): this way they are moved into a project as a whole.
#
# Environment variables:
#   BLENDER=<path>        custom Blender (by default — from PATH or /Applications)
#   FBX_SCALE=<number>    import multiplier, if the model came in different units
#   GLTF_FORMAT=GLB       a single file instead of .gltf + .bin (default GLTF_SEPARATE)
set -eu

if [ $# -lt 1 ]; then
    echo "usage: $0 <model.fbx|directory> [output directory]" >&2
    exit 2
fi

INPUT="$1"
OUTPUT="${2:-}"

BLENDER="${BLENDER:-}"
if [ -z "$BLENDER" ]; then
    if command -v blender >/dev/null 2>&1; then
        BLENDER="$(command -v blender)"
    elif [ -x "/Applications/Blender.app/Contents/MacOS/Blender" ]; then
        BLENDER="/Applications/Blender.app/Contents/MacOS/Blender"
    else
        echo "Blender was not found. Install it or pass BLENDER=<path to executable>." >&2
        exit 1
    fi
fi

# The conversion script for a single model: Blender executes it as an expression, the arguments
# travel through the environment — paths with spaces do not break the quoting.
PYTHON_SOURCE=$(cat <<'PY'
import bpy, os, sys
source = os.environ["PANDA_FBX_INPUT"]
target = os.environ["PANDA_GLTF_OUTPUT"]
fmt = os.environ.get("GLTF_FORMAT", "GLTF_SEPARATE")
scale = float(os.environ.get("FBX_SCALE", "1.0"))
try:
    bpy.ops.wm.read_factory_settings(use_empty=True)  # without the startup cube and light
    bpy.ops.import_scene.fbx(filepath=source, global_scale=scale)
    try:
        # FBX textures often carry absolute paths from another machine — search next to the model.
        bpy.ops.file.find_missing_files(directory=os.path.dirname(os.path.abspath(source)))
    except Exception:
        pass
    os.makedirs(os.path.dirname(target), exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=target,
        export_format=fmt,
        export_apply=True,   # modifiers are baked into the geometry
        export_cameras=False,
        export_lights=False,
    )
except Exception as error:
    print("PANDA_CONVERT_ERROR: %s" % error, file=sys.stderr)
    sys.exit(1)
PY
)

convert_one() {
    fbx="$1"
    name=$(basename "$fbx")
    name="${name%.*}"
    if [ -n "$OUTPUT" ]; then
        destination_dir="$OUTPUT/$name"
    else
        destination_dir="$(dirname "$fbx")/$name"
    fi
    case "${GLTF_FORMAT:-GLTF_SEPARATE}" in
        GLB) destination="$destination_dir/$name.glb" ;;
        *) destination="$destination_dir/$name.gltf" ;;
    esac

    echo "→ $fbx"
    PANDA_FBX_INPUT="$fbx" PANDA_GLTF_OUTPUT="$destination" \
        "$BLENDER" --background --factory-startup --python-expr "$PYTHON_SOURCE" >/tmp/panda_fbx_to_gltf.log 2>&1 || {
        echo "  FAILED (log of the last run: /tmp/panda_fbx_to_gltf.log)" >&2
        grep -E "PANDA_CONVERT_ERROR|Error" /tmp/panda_fbx_to_gltf.log | head -5 >&2 || true
        return 1
    }
    if [ ! -f "$destination" ]; then
        echo "  FAILED: $destination was not created (see /tmp/panda_fbx_to_gltf.log)" >&2
        return 1
    fi
    echo "  $destination"
}

failed=0
if [ -d "$INPUT" ]; then
    list=$(mktemp)
    # Paths with a newline in the name are not supported — assets never have them.
    find "$INPUT" -type f \( -name '*.fbx' -o -name '*.FBX' \) >"$list"
    if [ ! -s "$list" ]; then
        rm -f "$list"
        echo "No .fbx in $INPUT" >&2
        exit 1
    fi
    # The loop reads from a file, not from a pipe: otherwise the body runs in a subshell and the
    # error counter is lost.
    while IFS= read -r fbx; do
        convert_one "$fbx" || failed=1
    done <"$list"
    rm -f "$list"
elif [ -f "$INPUT" ]; then
    convert_one "$INPUT" || failed=1
else
    echo "No such file or folder: $INPUT" >&2
    exit 1
fi

exit "$failed"
