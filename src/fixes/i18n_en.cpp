// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
#include "fix.h"

#include <string.h>

struct FixEn {
	const char *section;
	const char *title;
	const char *gain;
	const char *summary;
	const char *details;
};

struct ParamEn {
	const char *section;
	const char *key;
	const char *label;
};

static const FixEn kFixEn[] = {
	{
		"AsiMemProtectCache",
		"VirtualProtect cache for ASI plugins",
		"-0.5-0.7% frame",
		"Caches memory protection unlocks in resamp++, FramerateVigilante, SAMPFUNCS, and crashes.asi.",
		"Several popular ASI plugins (including resamp++.asi and FramerateVigilante.SA.asi) call VirtualProtect every frame to unprotect and re-protect memory just to write 4 bytes. Each call transitions into the Windows kernel (ntdll!ZwProtectVirtualMemory) and flushes the TLB.\n\nLike LuaMemProtectCache, pages opened for writing on the main thread are cached for 2 seconds without repeated syscalls. Lossless.",
	},
	{
		"AudioSleepFix",
		"Script sounds off main thread",
		"no stutters",
		"Lua sounds (gunshots, hitsounds) start off the game thread and stop freezing the frame.",
		"When starting each sound, BASS waits on its mixer thread with Sleep(1) right on the game thread - at least one Windows timer tick. In gunfights, frame time jumps from 6.7 ms to 13 ms.\n\nShort BASS waits on the main thread switch immediately instead of sleeping a tick. Audio is identical.\n\nStarting a sound also waits for the BASS mixer thread (up to 10-15 ms if audio was idle). MoonLoader sound playback is offloaded to a worker thread so the script gets an instant return while the sound starts at the same time.",
	},
	{
		"LuaMemProtectCache",
		"Lua memory protection cache",
		"-1.5-2% frame",
		"MoonLoader stops calling VirtualQuery and VirtualProtect 3 times on every Lua memory write.",
		"When a Lua script calls memory.write(..., true) or writeMemory(..., true) in a loop, MoonLoader.asi makes 3 heavy Windows syscalls per write: VirtualQuery, VirtualProtect(RWX), and VirtualProtect(restore) with TLB flush (~2% of total frame time in scripted setups).\n\nPages opened for writing by MoonLoader on the main thread stay cached for 2 seconds instead of toggling hundreds of times per second. Lossless.",
	},
	{
		"SampfuncsDrawFast",
		"SAMPFUNCS draw bypass",
		"-4-5% frame",
		"GPU draw calls bypass SAMPFUNCS dispatch while none of its plugins are listening.",
		"SAMPFUNCS sits between the game and the GPU and checks all of its plugins on every model draw (thousands per frame) to see if any want to hook it. Usually none do, wasting CPU.\n\nEvery 250 ms the plugin checks which D3D9 functions actually have handlers. All others go straight to the GPU. Works with supported SAMPFUNCS versions.",
	},
	{
		"SilentPatchObjectFix",
		"Speed up SilentPatch CObject::Render",
		"-2% frame",
		"Removes 256 pointless constructor calls per drawn object inside SilentPatchSA.asi's hook.",
		"SilentPatchSA.asi hooks CObject::Render and on EVERY drawn object per frame runs a 256-iteration std::pair constructor loop to zero a 2 KB stack array - before checking if the array is even needed (for 99.9% of objects it is not).\n\nEven when used, elements are written sequentially from 0 to count and only those count entries are read. Removing this dead init loop saves ~2% frame time on mapped servers with 100% identical behavior.",
	},
	{
		"SkyboxLogOff",
		"RealSkybox log off",
		"no freezes",
		"RealSkybox stops writing debug logs to disk on the game thread, eliminating 20-25 ms freezes.",
		"This RealSkybox build continuously writes debug numbers to RealSkybox.SA.log (170,000+ lines) on the game thread, occasionally blocking for tens of milliseconds on disk I/O.\n\nFile logging inside RealSkybox is disabled. Sky rendering is untouched.",
	},
	{
		"AnimBlendSSE",
		"Ped animation blending on SSE2",
		"-1-1.5% frame",
		"Speeds up skeleton bone updates and blending in crowds (CAnimBlendNode::Update + FrameUpdateCallBackSkinned).",
		"In player crowds, GTA SA blends animation keyframes ~32 times per ped per frame using legacy x87 math with constant stack quaternion copies and fnstsw ax stalls.\n\nNode updates, Slerp, quaternion hemisphere alignment, and normalization are unified into clean SSE2 without redundant calls. Animations are 100% identical.",
	},
	{
		"AnimMathSSE",
		"Skeleton math",
		"-2% frame",
		"Fast polynomial sine for bone rotation without slow fsin, plus bone-by-index cache.",
		"For every bone of every ped, the game computes sine twice with legacy fsin (~100 cycles) and linearly scans ~32 bones by ID.\n\nSine uses an accurate polynomial (error below float precision) and bone lookups are cached and verified. Lossless.",
	},
	{
		"CMatrixSSE",
		"CMatrix & CrossProduct on SSE",
		"-3-9% frame",
		"CMatrix rotations, multiplication, inversion, and CrossProduct rewritten from x87 to SSE2.",
		"Alongside RenderWare (MatrixSSE), GTA SA itself constantly builds, multiplies, and inverts CMatrix and computes CrossProduct in collisions, vehicles, peds, and camera. Stock functions use scalar x87 and call fcos + fsin (~150 cycles) even for angle 0.\n\nHere 10 hot CMatrix/CVector functions are rewritten in SSE2 without stack stalls. Lossless.",
	},
	{
		"ColBoxSSE",
		"Collision spheres & boxes on SSE",
		"-2.5-3.5% frame",
		"Fast SSE2 sphere/box intersection checks (TestSphereBox and GetIsTouching) without x87 stalls.",
		"Before testing triangles, the game checks bounding spheres and boxes thousands of times per frame (GetIsTouching ~2% frame, TestSphereBox ~1.3%). Stock code uses scalar x87 with 6 consecutive fnstsw ax stalls, double model-table reads, and extra TransformFromObjectSpace calls.\n\nTestSphereBox runs in 6 branchless SSE2 vector ops, and GetIsTouching computes center and squared distance directly in XMM registers. Identical physics.",
	},
	{
		"ColLinesSSE",
		"Camera line collision on SSE2",
		"-1% frame",
		"Rewrites CCollision::ClosestSquaredDistanceBetweenFiniteLines (0x415A40) from scalar x87 to SSE2.",
		"When checking camera-vs-wall collisions (SphereCastVersusVsPoly), GTA SA computes segment distances thousands of times per frame in scalar x87 with 12 fnstsw ax stalls (~1% frame even in empty interiors).\n\nEricson's exact formula is rewritten in vector SSE2 with zero FPU stalls. 100% identical result.",
	},
	{
		"ColPlaneCache",
		"Collision plane cache",
		"-1.5% frame",
		"Collision triangle planes are computed once instead of recalculated every frame.",
		"GTA keeps precomputed collision triangle planes for only 50 models. In crowds on large maps, the cache thrashes and planes are recomputed over and over.\n\nCache size is expanded to `size` models. Planes depend only on model geometry, so results are identical. Lossless, uses a few MB of RAM.",
	},
	{
		"ColTriSSE",
		"Triangle collisions on SSE",
		"-3-4% frame",
		"ProcessLineTriangle, TestSphereTriangle, and ProcessSphereTriangle rewritten in SSE2 without x87 or extra sqrt.",
		"During ray and sphere collision checks, the game unpacks int16 coordinates per triangle via the stack (fild store-forwarding stalls on every number), checks distance only at the very end of ProcessLineTriangle, and repeatedly calls CrossProduct and fsqrt.\n\nPlanes unpack with a single SSE2 instruction, distant hits are rejected before reading vertices, and sphere-triangle tests use exact squared distance without extra sqrt. Identical physics.",
	},
	{
		"CursorCenterFix",
		"Throttle cursor centering",
		"-0.9% frame",
		"Stops calling SetCursorPos 600+ times per second to the exact same screen coordinates.",
		"GTA SA reads mouse input via DirectInput, yet still calls SetCursorPos to screen center every frame so the hidden OS cursor stays inside the window. At high FPS this causes hundreds of kernel transitions (win32u!NtUserSetCursorPos) per second (~0.9% frame).\n\nRepeated calls to the same coordinates are throttled to once per interval_ms (default 8 ms = 125 Hz). Zero impact on sensitivity, aim, or input lag.",
	},
	{
		"DrawDistance",
		"World draw distance",
		"up to -10% frame",
		"World draw distance below the game's built-in minimum. Changes visual range.",
		"Each building and object renders up to its draw distance multiplied by the game's draw distance setting (92% to 180% stock). Here it can be set directly, including below the stock minimum.\n\nLower percentage means detailed buildings and mapping appear closer (with LODs further out), reducing draw calls and visibility checks. Updates live without restart; overrides the in-game slider while enabled.",
	},
	{
		"EnvMapCache",
		"Vehicle reflection cache",
		"-0.5-2% frame",
		"Skips walking every vehicle part and material when its reflection intensity hasn't changed.",
		"Every frame in CVehicle::PreRender, the game converts vehicle reflection intensity to an integer and walks all body parts via RpClumpForAllAtomics - even when parked with the exact same value as last frame. With 5 cars that's ~0.5% frame; in a 30-car lot, several percent.\n\nThe last value is cached per model pointer (refreshed once per second). Identical visuals.",
	},
	{
		"FastAtan2SSE",
		"Fast GetATanOfXY angles on SSE2",
		"-0.6% frame",
		"Replaces slow x87 fpatan in CGeneral::GetATanOfXY (0x53CC70) with a fast SSE2 polynomial.",
		"GTA SA calls CGeneral::GetATanOfXY hundreds of times per frame (camera, radar, peds, shadows). Stock code hits 6 fnstsw ax stalls and legacy x87 fpatan (~140 cycles).\n\nThe SSE2 polynomial computes it in ~14 cycles (~10x faster) with accuracy better than 0.0001 deg over [0, 2*pi).",
	},
	{
		"FastStricmpSSE",
		"Fast ___ascii_stricmp",
		"-0.5% frame",
		"Speeds up texture, model, bone, and HUD key string comparisons without per-char overhead.",
		"GTA SA's 2003-era ___ascii_stricmp executes ~18 instructions per character and is called constantly when looking up textures, bones, fonts, and HUD keys (~0.57% frame).\n\nAn exact-byte fast path plus a lowercase lookup table gives 100% identical results 3-4x faster.",
	},
	{
		"FloorSSE4",
		"floor/ceil in one instruction",
		"-0.4% frame",
		"Uses SSE4.1 roundsd for floor() and ceil() instead of slow 2004 CRT routines.",
		"Built-in floor() and ceil() in gta_sa.exe read FPU/SSE control words (stmxcsr + fnstcw, flushing the pipeline) on every call and manually shift mantissa bits in ~40 instructions across rendering, collisions, streaming, and reflections.\n\nSSE4.1 roundsd (all CPUs since 2008) produces the exact bitwise IEEE 754 result in 1 cycle. Lossless.",
	},
	{
		"FrameSyncFast",
		"Fast bone hierarchy (FrameSync)",
		"-0.7-1.2% frame",
		"Iterative ped/vehicle skeleton traversal with inline SSE2 matrix math and parent LTM cache.",
		"When updating ped poses and vehicle parts, RenderWare recursively walks the RwFrame tree (_FrameSyncHierarchyRecurse), making empty recursive calls even for leaf bones and indirect RwMatrixMultiply calls per bone.\n\nRecursion is replaced by an iterative walk with memory prefetching, inline SSE2 multiplication, and cached parent LTM across sibling nodes. Order and result are 100% identical.",
	},
	{
		"FrustumSSE",
		"Visibility & transforms on SSE",
		"-1.2% frame",
		"Camera frustum checks (IsSphereVisible) and TransformPoint rewritten from x87 to SSE2.",
		"While scanning world sectors (ScanSectorList / SetupEntityVisibility), the game checks sphere visibility thousands of times per frame via stack copies, indirect RwV3dTransformPoints, and 6 x87 fcomp + fnstsw ax stalls. TransformPoint computes both x87 fcos and fsin (~150 cycles) per object even when angle is 0.\n\nTransform and all 4 frustum planes run in one 4-wide SSE2 op, and angle-0 TransformPoint uses 3 adds with fast SSE2 sin/cos otherwise. Lossless.",
	},
	{
		"FtolSSE3",
		"(int)float in one instruction",
		"-0.5% frame",
		"Float-to-int casts use SSE3 fisttp instead of the slow 2004 compiler helper.",
		"Every (int)float cast in GTA SA calls a helper that stores the float, rounds, reloads, and fixes rounding manually (~dozens of cycles) across the entire engine.\n\nSSE3 fisttp (all CPUs since 2005) does the exact same conversion in one instruction. Identical result.",
	},
	{
		"FxParticlesSSE",
		"Fx_c particle math on SSE2",
		"-1-3.5% frame",
		"Speeds up particle rotation and keyframe interpolation (smoke, exhaust, sparks, fire) on SSE2.",
		"During tire smoke, exhaust, and gunfights, GTA SA spends up to 4-5% frame time in scalar x87 inside Fx_c::RotateVecAboutVec and FxInterpInfo::GetVal (constant fild and fnstsw ax stalls per keyframe).\n\nRodrigues rotation and keyframe lookup are moved to SSE2. Visuals are 100% identical.",
	},
	{
		"MatrixSSE",
		"RenderWare matrices on SSE",
		"-2.5% frame",
		"Multiplies RenderWare matrices 4 floats at a time instead of legacy scalar x87.",
		"2004 RenderWare multiplies matrices one float at a time on x87 for every bone of every ped and every vehicle part - tens of thousands of times per second.\n\nUses the exact same formula (same addition order, same 12 floats) in SSE via RenderWare's own function table.",
	},
	{
		"MessagePumpFix",
		"Smart Windows message pump",
		"-6-9% frame",
		"Avoids hitting the Windows kernel (PeekMessageA + DINPUT8/driver hooks) every frame when no keys are pressed.",
		"GTA SA's main loop calls PeekMessageA on every frame (700-1000+ times/s). At high FPS, the kernel transition (win32u!NtUserPeekMessage) and DirectInput/NVIDIA hook chains consume up to 10-11% of the entire main thread.\n\nBefore entering the kernel, GetInputState() is checked (~5 cycles in usermode): any key or mouse click wakes the pump immediately on that exact frame (0 ms latency). Mouse aim uses DirectInput and doesn't depend on PeekMessage. Otherwise the queue is polled once per interval_ms (default 3 ms = 333 Hz).",
	},
	{
		"ModelNameLookup",
		"Model lookup by name",
		"no 20-30 ms freezes",
		"Looks up models by name via a hash table instead of scanning all 20,000 slots.",
		"To find a model by name, the game linearly scans all 20,000 model slots comparing name hashes. Loading a collision file with custom mapping repeats this for every model - a few hundred models means a 20-30 ms freeze.\n\nNames are now resolved in O(1) via a table and verified against the slot. Identical result.",
	},
	{
		"MsgKeysFast",
		"Skip empty ~k~ HUD key substitution",
		"-0.4% frame",
		"Skips zeroing 800 stack bytes in InsertPlayerControlKeysInString when no ~k~ token exists.",
		"Before drawing HUD strings, GTA SA calls CMessages::InsertPlayerControlKeysInString (0x69E160), which zeroes 400 bytes, copies the string twice, and zeroes the tail every time - even when there are no ~k~ key tokens (99.9% of SA-MP strings).\n\nA fast scan for ~k~ returns immediately when absent; otherwise calls the original unchanged.",
	},
	{
		"OcclusionOff",
		"Disable 2004 CPU software occlusion",
		"-5-19% frame in city",
		"Disables heavy CPU software occluders (COcclusion) that eat up to 19% frame time downtown.",
		"In 2004 for PlayStation 2, GTA SA added CPU software occlusion: every frame it rotates and projects dozens of invisible building occluder boxes (ProcessOneOccluder, ProcessLineSegment, CalcScreenCoors, SetRotateX/Y/Z) to spare PS2's weak GPU.\n\nDowntown (LS/SF/LV), this CPU calculation eats 15-20% of total frame time - far more than a modern GPU saves via hardware Z-buffer. NumActiveOccluders is zeroed (just like interiors do). Visuals are identical without flicker.",
	},
	{
		"PedDrawDistance",
		"Player draw distance",
		"distant crowds",
		"Remote players beyond max_m from the camera are not rendered. Nametags and sync stay intact.",
		"GTA renders any streamed player at any distance the server sends (200-300 m), even though beyond ~150 m they are a couple of pixels while still costing dozens of draw calls and skinning.\n\nPlayers beyond max_m skip rendering (along with their weapon). Nametags, sync, hitreg, and your own ped are untouched.",
	},
	{
		"RadarBatch",
		"Batched radar rendering",
		"-1% frame",
		"Radar player blips, icons, dots, and gang zones draw in batches instead of hundreds of single calls.",
		"Every radar blip (player, marker, checkpoint + black outline), icon, and gang zone issues its own GPU draw call through RenderWare, SAMPFUNCS, and DXVK. On busy servers that's 150-200 draw calls per frame just for the radar.\n\nConsecutive shapes now batch into a single draw call: blip outlines and fills, matching icons, gang zones. Draw order and colors remain identical.",
	},
	{
		"RefPool",
		"Reference pool expansion",
		"up to -30% frame",
		"Prevents the object reference pool from overflowing and scanning all peds/vehicles on every new ref.",
		"Whenever a game entity tracks another (AI target, neighbor, event), GTA stores a reference in a 3,000-slot pool. In crowds it fills up, and every new reference triggers a full scan of all peds, vehicles, and objects that finds nothing to free.\n\nStale refs are pruned incrementally each frame, and new blocks are allocated when full. Lossless; references are no longer dropped.",
	},
	{
		"RemotePedPhysics",
		"Kinematic remote player physics",
		"-10% frame",
		"Distant remote players move via server sync without full map collision physics.",
		"GTA treats every remote player as a local ped: every frame their bounding spheres are tested against map triangles, even while standing AFK, despite the server dictating their position.\n\nBeyond min_distance meters, remote peds disable map collision and gravity and move purely by packet velocity (kinematic). Your bullets, vehicles, hits, and your own ped are untouched. Full physics applies nearby.",
	},
	{
		"RemoteInAir",
		"Remote player IsInAir cache",
		"-4.5% frame",
		"Stops casting a downward ray under stationary remote players every frame.",
		"GTA's AI event scanner casts a vertical ray through the map for every ped every frame to check if they should enter a fall animation. 100 players = 100 rays per frame.\n\nFor remote peds, the result is cached while the ped stands still for up to max_age_ms. Moving peds and your own ped are checked normally.",
	},
	{
		"SearchLightCull",
		"Cull off-screen searchlights",
		"up to -1.5% frame",
		"Area 69 searchlight cones are not built when outside the camera frustum.",
		"Area 69's main.scm has 7 tower searchlights, and the game builds every cone every frame (41 steps with trig, sqrt, and normalization, ~8 us) regardless of where you are on the map.\n\nCones are now built only when visible in the camera view. Light flares register as before so turning the camera looks identical.",
	},
	{
		"SkinMatrixSSE",
		"Ped skinning matrices on SSE2",
		"-0.5-1% frame",
		"Speeds up computing and transposing 32 bone skinning matrices per visible ped.",
		"Before drawing each visible ped, GTA SA multiplies ~32 bone matrices (_rpD3D9SkinVertexShaderMatrixUpdate, up to 64 indirect RwMatrixMultiply calls per ped) and transposes 384 floats one-by-one into shader constants.\n\nMultiplication is inlined in SSE2 keeping the inverse matrix in XMM registers, and transposition uses _MM_TRANSPOSE4_PS. 100% identical visuals.",
	},
	{
		"StreamBudget",
		"Streaming unpack budget",
		"no 10-15 ms hitches",
		"Models read from disk are unpacked up to budget_kb per frame instead of all at once.",
		"The disk reader loads up to 16 models per batch, and the game unpacks all of them in a single frame (textures, geometry, collision): 1.5-4 MB at once = 10-15 ms hitches while driving or flying.\n\nUnpacking is capped at budget_kb per frame, carrying the rest cleanly to following frames in order. Urgent loads (teleports, spawns) run as before.",
	},
	{
		"StreamMemBoost",
		"Extended streaming RAM pool",
		"no map stutters",
		"Gives GTA SA streaming up to 3.5 GB RAM (default 2 GB) instead of 13-50 MB so map and mods stay cached.",
		"By default GTA SA (and stream.ini) caps streaming memory at 13-50 MB - a 2004 limit. Driving around forces the game to constantly evict buildings, cars, and textures only to re-read them from disk, unpack on CPU, and re-upload to GPU seconds later.\n\nConfigurable from 128 to 3584 MB (~3.5 GB is the safe ceiling for 32-bit LAA processes, leaving ~500 MB for game, DXVK, and plugins; 4096 MB overflows 32-bit to 0). Without LAA (2 GB exe), it safely caps at 1400 MB.",
	},
	{
		"VehicleLod",
		"Distant vehicle LODs",
		"up to -10% frame",
		"Vehicles beyond lod_m use their built-in low-detail model: 1-2 draw calls instead of ~55.",
		"A detailed vehicle takes ~55 draw calls (every part and material separately) and renders that way out to ~100 m. 7 cars in view = ~14% frame.\n\nEvery stock car has a built-in LOD model normally used only past 100 m. Now it activates past lod_m (no interior/glass/small details at range). Mods without an LOD model render as before. Lower lod_m = more FPS.",
	},
	{
		"AccessoryFindCache",
		"Player accessory cache",
		"-0.7% frame",
		"Stops SA-MP from scanning every player's accessories for every streetlamp and fence each frame.",
		"For every map object drawn, SA-MP checks all 10 accessory slots on every player every frame to see if that object is attached to someone.\n\nResults are cached: found accessories are verified in place, and non-accessories stay cached for the same object up to 1 second.",
	},
	{
		"ColLoadedCache",
		"Player ground collision cache",
		"-0.3% frame",
		"Caches 'is collision loaded under player' for 250 ms per 2 m grid cell.",
		"Every frame for each nearby player, SA-MP asks if collision is loaded underneath them, forcing GTA to iterate through all map collision areas.\n\nThis only changes when streaming loads/unloads collision, so caching for 250 ms is safe.",
	},
	{
		"GangZoneCull",
		"Cull off-radar gang zones",
		"up to -0.5% frame",
		"Gang zones outside the radar area are skipped before radar projection math.",
		"Every frame SA-MP passes every server gang zone to the radar, and GTA transforms and circle-clips all corners even for zones kilometers away that aren't visible on the radar.\n\nZones whose bounding box doesn't overlap the radar are skipped upfront. Pause menu map still draws everything.",
	},
	{
		"HealthBarBatch",
		"Batched HP & armor bars",
		"-0.5-1.5% frame",
		"Draws all overhead HP and armor bars in 1 DrawPrimitiveUP call per frame instead of 3-6 per player.",
		"Stock SA-MP calls GetViewport, D3DXVec3Project, two SetFVF calls, and up to 6 separate DrawPrimitiveUP calls for EVERY visible armored player (border, background, fill for HP and armor). 50 players = 300 draw calls per frame just for bars.\n\nView*Proj and Viewport are fetched once per frame, projections run in SSE2, and all bars submit in ONE DrawPrimitiveUP call. Pixel-identical.",
	},
	{
		"MaterialFontCache",
		"Object material text font cache",
		"no freezes",
		"Fonts for SetObjectMaterialText are cached instead of recreated via GDI on every sign.",
		"For every material text on an object, SA-MP creates a fresh GDI/D3DX font (slow), draws the texture once, and destroys the font.\n\nFonts are cached by parameters and reused, and purged cleanly before device reset (Alt+Tab). Works alongside TextCache.",
	},
	{
		"SampModelWait",
		"Remove object model Sleep waits",
		"no freezes up to 1s",
		"SA-MP no longer sleeps up to 15 timer ticks on the main thread waiting for an unloaded object model.",
		"When the server creates an object whose model isn't loaded yet, SA-MP requests it and polls up to 15 times with Sleep(1) on the main thread. That thread is the one that unpacks models, so sleeping can't load it anyway - costing 15-230 ms per object (up to 1s for several).\n\nSleep is removed so checks complete immediately. Tuning parts (up to 20x Sleep(1)) now load immediately and attach properly.",
	},
	{
		"NametagLOSCache",
		"Nametag line-of-sight cache",
		"-7% frame",
		"Caches whether a nametag is behind a wall for cache_ms instead of raycasting every frame.",
		"When server nametag LOS is enabled, SA-MP casts a ray through the whole map via GTA's slow script interpreter (opcode 06BD) for every nametag and 3D label every frame. 100 players = hundreds of rays per frame.\n\nThe result is cached for cache_ms while camera and target stay in the same 0.5 m cell. Nametags may hide behind walls a fraction of a second later.",
	},
	{
		"SampObjectCull",
		"Cull off-screen SA-MP objects",
		"up to -5% frame",
		"Server mapping objects outside the camera view are not rendered.",
		"SA-MP renders server objects itself and only checks draw distance: objects behind the camera are prepared and drawn just like ones in front. On heavy mapping servers that's ~700 objects and ~10% frame.\n\nObjects are frustum-culled by bounding sphere first; off-screen ones still update position without rendering. Visuals are identical.",
	},
	{
		"ObjectDrawLastFast",
		"Fast CObjectPool::DrawLast",
		"-1.2% frame",
		"Removes pointless 8 KB stack zeroing every frame and O(N^2) bubble sort in SA-MP.",
		"SA-MP's CObjectPool::DrawLast (transparent server objects) allocates and zeroes 8,000 stack bytes via _chkstk (2,000x rep stosd) every frame even when 0 objects exist, then runs an O(N^2) x87 bubble sort.\n\nStack zeroing is removed, only actually visible drawLast objects are collected, and they are sorted with fast std::sort in the exact same far-to-near order. Lossless.",
	},
	{
		"ObjectFindCache",
		"SA-MP object lookup cache",
		"-1% frame",
		"Caches pool+entity object lookups before drawing instead of scanning the whole pool.",
		"Before drawing each object, SA-MP scans all object pool slots (up to 1,000 cache misses each), ~200 times per frame.\n\nLookups are cached per (pool, GTA entity) pair and validated before returning, falling back to full scan on miss. Lossless.",
	},
	{
		"ObjectMatTextFast",
		"Fast object material text scan (SSE2)",
		"-0.6% frame",
		"Scans all 16 SA-MP object material slots with branchless SSE2 instead of 16 branches across 3.4 KB.",
		"Every frame SA-MP loops 16 times per material object (samp+0xA47F0) checking if SetObjectMaterialText needs texture creation - even when textures are already created or only normal materials are used.\n\nSSE2 checks all 16 slots in ~12 ns branchlessly and calls the original only when pending text actually exists. Lossless.",
	},
	{
		"ObjectPrefetch",
		"Nametag & HP loop prefetch",
		"-0.5% frame",
		"Prefetches player data for nametag and healthbar loops ahead of time.",
		"The nametag loop reads each player's structs sequentially, causing serialized CPU cache misses.\n\nPlayer structs are prefetched upfront so cache misses overlap in parallel. SA-MP's loop logic is untouched.",
	},
	{
		"ObjectSkip",
		"Static mapping skip",
		"-2-3% frame",
		"Static mapping objects aren't re-checked every frame until the camera moves enough.",
		"Every frame for ~460 server objects, SA-MP computes camera distance to stream the model in or out. For static mapping, the answer almost never changes.\n\nDistance can't change more than camera movement, so objects only re-check when the camera nears their range boundary or once per skip_ms. Moving/attached objects update normally.",
	},
	{
		"PlayerLoop",
		"Remote player loop filter",
		"-1.5-2% frame",
		"Runs per-frame processing only for streamed-in players (with a ped), prefetching memory upfront.",
		"SA-MP processes every connected player (~950 on a full server) every frame even though unstreamed players return immediately after 2 cache misses.\n\nA single prefetched pass collects only players with a ped and runs SA-MP's exact code on them. Lossless.",
	},
	{
		"PlayerTagState",
		"Lazy nametag & HP state blocks",
		"-1-2.5% frame",
		"Skips saving/restoring D3D9 states when no nametags or HP bars are actually visible.",
		"Every frame SA-MP calls BeginHealthBar/EndHealthBar and BeginLabel/EndLabel, triggering IDirect3DStateBlock9::Capture/Apply and ID3DXSprite::Begin/End even when zero players are visible on screen.\n\nBegin is deferred until the first actual bar or nametag is drawn; empty frames skip both heavy state switches completely. Identical visuals.",
	},
	{
		"SampRotationSSE",
		"Object rotation math (experimental)",
		"experimental",
		"SA-MP R1 object rotation: 3 sin/cos pairs computed in SSE with tiny approximation error.",
		"When rotating objects, SA-MP executes three fsin and three fcos instructions. SSE computes all three angles in parallel (benchmark matrix error < 0.000001, not bit-exact). Angles outside [-360, 360], unsupported FPU modes, and non-main threads fall back to original.",
	},
	{
		"SampKeyPollCache",
		"SA-MP F1/F5/F10 key poll cache",
		"-0.7-1.1% frame",
		"Stops calling GetAsyncKeyState 2,000-3,000 times/sec for F1, F5, and F10.",
		"SA-MP's render hook calls win32u!NtUserGetAsyncKeyState three times every frame (F5, F10, F1). At high FPS that's thousands of kernel transitions per second (~1% frame), plus SAMPFUNCS/mousefix.\n\nKey state is cached for interval_ms (default 4 ms = 250 Hz), clearing the edge-trigger bit (0x0001) on cache hits to prevent double presses.",
	},
	{
		"SampStateBlockFix",
		"Targeted UI StateBlock for SA-MP",
		"-1-1.8% frame",
		"Replaces heavy D3DSBT_ALL (~900 GPU states every frame) in CPlayerTags and CDXUTDialog with a targeted block.",
		"Every frame SA-MP captures and applies a FULL D3DSBT_ALL StateBlock (~900 Direct3D states: all 8 texture stages, 16 vertex streams, lights, matrices) for HP bars and chat scrollbar / dialogs (CDXUTDialog::OnRender). On DXVK this hits CPU by 1.5-2% frame.\n\nCaptures and restores only the ~54 states these UI passes actually modify. 100% identical visuals.",
	},
	{
		"SampTextMemset",
		"SA-MP colored text buffer",
		"-0.5% frame",
		"Stops zeroing a 40 KB buffer before every single chat line and 3D text.",
		"Before drawing each string, SA-MP converts it to Unicode and zeroes an entire 40 KB buffer even for a 10-char string.\n\nOnly the bytes right after the string are zeroed since SA-MP reads up to the first null terminator. Lossless.",
	},
	{
		"TextCache",
		"SA-MP text layout cache",
		"-15-20% frame",
		"Nametags, 3D labels, and chat stop re-shaping every frame: cached glyphs replay with offset.",
		"SA-MP draws every string 5 times (4 shadow passes + text) plus DT_CALCRECT, while d3dx runs every call through Uniscribe OpenType shaping. In crowds this takes 19-30% of the frame.\n\nFirst appearance draws via real d3dx and records glyph textures/positions; subsequent draws replay those exact commands with position offset and current color. Bit-identical. Every Nth hit is verified against real d3dx and auto-disables on any mismatch.",
	},
	{
		"UnprotectCache",
		"SA-MP VirtualProtect cache",
		"-1.5% frame",
		"SA-MP stops asking Windows to unlock memory pages that are already writable.",
		"Every frame for each player, SA-MP patches GTA code snippets and calls VirtualProtect (~140 times/frame) via WoW64 even though the page was already made writable.\n\nPages unlocked within the last second are not re-protected repeatedly. Lossless.",
	},
	{
		"LuaProfiler",
		"Lua profiler",
		"",
		"Measures frame time spent by each MoonLoader script.",
		"Times every MoonLoader entry into Lua (lua_pcall/lua_call/lua_resume) broken down by script and function. Overhead is two timer reads per call, negligible.",
	},
	{
		"SampProfiler",
		"SA-MP & GTA profiler",
		"",
		"Measures frame time of ~45 samp.dll functions, GTA frame stages, and entity vtable calls.",
		"Hooks entry/exit of ~45 SA-MP functions and GTA frame stages. Profiling itself costs ~6% frame time: turn off after measuring.",
	},
};

static const ParamEn kParamEn[] = {
	{ "ColPlaneCache", "size", "Models in cache" },
	{ "CursorCenterFix", "interval_ms", "Interval, ms" },
	{ "DrawDistance", "percent", "Draw distance, %" },
	{ "MessagePumpFix", "interval_ms", "Interval, ms" },
	{ "PedDrawDistance", "max_m", "Distance, m" },
	{ "RefPool", "block", "Refs per block" },
	{ "RefPool", "max_blocks", "Max blocks" },
	{ "RemotePedPhysics", "min_distance", "Distance, m" },
	{ "RemoteInAir", "max_age_ms", "Cache duration, ms" },
	{ "StreamBudget", "budget_kb", "Per frame, KB" },
	{ "StreamMemBoost", "memory_mb", "Streaming RAM, MB" },
	{ "VehicleLod", "lod_m", "Detailed within, m" },
	{ "NametagLOSCache", "cache_ms", "Cache duration, ms" },
	{ "ObjectSkip", "skip_ms", "Max skip, ms" },
	{ "SampKeyPollCache", "interval_ms", "Interval, ms" },
	{ "TextCache", "check_every", "Verify every Nth" },
	{ "LuaProfiler", "interval", "Report file every, s" },
	{ "SampProfiler", "interval", "Report file every, s" },
};

static const FixEn *FindFixEn(const Fix *f)
{
	if (!g_english || !f || !f->section) return nullptr;
	for (const FixEn &e : kFixEn)
		if (!strcmp(e.section, f->section)) return &e;
	return nullptr;
}

const char *FixTitle(const Fix *f)
{
	const FixEn *e = FindFixEn(f);
	return e ? e->title : f->title;
}

const char *FixGain(const Fix *f)
{
	const FixEn *e = FindFixEn(f);
	return e ? e->gain : f->gain;
}

const char *FixSummary(const Fix *f)
{
	const FixEn *e = FindFixEn(f);
	return e ? e->summary : f->summary;
}

const char *FixDetails(const Fix *f)
{
	const FixEn *e = FindFixEn(f);
	return e ? e->details : f->details;
}

const char *ParamLabel(const Fix *f, const Param &p)
{
	if (g_english && f && f->section && p.key) {
		for (const ParamEn &e : kParamEn)
			if (!strcmp(e.section, f->section) && !strcmp(e.key, p.key)) return e.label;
	}
	return p.label;
}
