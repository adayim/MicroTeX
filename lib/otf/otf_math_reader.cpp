#include "otf/otf_math_reader.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>
#include <freetype/tttags.h>
#include <freetype/ftsnames.h>
#include <freetype/ftoutln.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "microtex.h"
#include "unimath/font_src.h"
#include "otf/otf.h"
#include "otf/otfconfig.h"
#include "otf/math_consts.h"

// =============================================================================
// In-memory OTF → CLM synth
// -----------------------------------------------------------------------------
// Pulls font basics via FreeType, parses the OpenType MATH table ourselves,
// walks glyph outlines via FT_Outline_Decompose, and emits CLM v6 minor=2
// bytes matching what `CLMReader::read` expects. The bytes are handed to
// `MicroTeX::addFont` through `FontSrcData` — no disk cache, no external
// toolchain (FontForge / otf2clm.py) dependency.
//
// GSUB class-kerning and ligature tables are emitted as empty (count=0 /
// empty root); scripts-variants are also empty because OT MATH has no
// native source for them (FontForge derives these from GSUB `ssty`).
// =============================================================================

namespace {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using i16 = std::int16_t;
using i32 = std::int32_t;

// Pull wire-format constants straight from MicroTeX so the writer cannot drift
// from the reader on a version bump or a semantics change.
constexpr u16 CLM_VER_MAJOR_EXPECTED = CLM_VER_MAJOR;
constexpr i16 UNDEFINED_MATH_VALUE   = microtex::Otf::undefinedMathValue;
constexpr int MATH_CONSTS_COUNT      = TEX_MATH_CONSTS_COUNT;
static_assert(CLM_SUPPORT_GLYPH_PATH(2),
              "CLM minor=2 must signal 'has glyph paths' in the reader.");
static_assert(MATH_CONSTS_COUNT == 57,
              "MathConsts field count changed — audit read_math_constants.");

// ---------- Big-endian readers over a raw byte span --------------------------

struct BeReader {
    const u8*   data;
    std::size_t len;

    bool in_bounds(std::size_t off, std::size_t n) const {
        // Phrased to avoid off + n overflowing size_t on a malformed font.
        return off <= len && n <= (len - off);
    }

    u16 u16be(std::size_t off) const {
        if (!in_bounds(off, 2)) throw std::out_of_range("BE u16 OOB");
        return static_cast<u16>((static_cast<u16>(data[off]) << 8) |
                                static_cast<u16>(data[off + 1]));
    }
    i16 i16be(std::size_t off) const {
        return static_cast<i16>(u16be(off));
    }
    u32 u32be(std::size_t off) const {
        if (!in_bounds(off, 4)) throw std::out_of_range("BE u32 OOB");
        return (static_cast<u32>(data[off])     << 24) |
               (static_cast<u32>(data[off + 1]) << 16) |
               (static_cast<u32>(data[off + 2]) << 8)  |
                static_cast<u32>(data[off + 3]);
    }
};

// ---------- Big-endian writers into a growing byte vector --------------------

struct BeWriter {
    std::vector<u8> buf;

    void u8v(u8 v)      { buf.push_back(v); }
    void u16v(u16 v)    { buf.push_back(v >> 8);  buf.push_back(v & 0xFF); }
    void i16v(i16 v)    { u16v(static_cast<u16>(v)); }
    void u32v(u32 v) {
        buf.push_back((v >> 24) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >>  8) & 0xFF);
        buf.push_back( v        & 0xFF);
    }
    void i32v(i32 v)    { u32v(static_cast<u32>(v)); }
    void bytes(const u8* p, std::size_t n) { buf.insert(buf.end(), p, p + n); }
    void str0(const std::string& s) {
        bytes(reinterpret_cast<const u8*>(s.data()), s.size());
        u8v(0);
    }
};

// ---------- OT MATH coverage table -------------------------------------------
// Reads a Coverage table rooted at the given offset. Returns a map from
// glyph id → coverage index (the index is what indexes into parallel
// arrays defined alongside the coverage).

std::map<u16, u16> read_coverage(const BeReader& r, std::size_t off) {
    std::map<u16, u16> cov;
    const u16 format = r.u16be(off);
    if (format == 1) {
        const u16 n = r.u16be(off + 2);
        for (u16 i = 0; i < n; ++i) {
            const u16 g = r.u16be(off + 4 + i * 2);
            cov.emplace(g, i);
        }
    } else if (format == 2) {
        const u16 n = r.u16be(off + 2);
        for (u16 i = 0; i < n; ++i) {
            const std::size_t rec = off + 4 + i * 6;
            const u16 start_g = r.u16be(rec);
            const u16 end_g   = r.u16be(rec + 2);
            const u16 start_c = r.u16be(rec + 4);
            // u32 counter: a range ending at glyph 0xFFFF (legal per the
            // spec) would wrap a u16 and never terminate.
            for (u32 g = start_g; g <= end_g; ++g) {
                cov.emplace(static_cast<u16>(g),
                            static_cast<u16>(start_c + (g - start_g)));
            }
        }
    }
    // Unknown formats → empty coverage.
    return cov;
}

// ---------- MathValueRecord (i16 value + u16 device offset, ignored) ---------

inline i16 read_mvr_value(const BeReader& r, std::size_t off) {
    return r.i16be(off);
}

// ---------- MathConstants ----------------------------------------------------
// Reads all 57 fields into an array in the order MicroTeX expects
// (MathConsts::_fields). Field [56] (minConnectorOverlap) is populated from
// the MathVariants subtable separately.

void read_math_constants(const BeReader& math, std::size_t mc_off,
                         i16 out[MATH_CONSTS_COUNT]) {
    std::fill(out, out + MATH_CONSTS_COUNT, static_cast<i16>(0));
    out[0] = math.i16be(mc_off + 0);     // scriptPercentScaleDown
    out[1] = math.i16be(mc_off + 2);     // scriptScriptPercentScaleDown
    out[2] = static_cast<i16>(math.u16be(mc_off + 4));  // delimitedSubFormulaMinHeight (UFWORD)
    out[3] = static_cast<i16>(math.u16be(mc_off + 6));  // displayOperatorMinHeight (UFWORD)
    // Fields 4..54 are 51 consecutive MathValueRecords (4 bytes each).
    std::size_t p = mc_off + 8;
    for (int i = 4; i <= 54; ++i) {
        out[i] = read_mvr_value(math, p);
        p += 4;
    }
    // Field 55: radicalDegreeBottomRaisePercent — i16 (not a MVR).
    out[55] = math.i16be(p);
    // Field 56 set later from MathVariants.minConnectorOverlap.
}

// ---------- Per-glyph math info gathered from the OT MATH table --------------

struct MathKernPoints {
    std::vector<std::pair<i16, i16>> corner[4]; // TL, TR, BL, BR → (kern, height)
    bool any_present = false;
};

struct AssemblyPart {
    u16 glyph = 0;
    u16 flags = 0;
    u16 start_connector = 0;
    u16 end_connector   = 0;
    u16 full_advance    = 0;
};

struct Assembly {
    bool present = false;
    i16 italics_correction = 0;
    std::vector<AssemblyPart> parts;
};

struct GlyphMath {
    i16 italics_correction   = 0;
    i16 top_accent           = UNDEFINED_MATH_VALUE;
    std::vector<u16> h_variants;
    std::vector<u16> v_variants;
    Assembly h_assembly;
    Assembly v_assembly;
    MathKernPoints kerns;
};

// Read a MathGlyphConstruction record (variants + optional assembly) at `mgc_off`.
// `parent` is the MathVariants subtable origin, used to resolve offsets.
void read_construction(const BeReader& math, std::size_t mv_off,
                       std::size_t mgc_off,
                       std::vector<u16>& variants, Assembly& assembly) {
    const u16 assembly_off_rel   = math.u16be(mgc_off + 0);
    const u16 variant_count      = math.u16be(mgc_off + 2);
    for (u16 i = 0; i < variant_count; ++i) {
        const std::size_t rec = mgc_off + 4 + i * 4;
        variants.push_back(math.u16be(rec)); // variantGlyph (advanceMeasurement ignored)
    }
    if (assembly_off_rel != 0) {
        const std::size_t ga_off = mgc_off + assembly_off_rel;
        assembly.present = true;
        assembly.italics_correction = read_mvr_value(math, ga_off);
        const u16 part_count = math.u16be(ga_off + 4);
        assembly.parts.reserve(part_count);
        for (u16 i = 0; i < part_count; ++i) {
            const std::size_t pr = ga_off + 6 + i * 10;
            AssemblyPart pt;
            pt.glyph           = math.u16be(pr + 0);
            pt.start_connector = math.u16be(pr + 2);
            pt.end_connector   = math.u16be(pr + 4);
            pt.full_advance    = math.u16be(pr + 6);
            pt.flags           = math.u16be(pr + 8);
            assembly.parts.push_back(pt);
        }
    }
    (void)mv_off;
}

// Read a MathKern table: u16 heightCount, then heightCount MVRs for heights
// and (heightCount+1) MVRs for kern values. Returns (kern, height) pairs.
void read_math_kern(const BeReader& math, std::size_t kern_off,
                    std::vector<std::pair<i16, i16>>& out) {
    const u16 hc = math.u16be(kern_off);
    // u32 counters: with hc == 0xFFFF, `u16 i < hc + 1` never terminates
    // (hc + 1 promotes to int 65536, unreachable for a u16).
    std::vector<i16> heights(hc), kerns(static_cast<std::size_t>(hc) + 1);
    std::size_t p = kern_off + 2;
    for (u32 i = 0; i < hc;                        ++i) { heights[i] = read_mvr_value(math, p); p += 4; }
    for (u32 i = 0; i < static_cast<u32>(hc) + 1;  ++i) { kerns[i]   = read_mvr_value(math, p); p += 4; }
    // MicroTeX's MathKern stores (value, height) pairs; for i ∈ [0, hc] the
    // kern[i] applies below height[i] (or beyond the last, for i = hc).
    // To match MicroTeX's `indexOf(height)` lookup we store height[i]
    // alongside kern[i]; the final entry uses the last height as its anchor.
    for (u32 i = 0; i < static_cast<u32>(hc) + 1; ++i) {
        const i16 h = (hc == 0) ? 0 :
                      (i < hc ? heights[i] : heights[hc - 1]);
        out.emplace_back(kerns[i], h);
    }
}

// ---------- MathGlyphInfo parser ---------------------------------------------

void parse_math_glyph_info(const BeReader& math, std::size_t mgi_off,
                           std::vector<GlyphMath>& per_glyph) {
    // MathGlyphInfo header: 4 × Offset16 (italicCorr, topAccent, extShape, kerns)
    const u16 italic_off_rel   = math.u16be(mgi_off + 0);
    const u16 top_accent_rel   = math.u16be(mgi_off + 2);
    // ext_shape_rel ignored — not stored in CLM.
    const u16 math_kern_rel    = math.u16be(mgi_off + 6);

    if (italic_off_rel != 0) {
        const std::size_t tbl = mgi_off + italic_off_rel;
        const u16 cov_rel = math.u16be(tbl);
        const u16 count   = math.u16be(tbl + 2);
        auto cov = read_coverage(math, tbl + cov_rel);
        for (const auto& [g, idx] : cov) {
            if (g >= per_glyph.size() || idx >= count) continue;
            per_glyph[g].italics_correction = read_mvr_value(math, tbl + 4 + idx * 4);
        }
    }

    if (top_accent_rel != 0) {
        const std::size_t tbl = mgi_off + top_accent_rel;
        const u16 cov_rel = math.u16be(tbl);
        const u16 count   = math.u16be(tbl + 2);
        auto cov = read_coverage(math, tbl + cov_rel);
        for (const auto& [g, idx] : cov) {
            if (g >= per_glyph.size() || idx >= count) continue;
            per_glyph[g].top_accent = read_mvr_value(math, tbl + 4 + idx * 4);
        }
    }

    if (math_kern_rel != 0) {
        const std::size_t tbl = mgi_off + math_kern_rel;
        const u16 cov_rel = math.u16be(tbl);
        const u16 count   = math.u16be(tbl + 2);
        auto cov = read_coverage(math, tbl + cov_rel);
        for (const auto& [g, idx] : cov) {
            if (g >= per_glyph.size() || idx >= count) continue;
            const std::size_t rec = tbl + 4 + idx * 8;
            // 4 corner offsets per record: TR, TL, BR, BL. Offsets are
            // relative to the MathKernInfo table start, not the record.
            // MicroTeX stores kerns in TL, TR, BL, BR order.
            const u16 off_tr = math.u16be(rec + 0);
            const u16 off_tl = math.u16be(rec + 2);
            const u16 off_br = math.u16be(rec + 4);
            const u16 off_bl = math.u16be(rec + 6);
            auto& gm = per_glyph[g];
            auto load = [&](u16 off_rel, int corner) {
                if (off_rel == 0) return;
                read_math_kern(math, tbl + off_rel, gm.kerns.corner[corner]);
                gm.kerns.any_present = true;
            };
            load(off_tl, 0);
            load(off_tr, 1);
            load(off_bl, 2);
            load(off_br, 3);
        }
    }
}

// ---------- MathVariants parser ----------------------------------------------

u16 parse_math_variants(const BeReader& math, std::size_t mv_off,
                        std::vector<GlyphMath>& per_glyph) {
    const u16 min_connector_overlap = math.u16be(mv_off + 0);
    const u16 vcov_rel              = math.u16be(mv_off + 2);
    const u16 hcov_rel              = math.u16be(mv_off + 4);
    const u16 vglyph_count          = math.u16be(mv_off + 6);
    const u16 hglyph_count          = math.u16be(mv_off + 8);

    auto apply = [&](std::size_t cov_off_abs, u16 glyph_count,
                     std::size_t construction_array_off, bool vertical) {
        auto cov = read_coverage(math, cov_off_abs);
        for (const auto& [g, idx] : cov) {
            if (g >= per_glyph.size() || idx >= glyph_count) continue;
            const u16 mgc_rel = math.u16be(construction_array_off + idx * 2);
            if (mgc_rel == 0) continue;
            const std::size_t mgc_off = mv_off + mgc_rel;
            auto& gm = per_glyph[g];
            if (vertical) {
                read_construction(math, mv_off, mgc_off, gm.v_variants, gm.v_assembly);
            } else {
                read_construction(math, mv_off, mgc_off, gm.h_variants, gm.h_assembly);
            }
        }
    };

    const std::size_t vconst_arr = mv_off + 10;
    const std::size_t hconst_arr = vconst_arr + vglyph_count * 2;

    if (vcov_rel != 0) apply(mv_off + vcov_rel, vglyph_count, vconst_arr, true);
    if (hcov_rel != 0) apply(mv_off + hcov_rel, hglyph_count, hconst_arr, false);

    return min_connector_overlap;
}

// ---------- Top-level MATH parser --------------------------------------------

struct ParsedMath {
    bool present = false;
    i16 consts[MATH_CONSTS_COUNT]{};
    std::vector<GlyphMath> per_glyph;
};

ParsedMath parse_math_table(const u8* math_bytes, std::size_t math_len,
                            u16 num_glyphs) {
    ParsedMath out;
    out.per_glyph.assign(num_glyphs, GlyphMath{});
    if (math_bytes == nullptr || math_len < 10) return out;

    BeReader math{math_bytes, math_len};
    const u16 major = math.u16be(0);
    if (major != 1) return out;
    const u16 mc_rel  = math.u16be(4);
    const u16 mgi_rel = math.u16be(6);
    const u16 mv_rel  = math.u16be(8);

    if (mc_rel != 0) read_math_constants(math, mc_rel, out.consts);
    if (mgi_rel != 0) parse_math_glyph_info(math, mgi_rel, out.per_glyph);
    if (mv_rel != 0) {
        const u16 mco = parse_math_variants(math, mv_rel, out.per_glyph);
        out.consts[56] = static_cast<i16>(mco);
    }
    out.present = true;
    return out;
}

// ---------- FreeType helpers -------------------------------------------------

// Private FT_Library owned by this TU. We used to pull faces through
// systemfonts' `get_cached_face`, but that cache (v1 API) poisons itself on
// a failed lookup — once any caller passes a bad path, every subsequent
// lookup of a valid font also fails with the same error. Since our reads
// are one-shot (metadata + outline decompose) we don't benefit from caching
// anyway; a direct FT_New_Face is simpler and fully isolated.
FT_Library& shared_ft_library() {
    static FT_Library lib = nullptr;
    static bool initialised = false;
    if (!initialised) {
        if (FT_Init_FreeType(&lib) != 0) {
            lib = nullptr;
        }
        initialised = true;
    }
    return lib;
}

FT_Face open_face(const std::string& path, int index) {
    FT_Library& lib = shared_ft_library();
    if (lib == nullptr) {
        throw std::runtime_error("failed to initialise FreeType");
    }
    FT_Face face = nullptr;
    const FT_Error err = FT_New_Face(lib, path.c_str(),
                                     index < 0 ? 0 : index, &face);
    if (err != 0 || face == nullptr) {
        throw std::runtime_error("failed to open font '" + path + "'");
    }
    return face;
}

std::vector<u8> read_sfnt_table(FT_Face face, FT_ULong tag) {
    FT_ULong len = 0;
    if (FT_Load_Sfnt_Table(face, tag, 0, nullptr, &len) != 0 || len == 0) return {};
    // sfnt metadata tables are at most a few MB; reject absurd sizes from a
    // corrupt font rather than attempting a multi-GB heap allocation.
    if (len > 50u * 1024u * 1024u) return {};
    std::vector<u8> buf(len);
    if (FT_Load_Sfnt_Table(face, tag, 0, buf.data(), &len) != 0) return {};
    return buf;
}

// Pull a readable name from the 'name' table. FreeType's FT_Get_Sfnt_Name
// gives us raw records; prefer (platform 3, encoding 1) UTF-16BE, fall back
// to platform 1 MacRoman, and handle the ASCII-subset-of-UTF-16 case.
std::string decode_name_record(const FT_SfntName& rec) {
    if (rec.string_len == 0) return "";
    if (rec.platform_id == 3 || rec.platform_id == 0) {
        // UTF-16BE
        std::string out;
        out.reserve(rec.string_len / 2);
        for (FT_UInt i = 0; i + 1 < rec.string_len; i += 2) {
            const u16 hi = rec.string[i];
            const u16 lo = rec.string[i + 1];
            const u16 cp = static_cast<u16>((hi << 8) | lo);
            out.push_back(cp < 0x80 ? static_cast<char>(cp) : '?');
        }
        return out;
    }
    // MacRoman-ish; treat as ASCII with '?' fallback.
    std::string out;
    out.reserve(rec.string_len);
    for (FT_UInt i = 0; i < rec.string_len; ++i) {
        const u8 c = rec.string[i];
        out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
}

struct NameStrings {
    std::string family;    // name ID 1
    std::string ps_name;   // name ID 6
};

NameStrings read_names(FT_Face face) {
    NameStrings out;
    struct Pick { int score = -1; std::string value; };
    Pick family, ps_name;

    const FT_UInt n = FT_Get_Sfnt_Name_Count(face);
    for (FT_UInt i = 0; i < n; ++i) {
        FT_SfntName rec;
        if (FT_Get_Sfnt_Name(face, i, &rec) != 0) continue;
        if (rec.name_id != 1 && rec.name_id != 6) continue;

        int score = -1;
        if (rec.platform_id == 3 && rec.encoding_id == 1 && rec.language_id == 0x0409) score = 3;
        else if (rec.platform_id == 3 && rec.encoding_id == 1) score = 2;
        else if (rec.platform_id == 0) score = 2;
        else if (rec.platform_id == 1 && rec.encoding_id == 0) score = 1;
        if (score < 0) continue;

        auto& slot = (rec.name_id == 1) ? family : ps_name;
        if (score > slot.score) {
            slot.score = score;
            slot.value = decode_name_record(rec);
        }
    }
    out.family  = family.value;
    out.ps_name = ps_name.value.empty() ? family.value : ps_name.value;
    if (out.family.empty()) out.family = out.ps_name;
    return out;
}

// Read head table → unitsPerEm, macStyle.
struct HeadInfo { u16 em = 0; u16 mac_style = 0; };
HeadInfo read_head(FT_Face face) {
    HeadInfo out;
    auto head = read_sfnt_table(face, TTAG_head);
    if (head.size() >= 46) {
        BeReader r{head.data(), head.size()};
        out.em = r.u16be(18);
        out.mac_style = r.u16be(44);
    }
    return out;
}

// Read OS/2 → sxHeight (only on version ≥ 2).
i16 read_os2_xheight(FT_Face face) {
    auto os2 = read_sfnt_table(face, TTAG_OS2);
    if (os2.size() < 88) return 0;
    BeReader r{os2.data(), os2.size()};
    const u16 ver = r.u16be(0);
    if (ver < 2) return 0;
    const i16 x = r.i16be(86);
    return x < 0 ? static_cast<i16>(0) : x;
}

// Read hhea → ascent / descent.
void read_hhea(FT_Face face, i16& ascent, i16& descent) {
    auto hhea = read_sfnt_table(face, TTAG_hhea);
    if (hhea.size() < 8) return;
    BeReader r{hhea.data(), hhea.size()};
    ascent  = r.i16be(4);
    descent = -r.i16be(6); // spec is negative; CLM stores positive
    if (descent < 0) descent = 0;
}

// Translate OT/2 macStyle bits + PostScript hints into the small FontStyle
// enum MicroTeX uses (0 none; 1 bold; 2 italic; 3 bold+italic; higher bits
// sans/mono/caps not set by our synth).
u16 compute_style(const HeadInfo& h, const std::string& ps_name) {
    u16 style = 0;
    if (h.mac_style & 0x1) style |= 0x1; // bold
    if (h.mac_style & 0x2) style |= 0x2; // italic
    // Fallback: look for "Italic" / "Oblique" / "Bold" in PS name.
    auto contains = [&](const char* needle) {
        return ps_name.find(needle) != std::string::npos;
    };
    if (!(style & 0x1) && (contains("Bold") || contains("bold"))) style |= 0x1;
    if (!(style & 0x2) && (contains("Italic") || contains("Oblique"))) style |= 0x2;
    return style;
}

// Collect (unicode, glyph_id) pairs via FT_Get_First_Char / FT_Get_Next_Char.
struct CmapEntry { u32 cp; u16 gid; };
std::vector<CmapEntry> read_cmap(FT_Face face) {
    std::vector<CmapEntry> out;
    FT_UInt gid = 0;
    FT_ULong cp = FT_Get_First_Char(face, &gid);
    while (gid != 0) {
        if (cp <= 0xFFFFFFFFu && gid <= 0xFFFF) {
            out.push_back({static_cast<u32>(cp), static_cast<u16>(gid)});
        }
        cp = FT_Get_Next_Char(face, cp, &gid);
    }
    // Dedupe by codepoint (first occurrence wins).
    std::sort(out.begin(), out.end(),
              [](const CmapEntry& a, const CmapEntry& b) { return a.cp < b.cp; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const CmapEntry& a, const CmapEntry& b) { return a.cp == b.cp; }),
              out.end());
    return out;
}

// Per-glyph design-unit metrics via FT_LOAD_NO_SCALE.
struct GlyphMetrics { i16 width = 0; i16 height = 0; i16 depth = 0; i16 xMin = 0; };

std::vector<GlyphMetrics> read_glyph_metrics(FT_Face face, u16 num_glyphs,
                                             i16 fallback_ascent,
                                             i16 fallback_descent) {
    // MicroTeX's Metrics treats height/depth as non-negative distances from
    // the baseline. Clamp fallbacks (and OTF-derived values below) so they
    // cannot go negative — a negative depth for a glyph that sits entirely
    // above the baseline under-sizes the enclosing VBox and draws accents /
    // over-delimiters through the operand instead of above it.
    const i16 safe_ascent  = std::max<i16>(0, fallback_ascent);
    const i16 safe_descent = std::max<i16>(0, fallback_descent);
    std::vector<GlyphMetrics> out(num_glyphs);
    for (u16 g = 0; g < num_glyphs; ++g) {
        if (FT_Load_Glyph(face, g, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP) != 0) {
            out[g].height = safe_ascent;
            out[g].depth  = safe_descent;
            continue;
        }
        const FT_Glyph_Metrics& m = face->glyph->metrics;
        auto clamp16 = [](long v) -> i16 {
            if (v >  32767) return  32767;
            if (v < -32768) return -32768;
            return static_cast<i16>(v);
        };
        out[g].width  = clamp16(m.horiAdvance);
        const long yMax = m.horiBearingY;
        const long yMin = m.horiBearingY - m.height;
        out[g].height = clamp16(std::max<long>(0, yMax));
        out[g].depth  = clamp16(std::max<long>(0, -yMin));
        out[g].xMin   = clamp16(m.horiBearingX);
    }
    return out;
}

// ---------- CLM serialiser ---------------------------------------------------

void write_variants(BeWriter& w, const std::vector<u16>& v) {
    w.u16v(static_cast<u16>(v.size()));
    for (u16 g : v) w.u16v(g);
}

void write_assembly(BeWriter& w, const Assembly& a) {
    if (!a.present) { w.u8v(0); return; }
    w.u8v(1);
    w.u16v(static_cast<u16>(a.parts.size()));
    w.i16v(a.italics_correction);
    for (const auto& p : a.parts) {
        w.u16v(p.glyph);
        w.u16v(p.flags);
        w.u16v(p.start_connector);
        w.u16v(p.end_connector);
        w.u16v(p.full_advance);
    }
}

void write_math_kern(BeWriter& w, const std::vector<std::pair<i16, i16>>& pts) {
    // hc == 0xFFFF yields 65536 pairs but the CLM count field is u16 —
    // cap so the written count always matches the pairs that follow.
    const std::size_t n = std::min<std::size_t>(pts.size(), 0xFFFF);
    w.u16v(static_cast<u16>(n));
    for (std::size_t i = 0; i < n; ++i) {
        w.i16v(pts[i].first);   // kern value
        w.i16v(pts[i].second);  // height
    }
}

// ---------- Glyph outline → CLM path commands -------------------------------
// FT_Outline_Decompose walks each contour: one move_to, then line/conic/cubic
// segments. It does NOT emit an implicit close; we emit 'Z' before each new
// 'M' (past the first) and once at the end. OTF/FreeType has Y-up; CLM paths
// are drawn with Y-down, so we negate Y on the way out.

struct PathBuilder {
    std::vector<char>  cmds;
    std::vector<i16>   args;

    static i16 clamp16(long v) {
        if (v >  32767) return  32767;
        if (v < -32768) return -32768;
        return static_cast<i16>(v);
    }

    void emit(char cmd, std::initializer_list<long> vs) {
        cmds.push_back(cmd);
        for (long v : vs) args.push_back(clamp16(v));
    }
};

extern "C" {
    static int pb_move_to(const FT_Vector* to, void* user) {
        auto* pb = static_cast<PathBuilder*>(user);
        if (!pb->cmds.empty() && pb->cmds.back() != 'Z') {
            pb->cmds.push_back('Z');
        }
        pb->emit('M', {to->x, -to->y});
        return 0;
    }
    static int pb_line_to(const FT_Vector* to, void* user) {
        static_cast<PathBuilder*>(user)->emit('L', {to->x, -to->y});
        return 0;
    }
    static int pb_conic_to(const FT_Vector* c, const FT_Vector* to, void* user) {
        static_cast<PathBuilder*>(user)->emit('Q', {c->x, -c->y, to->x, -to->y});
        return 0;
    }
    static int pb_cubic_to(const FT_Vector* c1, const FT_Vector* c2,
                           const FT_Vector* to, void* user) {
        static_cast<PathBuilder*>(user)->emit(
            'C', {c1->x, -c1->y, c2->x, -c2->y, to->x, -to->y});
        return 0;
    }
}

PathBuilder build_glyph_path(FT_Face face, u16 gid) {
    PathBuilder pb;
    if (FT_Load_Glyph(face, gid,
                      FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP |
                      FT_LOAD_NO_HINTING) != 0) {
        return pb;
    }
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return pb;

    FT_Outline_Funcs fns{};
    fns.move_to  = pb_move_to;
    fns.line_to  = pb_line_to;
    fns.conic_to = pb_conic_to;
    fns.cubic_to = pb_cubic_to;
    fns.shift    = 0;
    fns.delta    = 0;

    if (FT_Outline_Decompose(&face->glyph->outline, &fns, &pb) != 0) {
        pb.cmds.clear(); pb.args.clear();
        return pb;
    }
    if (!pb.cmds.empty() && pb.cmds.back() != 'Z') pb.cmds.push_back('Z');
    return pb;
}

// Mirror of `microtex::pathCmdArgsCount` — kept local so we don't have to
// expose a MicroTeX header from the writer.
inline u16 cmd_args(char c) {
    switch (c) {
        case 'M': case 'm': case 'L': case 'l':
        case 'T': case 't':            return 2;
        case 'H': case 'h':
        case 'V': case 'v':            return 1;
        case 'Z': case 'z':            return 0;
        case 'C': case 'c':            return 6;
        case 'S': case 's':
        case 'Q': case 'q':            return 4;
        default:                       return 0;
    }
}

void write_glyph_path(BeWriter& w, const PathBuilder& pb) {
    w.u16v(static_cast<u16>(pb.cmds.size()));
    std::size_t ai = 0;
    for (char c : pb.cmds) {
        w.u8v(static_cast<u8>(c));
        const u16 n = cmd_args(c);
        for (u16 i = 0; i < n; ++i) {
            w.i16v(pb.args[ai++]);
        }
    }
}

std::vector<u8> build_clm_bytes_impl(const std::string& path, int index);

}  // namespace
namespace {

std::vector<u8> build_clm_bytes_impl(const std::string& path, int index) {
    FT_Face face = open_face(path, index);
    struct FaceGuard {
        FT_Face f;
        ~FaceGuard() { if (f) FT_Done_Face(f); }
    } guard{face};

    const u16 num_glyphs = static_cast<u16>(std::min<long>(face->num_glyphs, 0xFFFF));
    NameStrings names = read_names(face);
    HeadInfo head = read_head(face);
    i16 x_height = read_os2_xheight(face);
    i16 ascent = 0, descent = 0;
    read_hhea(face, ascent, descent);
    u16 style = compute_style(head, names.ps_name);

    auto cmap = read_cmap(face);
    auto metrics = read_glyph_metrics(face, num_glyphs, ascent, descent);

    auto math_bytes = read_sfnt_table(face, TTAG_MATH);
    ParsedMath math = parse_math_table(
        math_bytes.empty() ? nullptr : math_bytes.data(),
        math_bytes.size(), num_glyphs);
    const bool is_math = math.present;

    BeWriter w;
    // Header
    w.u8v('c'); w.u8v('l'); w.u8v('m');
    w.u16v(CLM_VER_MAJOR_EXPECTED);
    w.u8v(2);  // minor = 2 (with glyph paths) — see otfconfig.h CLM_SUPPORT_GLYPH_PATH

    // MicroTeX uses the CLM's "name" as the math-font registry key, so write
    // the human-readable family (e.g. "Lete Sans Math") rather than the
    // psName (e.g. "LeteSansMath").
    const std::string& primary = names.family.empty() ? names.ps_name : names.family;
    w.str0(primary);
    w.str0(primary);
    w.u8v(is_math ? 1 : 0);
    w.u16v(style);
    w.u16v(head.em);
    w.u16v(static_cast<u16>(std::max<i16>(0, x_height)));
    w.u16v(static_cast<u16>(std::max<i16>(0, ascent)));
    w.u16v(static_cast<u16>(std::max<i16>(0, descent)));

    // cmap
    const std::size_t n_cmap = std::min<std::size_t>(cmap.size(), 0xFFFF);
    w.u16v(static_cast<u16>(n_cmap));
    for (std::size_t i = 0; i < n_cmap; ++i) {
        if (cmap[i].gid >= num_glyphs) { w.u32v(0); w.u16v(0); continue; }
        w.u32v(cmap[i].cp);
        w.u16v(cmap[i].gid);
    }

    // ClassKernings: none
    w.u16v(0);

    // Ligatures: empty root (glyph=0, liga=-1, childCount=0)
    w.u16v(0);
    w.i32v(-1);
    w.u16v(0);

    // MathConsts (only if math font)
    if (is_math) {
        for (int i = 0; i < MATH_CONSTS_COUNT; ++i) w.i16v(math.consts[i]);
    }

    // Glyphs
    w.u16v(num_glyphs);
    for (u16 g = 0; g < num_glyphs; ++g) {
        const auto& m = metrics[g];
        w.i16v(m.width);
        w.i16v(m.height);
        w.i16v(m.depth);
        w.i16v(m.xMin);

        // kernRecord: none (we don't parse GPOS pair kerning here)
        w.u16v(0);

        if (is_math) {
            const GlyphMath& gm = math.per_glyph[g];
            w.i16v(gm.italics_correction);
            w.i16v(gm.top_accent);
            write_variants(w, gm.h_variants);
            write_variants(w, gm.v_variants);
            write_variants(w, {});                 // scriptsVariants: empty
            write_assembly(w, gm.h_assembly);
            write_assembly(w, gm.v_assembly);
            for (int c = 0; c < 4; ++c) write_math_kern(w, gm.kerns.corner[c]);
        }
        // Glyph path (minor = 0). Empty glyphs emit just a u16 length of 0.
        PathBuilder pb = build_glyph_path(face, g);
        write_glyph_path(w, pb);
    }

    return std::move(w.buf);
}

}  // namespace

// =============================================================================
// Public entry points
// =============================================================================

namespace microtex {

std::vector<u8> otfMathTableBytes(const std::string& path, int index) {
    FT_Face face = open_face(path, index);
    struct FaceGuard {
        FT_Face f;
        ~FaceGuard() { if (f) FT_Done_Face(f); }
    } guard{face};
    return read_sfnt_table(face, TTAG_MATH);
}

std::vector<u8> otfToClmBytes(const std::string& path, int index) {
    return build_clm_bytes_impl(path, index);
}

}  // namespace microtex
