#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk_root="$repo_root/SDKLib"
output_root="$repo_root/dist/mac"
versions=(2025 2026)
package_output=1

usage() {
  cat <<'EOF'
Usage: scripts/build-release-mac.sh [-v 2025|2026]... [-s SDKLib] [-o output] [--no-package]

The Vectorworks Mac SDK must be available as a directory containing SDKLib.
The script builds the checked-in Xcode project and packages KeeplAutoDimTest.vwlibrary.
EOF
}

while (($#)); do
  case "$1" in
    -v|--version)
      versions=("$2")
      shift 2
      ;;
    -s|--sdk-root)
      sdk_root="$(cd "$2" && pwd)"
      shift 2
      ;;
    -o|--output)
      mkdir -p "$2"
      output_root="$(cd "$2" && pwd)"
      shift 2
      ;;
    --no-package)
      package_output=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -d "$sdk_root/SDKLib" ]]; then
  sdk_root="$sdk_root/SDKLib"
fi
if [[ ! -d "$sdk_root/Include" || ! -d "$sdk_root/LibMac" || ! -x "$sdk_root/ToolsMac/BuildVWR/BuildVWR" ]]; then
  echo "Invalid Vectorworks Mac SDK root: $sdk_root" >&2
  echo "Expected Include, LibMac, and ToolsMac/BuildVWR/BuildVWR." >&2
  exit 1
fi

sdk_link="$repo_root/SDKLib"
cleanup_link=0
if [[ "$sdk_root" != "$sdk_link" ]]; then
  if [[ -e "$sdk_link" || -L "$sdk_link" ]]; then
    echo "Cannot use SDK root because $sdk_link already exists." >&2
    exit 1
  fi
  ln -s "$sdk_root" "$sdk_link"
  cleanup_link=1
fi
cleanup() {
  if ((cleanup_link)); then
    rm "$sdk_link"
  fi
}
trap cleanup EXIT

plugin_name="KeeplAutoDimTest"
for version in "${versions[@]}"; do
  case "$version" in
    2025|2026) ;;
    *) echo "Unsupported Vectorworks version: $version" >&2; exit 2 ;;
  esac

  project="$repo_root/sdk-projects/$version/AutoDimensionPlugin/EmptyModule.xcodeproj"
  if [[ ! -d "$project" ]]; then
    echo "Xcode project not found: $project" >&2
    exit 1
  fi

  build_root="$repo_root/.mac-build/$version"
  rm -rf "$build_root"
  mkdir -p "$build_root" "$output_root/$version"

  xcodebuild \
    -project "$project" \
    -scheme "EmptyModule Release 64" \
    -configuration "Release 64" \
    -derivedDataPath "$build_root/DerivedData" \
    SYMROOT="$build_root/SYMROOT" \
    OBJROOT="$build_root/OBJROOT" \
    CONFIGURATION_BUILD_DIR="$build_root/Products" \
    CODE_SIGNING_ALLOWED=NO \
    CODE_SIGNING_REQUIRED=NO \
    build

  product="$build_root/Products/KeeplAutoDimTest.vwlibrary"
  if [[ ! -d "$product" ]]; then
    echo "Expected product was not created: $product" >&2
    exit 1
  fi

  rm -rf "$output_root/$version/$plugin_name.vwlibrary"
  ditto "$product" "$output_root/$version/$plugin_name.vwlibrary"
  if ((package_output)); then
    archive="$output_root/Keepl-Auto-Dimension-Vectorworks-${version}-Mac.zip"
    rm -f "$archive"
    ditto -c -k --sequesterRsrc --keepParent \
      "$output_root/$version/$plugin_name.vwlibrary" "$archive"
    echo "Packaged Vectorworks $version Mac plugin: $archive"
  fi
done
