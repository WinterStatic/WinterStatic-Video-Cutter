#!/usr/bin/env bash
set -euo pipefail

DIST="$1"
# DIST is the isolated libmpv runtime directory inside the portable build.
mkdir -p "$DIST"

MINGW_BIN="/mingw64/bin"
MPV_DLL="$MINGW_BIN/libmpv-2.dll"

if [[ ! -f "$MPV_DLL" ]]; then
    echo "ERROR: $MPV_DLL was not found."
    echo "Install the MSYS2 MINGW64 mpv package first."
    exit 1
fi

declare -a queue=()
declare -A queued=()
queue_index=0

enqueue_dll() {
    local path="$1"
    local base="${path##*/}"
    local key="${base,,}"

    if [[ -n "${queued[$key]+x}" ]]; then
        return
    fi

    queued["$key"]=1
    queue+=("$path")
}

copy_and_enqueue() {
    local source="$1"
    local note="${2:-}"
    local base="${source##*/}"
    local target="$DIST/$base"

    if [[ ! -f "$target" ]]; then
        cp -f "$source" "$target"
        if [[ -n "$note" ]]; then
            echo "  + $base ($note)"
        else
            echo "  + $base"
        fi
    fi

    enqueue_dll "$target"
}

local_db_field() {
    local package="$1"
    local field="$2"
    local desc name value

    # pacman's installed-package database includes %BASE% for modern packages.
    # Validate %NAME% because a package name can be a prefix of another package.
    for desc in /var/lib/pacman/local/"$package"-*/desc; do
        [[ -f "$desc" ]] || continue
        name="$(awk '$0 == "%NAME%" { getline; print; exit }' "$desc")"
        [[ "$name" == "$package" ]] || continue
        value="$(awk -v marker="%${field}%" '$0 == marker { getline; print; exit }' "$desc")"
        [[ -n "$value" ]] || return 1
        printf '%s\n' "$value"
        return 0
    done

    return 1
}

echo "Collecting libmpv/FFmpeg runtime DLLs..."
copy_and_enqueue "$MPV_DLL"

# Some video backends are loaded dynamically and therefore do not appear in
# libmpv's static dependency chain. Seed those known optional runtime pieces
# into the same queue, then dependency-expand them exactly like everything else.
for extra in vulkan-1.dll d3dcompiler_47.dll libEGL.dll libGLESv2.dll; do
    if [[ -f "$MINGW_BIN/$extra" ]]; then
        copy_and_enqueue "$MINGW_BIN/$extra" "dynamic graphics runtime"
    fi
done

while (( queue_index < ${#queue[@]} )); do
    binary="${queue[$queue_index]}"
    ((queue_index += 1))

    while IFS= read -r dep; do
        [[ -z "$dep" ]] && continue
        copy_and_enqueue "$dep"
    done < <(
        ldd "$binary" 2>/dev/null |
        awk '{print $3}' |
        grep '^/mingw64/bin/.*\.dll$' || true
    )
done

echo
echo "Checking copied runtime DLLs..."
missing=0
for binary in "${queue[@]}"; do
    if ldd "$binary" 2>/dev/null | grep -q 'not found'; then
        echo "Missing dependency in: ${binary##*/}"
        ldd "$binary" | grep 'not found' || true
        missing=1
    fi
done

if [[ "$missing" -ne 0 ]]; then
    echo "ERROR: one or more MinGW runtime dependencies are still missing."
    exit 1
fi

# Resolve package provenance only after runtime collection has finished.
# A single batch query avoids repeatedly invoking pacman while dependencies are
# expanded and gives the same provenance data.
echo "Resolving runtime package provenance..."
declare -A runtime_owner=()
declare -A package_versions=()
declare -A package_bases=()
declare -a runtime_sources=()

for binary in "${queue[@]}"; do
    base="${binary##*/}"
    source="$MINGW_BIN/$base"
    if [[ -f "$source" ]]; then
        runtime_sources+=("$source")
    else
        runtime_owner["$base"]="unresolved|-"
    fi
done

if (( ${#runtime_sources[@]} > 0 )); then
    while IFS= read -r owner_line; do
        [[ "$owner_line" == *" is owned by "* ]] || continue
        owner_path="${owner_line%% is owned by *}"
        owner_tail="${owner_line##* is owned by }"
        package="${owner_tail% *}"
        version="${owner_tail##* }"
        base="${owner_path##*/}"
        runtime_owner["$base"]="$package|$version"
        package_versions["$package"]="$version"
    done < <(pacman -Qo -- "${runtime_sources[@]}" 2>/dev/null || true)
fi

# Ensure every bundled DLL appears in the manifest even if pacman could not
# resolve ownership for an unusual locally supplied runtime file.
for binary in "${queue[@]}"; do
    base="${binary##*/}"
    if [[ -z "${runtime_owner[$base]+x}" ]]; then
        runtime_owner["$base"]="unresolved|-"
    fi
done

# Resolve each binary package back to its MSYS2 source package (pkgbase). This
# gives the release-source collector a deterministic mapping to the exact
# Source-Only Tarball published by MSYS2 for that installed package version.
for package in "${!package_versions[@]}"; do
    pkgbase="$(local_db_field "$package" BASE || true)"
    if [[ -n "$pkgbase" ]]; then
        package_bases["$package"]="$pkgbase"
    else
        package_bases["$package"]="-"
    fi
done

manifest="$DIST/RUNTIME-MANIFEST.txt"
{
    echo "WinterStatic Video Cutter - bundled runtime manifest"
    echo "Generated from the builder's MSYS2 MINGW64 installation."
    echo
    echo "DLL -> MSYS2 package version"
    echo "---------------------------"
    for base in "${!runtime_owner[@]}"; do
        IFS='|' read -r package version <<< "${runtime_owner[$base]}"
        printf '%s -> %s %s\n' "$base" "$package" "$version"
    done | sort -f
    echo
    echo "Unique MSYS2 packages represented"
    echo "---------------------------------"
    for package in "${!package_versions[@]}"; do
        printf '%s %s (source base: %s)\n' \
            "$package" "${package_versions[$package]}" "${package_bases[$package]}"
    done | sort -f
    echo
    echo "This manifest records binary provenance. RUNTIME-PACKAGES.tsv contains"
    echo "the machine-readable package/version/source-base mapping used by the"
    echo "release-source collector."
} > "$manifest"

echo "  + RUNTIME-MANIFEST.txt"

package_table="$DIST/RUNTIME-PACKAGES.tsv"
{
    printf 'package\tinstalled_version\tsource_base\tsource_archive_version\n'
    for package in "${!package_versions[@]}"; do
        version="${package_versions[$package]}"
        # Epochs (for example 2:1.0-1) are part of pacman's installed version
        # but are not included in source package filenames.
        archive_version="${version#*:}"
        if [[ "$archive_version" == "$version" && "$version" != *:* ]]; then
            archive_version="$version"
        fi
        printf '%s\t%s\t%s\t%s\n' \
            "$package" "$version" "${package_bases[$package]}" "$archive_version"
    done | sort -f
} > "$package_table"

echo "  + RUNTIME-PACKAGES.tsv"

license_root="$DIST/licenses"
mkdir -p "$license_root"
license_count=0

copy_license_file() {
    local package="$1"
    local license_path="$2"
    local package_dir base target stem ext suffix

    [[ -n "$package" && -f "$license_path" ]] || return 0

    package_dir="$license_root/$package"
    mkdir -p "$package_dir"
    base="${license_path##*/}"
    target="$package_dir/$base"

    # Preserve duplicate filenames from nested license directories.
    if [[ -e "$target" ]]; then
        stem="${base%.*}"
        ext="${base##*.}"
        suffix=2
        while [[ -e "$target" ]]; do
            if [[ "$stem" == "$base" ]]; then
                target="$package_dir/${base}-${suffix}"
            else
                target="$package_dir/${stem}-${suffix}.${ext}"
            fi
            suffix=$((suffix + 1))
        done
    fi

    cp -f "$license_path" "$target"
    license_count=$((license_count + 1))
}

if (( ${#package_versions[@]} > 0 )); then
    echo "Collecting installed runtime license files..."

    # MSYS2 packages normally install license text directly under
    # <prefix>/share/licenses/<real-package-name>/. Read those directories
    # directly first; this is much cheaper than asking pacman to enumerate all
    # installed files for every represented package.
    mapfile -t packages < <(printf '%s\n' "${!package_versions[@]}" | sort -f)
    package_total=${#packages[@]}
    package_index=0
    declare -a pacman_fallback_packages=()

    local_db_license_files() {
        local package="$1"
        local desc files name entry

        # Reuse pacman's local package database directly. Validate %NAME% so a
        # package name that is a prefix of another cannot select the wrong entry.
        for desc in /var/lib/pacman/local/"$package"-*/desc; do
            [[ -f "$desc" ]] || continue
            name="$(awk '$0 == "%NAME%" { getline; print; exit }' "$desc")"
            [[ "$name" == "$package" ]] || continue
            files="${desc%/desc}/files"
            [[ -f "$files" ]] || return 1

            awk '
                $0 == "%FILES%" { in_files = 1; next }
                in_files && /^%/ { exit }
                in_files && /share\/licenses\// && $0 !~ /\/$/ { print "/" $0 }
            ' "$files"
            return 0
        done

        return 1
    }

    for package in "${packages[@]}"; do
        package_index=$((package_index + 1))
        printf '  [%d/%d] %s\n' "$package_index" "$package_total" "$package"

        found_for_package=0
        real_package="$package"
        if [[ "$real_package" == mingw-w64-x86_64-* ]]; then
            real_package="${real_package#mingw-w64-x86_64-}"
        fi

        # Current MSYS2 convention uses the environment prefix directory with
        # the environment prefix removed from the actual license directory. Use
        # Bash globbing here instead of spawning a separate find.exe per package.
        candidate_dirs=("/mingw64/share/licenses/$real_package")
        if [[ "$real_package" != "$package" ]]; then
            candidate_dirs+=("/mingw64/share/licenses/$package")
        fi

        shopt -s nullglob globstar
        for candidate_dir in "${candidate_dirs[@]}"; do
            [[ -d "$candidate_dir" ]] || continue
            for license_path in "$candidate_dir"/**/*; do
                [[ -f "$license_path" ]] || continue
                copy_license_file "$package" "$license_path"
                found_for_package=$((found_for_package + 1))
            done
        done
        shopt -u globstar

        local_db_resolved=0
        if (( found_for_package == 0 )); then
            # Reading /var/lib/pacman/local avoids starting a separate pacman
            # process for each package. A successful database lookup is
            # conclusive even when the package has no share/licenses files.
            # Only a database lookup failure should fall back to pacman -Ql.
            local_db_output=""
            if local_db_output="$(local_db_license_files "$package")"; then
                local_db_resolved=1
                while IFS= read -r license_path; do
                    [[ -n "$license_path" && -f "$license_path" ]] || continue
                    copy_license_file "$package" "$license_path"
                    found_for_package=$((found_for_package + 1))
                done <<< "$local_db_output"
            fi
        fi

        if (( found_for_package == 0 && local_db_resolved == 0 )); then
            pacman_fallback_packages+=("$package")
        fi
    done

    # Keep a compatibility fallback for unusual package layouts/database states,
    # but batch all such packages into ONE pacman invocation instead of launching
    # pacman once per package.
    if (( ${#pacman_fallback_packages[@]} > 0 )); then
        printf '  Checking %d unresolved package(s) with one pacman fallback...\n' \
            "${#pacman_fallback_packages[@]}"
        while IFS=$'\t' read -r package license_path; do
            [[ -n "$package" && -f "$license_path" ]] || continue
            copy_license_file "$package" "$license_path"
        done < <(
            pacman -Ql "${pacman_fallback_packages[@]}" 2>/dev/null |
            awk '$2 ~ /\/share\/licenses\// {print $1 "\t" $2}' || true
        )
    fi
fi

if (( license_count == 0 )); then
    echo "WARNING: no installed MSYS2 package license files were found to copy."
else
    echo "  + licenses/ ($license_count files)"
fi

echo "Runtime collection complete."
