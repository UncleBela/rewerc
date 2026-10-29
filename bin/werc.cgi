#!/bin/sh
set -eu
umask 077
werc_bin=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
werc_root=$(CDPATH= cd -- "$werc_bin/.." && pwd -P)
cd "$werc_bin"
export PLAN9=${PLAN9:-/opt/plan9}
export TMPDIR="$werc_root/var/tmp"
mkdir -p "$TMPDIR"
# Local content directory; the public hostname remains localhost.
case ${SERVER_NAME-} in
    localhost) export WERC_SITE=${WERC_SITE-rewerc-guide} ;;
esac
exec ./contrib/werc-guard
