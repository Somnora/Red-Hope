#!/usr/bin/env bash
# Lambda Cloud control for the Red Hope 3D lane. Needs LAMBDA_API_KEY in env
# (the Manifold app terminal exports it; there is no MCP involved).
#
#   rh_lambda.sh status              what is running, and what it is costing
#   rh_lambda.sh launch              A100 in us-east-1 with red-hope-east attached
#   rh_lambda.sh ip                  bare IP of the running instance
#   rh_lambda.sh sync                write the IP into ~/.config/rh3d/host.env
#   rh_lambda.sh terminate <id>      kill ONE instance, by id
#
# Region is NOT free choice: the red-hope-east filesystem (weights, HF cache,
# the whole pipeline - MIGRATED off the shared Somnora-East 2026-08-17, ~35 GB,
# checksum-verified) can only attach to an instance in its own region, so
# us-east-1 / gpu_1x_a100_sxm4 it is. Cheaper H100s elsewhere would strand it.
#
# THIS ACCOUNT IS SHARED. Red Hope is not the only project on it - Tally runs
# vLLM servers on the same Lambda account (their data stays on Somnora-East;
# ours no longer does, which is half the point of the split).
# `terminate` used to collect EVERY instance id the account returned and kill
# them all, off one word with no argument. Nothing in the output distinguished
# a Red Hope box from someone else's, so that was a project-wide outage waiting
# on a typo. It now requires an explicit id and refuses a bare invocation.
#
# The wider lesson, learned the hard way on 2026-08-17: a box that looks idle
# is not evidence it is free. A vLLM server loading a 27B model shows no users,
# no obvious processes and no NFS writes for 10-12 minutes while it reads
# weights from the shared cache - it looks exactly like an abandoned box right
# up until it starts serving. `nvidia-smi` is the honest check: a warming
# server already holds tens of GB of VRAM. If you did not launch it, leave it.
set -euo pipefail

API="https://cloud.lambdalabs.com/api/v1"
TYPE="gpu_1x_a100_sxm4"
REGION="us-east-1"
SSHKEY="lambda-burst-ed25519"
FS="red-hope-east"
HOSTENV="$HOME/.config/rh3d/host.env"

[ -n "${LAMBDA_API_KEY:-}" ] || { echo "LAMBDA_API_KEY not set - open the Manifold terminal" >&2; exit 1; }
api() { curl -s -u "$LAMBDA_API_KEY:" "$@"; }

case "${1:-status}" in
status)
  api "$API/instances" | python3 -c '
import sys, json
d = json.load(sys.stdin).get("data", [])
if not d:
    print("no instances running (billing $0.00/hr)"); raise SystemExit
for i in d:
    print("%s  %s  %s  ip=%s  status=%s" % (
        i.get("id"), i.get("instance_type", {}).get("name"),
        i.get("region", {}).get("name"), i.get("ip"), i.get("status")))
    c = i.get("instance_type", {}).get("price_cents_per_hour")
    if c: print("  billing $%.2f/hr" % (c / 100))
'
  ;;
launch)
  api -X POST "$API/instance-operations/launch" \
    -H "Content-Type: application/json" \
    -d "{\"region_name\":\"$REGION\",\"instance_type_name\":\"$TYPE\",
         \"ssh_key_names\":[\"$SSHKEY\"],\"file_system_names\":[\"$FS\"],
         \"quantity\":1}" | python3 -m json.tool
  echo "launched - poll 'rh_lambda.sh status' until status=active, then 'sync'"
  ;;
ip)
  api "$API/instances" | python3 -c '
import sys, json
d = json.load(sys.stdin).get("data", [])
print(d[0]["ip"] if d and d[0].get("ip") else "")
'
  ;;
sync)
  IP="$("$0" ip)"
  [ -n "$IP" ] || { echo "no running instance to sync" >&2; exit 1; }
  # the IP rotates on every relaunch, so host.env is rewritten not appended
  python3 - "$HOSTENV" "$IP" <<'PY'
import sys
path, ip = sys.argv[1], sys.argv[2]
lines = open(path).read().splitlines()
out = ["RH3D_HOST=%s" % ip if l.startswith("RH3D_HOST=") else l for l in lines]
open(path, "w").write("\n".join(out) + "\n")
print("RH3D_HOST=%s" % ip)
PY
  ;;
terminate)
  # One id, always. See the shared-account note at the top of this file.
  ID="${2:-}"
  if [ -z "$ID" ]; then
    echo "refusing to terminate without an instance id." >&2
    echo "  this Lambda account is SHARED - other projects' boxes are in here too." >&2
    echo "  usage: rh_lambda.sh terminate <instance-id>   (see 'rh_lambda.sh status')" >&2
    exit 2
  fi
  api "$API/instances" | ID="$ID" python3 -c '
import os, sys, json
want = os.environ["ID"]
run = {i["id"]: i for i in json.load(sys.stdin).get("data", [])}
if want not in run:
    sys.exit("no running instance with id %s" % want)
i = run[want]
sys.stderr.write("terminating %s  %s  %s\n" % (
    want, i.get("instance_type", {}).get("name"), i.get("region", {}).get("name")))
' || exit 1
  api -X POST "$API/instance-operations/terminate" \
    -H "Content-Type: application/json" \
    -d "{\"instance_ids\":[\"$ID\"]}" | python3 -m json.tool
  ;;
*)
  echo "usage: rh_lambda.sh {status|launch|ip|sync|terminate <id>}" >&2; exit 1 ;;
esac
