# HUD, Low graphics, and Advanced IP follow-up

## Intended qualities and strict review

| Quality | Before | After | Evidence / remaining limit |
| --- | --- | --- | --- |
| Readable, coherent Smoke and Ice Wave cards | 6/10 | 9/10 | Native-resolution text, larger Smoke card and labels, consistent smooth solid blue charges. Inspected both 1920x1080 Shipping screenshots. |
| Low settings prioritize weak hardware | 6/10 | 8/10 | Recommended scale 50%, reduced distance and foliage, disabled expensive effects; actual runtime settings and higher-tier restoration verified. No FPS improvement measured on an extremely slow PC. |
| Direct hosting and joining are clear and reliable | 5/10 | 8/10 | Equal Host IP / Join IP controls, local-address validation, selected port reaches the real network driver; two local processes reached a match. Physical two-PC networking remains unverified. |
| Neutral default room identity | 6/10 | 9/10 | CitixRoom1 appears in the real lobby and session log, remains editable. |

## Highest-impact review fixes

1. Replace small faceted charge outlines with smooth filled blue available charges and subdued empty rings; increase label readability without scaling UI with scene resolution.
2. Fix manual hosting so the selected port propagates into actual map travel. The initial URL-only unit check missed the live travel rejection; the corrected implementation was verified against a real listening driver at 7804 despite initial command-line port 7802.
3. Remove redundant idle status text while Advanced is expanded. The two controls and ESC footer now fit without clipping.

The final Shipping screenshots show a coherent pair of ability cards and a readable Advanced panel. No further high-impact issue was identified within this follow-up scope; existing gameplay, roles, charge timings, and match rules are preserved.

## Fresh verification

- Editor build and Shipping package succeeded.
- All 14 Citix automation tests succeeded in HUDLowAdvanced-Verified.log, including manual host port/address validation, Low-to-Max restoration, settings, drift, Ice Wave, and round rules.
- Actual Slate Host IP and Join IP controls connected two Editor processes at 127.0.0.1:7804; host log reports CitixRoom1, 2/2 players, playing=1.
- Packaged control-flow test connected two Shipping instances at 127.0.0.1:7805, entered the match, and captured both ability HUDs with settings closed.
- Graphics runtime probes on both Editor instances passed; manual scale and FPS remain independent of presets.
- Evidence saved under Saved/Validation/HUDLowAdvanced. Local process tests do not establish real two-computer LAN behavior or performance on a low-end device.

Delivery: PackageLAN/Windows/Citix.exe. Copy the whole Windows folder together. PackageIceWave is preserved.
