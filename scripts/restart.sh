#!/bin/bash
# Restarts the end4pC shell. Used by the control center's restart button, by `end4pC "restart"` and by SUPER + R.
# It works from outside the shell on purpose: it also fixes a shell that no longer answers.
exe="$(cd "$(dirname "$0")/.." && pwd)/build/end4pC"
pkill -x end4pC                                   # SIGTERM: the shell closes cleanly
for _ in $(seq 40); do pgrep -x end4pC >/dev/null || break; sleep 0.1; done
if pgrep -x end4pC >/dev/null; then pkill -9 -x end4pC; sleep 0.3; fi   # it did not close in 4 s: force it
setsid "$exe" >/dev/null 2>&1 &
