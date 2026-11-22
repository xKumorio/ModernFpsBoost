# ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
# Copying, redistribution or reuse of this file without the author's permission is prohibited.
"""Generates src/core/samp_addrs_gen.h: every samp.dll address and struct offset the plugin uses, for
each supported SA-MP version, found from the R1 ones with sampport.py.

Usage: python gen_samp_addrs.py <ref/samp dir with R1..DL/samp.dll> ../src/core/samp_addrs_gen.h [--review]
--review prints every port (first instructions / value) to check them by eye.
A value that is not found is written as kSampMissing: the fix that needs it stays off on that version.
"""
import os
import sys

from sampport import VERSION_NAMES, VERSIONS, Porter, Samp

# (name, kind, R1 value, extra, note)
#   code: instruction address; data: global; target: call target of (site, byte offset);
#   caller: the only direct call of (function); value: operand inside (R1 function, bytes to search)
SPEC = [
    # globals
    ('NETGAME', 'data', 0x21A0F8, None, 'CNetGame*'),
    ('FONTS', 'data', 0x21A0FC, None, 'CFonts*'),
    ('CHAT', 'data', 0x21A0E4, None, 'CChat*'),
    ('GAME', 'data', 0x21A10C, None, 'CGame*'),
    ('PLAYERTAGS_PTR', 'data', 0x21A0B0, None, 'CPlayerTags*'),
    ('DXUT_RESMGR_PTR', 'data', 0x21A180, None, 'CDXUTDialogResourceManager*'),
    ('MAT_VIEW', 'data', 0x12C940, None, 'D3DMATRIX view matrix'),
    ('MAT_PROJ', 'data', 0x12C980, None, 'D3DMATRIX projection matrix'),
    # call / patch sites (matched by their own code, or aligned inside their function)
    ('LOS_SITE_LABEL', 'code', 0x67577, None, 'call ScriptCommand(06BD) for 3D labels'),
    ('LOS_SITE_TAG', 'code', 0x6FE0A, None, 'call ScriptCommand(06BD) for a player tag'),
    ('LOS_SITE_TAG2', 'code', 0x70F1A, None, 'call ScriptCommand(06BD) for a player tag'),
    ('OBJFIND_SITE1', 'code', 0x9F88F, None, 'call CObjectPool::Find'),
    ('OBJFIND_SITE2', 'code', 0x9F8EA, None, 'call CObjectPool::Find'),
    ('OBJFIND_SITE3', 'code', 0xA01DC, None, 'call CObjectPool::Find'),
    ('OBJFIND_SITE4', 'code', 0xA042F, None, 'call CObjectPool::Find'),
    ('OBJFIND_SITE5', 'code', 0xA04A6, None, 'call CObjectPool::Find'),
    ('OBJ_PROCESS_SITE', 'code', 0xF601, None, 'call CObject::Process in CObjectPool::Process'),
    ('PLAYER_LOOP', 'code', 0x10359, None, 'cmp si, MAX_PLAYERS: CPlayerPool::Process loop head'),
    ('PLAYER_LOOP_EXIT', 'code', 0x103CA, None, 'after that loop'),
    ('PLAYER_LOOP_GETAT', 'code', 0x10377, None, 'call GetAt; mov ecx, eax; call CRemotePlayer::Process'),
    ('TAG_LOOP_SITE', 'code', 0x71195, None, 'call of the tag loop'),
    ('TO_WIDE_SITE', 'code', 0x6F67A, None, 'call TO_WIDE in the colored text draw'),
    ('FIND_ACCESSORY_SITE', 'code', 0x9F8BC, None, 'call FindAccessory in the object render hook'),
    ('COL_LOADED_SITE', 'code', 0xB09DB, None, 'mov edx, CColStore::HasCollisionLoaded; call edx'),
    ('MATFONT_CREATE_SITE', 'code', 0x6C3DD, None, 'call D3DXCreateFontA in CObjectMaterialText::Create'),
    ('OBJDRAW_SITE', 'code', 0xF70D, None, 'call CObject::Draw in CObjectPool::Draw'),
    ('OBJDRAW_LAST_SITE', 'code', 0xF91B, None, 'call CObject::Draw in CObjectPool::DrawLast'),
    ('OBJ_UPDATE_POS_SITE', 'code', 0xA48CC, None, 'call CObject::UpdatePosition in CObject::Draw'),
    # functions: what the sites above call (their own code is compared with it), else by their code
    ('SCRIPT_COMMAND', 'target', 0xABCD0, ('LOS_SITE_TAG', 0), 'ScriptCommand(cmd, ...)'),
    ('OBJPOOL_FIND', 'target', 0xF520, ('OBJFIND_SITE1', 0), 'CObjectPool::Find(entity)'),
    ('OBJ_PROCESS', 'target', 0xA3F70, ('OBJ_PROCESS_SITE', 0), 'CObject::Process(dt)'),
    ('PLAYERPOOL_GETAT', 'target', 0x10F0, ('PLAYER_LOOP_GETAT', 0), 'CPlayerPool::GetAt(id)'),
    ('REMOTE_PROCESS', 'target', 0x12EF0, ('PLAYER_LOOP_GETAT', 7), 'CRemotePlayer::Process'),
    ('TAG_LOOP', 'target', 0x70D40, ('TAG_LOOP_SITE', 0), 'player tag / health bar loop'),
    ('TO_WIDE', 'target', 0x6F3C0, ('TO_WIDE_SITE', 0), 'text -> UTF-16 (memsets the buffer)'),
    ('FIND_ACCESSORY', 'target', 0x106A0, ('FIND_ACCESSORY_SITE', 0), 'CPlayerPool::FindAccessory(entity)'),
    ('OBJ_DRAW', 'target', 0xA48A0, ('OBJDRAW_SITE', 0), 'CObject::Draw'),
    ('OBJ_UPDATE_POS', 'target', 0xA3970, ('OBJ_UPDATE_POS_SITE', 0), 'CObject: position for the distance checks'),
    ('CHAT_ADD_MESSAGE', 'code', 0x645A0, None, 'CChat::AddMessage(color, text)'),
    ('SET_CURSOR_MODE', 'code', 0x9BD30, None, 'CGame::SetCursorMode(mode, lock)'),
    ('UNFUCK', 'code', 0xA4DD0, None, 'UnFuck(addr, size)'),
    ('TEXTDRAW_POOL_DRAW', 'code', 0x1AD40, None, 'CTextDrawPool::Draw'),
    ('TEXTDRAW_SITE', 'caller', 0x9D929, 'TEXTDRAW_POOL_DRAW', 'the only call of CTextDrawPool::Draw'),
    ('TAG_BEGIN_HB_SITE', 'code', 0x6FC81, None, 'call CPlayerTags::BeginHealthBar'),
    ('TAG_DRAW_HB_SITE', 'code', 0x6FE65, None, 'call CPlayerTags::DrawHealthBar'),
    ('TAG_END_HB_SITE', 'code', 0x6FE7D, None, 'call CPlayerTags::EndHealthBar'),
    ('TAG_BEGIN_LB_SITE', 'code', 0x70D71, None, 'call CPlayerTags::BeginLabel'),
    ('TAG_DRAW_LB_SITE', 'code', 0x70F96, None, 'call CPlayerTags::DrawLabel'),
    ('TAG_END_LB_SITE', 'code', 0x70FAE, None, 'call CPlayerTags::EndLabel'),
    ('TAG_BEGIN_HB', 'target', 0x68FD0, ('TAG_BEGIN_HB_SITE', 0), 'CPlayerTags::BeginHealthBar'),
    ('TAG_DRAW_HB', 'target', 0x689C0, ('TAG_DRAW_HB_SITE', 0), 'CPlayerTags::DrawHealthBar'),
    ('TAG_END_HB', 'target', 0x68670, ('TAG_END_HB_SITE', 0), 'CPlayerTags::EndHealthBar'),
    ('TAG_BEGIN_LB', 'target', 0x686A0, ('TAG_BEGIN_LB_SITE', 0), 'CPlayerTags::BeginLabel'),
    ('TAG_DRAW_LB', 'target', 0x686C0, ('TAG_DRAW_LB_SITE', 0), 'CPlayerTags::DrawLabel'),
    ('TAG_END_LB', 'target', 0x686B0, ('TAG_END_LB_SITE', 0), 'CPlayerTags::EndLabel'),
    ('SB_TAGS_RESET_SITE', 'code', 0x68FC2, None, 'CreateStateBlock(ALL) in CPlayerTags::ResetDevice'),
    ('SB_TAGS_BEGIN_SITE', 'code', 0x68FF8, None, 'CreateStateBlock(ALL) in CPlayerTags::BeginHealthBar'),
    ('SB_DXUT_RESET_SITE', 'code', 0x829A6, None, 'CreateStateBlock(ALL) in CDXUTDialogResourceManager::OnResetDevice'),
    ('OBJPOOL_DRAWLAST', 'code', 0xF720, None, 'CObjectPool::DrawLast'),
    ('OBJ_GETDISTFROMCAM', 'code', 0x9A7D0, None, 'CEntity::GetDistanceFromCamera'),
    ('OBJ_MATTEXT_FN', 'code', 0xA47F0, None, 'CObject::CreateMaterialTextTextures'),
    ('OBJ_MATTEXT_SITE', 'caller', 0xF689, 'OBJ_MATTEXT_FN', 'the only call of CObject::CreateMaterialTextTextures'),
    ('RENDER_HOOK_F5', 'code', 0x71368, None, 'push VK_F5; call esi in render hook (6 bytes before loads GetAsyncKeyState)'),
    # struct offsets and frame slots: the operand of the same instruction in the aligned function
    ('NET_POOLS', 'value', 0x3CD, (0x70D40, 0x50), 'CNetGame -> pools'),
    # CPlayerPool and the player structs differ in every version; read from CPlayerPool::FindAccessory
    # and the player tag loop of each samp.dll (--review prints them) and checked there below
    ('POOLS_PLAYER', 'fixed', (0x18, 0x8, 0x8, 0x8, 0x4, 0x8), 'TAG_LOOP', 'pools -> CPlayerPool'),
    ('POOL_LARGEST', 'fixed', (0x0, 0x22, 0x0, 0x0, 0x2F3A, 0x22), 'FIND_ACCESSORY', 'CPlayerPool -> largest player id'),
    ('POOL_LOCAL', 'fixed', (0x22, 0x1E, 0x2F3A, 0x2A, 0x26, 0x1E), 'FIND_ACCESSORY', 'CPlayerPool -> CLocalPlayer*'),
    ('POOL_INFO', 'fixed', (0x2E, 0x26, 0x4, 0x2E, 0x1F8A, 0x26), 'FIND_ACCESSORY', 'CPlayerPool -> CPlayerInfo*[]'),
    ('POOL_USED', 'fixed', (0xFDE, 0xFD6, 0xFB4, 0xFDE, 0x2A, 0xFD6), 'FIND_ACCESSORY', 'CPlayerPool -> BOOL used[]'),
    ('INFO_REMOTE', 'fixed', (0x0, 0xC, 0x0, 0x10, 0x10, 0x8), 'FIND_ACCESSORY', 'CPlayerInfo -> CRemotePlayer*'),
    ('REMOTE_PED', 'fixed', (0x0, 0x1C, 0x0, 0x1DD, 0x1DD, 0x4), 'REMOTE_PROCESS', 'CRemotePlayer -> CPed* (Process returns at once without it)'),
    ('LOCAL_PED', 'fixed', (0x0, 0x0, 0x0, 0x104, 0x104, 0x0), 'FIND_ACCESSORY', 'CLocalPlayer -> CPed*'),
    ('MAX_PLAYERS', 'value', 0x3EC, (0x10320, 0x60), 'player slots'),
    ('PLAYER_LOOP_IDX', 'value', -0x1C, (0x10320, 0x3C), 'ebp slot of the loop index (SEH resume)'),
    ('PLAYER_LOOP_TRY', 'value', -0x4, (0x10320, 0x58), 'ebp slot of the try state'),
    ('PED_ACC_OBJ', 'fixed', (0x27C,) * 6, 'FIND_ACCESSORY', 'CPed -> CObject* accessories[10]'),
    ('PED_ACC_USED_REL', 'fixed', (-0x230,) * 6, 'FIND_ACCESSORY', 'accessories used[] relative to PED_ACC_OBJ'),
    ('OBJPOOL_OBJS', 'value', 0xFA4, (0xF520, 0x40), 'CObjectPool -> CObject*[]'),
    ('ENT_GAME', 'value', 0x40, (0xF520, 0x30), 'CEntity -> GTA entity'),
    ('OBJ_DRAWN', 'value', 0x118F, (0xA48A0, 0x160), 'CObject -> drawn once (set at the end of Draw)'),
]


def all_calls(d, f):
    return d.calls_to(f)


def uses_operand(d, f, value, limit=0x300):
    for i in d.insns(f, count=2000, limit=limit):
        if i.disp_size and i.disp == value:
            return True
    return False


def port_all(ref):
    r1 = Samp(os.path.join(ref, 'R1', 'samp.dll'))
    dlls = {v: Samp(os.path.join(ref, v, 'samp.dll')) for v in VERSIONS}
    table = {}
    for v in VERSIONS:
        d = dlls[v]
        p = Porter(r1, d) if v != 'R1' else None
        got = {}
        for name, kind, val, extra, _ in SPEC:
            if v == 'R1':
                r = val[0] if kind == 'fixed' else val
            elif kind == 'code':
                r = p.code(val)
            elif kind == 'data':
                r = p.data(val)
            elif kind == 'target':
                site, off = extra
                r = d.call_target(got[site] + off) if got.get(site) is not None else None
                own = p.func(val)
                if r is None:
                    r = own
                elif own is not None and own != r:
                    print('conflict', v, name, hex(r), hex(own))
                if r is not None:
                    p.cache[('f', val)] = r  # lets the values below align this function
            elif kind == 'fixed':
                r = val[VERSIONS.index(v)]
                f = got.get(extra)
                if v != 'R1' and r and (f is None or not uses_operand(d, f, r)):
                    print('CHECK FAILED', v, name, hex(r), 'not used in', extra)
                    r = None
            elif kind == 'caller':
                f = got.get(extra)
                calls = all_calls(d, f) if f else []
                r = calls[0] if len(calls) == 1 else None
            else:
                func, limit = extra
                r = p.value(func, val, limit)
            got[name] = r
            table.setdefault(name, {})[v] = r
    return r1, dlls, table


def write(path, dlls, table):
    L = ['// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.', "// Copying, redistribution or reuse of this file without the author's permission is prohibited.", '// Generated by tools/gen_samp_addrs.py from the samp.dll of each version - do not edit.',
         '// Addresses are rvas in samp.dll; values are struct offsets / constants. Found from the R1 ones',
         '// (tools/sampport.py); every fix still checks the bytes it patches before patching.',
         '#pragma once', '',
         'enum SampVersion { ' + ', '.join('SAMP_' + v for v in VERSIONS) + ', SAMP_VERSIONS };', '',
         'static const char *const kSampVersionNames[SAMP_VERSIONS] = { ' + ', '.join('"%s"' % VERSION_NAMES[v] for v in VERSIONS) + ' };',
         'static const DWORD kSampEntryPoints[SAMP_VERSIONS] = { ' + ', '.join('0x%X' % dlls[v].entry for v in VERSIONS) + ' };', '',
         'enum SampId {']
    for name, kind, val, extra, note in SPEC:
        L.append('\tS_%s, // %s' % (name, note))
    L += ['\tS_COUNT,', '};', '', 'static const int kSampMissing = (int)0x80000000;', '',
          'static const int kSampTable[S_COUNT][SAMP_VERSIONS] = {']
    for name, kind, val, extra, note in SPEC:
        cells = []
        for v in VERSIONS:
            r = table[name][v]
            cells.append('kSampMissing' if r is None else ('%d' % r if r < 0 else '0x%X' % r))
        L.append('\t{ %s }, // %s' % (', '.join(cells), name))
    L += ['};', '']
    # every direct call of UnFuck, per version (UnprotectCache patches them all)
    sites = {v: dlls[v].calls_to(table['UNFUCK'][v]) if table['UNFUCK'][v] is not None else [] for v in VERSIONS}
    n = max(len(x) for x in sites.values())
    L += ['static const int kSampUnfuckSitesMax = %d;' % n,
          'static const DWORD kSampUnfuckSites[SAMP_VERSIONS][kSampUnfuckSitesMax + 1] = { // 0-terminated']
    for v in VERSIONS:
        L.append('\t{ %s }, // %s' % (', '.join('0x%X' % a for a in sites[v] + [0]), v))
    L += ['};', '']
    open(path, 'w', encoding='utf-8', newline='\n').write('\n'.join(L))


def review(dlls, table):
    for name, kind, val, extra, note in SPEC:
        print('==', name, kind, note)
        for v in VERSIONS:
            r = table[name][v]
            if r is None:
                print('  %-3s MISSING' % v)
            elif kind in ('code', 'target', 'caller'):
                ins = dlls[v].insns(r, count=3)
                print('  %-3s %06X  %s' % (v, r, ' ; '.join('%s %s' % (i.mnemonic, i.op_str) for i in ins)))
            else:
                print('  %-3s %s' % (v, hex(r)))


if __name__ == '__main__':
    r1, dlls, table = port_all(sys.argv[1])
    write(sys.argv[2], dlls, table)
    if '--review' in sys.argv:
        review(dlls, table)
    for name, kind, val, extra, note in SPEC:
        miss = [v for v in VERSIONS if table[name][v] is None]
        if miss:
            print('missing', name, miss)
