# ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
# Copying, redistribution or reuse of this file without the author's permission is prohibited.
"""Finds samp.dll 0.3.7 R1 addresses in the other SA-MP versions (R2, R3-1, R4, R5-1, 0.3.DL-R1).

A code address is matched by the instructions starting at it: relocated dwords and rel32 branch
targets are masked (they differ between builds), the pattern grows until it occurs exactly once in
the target's code. If nothing matches, displacements, immediates and short jumps are masked too
(struct layouts and code sizes shift between versions), then a pattern starting a few instructions
later is tried and its offset taken back.
A global (data) address is found through the instructions that reference it: each is ported and
the target's operand read; the answers have to agree.
A value (struct offset, constant, stack slot) is found through the instruction that uses it inside
a given R1 function, read the same way.

    from sampport import Samp, Porter
    r1 = Samp('ref/samp/R1/samp.dll'); r5 = Samp('ref/samp/R5/samp.dll')
    p = Porter(r1, r5); p.code(0x645A0); p.data(0x21A0E4); p.value(0xF520, 0xFA4)
"""
import bisect
import difflib
import re
import struct
from collections import Counter

import capstone
import pefile

VERSIONS = ['R1', 'R2', 'R3', 'R4', 'R5', 'DL']
VERSION_NAMES = {'R1': '0.3.7-R1', 'R2': '0.3.7-R2', 'R3': '0.3.7-R3-1', 'R4': '0.3.7-R4', 'R5': '0.3.7-R5-1', 'DL': '0.3.DL-R1'}

_BRANCH_GROUPS = (capstone.x86.X86_GRP_JUMP, capstone.x86.X86_GRP_CALL)


class Samp:
    def __init__(self, path):
        pe = pefile.PE(path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.entry = pe.OPTIONAL_HEADER.AddressOfEntryPoint
        self.img = bytes(pe.get_memory_mapped_image())
        text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
        self.t0 = text.VirtualAddress
        self.t1 = text.VirtualAddress + text.Misc_VirtualSize
        self.code = self.img[self.t0:self.t1]
        self.relocs = set()
        for block in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', []):
            for e in block.entries:
                if e.type == 3:
                    self.relocs.add(e.rva)
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self._insn = {}

    def sweep(self):
        """(address, size, mnemonic) of a linear disassembly of the whole code, resumed after bytes
        that do not decode"""
        if not hasattr(self, '_sweep'):
            out = []
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            at = self.t0
            while at < self.t1:
                last = at
                for addr, size, mnem, _ in md.disasm_lite(self.img[at:self.t1], at):
                    out.append((addr, size, mnem))
                    last = addr + size
                at = last + 1  # decoding stopped at a byte that is not an instruction
            self._sweep = out
        return self._sweep

    def calls_to(self, f):
        """direct call sites of f"""
        if not hasattr(self, '_calls'):
            calls = {}
            for addr, size, mnem in self.sweep():
                if mnem == 'call' and size == 5 and self.img[addr] == 0xE8:
                    calls.setdefault(self.call_target(addr), []).append(addr)
            self._calls = calls
        return self._calls.get(f, [])

    @property
    def funcs(self):
        """sorted function starts: targets of direct calls"""
        if not hasattr(self, '_funcs'):
            self.calls_to(0)
            self._funcs = sorted(t for t in self._calls if self.t0 <= t < self.t1)
        return self._funcs

    def func_of(self, rva):
        k = bisect.bisect_right(self.funcs, rva) - 1
        return self.funcs[k] if k >= 0 else None

    def func_end(self, start, cap=0x3000):
        k = bisect.bisect_right(self.funcs, start)
        end = self.funcs[k] if k < len(self.funcs) else self.t1
        return min(end, start + cap)

    def insn(self, rva):
        if rva not in self._insn:
            self._insn[rva] = next(self.md.disasm(self.img[rva:rva + 16], rva), None)
        return self._insn[rva]

    def dword(self, rva):
        return struct.unpack_from('<I', self.img, rva)[0]

    def call_target(self, rva):
        if self.img[rva] != 0xE8:
            return None
        return (rva + 5 + struct.unpack_from('<i', self.img, rva + 1)[0]) & 0xFFFFFFFF

    def insns(self, rva, count=64, limit=512):
        out, at = [], rva
        while len(out) < count and at < rva + limit:
            i = self.insn(at)
            if not i:
                break
            out.append(i)
            at += i.size
        return out


def _mask(samp, i, loose):
    """bytes of one instruction with the build-dependent parts as None"""
    b = list(i.bytes)
    for k in range(i.size):
        if i.address + k in samp.relocs:
            for j in range(4):
                if k + j < i.size:
                    b[k + j] = None
    is_branch = any(g in i.groups for g in _BRANCH_GROUPS)
    if is_branch and i.imm_size:
        # rel32 always differs; rel8 only when code between moved (loose)
        if i.imm_size == 4 or loose:
            for k in range(i.imm_offset, i.imm_offset + i.imm_size):
                b[k] = None
    if loose:
        if i.disp_size:
            for k in range(i.disp_offset, i.disp_offset + i.disp_size):
                b[k] = None
        if i.imm_size and not is_branch:
            for k in range(i.imm_offset, i.imm_offset + i.imm_size):
                b[k] = None
    return b


def _regex(masked):
    return re.compile(b''.join(b'.' if x is None else re.escape(bytes([x])) for x in masked), re.DOTALL)


class Porter:
    def __init__(self, src, dst):
        self.src, self.dst = src, dst
        self.cache = {}

    def _find(self, rva, loose, max_len=320):
        """dst rva for src code rva, or None.
        The pattern grows until it is unique, then keeps growing to a safety margin: a pattern that is
        unique by luck (a short common sequence) must not decide. Every longer pattern must still hit
        the same single place; the match ends at the src function's end (ret/int3 padding)."""
        ins = self.src.insns(rva, count=200, limit=max_len)
        masked, fixed, found, extra = [], 0, None, 0
        need_fixed = 28 if loose else 20
        for i in ins:
            m = _mask(self.src, i, loose)
            masked += m
            fixed += sum(x is not None for x in m)
            end = i.mnemonic in ('ret', 'int3') or (i.mnemonic == 'jmp' and i.imm_size == 4)
            if fixed < 8 and not end:
                continue
            hits = [h.start() for h in _regex(masked).finditer(self.dst.code)][:2]
            if not hits:
                return None if found is None or fixed < need_fixed else found
            if len(hits) > 1:
                if found is not None:
                    return None
                if end:
                    return None
                continue
            found = self.dst.t0 + hits[0]
            if fixed >= need_fixed:
                extra += 1
                if extra >= 3 or end:
                    return found
            if end:
                return found if fixed >= need_fixed else None
        return found if found is not None and fixed >= need_fixed else None

    def func(self, f):
        """dst start of the src function f: by its own bytes, else through a caller that ports"""
        key = ('f', f)
        if key in self.cache:
            return self.cache[key]
        self.cache[key] = None  # no recursion loops
        r = self._find(f, False) or self._find(f, True)
        if r is None:
            votes = Counter()
            for site in self.callers(f)[:6]:
                cf = self.src.func_of(site)
                if cf is None or cf == f:
                    continue
                d = self.aligned(cf, site, ports_func=False)
                if d is not None:
                    t = self.dst.call_target(d)
                    if t is not None:
                        votes[t] += 1
            r = _agree(votes)
        self.cache[key] = r
        return r

    def callers(self, f):
        return self.src.calls_to(f)

    def _tokens(self, samp, start, end):
        ins = samp.insns(start, count=100000, limit=end - start)
        return ins, [i.mnemonic + ' ' + re.sub(r'0x[0-9a-f]+|\d+', '#', i.op_str) for i in ins]

    def aligned(self, f, rva, ports_func=True):
        """dst address of src instruction rva inside src function f, by aligning the two functions'
        instruction streams (mnemonics + registers; numbers ignored)"""
        tf = self.func(f) if ports_func else self._find(f, False) or self._find(f, True)
        if tf is None:
            return None
        a_ins, a = self._tokens(self.src, f, self.src.func_end(f))
        idx = next((k for k, i in enumerate(a_ins) if i.address == rva), None)
        if idx is None:
            return None
        span = self.src.func_end(f) - f
        b_ins, b = self._tokens(self.dst, tf, min(self.dst.func_end(tf, cap=0x6000), tf + span * 2 + 0x200))
        sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
        for blk in sm.get_matching_blocks():
            if blk.a <= idx < blk.a + blk.size:
                # the block must be long enough to mean something
                if blk.size < 3:
                    return None
                return b_ins[blk.b + idx - blk.a].address
        return None

    def code(self, rva):
        """dst rva of the instruction at src rva (function start, call site, patch site)"""
        if rva in self.cache:
            return self.cache[rva]
        self.cache[rva] = None
        r = self._find(rva, False) or self._find(rva, True)
        if r is None:
            f = self.src.func_of(rva)
            if f == rva:
                r = self.func(rva)
            elif f is not None:
                r = self.aligned(f, rva)
        self.cache[rva] = r
        return r

    def _operand_at(self, src_insn, dst_rva, field_off, size=4):
        di = self.dst.insn(dst_rva)
        if not di or di.size != src_insn.size or di.mnemonic != src_insn.mnemonic:
            return None
        fmt = {1: '<b', 2: '<h', 4: '<i'}[size]
        return struct.unpack_from(fmt, bytes(di.bytes), field_off)[0]

    def data(self, rva, votes=12):
        """dst rva of a global the src code references by absolute address"""
        va = self.src.base + rva
        refs = []
        for f in sorted(self.src.relocs):
            if self.src.t0 <= f < self.src.t1 and self.src.dword(f) == va:
                refs.append(f)
        answers = Counter()
        for f in refs:
            for start in range(f - 1, f - 8, -1):
                i = self.src.insn(start)
                if not i or start + i.size <= f:
                    continue
                d = self.code(start)
                if d is None:
                    break
                v = self._operand_at(i, d, f - start)
                if v is not None:
                    answers[(v & 0xFFFFFFFF) - self.dst.base] += 1
                break
            if sum(answers.values()) >= votes:
                break
        return _agree(answers)

    def value(self, func, value, limit=0x600, which=None):
        """dst value of a displacement/immediate the src function uses (struct offset, constant)"""
        answers = Counter()
        hits = 0
        for i in self.src.insns(func, count=4000, limit=limit):
            for kind, off, size in (('disp', i.disp_offset, i.disp_size), ('imm', i.imm_offset, i.imm_size)):
                if not size or (kind == 'imm' and any(g in i.groups for g in _BRANCH_GROUPS)):
                    continue
                fmt = {1: '<b', 2: '<h', 4: '<i'}[size]
                v = struct.unpack_from(fmt, bytes(i.bytes), off)[0]
                if v != value and (v & 0xFFFFFFFF) != value and (size != 2 or (v & 0xFFFF) != value):
                    continue
                hits += 1
                if which is not None and hits - 1 != which:
                    continue
                d = self.aligned(func, i.address) if self.src.func_of(func) == func else None
                if d is None:
                    d = self.code(i.address)
                if d is None:
                    continue
                dv = self._operand_at(i, d, off, size)
                if dv is not None:
                    if size == 2 and value > 0x7FFF:
                        dv &= 0xFFFF
                    answers[dv] += 1
        return _agree(answers)


def _agree(answers):
    if not answers:
        return None
    (best, n), = answers.most_common(1)
    total = sum(answers.values())
    return best if n * 2 > total else None
