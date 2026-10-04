#!/usr/bin/env bash
# One-line installer for FrameEyePointer on the Steam Frame. In Konsole on the headset:
#
#   curl -fsSL https://raw.githubusercontent.com/FarhanTheTinkerer/FrameEyePointer/main/get.sh | bash
#
# Downloads the latest headset package from the repository's releases and runs its
# install.sh. Run it again to update. Arguments after "bash -s --" go to install.sh.
set -euo pipefail

main() {
  local url=${FRAMEEYEPOINTER_URL:-https://github.com/FarhanTheTinkerer/FrameEyePointer/releases/download/frame-latest/FrameEyePointer-steamframe-arm64.tar.gz}
  local tmp
  tmp=$(mktemp -d)
  # Expanded now: the trap runs after main's locals are gone.
  trap "rm -rf -- '$tmp'" EXIT
  echo "Downloading FrameEyePointer..."
  curl -fL --progress-bar "$url" -o "$tmp/package.tar.gz"
  tar -xzf "$tmp/package.tar.gz" -C "$tmp"
  bash "$tmp/FrameEyePointer/install.sh" "$@"
}

# Everything runs from here, so a download cut short runs nothing.
main "$@"
