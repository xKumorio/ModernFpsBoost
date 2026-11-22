# ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
# Copying, redistribution or reuse of this file without the author's permission is prohibited.
"""Generates src/profilers/samp_timers_gen.h: what SampProfiler times, as a call tree.

Function bodies are never touched (SAMPFUNCS and others hook them and jump back into their first
bytes); instead the rel32 of direct calls to a timed function is redirected. Call sites come from a
linear disassembly of the code, so bytes that merely look like E8 inside other instructions are not
taken. The plugin re-checks each site (E8 + expected target) before patching.

Timers (timer_lists.py):
  roots    - FUNCS (samp.dll) and GTA_FUNCS (gta_sa.exe), timed at every direct call;
  children - every function called directly inside an opened function (EXPAND_SAMP, EXPAND_GTA,
             and children big enough to be worth opening, down to MAX_DEPTH), timed at the calls
             inside that body only. Each timer has a parent: the tree the menu opens.
Tiny leaf functions (getters) are not timed: timing them costs more than they do.

Usage: python gen_samp_timers.py ../ref/samp/R1/samp.dll <gta_sa.exe> <samp-api src/sampapi/0.3.7-R1> <gta-reversed source> ../src/profilers/samp_timers_gen.h
"""
import glob
import os
import re
import sys

from sampport import Samp
from timer_lists import EXPAND_SAMP, FUNCS, GTA_FUNCS, GTA_SITES, NO_TIME, SCRIPT_COMMAND, SLOTS, VTABLES

# GTA frame stages are opened; collision/AI entry points are hot and stay closed
EXPAND_GTA = [a for a, label in GTA_FUNCS if not label.startswith(('GTA col', 'GTA ai', 'GTA anim', 'GTA audio'))]
MAX_DEPTH = 3       # parent > child > grandchild
MIN_BODY = 0x180    # a child is opened in turn when its body is at least this big
MAX_TIMERS = 2000
# subtrees opened deeper and down to small bodies, ahead of the rest: (gta, root) -> (depth, min body)
DEEP = {(1, 0x58FAE0): (7, 0x30),  # CHud::Draw: radar, blips, sprites, text
        (1, 0x53BEE0): (2, 0x20),  # CGame::Process: every per-frame update, so its own time is only its own
        (1, 0x6C58E0): (3, 0x30),  # search light cone
        (1, 0x6A2B10): (3, 0x60), (1, 0x6AAB50): (3, 0x60), (1, 0x6D0E60): (3, 0x60),  # vehicle Render / PreRender
        (1, 0x733EC0): (3, 0x60), (1, 0x732B40): (3, 0x60), (1, 0x732480): (3, 0x60)}  # see-through lists


def samp_api_names(src_dir):
    names = {}
    for f in glob.glob(os.path.join(src_dir, '*.cpp')):
        cur = None
        for line in open(f, encoding='utf-8', errors='replace'):
            m = re.match(r'^[\w\*&<>:\s]*?\b(\w+::~?\w+)\s*\(', line)
            if m:
                cur = m.group(1)
            m = re.search(r'GetAddress\((0x[0-9A-Fa-f]+)\)', line)
            if m and cur:
                names.setdefault(int(m.group(1), 16), cur)
    return names


def gta_names(src_dir):
    """gta-reversed: RH_ScopedInstall(Name, 0xADDR) under RH_ScopedClass(Class) / namespace"""
    names = {}
    for f in glob.glob(os.path.join(src_dir, '**', '*.cpp'), recursive=True):
        try:
            text = open(f, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        if 'Install' not in text:
            continue
        cls = None
        for line in text.splitlines():
            m = re.search(r'RH_Scoped(?:Class|Namespace)\(\s*(\w+)', line) or re.search(r'RH_ScopedNamespaceName\(\s*"(\w+)"', line)
            if m:
                cls = m.group(1)
            m = re.search(r'RH_Scoped\w*Install\(\s*([~\w:]+)\s*,\s*(?:"[^"]*"\s*,\s*)?(0x[0-9A-Fa-f]{5,6})', line)
            if m:
                name = m.group(1)
                full = name if '::' in name or not cls else '%s::%s' % (cls, name)
                names.setdefault(int(m.group(2), 16), full)
    return names


def is_tiny(mod, rva):
    """a short leaf: up to its first ret without a call"""
    for i in mod.insns(rva, count=24, limit=0x40):
        if i.mnemonic == 'call':
            return False
        if i.mnemonic.startswith('ret'):
            # an early "if (...) return;" is not the end: the whole body decides
            return i.address - rva < 0x28 and mod.func_end(rva, cap=0x200) - rva < 0x30
        if i.mnemonic == 'jmp' and i.op_str.startswith('0x'):
            return False
    return False


def body_calls(mod, start):
    """direct calls inside the function at start (rva): (site rva, target rva)"""
    end = mod.func_end(start, cap=0x4000)
    out = []
    for i in mod.insns(start, count=100000, limit=end - start):
        if i.mnemonic == 'call' and i.size == 5 and i.bytes[0] == 0xE8:
            t = mod.call_target(i.address)
            if mod.t0 <= t < mod.t1:
                out.append((i.address, t))
    return out


def main():
    samp_path, gta_path, api_dir, gr_dir, out = sys.argv[1:6]
    samp = Samp(samp_path)
    gta = Samp(gta_path)
    api = samp_api_names(api_dir)
    gnames = gta_names(gr_dir)
    gbase = gta.base

    timers, sites = [], []  # timer: [addr, label, gta, parent]; site: (at, timer, gta)
    taken = set()
    rep = {}  # (gta, function) -> the timer that stands for it in the tree

    def add_timer(addr, label, is_gta, parent):
        timers.append([addr, label, is_gta, parent])
        rep.setdefault((is_gta, addr), len(timers) - 1)
        return len(timers) - 1

    def name(is_gta, addr):
        if is_gta:
            return gnames.get(addr, 'sub_%06X' % addr)
        return api.get(addr, 'samp_%05X' % addr)

    # roots: every direct call site
    for rva, label in FUNCS:
        add_timer(rva, label, 0, -1)
    for a, label in GTA_FUNCS:
        add_timer(a, label, 1, -1)
    root_of = {(t[2], t[0]): k for k, t in enumerate(timers)}
    for mod, g, base in ((samp, 0, 0), (gta, 1, gbase)):
        for a, size, m in mod.sweep():
            if m != 'call' or size != 5 or mod.img[a] != 0xE8:
                continue
            k = root_of.get((g, mod.call_target(a) + base))
            if k is not None:
                sites.append((a + base, k, g))
                taken.add((g, a + base))
    for a, rva in GTA_SITES:  # SA-MP's calls patched into gta_sa.exe
        sites.append((a, root_of[(0, rva)], 1))
        taken.add((1, a))

    # children, breadth first
    deep = [(g, a, 1, DEEP[(g, a)]) for g, a in DEEP]
    queue = [(0, rva, 1, (MAX_DEPTH, MIN_BODY)) for rva in EXPAND_SAMP] + [(1, a, 1, (MAX_DEPTH, MIN_BODY)) for a in EXPAND_GTA]
    opened = set()
    while (deep or queue) and len(timers) < MAX_TIMERS:
        is_gta, func, depth, (max_depth, min_body) = deep.pop(0) if deep else queue.pop(0)
        is_deep = (max_depth, min_body) != (MAX_DEPTH, MIN_BODY)
        if (is_gta, func) in opened:
            continue
        opened.add((is_gta, func))
        mod, base = (gta, gbase) if is_gta else (samp, 0)
        parent = rep.get((is_gta, func))
        if parent is None:
            continue
        child_of = {}
        for at, t in body_calls(mod, func - base):
            a_abs, t_abs = at + base, t + base
            if (is_gta, a_abs) in taken:
                continue
            if (not is_gta and t in NO_TIME) or is_tiny(mod, t):
                continue
            k = child_of.get(t_abs)
            if k is None:
                if len(timers) >= MAX_TIMERS:
                    break
                k = child_of[t_abs] = add_timer(t_abs, name(is_gta, t_abs), is_gta, parent)
                if depth < max_depth and mod.func_end(t, cap=0x4000) - t >= min_body:
                    (deep if is_deep else queue).append((is_gta, t_abs, depth + 1, (max_depth, min_body)))
            sites.append((a_abs, k, is_gta))
            taken.add((is_gta, a_abs))

    # a root called only from inside one opened function hangs under it
    by_timer = {}
    for at, k, g in sites:
        by_timer.setdefault(k, []).append((at, g))
    for k, t in enumerate(timers):
        if t[3] != -1:
            continue
        holders = set()
        for at, g in by_timer.get(k, []):
            mod, base = (gta, gbase) if g else (samp, 0)
            f = mod.func_of(at - base)
            holders.add((g, f + base if f is not None else None))
        if len(holders) == 1:
            g, f = holders.pop()
            p = rep.get((g, f))
            if p is not None and p != k and (g, f) in opened:
                t[3] = p

    print('%d timers, %d call sites' % (len(timers), len(sites)))
    nl = '\n'
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.' + nl + "// Copying, redistribution or reuse of this file without the author's permission is prohibited." + nl)
        f.write('// Generated by tools/gen_samp_timers.py from samp.dll 0.3.7 R1 + gta_sa.exe 1.0 US - do not edit.' + nl)
        f.write('#pragma once' + nl + nl)
        f.write('static const DWORD kSampTimerScriptCommand = 0x%X;' % SCRIPT_COMMAND + nl + nl)
        f.write('// rva in samp.dll (R1), or an absolute gta_sa.exe address when gta = 1; parent: timer it runs inside, -1 = top' + nl)
        f.write('struct SampTimerFunc { DWORD rva; const char *label; BYTE gta; short parent; };' + nl)
        f.write('static const SampTimerFunc kSampTimerFuncs[] = {' + nl)
        for rva, label, g, parent in timers:
            f.write('\t{ 0x%05X, "%s", %d, %d },' % (rva, label.replace('"', "'"), g, parent) + nl)
        f.write('};' + nl + nl)
        f.write('// at = samp rva of the E8, or the absolute gta_sa.exe address when gta = 1; func = index above' + nl)
        f.write('struct SampTimerSite { DWORD at; WORD func; BYTE gta; };' + nl)
        f.write('static const SampTimerSite kSampTimerSites[] = {' + nl)
        for at, k, g in sites:
            f.write('\t{ 0x%05X, %d, %d },' % (at, k, g) + nl)
        f.write('};' + nl + nl)
        f.write('// entity virtual functions: the vtable slot gets a timing stub that calls what was there' + nl)
        f.write('struct GtaVtTimer { DWORD vtbl; BYTE slot; const char *label; };' + nl)
        f.write('static const GtaVtTimer kGtaVtTimers[] = {' + nl)
        for vt, cls in VTABLES:
            for slot, fn in SLOTS:
                f.write('\t{ 0x%06X, %d, "%s::%s" },' % (vt, slot, cls, fn) + nl)
        f.write('};' + nl)


if __name__ == '__main__':
    main()
