#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_dir="${RACK_DIR:?Set RACK_DIR to an extracted Rack 2 SDK directory}"

for command_name in git jq make zstd tar shasum; do
    command -v "$command_name" >/dev/null || { printf 'Missing %s\n' "$command_name" >&2; exit 1; }
done
[[ -f "$sdk_dir/plugin.mk" ]] || { printf 'Rack SDK not found: %s\n' "$sdk_dir" >&2; exit 1; }

cd "$repo_dir"
git diff --quiet HEAD -- || { printf 'Commit tracked changes before making a release build.\n' >&2; exit 1; }
git diff --cached --quiet || { printf 'Commit staged changes before making a release build.\n' >&2; exit 1; }

version="$(jq -er '.version' vcv/plugin.json)"
slug="$(jq -er '.slug' vcv/plugin.json)"
commit="$(git rev-parse HEAD)"
temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/slolfo-release.XXXXXX")"
trap 'rm -rf "$temp_dir"' EXIT
mkdir -p "$temp_dir/source"
git archive HEAD | tar -xf - -C "$temp_dir/source"

make -C "$temp_dir/source/vcv" dist RACK_DIR="$sdk_dir"
output_dir="$repo_dir/_local/releases/v$version"
mkdir -p "$output_dir"
packages=("$temp_dir/source/vcv/dist/$slug-$version-"*.vcvplugin)
[[ ${#packages[@]} -eq 1 && -f "${packages[0]}" ]] || { printf 'Expected one plugin package.\n' >&2; exit 1; }
package="${packages[0]}"
for required in "$slug/plugin.json" "$slug/LICENSE" "$slug/res/SlowLFO.svg"; do
    zstd -dc "$package" | tar -tf - | grep -Fqx "$required" || { printf 'Package missing %s\n' "$required" >&2; exit 1; }
done
cp "$package" "$output_dir/"
(cd "$output_dir" && shasum -a 256 "$(basename "$package")" > SHA256SUMS)
printf 'Source commit: %s\nOutput: %s\n' "$commit" "$output_dir"
