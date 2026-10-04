#!/usr/bin/env bash
# FrameEyePointer installer for the Steam Frame. Run it on the headset (Konsole in
# the desktop, or over SSH) from the unpacked package folder:
#
#   ./install.sh            install or update (no sudo needed; everything goes in ~)
#   ./install.sh probe      check eye tracking, the driver, buttons; paste this in bug reports
#   ./install.sh status     is the service running
#   ./install.sh log [N]    last N lines of the service log (default 40)
#   ./install.sh restart    restart the service (after changing settings: not needed, they reload)
#   ./install.sh uninstall  remove everything but your settings
set -euo pipefail

src=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
dest=${FRAMEEYEPOINTER_DIR:-$HOME/.local/share/frameeyepointer}
unit=frameeyepointer.service
unit_dir=$HOME/.config/systemd/user
config=${XDG_CONFIG_HOME:-$HOME/.config}/frameeyepointer/frameeyepointer.ini

say() { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }

find_vrpathreg() {
  local c
  for c in /opt/steamvr/bin/linuxarm64/vrpathreg \
           "$HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linuxarm64/vrpathreg" \
           "$HOME/.local/share/Steam/steamapps/common/SteamVR/bin/linux64/vrpathreg"; do
    [ -x "$c" ] && { printf '%s\n' "$c"; return 0; }
  done
  return 1
}

vrpathreg() {
  local reg
  reg=$(find_vrpathreg) || { warn "can't find SteamVR's vrpathreg"; return 1; }
  LD_LIBRARY_PATH="$(dirname "$reg")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$reg" "$@"
}

has_steamvr_unit() { systemctl --user cat steamvr.service >/dev/null 2>&1; }

install_files() {
  [ -x "$src/frameeyepointerd" ] || { say "run this from the unpacked FrameEyePointer package"; exit 1; }
  if [ "$(uname -m)" != aarch64 ]; then
    warn "this is $(uname -m), not the Frame's aarch64: the package may not run here"
  fi
  if ! { grep -qx 'ID=steamos' /etc/os-release && grep -qE '^VARIANT_ID="?vr"?$' /etc/os-release; } 2>/dev/null; then
    warn "this doesn't look like a Steam Frame (SteamOS VR variant); continuing anyway"
  fi

  if [ "$src" != "$dest" ]; then
    mkdir -p "$(dirname "$dest")"
    rm -rf "$dest.new"
    cp -a "$src" "$dest.new"
    rm -rf "$dest.old"
    [ -e "$dest" ] && mv "$dest" "$dest.old"
    mv "$dest.new" "$dest"
    rm -rf "$dest.old"
  fi
  say "installed to $dest"

  if vrpathreg adddriver "$dest/driver/frameeyepointer"; then
    say "registered the SteamVR driver"
  else
    warn "couldn't register the driver; register $dest/driver/frameeyepointer with vrpathreg adddriver"
  fi

  mkdir -p "$unit_dir"
  local extra wanted
  if has_steamvr_unit; then
    extra=$'After=steamvr.service\nPartOf=steamvr.service'
    wanted=steamvr.service
  else
    # No steamvr.service here: run all the time; the service waits for SteamVR itself.
    extra=''
    wanted=default.target
  fi
  local tmpl
  tmpl=$(<"$dest/frameeyepointer.service")
  tmpl=${tmpl//@DIR@/$dest}
  tmpl=${tmpl//@UNIT_EXTRA@/$extra}
  tmpl=${tmpl//@WANTED_BY@/$wanted}
  printf '%s\n' "$tmpl" >"$unit_dir/$unit"
  systemctl --user daemon-reload
  systemctl --user enable "$unit" >/dev/null 2>&1
  say "service installed (starts with ${wanted%.*})"

  if [ ! -e "$config" ]; then
    say "settings will be created at $config on first start"
  else
    say "keeping your settings at $config"
  fi

  if has_steamvr_unit && systemctl --user is-active --quiet steamvr.service; then
    say ""
    say "SteamVR loads drivers when it starts, so it needs a restart to pick up the eye laser."
    local answer=n
    if { : </dev/tty; } 2>/dev/null; then
      read -r -p "Restart SteamVR now? [y/N] " answer </dev/tty || answer=n
    fi
    if [[ $answer == [yY]* ]]; then
      systemctl --user restart steamvr.service
      say "SteamVR restarting; the eye laser starts with it."
    else
      say "Restart SteamVR yourself when convenient (or reboot the headset)."
    fi
  else
    systemctl --user restart "$unit" 2>/dev/null || true
    say "Start SteamVR (or restart it) to load the eye laser."
  fi
  say ""
  say "Then run: $dest/install.sh probe"
}

case ${1:-install} in
  install) install_files ;;
  probe) "$dest/frameeyepointerd" --probe ;;
  status) systemctl --user status "$unit" --no-pager || true ;;
  log) journalctl --user -u "$unit" --no-pager -o cat -n "${2:-40}" ;;
  restart) systemctl --user restart "$unit"; systemctl --user is-active "$unit" ;;
  uninstall)
    systemctl --user disable --now "$unit" 2>/dev/null || true
    rm -f "$unit_dir/$unit"
    systemctl --user daemon-reload
    vrpathreg removedriver "$dest/driver/frameeyepointer" 2>/dev/null || true
    rm -rf "$dest"
    say "removed. Your settings stay at $config. Restart SteamVR to unload the driver." ;;
  -h|--help|help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//' ;;
  *) say "unknown command: $1 (try: install, probe, status, log, restart, uninstall)"; exit 2 ;;
esac
