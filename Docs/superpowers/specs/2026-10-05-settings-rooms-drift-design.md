# CitixChase settings, discoverable rooms and responsive drift

Status: approved for implementation. User narrowed multiplayer to LAN for now; EOS, internet room codes and cross-internet testing are deferred.

## User outcome and preservation boundaries

Players should adjust performance in ESC, connect to a friend without knowing an IP address, and drift through tight corners with less understeer. Keep the existing two-player capacity, role assignment/swap, city identity verification, ready/rematch flow, chase rules, abilities, vehicle speed limits and city generation. Refine the entire flow after implementation, with at most three major product review cycles.

## ESC settings

Keep the existing graphics presets and automatic hardware recommendation. Add two labeled sliders with a live value beside each:

- **Resolution scale:** 30%–110%, with 1% steps and a visible 100% native marker. Scale the 3D scene while keeping UI at native resolution. First launch uses the existing hardware benchmark recommendation (70%, 85% or 100% according to the detected preset). Preserve a player's manual scale across restart, preset changes and map travel. “Use Automatic” explicitly restores the recommended graphics preset and resolution scale.
- **Maximum FPS:** six equally spaced detents: 30, 60, 120, 144, 240, Unlimited. Dragging, mouse clicks and keyboard input snap to these values. Default remains Unlimited, matching current behavior; FPS is saved independently and Automatic does not reset a chosen cap.

Changes apply immediately; save when a slider interaction completes, rather than repeatedly writing configuration during every drag frame. Support keyboard/gamepad focus and restore focus to gameplay on Resume/ESC. Keep the card readable at 720p with scrolling or adaptive height if necessary. Show a short explanation that resolution scale affects clarity/performance and that multiplayer continues while settings are open.

UE 5.8's standard scalability API clamps scale at 100%. Retain 30%–110% in Citix's own saved setting and explicitly apply the requested screen percentage after scalability changes. Verify the runtime value at 30%, 100% and 110%, rather than accepting a slider that silently caps at 100%.

## Multiplayer architecture

Recommended approach: one `UGameInstanceSubsystem` owns connection state, Unreal Online Subsystem session delegates, search results, authentication, session lifecycle and travel. Use the **NULL subsystem for LAN** and **EOS sessions backed by lobbies for internet rooms**. Enable the bundled engine plugins and modules; add no custom matchmaking server.

Alternatives considered: EOS-only discovery makes nearby play dependent on an online product/login; a custom discovery/code backend duplicates session infrastructure and adds deployment work. Explicit providers in one subsystem match the requested LAN-first delivery and allow nearby play without EOS credentials.

Connection operations must survive widget recreation and world travel. Bind network/travel failure handlers once in the subsystem, clear delegates on completion/deinitialization, and protect against late callbacks after cancellation. Allow one connection operation at a time. Destroy/leave previous sessions before switching rooms. Cancel pending travel and clean up any partially created/joined session before returning to idle. Timers and status cannot live only in the lobby widget.

Use the UE 5.8 `SocketSubsystemEOS.NetDriverEOS`, with IP fallback. LAN host travel explicitly requests IP sockets (`bIsLanMatch`); online host travel uses EOS P2P after authentication. Manual IP connects remain IP connections. The installed engine marks `NetDriverEOSBase` and the old `bIsUsingP2PSockets` configuration as deprecated, so use the current implementation rather than copying older configuration examples.

## Lobby experience

The opening card presents two clear sections:

**Nearby / same network**

- **Host Game:** editable room name, defaulting to a friendly local name such as “Yien's Room”; create and advertise a two-player LAN session, then enter the existing waiting/ready lobby.
- **Find Nearby Games:** show Searching with Cancel, then a scrollable list of named rooms such as “Yien's Room · 1/2 players.” Each row has Join; a full room visibly shows 2/2 and cannot be joined. Refresh/retry remains available. Empty results explain that both players need the same local network.

**Internet / play with a friend**

- **Create Room:** sign in through Epic's account portal when needed, create a two-player EOS lobby, and display a short uppercase share code with Copy and a clear waiting state.
- **Join with Code:** accept a normalized code such as ICE-482, search the EOS lobby attribute for it, validate capacity/version, join the lobby and travel using the resolved EOS address. Do not display an IP address in this flow.

Room codes use three letters and three digits, with case-insensitive input and optional hyphen. Use the larger space of random letter prefixes rather than a fixed “ICE” prefix. Check for existing codes before advertising and regenerate collisions; if a join search returns multiple matches, fail clearly instead of joining an arbitrary room. Codes are temporary room identifiers, not passwords. Lobby expiry/host departure makes a code unusable.

Keep **Advanced** collapsed by default; it contains the existing manual IP/port entry and Connect action. Preserve development command-line/manual-connect probes without exposing them in normal player flows.

Connected players see room name, online code when applicable, two driver slots, current ready states, and the existing Ready button. Host updates session occupancy when players arrive/leave. Capacity is also enforced by server admission, covering races where two friends click Join simultaneously. Preserve the existing match and rematch behavior; reject joining a match already in progress with a clear message.

## Status, recovery and compatibility

Use explicit states: Idle, Signing in, Creating, Searching, Joining, Connecting, In room, Leaving and Error. Display plain messages for no rooms found, invalid/expired code, room full, match in progress, version mismatch, failed sign-in, failed connection and host departure. Retry repeats the relevant action; Cancel returns safely to the prior idle screen. Disable duplicate submissions while working.

Advertise room name, game identifier, protocol/build version, capacity and room code as appropriate. Show incompatible nearby rooms as “Different version” rather than silently hiding every mismatch where the provider permits it. Validate the protocol again on server admission; map engine network-version failures to the same readable message. The session/room membership must not start the round: city verification and both existing Ready actions still gate play.

If EOS product configuration is absent, LAN and Advanced remain usable. Internet actions explain that online play is not configured; do not pretend to create a working code or silently substitute IP networking.

## Drift response

Apply changes to both chase roles' drift behavior while preserving their existing normal steering, engine output, speed limits, braking and boost rules. Increase steering response while handbrake is held, add bounded yaw assistance in the requested turn direction, and increase controlled planar momentum reduction/alignment so the car follows its rotated nose through a corner instead of continuing straight.

Only assist while grounded, moving at a useful speed and actively turning with the handbrake. Scale forces by mass/time, avoid airborne rotation assistance, preserve vertical suspension/collision response, and do not create additional speed. Blend the response to avoid entry/release snaps. Restore the ordinary steering/stability behavior as the drift ends and prevent runaway spins. Use the same movement path for the authoritative car and owning client's simulation.

## Delivery and acceptance evidence

Deliver LAN first, then EOS online integration, plus ESC settings and drift refinement. Package a separate new Windows build and preserve the current Ice Wave package.

1. Automated coverage: slider mapping/clamps and saved settings, operation cancellation/stale callbacks, room/code validation, session metadata/version checks and two-player admission.
2. Runtime checks: actual render percentage and FPS limit at all relevant detents; restart/preset/travel persistence; settings interaction screenshots at 720p and 1080p.
3. LAN: packaged host/find/join without typing an IP, correct 1/2 → 2/2 occupancy, two Ready actions and full gameplay/rematch regression. Test full, incompatible, cancelled and failed connections and retry cleanup.
4. Online: two separate Epic accounts and internet connections; create/copy/join code, resolved EOS P2P transport (not localhost/IP substitution), roles/ready/gameplay, host departure, expired code and connection failure. Preserve logs without authentication tokens.
5. Drift: compare identical initial speed/turn input before and after tuning; demonstrate quicker heading rotation and less distance along the old forward direction, stable release/countersteer, left/right symmetry and no speed gain. Exercise low/high speeds and different frame rates. Visually review a tight-corner drive with both roles.
6. Product self-review: score connection simplicity, readable feedback, performance controls, drift feel and preservation of gameplay; fix the three highest-impact weaknesses and review again before delivery.

Two local processes are useful regression evidence but do not satisfy the user's required two-computer LAN test or different-internet online test. Final completion depends on access to those test environments and a configured EOS product/client policy/deployment. User authentication/consent remains in Epic's UI; never request Epic passwords in chat or fabricate EOS identifiers.

## Dependencies and current findings

- The existing lobby connects by direct ServerTravel/ClientTravel; no session subsystem exists.
- Graphics preset application currently overwrites resolution scale and resets FPS to unlimited.
- Chase drift already has steering/grip tuning, but lacks a dedicated bounded yaw/momentum response for tight corners.
- No EOS artifact/product configuration was found in this project's configuration.
- Only the current Windows host is available through the connected tools. A second computer and independent internet path have not been established.

References: [Epic OSS session interface](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-session-interface-in-unreal-engine), [Epic EOS plugin configuration and login](https://dev.epicgames.com/documentation/unreal-engine/online-subsystem-eos-plugin-in-unreal-engine). API/driver details above were also checked against the installed UE 5.8 source.
