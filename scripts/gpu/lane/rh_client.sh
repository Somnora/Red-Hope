#!/usr/bin/env bash
# Red Hope generation-server client (run on the laptop). Opens an SSH tunnel to the
# A100, fires a request, saves the result. Creds come from ~/.config/rh3d/host.env
# (never hardcoded). The API token lives on the box at $RH3D_NS/.api_token.
#
#   rh_client.sh reconstruct   <sprite.png> <out.glb>
#   rh_client.sh style-lock    <sprite.png> <out.png> "<subject>"
#   rh_client.sh sprite-to-mesh <sprite.png> <out.glb> "<subject>"
#   rh_client.sh health
set -euo pipefail
source ~/.config/rh3d/host.env
SSH="ssh -i $RH3D_SSH_KEY -o ConnectTimeout=15 $RH3D_USER@$RH3D_HOST"
LPORT=18700

verb=${1:?usage: reconstruct|style-lock|sprite-to-mesh|health}
TOKEN=$($SSH "cat $RH3D_NS/.api_token")

# tunnel up (auto-closed on exit)
$SSH -f -N -L ${LPORT}:127.0.0.1:8700
trap 'pkill -f "${LPORT}:127.0.0.1:8700" 2>/dev/null || true' EXIT
sleep 1
BASE="http://127.0.0.1:${LPORT}"

case "$verb" in
  health)
    curl -s "$BASE/health"; echo ;;
  reconstruct)
    curl -s -H "Authorization: Bearer $TOKEN" -F "file=@${2}" -F "tris=${4:-18000}" \
      -o "${3}" "$BASE/reconstruct" && echo "saved ${3}" ;;
  style-lock)
    curl -s -H "Authorization: Bearer $TOKEN" -F "file=@${2}" -F "subject=${4:-the subject}" \
      -o "${3}" "$BASE/style-lock" && echo "saved ${3}" ;;
  sprite-to-mesh)
    curl -s -H "Authorization: Bearer $TOKEN" -F "file=@${2}" -F "subject=${4:-the subject}" \
      -o "${3}" "$BASE/sprite-to-mesh" && echo "saved ${3}" ;;
  *) echo "unknown verb: $verb"; exit 1 ;;
esac
