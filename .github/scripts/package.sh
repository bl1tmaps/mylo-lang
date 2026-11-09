#!/usr/bin/env bash
# Assembles a finished build into Mylo/ and zips it, then checks the result.
# Run from the repo root after building into ./build:
#   bash .github/scripts/package.sh <version> <platform>
set -euo pipefail

version="${1:?usage: package.sh <version> <platform>}"
platform="${2:?usage: package.sh <version> <platform>}"

# Full commit hashes are shortened; release tags are left alone.
if [[ "$version" =~ ^[0-9a-f]{40}$ ]]; then
    version="${version:0:7}"
fi

# Visual Studio builds land in build/Release; Make and Ninja builds land in build/.
if [[ "$platform" == windows-* ]]; then
    bin_dir=build/Release
    ide_dir=build/ide/Release
    ext=.exe
else
    bin_dir=build
    ide_dir=build/ide
    ext=
fi

stage_root="dist/stage"
package="$stage_root/Mylo"
zip_name="Mylo-$version-$platform.zip"

rm -rf dist
mkdir -p "$package"

for tool in mylo mylo-lsp mylo-vsproject; do
    cp "$bin_dir/$tool$ext" "$package/"
done
cp "$ide_dir/mylo-ide$ext" "$package/"
cp -R "$ide_dir/assets" "$package/"

cp -R examples docs editors "$package/"
cp README.md LICENSE.txt tutorial.md "$package/"

# Copy the module sources, leaving out anything a local build left behind.
cp -R modules "$package/"
find "$package/modules" -type d \( -name Debug -o -name Release -o -name 'build*' -o -name 'raylib-*' -o -name '_deps' \) -prune -exec rm -rf {} +
find "$package/modules" -type f \( -name '*.dll' -o -name '*.so' -o -name '*.dylib' -o -name '*.obj' -o -name '*.o' -o -name '*.exe' \) -delete

# Each built native module goes next to its .mylo file, which is where `import native` loads it from.
if [[ "$platform" == windows-* ]]; then
    module_ext=dll
else
    module_ext=so
fi
# Usage: add_module <module folder> <library name> <required: yes/no>
add_module() {
    local dir="$1" lib="$2" required="$3" built
    # Most modules build into build/modules/<dir>; pixel_window builds into its source folder.
    built=$(find "build/modules/$dir" "modules/$dir" -name "$lib.$module_ext" -not -path '*/_deps/*' 2>/dev/null | head -n 1 || true)
    if [[ -z "$built" ]]; then
        if [[ "$required" == yes ]]; then
            echo "error: no built $lib.$module_ext for modules/$dir (configure with -DMODULES=ON)" >&2
            exit 1
        fi
        echo "Skipping $lib.$module_ext (not built)"
        return
    fi
    cp "$built" "$package/modules/$dir/"
    echo "Packaged $built -> modules/$dir/"
}
add_module raylib raylib_binding yes
add_module pixel_window pixel_window yes
# GTK is only installed on the Linux runner.
if [[ "$platform" == linux-* ]]; then gtk_required=yes; else gtk_required=no; fi
add_module gtk gtk_bridge "$gtk_required"

if [[ "$platform" == windows-* ]]; then
    # Bundled so the tools run on PCs without the Visual C++ Redistributable.
    for dll in msvcp140.dll vcruntime140.dll vcruntime140_1.dll; do
        cp "/c/Windows/System32/$dll" "$package/"
    done
fi

# Check the package itself, not the build tree.
smoke_dir=$(mktemp -d)
cat > "$smoke_dir/smoke.mylo" <<'EOF'
fn square(n: i32) -> i32 {
    ret n * n
}
total: i32 = 0
i: i32 = 0
for (i < 10) {
    total = total + square(i)
    i = i + 1
}
print(f"total {total}")
EOF
(
    cd "$package"
    ./mylo$ext --version
    ./mylo$ext --write-config mylovm.conf.example
    ./mylo$ext --print-config
    output=$(./mylo$ext -X stack_size=4k "$smoke_dir/smoke.mylo")
    echo "$output"
    if [[ "$output" != "total 285" ]]; then
        echo "error: the smoke script printed '$output', not 'total 285'" >&2
        exit 1
    fi
    ./mylo-ide$ext --version

    # Load the packaged native modules (no window is opened).
    printf '%s\n' \
        'import native "modules/raylib/raylib_binding.mylo"' \
        'import native "modules/pixel_window/pixel_window.mylo"' \
        'print("modules loaded")' > modules_smoke.mylo
    output=$(./mylo$ext modules_smoke.mylo)
    rm modules_smoke.mylo
    echo "$output"
    if [[ "$output" != "modules loaded" ]]; then
        echo "error: the module smoke script printed '$output', not 'modules loaded'" >&2
        exit 1
    fi
)
rm -rf "$smoke_dir"

(
    cd "$stage_root"
    if [[ "$platform" == windows-* ]]; then
        cmake -E tar cf "../$zip_name" --format=zip Mylo
    else
        # Keeps the executable permissions that macOS and Linux need.
        zip -qry "../$zip_name" Mylo
    fi
)
rm -rf "$stage_root"

echo "Created dist/$zip_name"
if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    echo "zip=dist/$zip_name" >> "$GITHUB_OUTPUT"
fi
