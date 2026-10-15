# Third-party notices

Attributions required by the licences of the tools and models used to produce
Red Hope's art. See `asset-licence-audit.md` for the full assessment, including
two items that are **not** resolved by attribution.

**SHIPPED since 2026-08-17.** `Notices/NOTICES.txt` is staged into packaged
builds as loose files beside the executable, via
`+DirectoriesToAlwaysStageAsNonUFS=(Path="Notices")` in `Config/DefaultGame.ini`.
That satisfies DINOv3's "product documentation" wording and Tencent's
requirement that the notice accompany any distribution.

`scripts/unreal/rh_check_shipping_meta.py` verifies the staging entry, the
project metadata and the required verbatim strings, and exits non-zero if any
of it regresses. Run it before a release.

**This markdown file is now the source of record, not the deliverable.** Edit
`Notices/NOTICES.txt` alongside it or the two drift; the checker only guards
the strings it knows about.

Still owed: a *prominent* in-UI credit. DINOv3's wording asks for a website,
user interface or about page, and a text file beside the binary is the minimum
defensible reading. The project has a Slate UI (`SRHCommandDeck`, C++ only —
there is no UMG anywhere), so an About panel is achievable but needs a director
compile. Not written speculatively, because an uncompilable change costs the
director's time rather than mine.

---

## Built with DINOv3

Required by the DINOv3 License (Meta), which asks that "Built with DINOv3" be
displayed prominently on a related website, user interface, blogpost, about page
or product documentation.

> Built with DINOv3

## Tencent Hunyuan 3D 2.1

Required with any distribution of works produced with it:

> Tencent Hunyuan 3D 2.1 is licensed under the Tencent Hunyuan 3D 2.1 Community
> License Agreement, Copyright © 2025 Tencent.

## Microsoft TRELLIS.2-4B

MIT licensed. Retain the copyright and permission notice:

> Copyright (c) Microsoft Corporation. Released under the MIT License.

## Real-ESRGAN

BSD-3-Clause. Retain the copyright notice, condition list and disclaimer:

> Copyright (c) 2021, Xintao Wang. All rights reserved. BSD 3-Clause License.

## rembg

MIT licensed (Daniel Gatis). The background-removal **model weights**
(u2net / isnet-general-use) are licensed separately from rembg's code and are
still to be confirmed — see the audit.

## Stable Diffusion XL 1.0 + IP-Adapter

CreativeML Open RAIL++-M. The licence carries behavioural use restrictions that
must be passed downstream to any recipient of the model or its derivatives.

## MakeHuman assets via MPFB 2

The crew walkers (`scripts/blender/rh_crew_build.py`, 2026-10) are built from
MakeHuman's base mesh, proxies, skins, clothes, hair and eyebrows as bundled
with MPFB 2 (v2.0.17). Each of those asset files carries the header "This asset
was explicitly released as CC0 in september 2020" (copyright holders at the time
of release: Data Collection AB, Joel Palmius, Jonas Hauquier). CC0 needs no
attribution; this entry is informational, as a courtesy credit:

> Character base meshes and clothing from MakeHuman (makehumancommunity.org), CC0.

MPFB itself is GPL-3.0-or-later and was used as a TOOL, like Blender below; none
of its code ships. The `male_casualsuit01` diffuse texture carries a
"www.makehuman.org" mark in its top-left corner; no face of the garment samples
that corner, so the mark never reaches a baked crew atlas.

## Kenney sound packs

Part of the game's sound (UI clicks and confirmations, the elevator doors, the
command module's chatter, the lander's idle engine, the borer's mining hits,
the crew lander's touchdown) comes from Kenney's Sci-Fi Sounds, Interface
Sounds and Impact Sounds packs (kenney.nl, downloaded 2026-10-14), released
under CC0. No attribution is required; this is a courtesy credit:

> Sound effects by Kenney (www.kenney.nl), CC0.

The rest of the soundscape (ambience beds, machine loops, chime and stingers)
is synthesised in `scripts/audio/rh_sfx_build.py`.

## Music generated with Google Lyria

The six soundtrack pieces (`/Game/RedHope/Audio/Music`, two per phase) were
generated on 2026-10-15 with Google's Lyria 3 Pro model on Vertex AI
(project somnora-dev-01), from the prompts in
`scripts/audio/rh_music_prompts.json`. This is a provenance note, not a
licence requirement. Lyria output carries Google's inaudible SynthID
watermark.

## Blender

Blender is GPL. It is used as a tool; the GPL binds Blender itself, not artwork
produced with it. No notice is required for the artwork, and none is claimed here.
