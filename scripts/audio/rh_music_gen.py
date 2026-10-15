"""rh_music_gen - generate one instrumental track with Google Lyria on Vertex AI.

  python3 scripts/audio/rh_music_gen.py <model> <out_path_without_ext> "<prompt>"

Project somnora-dev-01. Auth is gcloud application-default credentials (run
`gcloud auth application-default login` when they expire); no key is stored
here. lyria-3-pro-preview (the v1beta1 global "interactions" endpoint) returns
a full ~3 min MP3; lyria-002 (:predict) returns 32.8 s WAV clips.

The soundtrack shipped 2026-10-15 is listed, prompt by prompt, in
scripts/audio/rh_music_prompts.json. Lyria may refuse a prompt with
"content_blocked" for an unspecified policy reason; reword and retry
(p3_sovereignty_b needed one retry).
Then level and import: ffmpeg loudnorm I=-20 with 1.5 s / 3 s fades, then
scripts/unreal/rh_import_audio.py as /Game/RedHope/Audio/Music/A_mus_p<phase>_<letter>.
"""
import base64, json, subprocess, sys, urllib.request
model, out, prompt = sys.argv[1], sys.argv[2], sys.argv[3]
tok = subprocess.check_output(["gcloud", "auth", "application-default", "print-access-token"], text=True).strip()
if model.startswith("lyria-3"):
    url = "https://aiplatform.googleapis.com/v1beta1/projects/somnora-dev-01/locations/global/interactions"
    body = {"model": model, "input": [{"type": "text", "text": prompt}]}
else:
    url = ("https://us-central1-aiplatform.googleapis.com/v1/projects/somnora-dev-01/locations/us-central1/"
           "publishers/google/models/%s:predict" % model)
    body = {"instances": [{"prompt": prompt, "negative_prompt": "vocals, singing, drums kit, distortion"}],
            "parameters": {"sample_count": 1}}
req = urllib.request.Request(url, data=json.dumps(body).encode(), method="POST",
                             headers={"Authorization": "Bearer " + tok, "Content-Type": "application/json"})
try:
    resp = json.load(urllib.request.urlopen(req, timeout=600))
except urllib.error.HTTPError as e:
    print("HTTP", e.code, e.read().decode()[:800]); sys.exit(1)
n = 0
for o in resp.get("outputs", []):
    if o.get("type") == "audio":
        ext = "mp3" if "mpeg" in o.get("mime_type", "") else "wav"
        open("%s.%s" % (out, ext), "wb").write(base64.b64decode(o["data"])); n += 1
    elif o.get("type") == "text":
        print("TEXT:", o.get("text", "")[:300])
for p in resp.get("predictions", []):
    open("%s.wav" % out, "wb").write(base64.b64decode(p["audioContent"])); n += 1
print("saved", n, "file(s); keys:", list(resp.keys()), "status:", resp.get("status"))
