# ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
# Copying, redistribution or reuse of this file without the author's permission is prohibited.
"""What SampProfiler times (used by gen_samp_timers.py). Addresses: samp.dll 0.3.7 R1 rvas, gta_sa.exe 1.0 US absolute."""

FUNCS = [
    (0x9D9A0, 'samp game-loop hook (SA-MP per-frame logic + CGame::Process inside)'),
    (0x9DCA0, 'samp_9DCA0'),
    (0x9E3E0, 'samp_9E3E0'),
    (0x9FC20, 'samp_9FC20'),
    (0xA4DD0, 'samp_A4DD0'),
    (0xA4780, 'samp_A4780'),
    (0x71130, 'samp render hook 71130 (nametags, chat, labels ...)'),
    (0x70560, 'samp_70560'),
    (0x70290, 'samp_70290'),
    (0x70D40, 'samp_70D40 (player tag loop)'),
    (0x6FC30, 'samp_6FC30'),
    (0x8C280, 'samp_8C280'),
    (0xA03A0, 'samp render hook A03A0 (objects)'),
    (0xA0360, 'samp render hook A0360'),
    (0xB3960, 'samp_B3960 (2d stuff)'),
    (0x104B0, 'samp_104B0'),
    (0x8A10, 'CNetGame::UpdatePlayers'),
    (0xAD70, 'CNetGame::UpdateNetwork'),
    (0x7280, 'CLocalPlayer::Process'),
    (0x10320, 'CPlayerPool::Process'),
    (0x10650, 'CPlayerPool::Deactivate?'),
    (0x12EF0, 'CRemotePlayer::Process'),
    (0x1B8D0, 'CVehiclePool::Process'),
    (0x10070, 'CPickupPool::Process'),
    (0xF5A0, 'CObjectPool::Process'),
    (0xA3F70, 'CObject::Process'),
    (0xF520, 'CObjectPool::Find'),
    (0xF6E0, 'CObjectPool::Draw'),
    (0xF720, 'CObjectPool::DrawLast'),
    (0xA48A0, 'CObject::Draw'),
    (0xF660, 'CObjectPool::ConstructMaterials'),
    (0x1340, 'CLabelPool::Draw'),
    (0x67530, 'CLabel::HasNoObstacles'),
    (0x675B0, 'CLabel::Draw'),
    (0x2260, 'CGangZonePool::Draw'),
    (0x1AD40, 'CTextDrawPool::Draw'),
    (0x63310, 'CChatBubble::Draw'),
    (0x64230, 'CChat::Draw'),
    (0x66640, 'CDeathWindow::Draw'),
    (0x66C80, 'CFonts::DrawText'),
    (0x686C0, 'CPlayerTags::DrawLabel'),
    (0x689C0, 'CPlayerTags::DrawHealthBar'),
    (0x68FD0, 'CPlayerTags::BeginHealthBar'),
    (0x68670, 'CPlayerTags::EndHealthBar'),
    (0x686A0, 'CPlayerTags::BeginLabel'),
    (0xABCD0, 'ScriptCommand (GTA script opcode via interpreter)'),
]
SCRIPT_COMMAND = 0xABCD0

# every direct call inside these functions is timed too, labelled 'parent > callee'
EXPAND_SAMP = [0x9D9A0, 0x9DCA0, 0x9E3E0, 0x9FC20, 0x71130, 0x70560, 0x70290, 0x70D40, 0x6FC30, 0xB3960,
          0x63310, 0x10320, 0x12EF0, 0xF5A0, 0xA3F70, 0xF6E0, 0xA48A0, 0xA03A0, 0xA0360, 0x8C280]


# GTA frame stages, timed at their direct calls inside gta_sa.exe
GTA_FUNCS = [
    (0x53BEE0, 'GTA CGame::Process (world update)'), (0x5684A0, 'GTA CWorld::Process (entities)'),
    (0x46A000, 'GTA CTheScripts::Process'), (0x40E670, 'GTA CStreaming::Update'), (0x616650, 'GTA CPopulation::Update'),
    (0x4341C0, 'GTA CCarCtrl::GenerateRandomCars'), (0x52B730, 'GTA CCamera::Process'), (0x411E20, 'GTA CCollision::Update'),
    (0x450A60, 'GTA CPathFind::UpdateStreaming'), (0x4A9A80, 'GTA FxManager_c::Update'), (0x72B850, 'GTA CWeather::Update'),
    (0x561760, 'GTA CTimeCycle::Update'), (0x507750, 'GTA CAudioEngine::Service'), (0x5DCFA0, 'GTA CPlantMgr::Update'),
    (0x706AB0, 'GTA CRealTimeShadowManager::Update'), (0x563430, 'GTA CWorld::ProcessPedsAfterPreRender'),
    (0x5647F0, 'GTA CWorld::ProcessAttachedEntities'), (0x458DE0, 'GTA CPickups::Update'), (0x44C8C0, 'GTA CGarages::Update'),
    (0x440D10, 'GTA CEntryExitManager::Update'), (0x605A30, 'GTA CInterestingEvents::ScanForNearbyEntities'),
    (0x53DF40, 'GTA RenderScene (3D world)'), (0x5556E0, 'GTA CRenderer::ConstructRenderList (culling)'),
    (0x554FE0, 'GTA CRenderer::ScanWorld'), (0x553770, 'GTA CRenderer::ProcessLodRenderLists'),
    (0x553910, 'GTA CRenderer::PreRender (all entities)'), (0x553A10, 'GTA CRenderer::RenderRoads'),
    (0x553AA0, 'GTA CRenderer::RenderEverythingBarRoads'), (0x5531E0, 'GTA CRenderer::RenderFadingInEntities'),
    (0x553220, 'GTA CRenderer::RenderFadingInUnderwaterEntities'), (0x733F10, 'GTA CVisibilityPlugins::RenderFadingEntities'),
    (0x732F30, 'GTA CVisibilityPlugins::RenderWeaponPedsForPC'), (0x733800, 'GTA CVisibilityPlugins::RenderReallyDrawLastObjects'),
    (0x70A960, 'GTA CShadows::RenderStoredShadows'), (0x708300, 'GTA CShadows::RenderStaticShadows'),
    (0x707F40, 'GTA CShadows::UpdateStaticShadows'), (0x70C950, 'GTA CShadows::UpdatePermanentShadows'),
    (0x6EF650, 'GTA CWaterLevel::RenderWater'), (0x5DBAE0, 'GTA CPlantMgr::Render'), (0x713950, 'GTA CClouds::Render'),
    (0x716380, 'GTA CClouds::VolumetricCloudsRender'), (0x716C90, 'GTA CClouds::MovingFogRender'),
    (0x712810, 'GTA CBirds::Render'), (0x6FAEC0, 'GTA CCoronas::Render'), (0x6FB630, 'GTA CCoronas::RenderReflections'),
    (0x720640, 'GTA CSkidmarks::Render'), (0x4A92A0, 'GTA FxManager_c::Render'), (0x726AD0, 'GTA CSpecialFX::Render'),
    (0x71CE20, 'GTA CGlass::Render'), (0x717150, 'GTA CMovingThings::Render'), (0x72AF70, 'GTA CWeather::RenderRainStreaks'),
    (0x7046E0, 'GTA CPostEffects::Render'), (0x727140, 'GTA CMirrors::BeforeMainRender'),
    (0x53E170, 'GTA RenderEffects'), (0x53E230, 'GTA Render2dStuff (HUD, radar)'), (0x58FAE0, 'GTA CHud::Draw'),
    (0x586B00, 'GTA CRadar::DrawMap'), (0x588050, 'GTA CRadar::DrawBlips'), (0x53E530, 'GTA RenderMenus'),
    # culling: what ScanWorld spends its time on (the two scan callbacks are timed at their push in CRenderer::ScanWorld)
    (0x72CAE0, 'GTA cull CWorldScan::ScanWorld'), (0x554840, 'GTA cull CRenderer::ScanSectorList'),
    (0x554B10, 'GTA cull CRenderer::ScanBigBuildingList'), (0x554230, 'GTA cull CRenderer::SetupEntityVisibility'),
    (0x553F60, 'GTA cull CRenderer::SetupBigBuildingVisibility'), (0x534540, 'GTA cull CEntity::GetIsOnScreen'),
    (0x71FAE0, 'GTA cull COcclusion::IsAABoxOccluded'), (0x5534B0, 'GTA cull CRenderer::AddEntityToRenderList'),
    # what search lights, vehicles and see-through things spend their time on (vehicle Render/PreRender and the alpha
    # list callbacks are called through pointers: their bodies are opened so the calls inside are timed)
    (0x6C58E0, 'GTA sl CHeli::SearchLightCone'), (0x6AAB50, 'GTA veh CAutomobile::PreRender body'),
    (0x6A2B10, 'GTA veh CAutomobile::Render body'), (0x6D0E60, 'GTA veh CVehicle::Render'),
    (0x6D64F0, 'GTA veh CVehicle::SetupRender'), (0x534310, 'GTA CEntity::Render'),
    (0x733EC0, 'GTA alpha render atomic list'), (0x732B40, 'GTA alpha CVisibilityPlugins::RenderEntity'),
    (0x732480, 'GTA alpha CVisibilityPlugins::RenderAtomicWithAlpha'),
    # who casts the rays and spheres, ped AI and animation
    (0x56BA00, 'GTA col CWorld::ProcessLineOfSight'), (0x5674E0, 'GTA col CWorld::ProcessVerticalLine'),
    (0x56A490, 'GTA col CWorld::GetIsLineOfSightClear'), (0x569E20, 'GTA col CWorld::TestSphereAgainstWorld'),
    (0x564A20, 'GTA col CWorld::FindObjectsInRange'), (0x568B80, 'GTA col CWorld::FindObjectsKindaColliding'),
    (0x4185C0, 'GTA col CCollision::ProcessColModels'), (0x417730, 'GTA col CCollision::TestLineOfSight'),
    (0x54BA60, 'GTA col CPhysical::ProcessCollisionSectorList'), (0x5E2530, 'GTA col ProcessEntityCollision 5E2530'),
    (0x520190, 'GTA col CCamera::CameraColDetAndReact'), (0x5F2F00, 'GTA col CPedGeometryAnalyser::GetIsLineOfSightClear'),
    (0x5F5A30, 'GTA col CPedGeometryAnalyser::GetIsLineOfSightClear 2'), (0x5F1CB0, 'GTA col CPedGeometryAnalyser::IsInAir'),
    (0x608260, 'GTA ai CPedIntelligence::Process'), (0x6073A0, 'GTA ai CPedIntelligence::ProcessFirst'),
    (0x6018F0, 'GTA ai CPedIntelligence::ProcessAfterProcCol'), (0x6019B0, 'GTA ai CPedIntelligence::ProcessAfterPreRender'),
    (0x681C10, 'GTA ai CTaskManager::ManageTasks'), (0x607E30, 'GTA ai CEventScanner::ScanForEvents'),
    (0x5FFA20, 'GTA ai CEntityScanner::ScanForEntitiesInRange'), (0x4D34F0, 'GTA anim RpAnimBlendClumpUpdateAnimations'),
    (0x4E2EE0, 'GTA audio CAEPedAudioEntity::Service'),
]

# virtual functions per entity class: (vtable, class)
VTABLES = [
    (0x86D168, 'CPlayerPed (players)'), (0x86C358, 'CPed'), (0x86C0A8, 'CCivilianPed'), (0x86C120, 'CCopPed'),
    (0x871120, 'CAutomobile'), (0x871360, 'CBike'), (0x871528, 'CBmx'), (0x8721A0, 'CBoat'), (0x871680, 'CHeli'),
    (0x871948, 'CPlane'), (0x871AE8, 'CQuadBike'), (0x8717D8, 'CMonsterTruck'), (0x871C28, 'CTrailer'), (0x872370, 'CTrain'),
    (0x866F60, 'CObject'), (0x8585C8, 'CBuilding'), (0x8639B0, 'CAnimatedBuilding'),
]
SLOTS = [(10, 'ProcessControl'), (11, 'ProcessCollision'), (12, 'ProcessShift'), (17, 'PreRender'), (18, 'Render')]

# tiny getters called hundreds of thousands of times per second: timing them costs more than they do
NO_TIME = {0x9A150, 0x9A7D0, 0x9B0A0, 0x1010, 0x9AA10, 0x10F0, 0x2DD0, 0x1110, 0x2AD00, 0x990A0, 0x9A700}

GTA_SITES = [  # SA-MP's own calls patched into gta_sa.exe (from stacks): (absolute address, function rva)
    (0x53E981, 0x9D9A0),
    (0x7F9B0D, 0x71130),
]
