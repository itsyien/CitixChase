# Automatic graphics and Esc menu — review

Intent: per-PC automatic graphics, cheap low-performance rendering, full quality on the development PC, a clean Esc menu, and saved Low/Medium/High/Max overrides.

Implementation: native Unreal hardware benchmark on first rendered launch; conservative CPU/GPU thresholds for the city, choosing the weaker tier. CPU thresholds 60/100/160; GPU thresholds 90/180/300. These are initial synthetic-index thresholds, not guaranteed FPS targets. The current PC measured CPU 241.9 and GPU 405.2 and selected Max. Manual presets save to the local GameUserSettings file; Automatic restores the benchmark recommendation. No physics, traffic population or multiplayer rules are changed by graphics settings.

Low/Medium render the scene at 70/85 percent, High/Max at native resolution. UI retains native resolution. Low removes expensive lighting/reflection/shadow detail but keeps basic temporal antialiasing to avoid road-edge shimmer. All profiles disable VSync and leave the frame-rate cap unlimited.

Esc opens a compact blue panel with four presets, the active/recommended tier, a short description, Automatic and Resume. The online match continues. Held keys are flushed, throttle/boost cleared and local movement/looking blocked until Resume. Menu widgets are removed on travel.

Review pass 1: request fidelity 8/10, hierarchy 8/10, local input handling 8/10, performance confidence 7/10. Highest-impact weaknesses were aliased Low scene edges, shifting buttons across descriptions, and menu lifetime during travel. Refined with basic antialiasing, fixed-height description spacing, and explicit travel cleanup. Re-reviewed at 720p and 1080p: legible, cohesive, selected tier explicit. No added features or gameplay redesign needed.

Evidence: native benchmark log `Saved/Logs/graphics-native.log`, passing native preset/persistence/Esc receipts and images in `Docs/evidence/graphics`; rules suite passed. Native controls and saved presets were checked by invoking the bound button callbacks and simulated Esc, rather than a human mouse playtest. Real low-spec hardware FPS and latency remain unmeasured; no minimum FPS claim is made.

Final Shipping BuildCookRun succeeded, exit 0, including DefaultScalability.ini. Fresh packaged Host/Join/Ready live-round tests passed on both players: four preset callbacks, saved preferences reloaded, Automatic restored, vehicle throttle blocked while menu open, Esc input restored, and round time advanced more than ten seconds. Receipts: packaged-client.json and packaged-host.json in Docs/evidence/graphics. Test harness sequencing was corrected to use controller-owned state after travel and to allow the live round to finish its assertions. Shipping SHA256: 908905FD9F9B97D27B8CAAC8DA44EA284909D6FFD5DD63F1B5A15316152B74A5. Original preservation check: 142 source/config files, zero changed.

