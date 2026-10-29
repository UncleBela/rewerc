#!/bin/sh
# Foreground CGI worker for a supervisor; Artix/systemd independent.
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
[ "$(id -un)" = rewerc ] || { echo 'Run as the dedicated rewerc account.' >&2; exit 1; }
state="$app/var/service"
[ -w "$state/tmp" ] && [ -w "$state/auth" ] && [ -w "$state/socket" ] || exit 1
[ ! -e "$state/socket/werc.sock" ] && [ ! -L "$state/socket/werc.sock" ] || { echo 'Verify the previous worker is stopped before removing its socket.' >&2; exit 1; }
set -- --unshare-all --unshare-user --disable-userns --uid "$(id -u)" --gid "$(id -g)" \
    --die-with-parent --new-session --cap-drop ALL --clearenv \
    --setenv PATH /usr/bin:/bin --setenv PLAN9 /opt/plan9 --setenv HOME /nonexistent \
    --proc /proc --dev /dev --tmpfs /tmp
for directory in /usr /bin /lib /lib64 /opt/plan9; do
    if [ -e "$directory" ]; then set -- "$@" --ro-bind "$directory" "$directory"; fi
done
for file in /etc/ld.so.cache /etc/localtime; do
    if [ -f "$file" ]; then set -- "$@" --ro-bind "$file" "$file"; fi
done
for directory in bin apps etc lib tpl pub sites; do
    [ -d "$app/$directory" ] && [ ! -L "$app/$directory" ] || exit 1
    set -- "$@" --ro-bind "$app/$directory" "$app/$directory"
done
set -- "$@" --bind "$state/tmp" "$app/var/tmp" --bind "$state/auth" "$app/var/auth" \
    --bind "$state/socket" "$state/socket"
umask 007
# Limits also apply to the worker; each CGI guard sets stricter per-request limits.
ulimit -c 0
ulimit -n 256
ulimit -v 524288
exec bwrap "$@" --remount-ro / --chdir "$app" /usr/bin/fcgiwrap -f -s "unix:$state/socket/werc.sock"
