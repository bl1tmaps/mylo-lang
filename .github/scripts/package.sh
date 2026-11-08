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

# Modules ship as source; leave out anything a local build left behind.
cp -R modules "$package/"
find "$package/modules" -type d \( -name Debug -o -name Release -o -name 'build*' -o -name 'raylib-*' -o -name '_deps' \) -prune -exec rm -rf {} +
find "$package/modules" -type f \( -name '*.dll' -o -name '*.so' -o -name '*.dylib' -o -name '*.obj' -o -name '*.o' -o -name '*.exe' \) -delete

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
