#!/bin/sh
# Administrator preparation only. Changes no existing content ownership or services.
set -eu
app=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
[ "$(id -u)" -eq 0 ] || { echo 'Run this preparation as administrator; it creates the rewerc service account.' >&2; exit 1; }
web_user=${1:-anon}
for command in useradd getent bwrap fcgiwrap install; do command -v "$command" >/dev/null; done
id "$web_user" >/dev/null
if ! getent passwd rewerc >/dev/null; then
    useradd --system --user-group --no-create-home --home-dir /nonexistent --shell /usr/bin/nologin rewerc
fi
[ "$(getent passwd rewerc | cut -d: -f6-7)" = '/nonexistent:/usr/bin/nologin' ] || exit 1
install -d -o root -g root -m 0755 "$app/var/service"
install -d -o rewerc -g rewerc -m 0700 "$app/var/service/tmp" "$app/var/service/auth"
install -d -o rewerc -g "$(id -gn "$web_user")" -m 2750 "$app/var/service/socket"
[ ! -L "$app/etc/service.uid" ] || exit 1
service_uid=$(id -u rewerc)
[ "$service_uid" -ne 0 ] || exit 1
# Root owns this activation marker; ordinary worker invocations fail closed.
install -o root -g root -m 0644 /dev/null "$app/etc/service.uid"
printf '%s\n' "$service_uid" > "$app/etc/service.uid"
printf '%s\n' 'Prepared rewerc account and private runtime; no services started.' \
    'Provision an account using the documented WERC_AUTH_DIR command, then start service-run.sh under your supervisor.'
