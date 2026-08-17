#!/usr/bin/env bash
# Completes the TRELLIS.2 env after bootstrap_trellis2.sh reports INCOMPLETE.
# Three gaps found 2026-08-16 on the first fresh-box rebuild:
#  - plyfile   : o_voxel imports it, setup.sh --basic does not pull it
#  - utils3d   : pinned 0.1.1 in the working freeze, likewise not pulled
#  - nvdiffrast: the "known-good copy" saved at wheels/ has ZERO .so files. The
#    compiled extension is _nvdiffrast_c, a SIBLING top-level module in
#    site-packages, not a child of the nvdiffrast/ package dir - so copying the
#    package directory could never have carried it. ops.py imports it at module
#    load, so the copy fails immediately. It has to be BUILT.
#    Built LAST on purpose: trap 4 is that a later nameless package silently
#    uninstalls it, and installing it after everything else removes the ordering.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/TRELLIS.2
SITE=$HOME/.local/lib/python3.10/site-packages
export PIP_USER=1
export PATH=$HOME/.local/bin:$PATH

echo "=== deps o_voxel/trellis2 actually need ==="
/usr/bin/python3 -m pip install -q plyfile "utils3d==0.1.1" || echo "  pip rc=$?"

echo "=== nvdiffrast: BUILD (the saved copy has no compiled extension) ==="
cd "$REPO" && bash setup.sh --nvdiffrast 2>&1 | tail -4

echo "=== preserve a COMPLETE copy for the next fresh box ==="
rm -rf "$NS/wheels/nvdiffrast" "$NS/wheels/nvdiffrast-"*.dist-info
cp -r "$SITE/nvdiffrast" "$NS/wheels/" 2>/dev/null
cp -r "$SITE"/nvdiffrast-*.dist-info "$NS/wheels/" 2>/dev/null
cp "$SITE"/_nvdiffrast_c*.so "$NS/wheels/" 2>/dev/null && echo "  saved _nvdiffrast_c .so alongside" || echo "  NOTE: no _nvdiffrast_c*.so found in site-packages"
echo "  wheels now: $(ls $NS/wheels | tr '\n' ' ')"

echo "=== VERIFY IMPORTS ==="
cd "$REPO" && PYTHONPATH="$REPO" /usr/bin/python3 - <<'PY'
import importlib
ok=True
for m in ["torch","nvdiffrast.torch","o_voxel","trellis2","flash_attn","utils3d","scipy","sklearn","plyfile"]:
    try: importlib.import_module(m); print("  OK   %s"%m)
    except Exception as e: ok=False; print("  FAIL %s -> %s"%(m,str(e)[:120]))
print("TRELLIS2_ENV_READY" if ok else "TRELLIS2_ENV_INCOMPLETE")
PY
