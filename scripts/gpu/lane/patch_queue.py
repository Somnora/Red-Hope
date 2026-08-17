p='/lambda/nfs/red-hope-east/red_hope/scripts/rh_queue.sh'
s=open(p).read()
old='    "$BL" --background --python "$NS/scripts/mesh_cleanup.py"   -- "${job}out/${base}_textured.glb" "${job}out/${base}_game.glb" 18000 2>&1 | grep -a "\\[cleanup\\]"'
new='    "$BL" --background --python "$NS/scripts/mesh_cleanup.py"   -- "${job}out/${base}_textured.glb" "${job}out/${base}_game.glb" "$TRIS" "$STRIP" 2>&1 | grep -a "\\[cleanup\\]"'
assert old in s, 'cleanup line not found'
s=s.replace(old,new)
anchor='  mkdir -p "${job}out"'
inject=anchor+'\n  TRIS=18000; [ -f "${job}tris" ] && TRIS=$(tr -dc 0-9 < "${job}tris")\n  STRIP=0;    [ -f "${job}stripbase" ] && STRIP=1\n  log "budget: ${TRIS} tris, stripbase=${STRIP}"'
assert anchor in s, 'mkdir anchor not found'
s=s.replace(anchor,inject,1)
open(p,'w').write(s)
print('patched rh_queue.sh')
