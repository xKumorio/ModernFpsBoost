// ModernFpsBoost - Copyright (c) 2026 Softik Inquisitor (https://vk.ru/miurawr). All rights reserved.
// Copying, redistribution or reuse of this file without the author's permission is prohibited.
// [TextCache] lossless cache for ID3DXFont::DrawText (d3dx9_25) used by SA-MP 0.3.7 R1.
//
// Why: every ID3DXFont::DrawText call re-runs Uniscribe (ScriptItemize/ScriptShape/ScriptPlace ->
// textshaping.dll OpenType shaping) for the whole string, every frame. SA-MP draws each nametag,
// 3D label and chat line 5 times (4 shadow passes + the text) and measures it with DT_CALCRECT too.
//
// How: the first DrawText of a (font, string, format[, rect size]) runs for real through a recording
// sprite that captures the ID3DXSprite::Draw calls d3dx emits (glyph texture, source rect, center,
// position). Later calls replay those Draw calls on the caller's sprite, shifted by the rect offset
// and with the current color. d3dx positions glyphs at integer + 0.5*size, so the shift is exact in
// float and the replay is bit-identical to what d3dx would have issued. Every Nth hit is re-checked
// against a real (non-drawing) DrawText; any mismatch disables the cache.
//
// The font vtable is d3dx9_25's, shared by every font made with it, so all D3DX text of the process
// goes through here; it is found via SA-MP's CFonts (samp+0x21A0FC, +8 = the shadow font).

#include <intrin.h>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../registry.h"
#include "material_font_cache.h"

typedef unsigned long D3DCOLOR;
struct Vec3 { float x, y, z; };

static bool g_verifyAll = false;
static int  g_checkEvery = 256;
static int  g_statsSeconds = 0;

// ---------------------------------------------------------------- recorded layout

struct DrawOp {
	void *tex;
	RECT  src;
	Vec3  center;
	Vec3  pos;
	BYTE  hasSrc, hasCenter;
};

// Glyph positions are stored relative to the text block origin (x0, y0); W, H is the block size.
struct Entry {
	std::vector<DrawOp> ops;
	LONG W, H;
	INT  retRel; // DrawText return value minus (y0 - rect.top)
};

// Keys are a 24-byte header + the text. std::hash walks them a byte at a time (FNV-1a) and a miss
// hashes the same key three times (cache, no-cache set, seen set); this one takes 4 bytes a step.
struct KeyHash {
	size_t operator()(const std::string &k) const noexcept
	{
		const BYTE *p = (const BYTE *)k.data();
		size_t n = k.size();
		DWORD h = 0x9E3779B9u ^ (DWORD)n;
		for (; n >= 4; n -= 4, p += 4) {
			DWORD v = *(const DWORD *)p * 0xCC9E2D51u;
			h = _rotl(h ^ (_rotl(v, 15) * 0x1B873593u), 13) * 5 + 0xE6546B64u;
		}
		for (; n; n--, p++) h = (h ^ *p) * 0x01000193u;
		h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13;
		return h;
	}
};

static std::unordered_map<std::string, Entry, KeyHash> g_cache;
static std::unordered_map<void *, std::vector<std::string>> g_fontKeys;
static std::string g_key;
static bool  g_broken;
static int   g_mismatches;
static DWORD g_hits, g_misses, g_bypass, g_checks, g_lastStats;
static DWORD g_bypassWhy[7]; // disabled/bad args, no sprite available, clipped+aligned, huge coords, too long, uncacheable, first sight
// Admission: a text is measured and recorded only from its second appearance. A one-off string (a counter
// that changes every frame) is drawn once as before instead of measure + recorded draw. Keys that turned
// out uncacheable are drawn directly from then on.
static std::unordered_set<std::string, KeyHash> g_seen, g_noCache;
static unsigned g_hitCounter;
static const size_t kMaxEntries = 16384;
static const int    kMaxChars = 1024;

// ---------------------------------------------------------------- recording sprite (ID3DXSprite)

enum { SPR_QI, SPR_ADDREF, SPR_RELEASE, SPR_GETDEVICE, SPR_GETTRANSFORM, SPR_SETTRANSFORM,
       SPR_SETWVRH, SPR_SETWVLH, SPR_BEGIN, SPR_DRAW, SPR_FLUSH, SPR_END, SPR_ONLOST, SPR_ONRESET,
       SPR_COUNT };

struct RecSprite {
	void **vtbl;
	void  *inner;      // caller's sprite
	bool   forward;    // false = dry run for checks
	bool   unsupported;
	DWORD  color;
	std::vector<DrawOp> *ops;
};

#define INNER_VT(s) (*(void ***)(s)->inner)

static HRESULT __stdcall Rec_QI(RecSprite *s, REFIID r, void **p)
{ return ((HRESULT(__stdcall *)(void *, REFIID, void **))INNER_VT(s)[SPR_QI])(s->inner, r, p); }
static ULONG __stdcall Rec_AddRef(RecSprite *) { return 1; }
static ULONG __stdcall Rec_Release(RecSprite *) { return 1; }
static HRESULT __stdcall Rec_GetDevice(RecSprite *s, void **p)
{ return ((HRESULT(__stdcall *)(void *, void **))INNER_VT(s)[SPR_GETDEVICE])(s->inner, p); }
static HRESULT __stdcall Rec_GetTransform(RecSprite *s, void *m)
{ return ((HRESULT(__stdcall *)(void *, void *))INNER_VT(s)[SPR_GETTRANSFORM])(s->inner, m); }
static HRESULT __stdcall Rec_SetTransform(RecSprite *s, const void *m)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *, const void *))INNER_VT(s)[SPR_SETTRANSFORM])(s->inner, m);
}
static HRESULT __stdcall Rec_SetWVRH(RecSprite *s, const void *a, const void *b)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *, const void *, const void *))INNER_VT(s)[SPR_SETWVRH])(s->inner, a, b);
}
static HRESULT __stdcall Rec_SetWVLH(RecSprite *s, const void *a, const void *b)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *, const void *, const void *))INNER_VT(s)[SPR_SETWVLH])(s->inner, a, b);
}
static HRESULT __stdcall Rec_Begin(RecSprite *s, DWORD f)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *, DWORD))INNER_VT(s)[SPR_BEGIN])(s->inner, f);
}
static HRESULT __stdcall Rec_Draw(RecSprite *s, void *tex, const RECT *src, const Vec3 *center, const Vec3 *pos, D3DCOLOR color)
{
	if (!pos || color != s->color) s->unsupported = true;
	else {
		DrawOp op = {};
		op.tex = tex;
		op.hasSrc = src != nullptr;
		if (src) op.src = *src;
		op.hasCenter = center != nullptr;
		if (center) op.center = *center;
		op.pos = *pos;
		s->ops->push_back(op);
	}
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *, void *, const RECT *, const Vec3 *, const Vec3 *, D3DCOLOR))
		INNER_VT(s)[SPR_DRAW])(s->inner, tex, src, center, pos, color);
}
static HRESULT __stdcall Rec_Flush(RecSprite *s)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *))INNER_VT(s)[SPR_FLUSH])(s->inner);
}
static HRESULT __stdcall Rec_End(RecSprite *s)
{
	s->unsupported = true;
	if (!s->forward) return S_OK;
	return ((HRESULT(__stdcall *)(void *))INNER_VT(s)[SPR_END])(s->inner);
}
static HRESULT __stdcall Rec_OnLost(RecSprite *s)
{ return ((HRESULT(__stdcall *)(void *))INNER_VT(s)[SPR_ONLOST])(s->inner); }
static HRESULT __stdcall Rec_OnReset(RecSprite *s)
{ return ((HRESULT(__stdcall *)(void *))INNER_VT(s)[SPR_ONRESET])(s->inner); }

static void *g_recVtbl[SPR_COUNT] = {
	(void *)Rec_QI, (void *)Rec_AddRef, (void *)Rec_Release, (void *)Rec_GetDevice,
	(void *)Rec_GetTransform, (void *)Rec_SetTransform, (void *)Rec_SetWVRH, (void *)Rec_SetWVLH,
	(void *)Rec_Begin, (void *)Rec_Draw, (void *)Rec_Flush, (void *)Rec_End,
	(void *)Rec_OnLost, (void *)Rec_OnReset,
};

// ---------------------------------------------------------------- ID3DXFont hooks

enum { FNT_RELEASE = 2, FNT_DRAWTEXTA = 14, FNT_DRAWTEXTW = 15, FNT_ONLOST = 16, FNT_ONRESET = 17, FNT_COUNT = 18 };

typedef INT(__stdcall *DrawTextA_t)(void *, void *, LPCSTR, INT, LPRECT, DWORD, D3DCOLOR);
typedef INT(__stdcall *DrawTextW_t)(void *, void *, LPCWSTR, INT, LPRECT, DWORD, D3DCOLOR);
typedef ULONG(__stdcall *Release_t)(void *);
typedef HRESULT(__stdcall *Device_t)(void *);

static DrawTextA_t oDrawTextA;
static DrawTextW_t oDrawTextW;
static Release_t   oRelease;
static Device_t    oOnLost, oOnReset;

static void Purge(const char *why)
{
	if (!g_cache.empty() && g_statsSeconds) Log("cache cleared (%s), %u entries", why, (unsigned)g_cache.size());
	g_cache.clear();
	g_fontKeys.clear();
	g_seen.clear();
	g_noCache.clear();
}

static void PurgeFont(void *font)
{
	auto it = g_fontKeys.find(font);
	if (it == g_fontKeys.end()) return;
	for (const std::string &k : it->second) g_cache.erase(k);
	g_fontKeys.erase(it);
}

// Text block origin exactly as d3dx9_25 CFont::DrawTextAW computes it (0x2EC5E):
// DT_CENTER x0 = (R - W + L) / 2, DT_RIGHT x0 = R - W, else L; same vertically; RTL flips DT_RIGHT.
static void Origin(DWORD fmt, const RECT &r, LONG W, LONG H, LONG &x0, LONG &y0)
{
	if (fmt & DT_RTLREADING) fmt ^= DT_RIGHT;
	if (fmt & DT_CENTER) x0 = (r.right - W + r.left) / 2;
	else if (fmt & DT_RIGHT) x0 = r.right - W;
	else x0 = r.left;
	if (fmt & DT_VCENTER) y0 = (r.bottom + r.top - H) / 2;
	else if (fmt & DT_BOTTOM) y0 = r.bottom - H;
	else y0 = r.top;
}

// Rect size matters to the layout only for wrapping/ellipsis and for clipping.
static void KeyRectSize(DWORD fmt, const RECT *rc, LONG &w, LONG &h)
{
	bool clip = !(fmt & (DT_NOCLIP | DT_CALCRECT));
	w = (clip || (fmt & (DT_WORDBREAK | DT_END_ELLIPSIS | DT_PATH_ELLIPSIS | DT_WORD_ELLIPSIS))) ? rc->right - rc->left : 0;
	h = clip ? rc->bottom - rc->top : 0;
}

static char g_diff[256];

static bool SameOps(const std::vector<DrawOp> &a, const std::vector<DrawOp> &b, float dx, float dy)
{
	if (a.size() != b.size()) { sprintf_s(g_diff, "op count"); return false; }
	for (size_t i = 0; i < a.size(); i++) {
		const DrawOp &p = a[i], &q = b[i];
		Vec3 moved = { p.pos.x + dx, p.pos.y + dy, p.pos.z };
		const char *what = nullptr;
		if (p.tex != q.tex) what = "texture";
		else if (p.hasSrc != q.hasSrc || p.hasCenter != q.hasCenter) what = "null args";
		else if (p.hasSrc && memcmp(&p.src, &q.src, sizeof(RECT))) what = "src rect";
		else if (p.hasCenter && memcmp(&p.center, &q.center, sizeof(Vec3))) what = "center";
		else if (memcmp(&moved, &q.pos, sizeof(Vec3))) what = "position";
		if (what) {
			sprintf_s(g_diff, "op %u %s: cached pos %.2f,%.2f real %.2f,%.2f src %ld,%ld/%ld,%ld", (unsigned)i, what,
			          moved.x, moved.y, q.pos.x, q.pos.y, p.src.left, p.src.top, q.src.left, q.src.top);
			return false;
		}
	}
	return true;
}

static void MaybeStats()
{
	if (g_statsSeconds <= 0) return;
	DWORD now = GetTickCount();
	if (now - g_lastStats < (DWORD)g_statsSeconds * 1000) return;
	g_lastStats = now;
	Log("hits %lu, misses %lu, bypass %lu, checks %lu, entries %u", g_hits, g_misses, g_bypass, g_checks, (unsigned)g_cache.size());
	g_hits = g_misses = g_bypass = g_checks = 0;
}

template <typename Ch, typename Orig>
static INT CachedDrawText(Orig orig, void *font, void *sprite, const Ch *str, INT count, LPRECT rc, DWORD fmt, D3DCOLOR color)
{
	int reason = 0;
	auto passthrough = [&]() { g_bypass++; g_bypassWhy[reason]++; return orig(font, sprite, str, count, rc, fmt, color); };

	if (g_broken || !str || !rc || (fmt & DT_MODIFYSTRING))
		return passthrough();
	const bool calc = (fmt & DT_CALCRECT) != 0;
	// Without a sprite d3dx draws with its own one (CFont+0xC): Begin(D3DXSPRITE_ALPHABLEND), the glyphs,
	// End (d3dx9_25 0x2ED1F / 0x2EF6D). Replaying into that sprite between the same Begin/End is the same.
	void *own = nullptr;
	if (!sprite && !calc) {
		reason = 1;
		own = *(void **)((BYTE *)font + 0xC);
		if (!own) return passthrough();
	}
	// Clipped text that is not left/top aligned: the clip rect and the block origin can move by different
	// amounts (rounding of the centering), so the recorded layout would not be translation invariant.
	reason = 2;
	if (!calc && !(fmt & DT_NOCLIP) && (fmt & (DT_CENTER | DT_RIGHT | DT_VCENTER | DT_BOTTOM)))
		return passthrough();
	reason = 3;
	// d3dx stores glyph positions as float(int) + 0.5*size; beyond 2^21 the .5 no longer fits, so far
	// off-screen draws (SA-MP projects labels behind the camera to huge coordinates) are not translation
	// invariant. Measuring (DT_CALCRECT) is pure integer math and is not affected.
	const LONG kFloatExact = 1 << 21;
	if (!calc && (labs(rc->left) >= kFloatExact || labs(rc->top) >= kFloatExact ||
	              labs(rc->right) >= kFloatExact || labs(rc->bottom) >= kFloatExact))
		return passthrough();

	int len;
	if (count < 0) {
		len = 0;
		while (len <= kMaxChars && str[len]) len++;
	} else len = count;
	reason = 4;
	if (len > kMaxChars)
		return passthrough();

	MaybeStats();

	LONG w, h;
	KeyRectSize(fmt, rc, w, h);
	struct { void *font; DWORD fmt; LONG w, h; BYTE wide, hasSprite; } hdr = { font, fmt, w, h, sizeof(Ch) == 2, sprite != nullptr };
	g_key.clear();
	g_key.append((const char *)&hdr, sizeof(hdr));
	g_key.append((const char *)str, len * sizeof(Ch));

	const RECT in = *rc;
	auto it = g_cache.find(g_key);
	if (it != g_cache.end()) {
		Entry &e = it->second;
		LONG x0, y0;
		Origin(fmt, in, e.W, e.H, x0, y0);
		const RECT out = { x0, y0, x0 + e.W, y0 + e.H };
		const INT ret = (y0 - in.top) + e.retRel;
		const float dx = (float)x0, dy = (float)y0;

		bool check = g_verifyAll || (g_checkEvery > 0 && ++g_hitCounter % (unsigned)g_checkEvery == 0);
		if (check) {
			g_checks++;
			std::vector<DrawOp> ops;
			RecSprite rec = { g_recVtbl, sprite ? sprite : own, false, false, color, &ops };
			RECT r2 = in;
			INT realRet = orig(font, (sprite || own) ? &rec : nullptr, str, count, &r2, fmt, color);
			g_diff[0] = 0;
			bool ok = !rec.unsupported && realRet == ret && SameOps(e.ops, ops, dx, dy) &&
			          memcmp(calc ? &out : &in, &r2, sizeof(RECT)) == 0;
			if (!ok) {
				const RECT &want = calc ? out : in;
				g_broken = ++g_mismatches >= 3;
				Log("MISMATCH #%d: cached layout differs from d3dx (fmt %08lX, len %d, in %ld,%ld,%ld,%ld, ret %d/%d, ops %u/%u, rect %ld,%ld,%ld,%ld/%ld,%ld,%ld,%ld, %s) - %s",
				    g_mismatches, fmt, len, in.left, in.top, in.right, in.bottom, ret, realRet, (unsigned)e.ops.size(), (unsigned)ops.size(),
				    want.left, want.top, want.right, want.bottom, r2.left, r2.top, r2.right, r2.bottom, g_diff[0] ? g_diff : "-",
				    g_broken ? "cache disabled" : "entry dropped");
				if (g_broken) Purge("mismatch");
				else g_cache.erase(it);
				return orig(font, sprite, str, count, rc, fmt, color);
			}
		}

		g_hits++;
		if (void *target = sprite ? sprite : own) {
			void **svt = *(void ***)target;
			if (own && FAILED(((HRESULT(__stdcall *)(void *, DWORD))svt[SPR_BEGIN])(own, 0x10 /* D3DXSPRITE_ALPHABLEND */)))
				return orig(font, sprite, str, count, rc, fmt, color);
			auto draw = (HRESULT(__stdcall *)(void *, void *, const RECT *, const Vec3 *, const Vec3 *, D3DCOLOR))svt[SPR_DRAW];
			for (const DrawOp &op : e.ops) {
				Vec3 pos = { op.pos.x + dx, op.pos.y + dy, op.pos.z };
				if (FAILED(draw(target, op.tex, op.hasSrc ? &op.src : nullptr, op.hasCenter ? &op.center : nullptr, &pos, color)))
					break;
			}
			if (own) ((HRESULT(__stdcall *)(void *))svt[SPR_END])(own);
		}
		if (calc) *rc = out;
		return ret;
	}

	reason = 5;
	if (g_noCache.count(g_key))
		return passthrough();
	reason = 6;
	if (g_seen.insert(g_key).second) {
		if (g_seen.size() > kMaxEntries) {
			g_seen.clear();
			g_seen.insert(g_key);
		}
		return passthrough();
	}

	// miss: measure the block (DT_CALCRECT gives its origin and size), then draw for real while recording
	g_misses++;
	Entry e;
	RECT m = in;
	INT mret = orig(font, nullptr, str, count, &m, fmt | DT_CALCRECT, color);
	if (mret == 0) {
		if (g_noCache.size() >= kMaxEntries) g_noCache.clear();
		g_noCache.insert(g_key);
		if (!calc) return orig(font, sprite, str, count, rc, fmt, color);
		*rc = m;
		return mret;
	}
	e.W = m.right - m.left;
	e.H = m.bottom - m.top;
	LONG x0, y0;
	Origin(fmt, in, e.W, e.H, x0, y0);
	bool cacheable = x0 == m.left && y0 == m.top && mret == (y0 - in.top) + e.H;

	INT ret;
	if (calc) {
		*rc = m;
		ret = mret;
	} else {
		RecSprite rec = { g_recVtbl, sprite ? sprite : own, true, false, color, &e.ops };
		void **svt = own ? *(void ***)own : nullptr;
		if (own && FAILED(((HRESULT(__stdcall *)(void *, DWORD))svt[SPR_BEGIN])(own, 0x10)))
			return orig(font, sprite, str, count, rc, fmt, color);
		ret = orig(font, &rec, str, count, rc, fmt, color);
		if (own) ((HRESULT(__stdcall *)(void *))svt[SPR_END])(own);
		if (rec.unsupported || memcmp(&in, rc, sizeof(RECT)))
			cacheable = false;
	}
	if (!cacheable) {
		if (g_noCache.size() >= kMaxEntries) g_noCache.clear();
		g_noCache.insert(g_key);
		return ret;
	}
	e.retRel = ret - (y0 - in.top);
	for (DrawOp &op : e.ops) {
		op.pos.x -= (float)x0;
		op.pos.y -= (float)y0;
	}
	if (g_cache.size() >= kMaxEntries)
		Purge("full");
	g_fontKeys[font].push_back(g_key);
	g_cache.emplace(g_key, std::move(e));
	return ret;
}

static INT __stdcall hkDrawTextA(void *font, void *sprite, LPCSTR str, INT count, LPRECT rc, DWORD fmt, D3DCOLOR color)
{
	return CachedDrawText<char>(oDrawTextA, font, sprite, str, count, rc, fmt, color);
}

static INT __stdcall hkDrawTextW(void *font, void *sprite, LPCWSTR str, INT count, LPRECT rc, DWORD fmt, D3DCOLOR color)
{
	return CachedDrawText<wchar_t>(oDrawTextW, font, sprite, str, count, rc, fmt, color);
}

static ULONG __stdcall hkRelease(void *font)
{
	ULONG n = oRelease(font);
	if (n == 0) PurgeFont(font);
	return n;
}

static HRESULT __stdcall hkOnLost(void *font)
{
	Purge("device lost");
	MaterialFontCachePurge();
	return oOnLost(font);
}

static HRESULT __stdcall hkOnReset(void *font)
{
	Purge("device reset");
	MaterialFontCachePurge();
	return oOnReset(font);
}

// ---------------------------------------------------------------- attach

static bool PatchFontVtable(void *font)
{
	HMODULE d3dx = GetModuleHandleA("d3dx9_25.dll");
	void **vt = *(void ***)font;
	if (!d3dx || !InModule(d3dx, vt) || !InModule(d3dx, vt[FNT_DRAWTEXTA]) || !InModule(d3dx, vt[FNT_DRAWTEXTW])) {
		Log("font vtable is not d3dx9_25's - not patching");
		return false;
	}
	DWORD old;
	if (!VirtualProtect(vt, FNT_COUNT * sizeof(void *), PAGE_READWRITE, &old)) return false;
	oRelease = (Release_t)vt[FNT_RELEASE];
	oDrawTextA = (DrawTextA_t)vt[FNT_DRAWTEXTA];
	oDrawTextW = (DrawTextW_t)vt[FNT_DRAWTEXTW];
	oOnLost = (Device_t)vt[FNT_ONLOST];
	oOnReset = (Device_t)vt[FNT_ONRESET];
	MemoryBarrier();
	vt[FNT_RELEASE] = (void *)hkRelease;
	vt[FNT_DRAWTEXTA] = (void *)hkDrawTextA;
	vt[FNT_DRAWTEXTW] = (void *)hkDrawTextW;
	vt[FNT_ONLOST] = (void *)hkOnLost;
	vt[FNT_ONRESET] = (void *)hkOnReset;
	VirtualProtect(vt, FNT_COUNT * sizeof(void *), old, &old);
	return true;
}

static void Load(const Ini &ini)
{
	g_checkEvery = ini.Int("TextCache", "check_every", 256);
	g_verifyAll = ini.Bool("TextCache", "verify_all", false);
	g_statsSeconds = ini.Int("Log", "stats_seconds", 0);
}

static void Init()
{
	g_key.reserve(4096);
	g_cache.max_load_factor(0.5f);
	g_seen.max_load_factor(0.5f);
	g_noCache.max_load_factor(0.5f);
	g_cache.reserve(4096);
	g_seen.reserve(4096);
	g_noCache.reserve(1024);
}

// waits for SA-MP's fonts: CFonts* (R1 samp+0x21A0FC), CFonts+8 is a plain ID3DXFont (shadow font) in every version
static bool Attach(BYTE *samp)
{
	void **fontsVar = (void **)SampAt(S_FONTS);
	if (!fontsVar) return true;
	void *fonts = *fontsVar;
	if (!fonts) return false;
	void *shadowFont = *(void **)((BYTE *)fonts + 8);
	if (!shadowFont) return false;
	if (PatchFontVtable(shadowFont)) {
		g_fixTextCache.active = true;
		Log("attached: ID3DXFont::DrawText cached (check_every=%d, verify_all=%d)", g_checkEvery, (int)g_verifyAll);
		// needs the OnLostDevice hook above to drop its fonts before a Reset
		if (g_fixMaterialFontCache.enabled) MaterialFontCacheApply(samp);
	}
	return true;
}

static void Report(std::string &out, double secs)
{
	Appendf(out, "TextCache в секунду: из кеша %.0f, записано %.0f, мимо %.0f (выкл/аргументы %.0f, без спрайта %.0f, обрезка+выравнивание %.0f, далеко %.0f, длинные %.0f, не кешируются %.0f, первый раз %.0f)\n",
	        g_hits / secs, g_misses / secs, g_bypass / secs, g_bypassWhy[0] / secs, g_bypassWhy[1] / secs, g_bypassWhy[2] / secs,
	        g_bypassWhy[3] / secs, g_bypassWhy[4] / secs, g_bypassWhy[5] / secs, g_bypassWhy[6] / secs);
	if (!g_statsSeconds) {
		g_hits = g_misses = g_bypass = g_checks = 0;
		memset(g_bypassWhy, 0, sizeof(g_bypassWhy));
	}
}

static const Param kParams[] = {
	{ "check_every", "Сверка: каждый N-й", &g_checkEvery, nullptr, 0, 4096, true },
};

Fix g_fixTextCache = {
	.section = "TextCache",
	.title = "Кеш текста SA-MP",
	.gain = "-15–20% кадра",
	.summary = "Ники, 3D-тексты и чат больше не раскладываются шрифтом заново: готовые буквы повторяются со сдвигом.",
	.details = "SA-MP рисует каждую строку 5 раз (4 прохода тени + текст) и ещё раз меряет, а d3dx на каждый вызов заново гоняет строку через Uniscribe (OpenType-шейпинг). В толпе это 19–30% кадра.\n\nПервый раз строка рисуется по-настоящему и записывается, дальше те же команды отрисовки повторяются со сдвигом и текущим цветом. Картинка побитово та же. Каждый N-й повтор сверяется с настоящим d3dx, при расхождении кеш выключается сам.",
	.load = Load,
	.atStart = Init,
	.atSamp = Attach,
	.report = Report,
	.params = kParams,
	.paramCount = Count(kParams),
};
