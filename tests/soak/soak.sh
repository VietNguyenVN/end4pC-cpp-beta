#!/usr/bin/env bash
# Soak test of the running shell: opens and closes every popup, panel, window and widget over and over through the IPC while it samples
# the shell's memory (RSS), open file descriptors, threads and child processes, and keeps its stderr. At the end it fits a line to the
# memory after a warm-up and says whether anything grows. Run it from the session the shell runs in (it restarts the shell once to catch
# its stderr). Do not type while it runs: the popups take the keyboard for a moment.
#   tests/soak/soak.sh [SECONDS]        default 480 (8 minutes); the first 90 s are the warm-up and do not count for the trend
set -u
dur=${1:-480}; warm=${WARMUP:-90}
root="$(cd "$(dirname "$0")/../.." && pwd)"
exe="$root/build/end4pC"
out="${SOAK_DIR:-${TMPDIR:-/tmp}/end4pC-soak}"; mkdir -p "$out"
cfg="${XDG_DATA_HOME:-$HOME/.local/share}/end4pC/config.json"
cp "$cfg" "$out/config.before.json"
log="$out/stderr.txt"; csv="$out/samples.csv"; : > "$log"; echo "t,rss_kb,fds,threads,children" > "$csv"

# Restart the shell with its stderr in a file
pkill -x end4pC; for _ in $(seq 40); do pgrep -x end4pC >/dev/null || break; sleep 0.1; done
setsid "$exe" >/dev/null 2>>"$log" & sleep 4
pid=$(pgrep -x end4pC | head -n1); [ -n "$pid" ] || { echo "the shell did not start"; exit 1; }
echo "shell pid $pid, $dur s"

sample() {
  local t=$1 rss fds thr kids
  rss=$(awk '/VmRSS/{print $2}' /proc/$pid/status 2>/dev/null) || return 1
  fds=$(ls /proc/$pid/fd 2>/dev/null | wc -l)
  thr=$(awk '/Threads/{print $2}' /proc/$pid/status)
  kids=$(pgrep -P "$pid" 2>/dev/null | wc -l)
  echo "$t,$rss,$fds,$thr,$kids" >> "$csv"
}
ipc() { "$exe" "$1" >/dev/null 2>&1; }

cycle() {  # one pass over everything
  ipc "launcher toggle"; sleep 0.5; ipc "launcher toggle"; sleep 0.5
  ipc "control"; sleep 0.6; ipc "control close"; sleep 0.5
  ipc "media toggle"; sleep 0.7; ipc "media toggle"; sleep 0.5
  for k in clockpanel weatherpanel aipanel respanel; do
    ipc "$k open"; sleep 0.5; ipc "$k tab 1"; sleep 0.4; ipc "$k tab 0"; sleep 0.4; ipc "$k close"; sleep 0.5
  done
  ipc "audiomenu open"; sleep 0.6; for t in input apps output; do ipc "audiomenu tab $t"; sleep 0.4; done; ipc "audiomenu close"; sleep 0.5
  ipc "desktopmenu open 600 400"; sleep 0.6; ipc "desktopmenu close"; sleep 0.5
  ipc "selector toggle"; sleep 1.0; ipc "selector toggle"; sleep 0.6
  ipc "power toggle"; sleep 0.5; ipc "power toggle"; sleep 0.5
  ipc "switcher open"; sleep 0.5; ipc "switcher close"; sleep 0.5
  ipc "osd volume"; sleep 0.3; ipc "osd brightness"; sleep 0.3
  ipc "tray open"; sleep 0.4; ipc "tray close"; sleep 0.3
  notify-send -a soak "Soak test" "notification $RANDOM"; sleep 0.6
  ipc "set materialIcons 1"; sleep 0.5; ipc "set materialIcons 0"; sleep 0.4
  ipc "dock toggle"; sleep 0.8; ipc "dock toggle"; sleep 0.6
  n=$((n+1))
  if [ $((n % 4)) -eq 1 ]; then ipc "settings"; sleep 2.5; ipc "settings"; sleep 1; ipc "tools"; sleep 2.5; ipc "tools"; sleep 1; fi
}

n=0; start=$(date +%s)
( while kill -0 "$pid" 2>/dev/null; do sample $(( $(date +%s) - start )) || break; sleep 2; done ) &
sampler=$!
while [ $(( $(date +%s) - start )) -lt "$dur" ]; do
  kill -0 "$pid" 2>/dev/null || { echo "THE SHELL DIED at $(( $(date +%s) - start )) s"; break; }
  cycle
done
for k in launcher control media clockpanel weatherpanel aipanel respanel audiomenu desktopmenu selector power; do ipc "$k close"; done
sleep 25; sample $(( $(date +%s) - start )); kill "$sampler" 2>/dev/null; wait "$sampler" 2>/dev/null
alive=no; kill -0 "$pid" 2>/dev/null && alive=yes
cp "$cfg" "$out/config.after.json"

python3 - "$csv" "$warm" "$alive" "$log" "$out" <<'PY'
import sys,csv,statistics
rows=list(csv.DictReader(open(sys.argv[1]))); warm=float(sys.argv[2]); alive=sys.argv[3]; log=sys.argv[4]; out=sys.argv[5]
R=[(float(r['t']),float(r['rss_kb']),int(r['fds']),int(r['threads']),int(r['children'])) for r in rows]
def slope(pts):
    n=len(pts)
    if n<3: return 0.0
    mx=sum(p[0] for p in pts)/n; my=sum(p[1] for p in pts)/n
    d=sum((p[0]-mx)**2 for p in pts)
    return sum((p[0]-mx)*(p[1]-my) for p in pts)/d if d else 0.0
after=[r for r in R if r[0]>=warm]
print(f"samples: {len(R)} ({len(after)} after the {warm:.0f}s warm-up); shell alive at the end: {alive}")
print(f"RSS: start {R[0][1]/1024:.1f} MB, after warm-up {after[0][1]/1024:.1f} MB, end {R[-1][1]/1024:.1f} MB, peak {max(r[1] for r in R)/1024:.1f} MB")
print(f"RSS trend after warm-up: {slope([(r[0],r[1]) for r in after])*60/1024:+.2f} MB/min")
print(f"fds: min {min(r[2] for r in R)}, max {max(r[2] for r in R)}, end {R[-1][2]} (start {R[0][2]}); threads: {min(r[3] for r in R)}..{max(r[3] for r in R)}; children max {max(r[4] for r in R)}, end {R[-1][4]}")
err=[l for l in open(log,errors='replace') if l.strip()]
print(f"stderr: {len(err)} lines")
for l in err[:15]: print("  "+l.rstrip()[:200])
open(out+'/summary.txt','w').write('ok\n')
PY
echo "files in $out (samples.csv, stderr.txt, config.before/after.json)"
cmp -s "$out/config.before.json" "$out/config.after.json" && echo "config.json identical before/after" || echo "config.json CHANGED (diff the two files in $out)"
