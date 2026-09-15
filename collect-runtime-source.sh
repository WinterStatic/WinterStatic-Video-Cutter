#!/usr/bin/env bash
set -euo pipefail

RUNTIME_DIR="$1"
OUT_DIR="$2"
OUT_ZIP="${3:-${OUT_DIR}.zip}"
PACKAGE_TABLE="$RUNTIME_DIR/RUNTIME-PACKAGES.tsv"
MIRROR_BASE="https://mirror.msys2.org/mingw/sources"

if [[ ! -f "$PACKAGE_TABLE" ]]; then
    echo "ERROR: $PACKAGE_TABLE was not found."
    echo "Build the Full Portable runtime before collecting release source."
    exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
    echo "ERROR: curl was not found in the MSYS2 environment."
    exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR/packages"

cp -f "$RUNTIME_DIR/RUNTIME-MANIFEST.txt" "$OUT_DIR/RUNTIME-MANIFEST.txt"
cp -f "$PACKAGE_TABLE" "$OUT_DIR/RUNTIME-PACKAGES.tsv"
if [[ -f "$RUNTIME_DIR/THIRD-PARTY-NOTICE.txt" ]]; then
    cp -f "$RUNTIME_DIR/THIRD-PARTY-NOTICE.txt" "$OUT_DIR/THIRD-PARTY-NOTICE.txt"
fi

if [[ -d "$RUNTIME_DIR/licenses" ]]; then
    cp -a "$RUNTIME_DIR/licenses" "$OUT_DIR/licenses"
fi

manifest="$OUT_DIR/SOURCE-MANIFEST.txt"
status_table="$OUT_DIR/SOURCE-PACKAGES.tsv"
: > "$status_table"
printf 'package\tinstalled_version\tsource_base\tsource_archive\tsha256\tsignature\n' > "$status_table"

declare -A downloaded=()
package_count=0
archive_count=0
failed=0

while IFS=$'\t' read -r package installed_version source_base source_archive_version; do
    [[ "$package" == "package" ]] && continue
    [[ -n "$package" ]] || continue
    ((package_count += 1))

    if [[ -z "$source_base" || "$source_base" == "-" ]]; then
        echo "ERROR: source package base could not be resolved for $package $installed_version"
        printf '%s\t%s\t-\t-\t-\tunresolved-source-base\n' \
            "$package" "$installed_version" >> "$status_table"
        failed=1
        continue
    fi

    archive="${source_base}-${source_archive_version}.src.tar.zst"
    key="${archive,,}"
    target="$OUT_DIR/packages/$archive"
    sig_target="$target.sig"
    url="$MIRROR_BASE/$archive"
    sig_url="$url.sig"

    if [[ -z "${downloaded[$key]+x}" ]]; then
        echo "Downloading $archive"
        temp="$target.part"
        rm -f "$temp"
        if ! curl -fL --retry 4 --retry-delay 2 --connect-timeout 20 \
            -o "$temp" "$url"; then
            echo "ERROR: MSYS2 Source-Only Tarball not found/downloadable: $url"
            rm -f "$temp"
            failed=1
            continue
        fi
        mv -f "$temp" "$target"
        downloaded["$key"]=1
        ((archive_count += 1))

        # Signatures are useful provenance evidence but are not required to make
        # the corresponding source itself complete. Download/verify when possible.
        signature_status="unavailable"
        if curl -fL --retry 2 --retry-delay 1 --connect-timeout 20 \
            -o "$sig_target.part" "$sig_url" 2>/dev/null; then
            mv -f "$sig_target.part" "$sig_target"
            signature_status="downloaded"
            if command -v pacman-key >/dev/null 2>&1; then
                if pacman-key --verify "$sig_target" "$target" >/dev/null 2>&1; then
                    signature_status="verified"
                else
                    signature_status="downloaded-unverified"
                fi
            fi
        else
            rm -f "$sig_target.part"
        fi
    else
        signature_status="shared-source-archive"
    fi

    if [[ ! -f "$target" ]]; then
        echo "ERROR: source archive is missing after download: $archive"
        failed=1
        continue
    fi

    sha256="$(sha256sum "$target" | awk '{print $1}')"
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$package" "$installed_version" "$source_base" "$archive" "$sha256" "$signature_status" \
        >> "$status_table"
done < "$PACKAGE_TABLE"

if [[ "$failed" -ne 0 ]]; then
    echo
    echo "ERROR: one or more exact MSYS2 source archives could not be collected."
    echo "The partial source directory has been kept for diagnosis:"
    echo "  $OUT_DIR"
    exit 1
fi

runtime_manifest_sha="$(sha256sum "$RUNTIME_DIR/RUNTIME-MANIFEST.txt" | awk '{print $1}')"
{
    echo "WinterStatic Video Cutter - corresponding runtime source bundle"
    echo "Purpose"
    echo "-------"
    echo "This bundle accompanies the Full Portable binary release. It contains the"
    echo "official MSYS2 Source-Only Tarballs corresponding to every MSYS2 package"
    echo "represented by the DLLs in the bundled libmpv runtime."
    echo
    echo "The source archive names are derived from the exact installed package"
    echo "version and pacman's recorded source package base (%BASE%). MSYS2 publishes"
    echo "these source-only archives at:"
    echo "  $MIRROR_BASE/"
    echo
    echo "Contents"
    echo "--------"
    echo "packages/              official MSYS2 .src.tar.zst archives"
    echo "packages/*.sig         MSYS2 signatures where available"
    echo "RUNTIME-MANIFEST.txt   DLL -> binary package provenance from Full Portable"
    echo "RUNTIME-PACKAGES.tsv   exact binary package -> source package mapping"
    echo "SOURCE-PACKAGES.tsv    downloaded archive hashes/signature status"
    echo "THIRD-PARTY-NOTICE.txt runtime third-party distribution notice"
    echo "licenses/              installed runtime package license files (copy)"
    echo
    echo "Counts"
    echo "------"
    echo "Runtime binary packages represented: $package_count"
    echo "Unique source archives included:     $archive_count"
    echo
    echo "Matching Full Portable runtime manifest SHA-256:"
    echo "  $runtime_manifest_sha"
    echo
    echo "The .src.tar.zst files are MSYS2's source-only package archives. They retain"
    echo "the package build recipe/patches and source inputs used for the represented"
    echo "package version. This bundle intentionally includes source archives for all"
    echo "represented MSYS2 runtime packages rather than trying to classify only the"
    echo "copyleft subset at release time."
    echo
    echo "This file is release-engineering information, not legal advice."
} > "$manifest"

echo
echo "Source collection complete:"
echo "  $OUT_DIR"

# Produce a GitHub-release-friendly archive when bsdtar is available. The source
# tarballs are already compressed; wrapping them in ZIP is for convenient upload.
if command -v bsdtar >/dev/null 2>&1; then
    rm -f "$OUT_ZIP"
    parent="$(dirname "$OUT_DIR")"
    base="$(basename "$OUT_DIR")"
    bsdtar -a -cf "$OUT_ZIP" -C "$parent" "$base"
    echo "Release archive:"
    echo "  $OUT_ZIP"
else
    echo "WARNING: bsdtar was not found; source folder is complete but ZIP was not created."
fi
