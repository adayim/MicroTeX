#include "macro/macro.h"
#include "macro/macro_accent.h"
#include "macro/macro_boxes.h"
#include "macro/macro_colors.h"
#include "macro/macro_delims.h"
#include "macro/macro_env.h"
#include "macro/macro_fonts.h"
#include "macro/macro_frac.h"
#include "macro/macro_misc.h"
#include "macro/macro_scripts.h"
#include "macro/macro_sizes.h"
#include "macro/macro_space.h"
#include "macro/macro_styles.h"
#include "macro/macro_types.h"

using namespace std;
using namespace microtex;

#define mac3(argc, name, code) defMac(code, argc, name)

#define mac4(argc, posOpts, name, code) defMac(code, argc, posOpts, name)

namespace microtex {

// Handlers that read their arguments through CommandArgs (macro_args.h).
inline auto defMac(const char* code, int argc, int posOpts, CommandDelegate del) {
  return std::make_pair(code, new CommandMacro(argc, posOpts, del));
}

inline auto defMac(const char* code, int argc, CommandDelegate del) {
  return std::make_pair(code, new CommandMacro(argc, del));
}

}  // namespace microtex

map<string, MacroInfo*> MacroInfo::_commands{
#define mac mac4
  mac(2, 1, macro_rule, "rule"),
  mac(1, 1, macro_includegraphics, "includegraphics"),
  mac(2, 1, macro_cfrac, "cfrac"),
  // region arrows
  mac(1, 1, macro_xarrow, "xleftarrow"),
  mac(1, 1, macro_xarrow, "xrightarrow"),
  mac(1, 1, macro_xarrow, "xleftrightarrow"),
  mac(1, 1, macro_xarrow, "xRightarrow"),
  mac(1, 1, macro_xarrow, "xLeftarrow"),
  mac(1, 1, macro_xarrow, "xLeftrightarrow"),
  mac(1, 1, macro_xarrow, "xhookleftarrow"),
  mac(1, 1, macro_xarrow, "xhookrightarrow"),
  mac(1, 1, macro_xarrow, "xmapsto"),
  mac(1, 1, macro_xarrow, "xrightharpoondown"),
  mac(1, 1, macro_xarrow, "xrightharpoonup"),
  mac(1, 1, macro_xarrow, "xleftharpoondown"),
  mac(1, 1, macro_xarrow, "xleftharpoonup"),
  mac(1, 1, macro_xarrow, "xrightleftharpoons"),
  mac(1, 1, macro_xarrow, "xleftrightharpoons"),
  // endregion
  mac(1, 1, macro_sqrt, "sqrt"),
  mac(1, 1, macro_smash, "smash"),
  mac(1, 1, macro_hdotsfor, "hdotsfor"),
  mac(2, 1, macro_stackbin, "stackbin"),
  mac(2, 1, macro_stackrel, "stackrel"),
  mac(2, 1, macro_rotatebox, "rotatebox"),
  mac(2, 2, macro_scalebox, "scalebox"),
  mac(2, 2, macro_raisebox, "raisebox"),
  mac(1, 1, macro_mathversion, "mathversion"),
#undef mac
#define mac mac3
  mac(1, macro_fatalIfCmdConflict, "fatalIfCmdConflict"),
  mac(1, macro_breakEverywhere, "breakEverywhere"),
  // region array environments
  mac(1, macro_smallmatrixATATenv, "smallmatrix@@env"),
  mac(1, macro_matrixATATenv, "matrix@@env"),
  mac(2, macro_arrayATATenv, "array@@env"),
  mac(2, macro_arrayATATenv, "tabular@@env"),
  mac(2, macro_alignATATenv, "align@@env"),
  mac(2, macro_alignedATATenv, "aligned@@env"),
  mac(2, macro_flalignATATenv, "flalign@@env"),
  mac(2, macro_alignatATATenv, "alignat@@env"),
  mac(2, macro_alignedatATATenv, "alignedat@@env"),
  mac(2, macro_multlineATATenv, "multline@@env"),
  mac(2, macro_gatherATATenv, "gather@@env"),
  mac(2, macro_gatheredATATenv, "gathered@@env"),
  mac(1, macro_itemizeATATenv, "itemize@@env"),
  mac(1, macro_enumerateATATenv, "enumerate@@env"),
  mac(3, macro_multicolumn, "multicolumn"),
  mac(0, macro_hline, "hline"),
  mac(0, macro_thickhline, "thickhline"),
  mac(3, macro_specialrule, "specialrule"),
  mac(1, macro_cline, "cline"),
  mac(3, macro_multirow, "multirow"),
  mac(1, macro_rowcolor, "rowcolor"),
  mac(1, macro_columnbg, "columncolor"),
  mac(1, macro_arrayrulecolor, "arrayrulecolor"),
  mac(2, macro_newcolumntype, "newcolumntype"),
  mac(1, macro_cellcolor, "cellcolor"),
  mac(1, macro_shoveright, "shoveright"),
  mac(1, macro_shoveleft, "shoveleft"),
  // endregion
  // region sizes
  mac(4, macro_declaremathsizes, "DeclareMathSizes"),
  mac(1, macro_magnification, "magnification"),
  mac(1, macro_big, "big"),
  mac(1, macro_Big, "Big"),
  mac(1, macro_bigg, "bigg"),
  mac(1, macro_Bigg, "Bigg"),
  mac(1, macro_bigl, "bigl"),
  mac(1, macro_Bigl, "Bigl"),
  mac(1, macro_biggl, "biggl"),
  mac(1, macro_Biggl, "Biggl"),
  mac(1, macro_bigr, "bigr"),
  mac(1, macro_Bigr, "Bigr"),
  mac(1, macro_biggr, "biggr"),
  mac(1, macro_Biggr, "Biggr"),
  // endregion
  // region scripts & frac
  mac(2, macro_frac, "frac"),
  mac(6, macro_genfrac, "genfrac"),
  mac(3, macro_sideset, "sideset"),
  mac(3, macro_prescript, "prescript"),
  // endregion
  // region under & over delimiters
  mac(1, macro_overdelim, "overrightarrow"),
  mac(1, macro_overdelim, "overleftarrow"),
  mac(1, macro_overdelim, "overleftrightarrow"),
  mac(1, macro_underdelim, "underrightarrow"),
  mac(1, macro_underdelim, "underleftarrow"),
  mac(1, macro_underdelim, "underleftrightarrow"),
  mac(1, macro_overdelim, "overbrace"),
  mac(1, macro_overdelim, "overbracket"),
  mac(1, macro_overdelim, "overparen"),
  mac(1, macro_underdelim, "underbrace"),
  mac(1, macro_underdelim, "underbracket"),
  mac(1, macro_underdelim, "underparen"),
  mac(1, macro_overline, "overline"),
  mac(1, macro_underline, "underline"),
  mac(1, macro_Braket, "Braket"),
  mac(1, macro_Set, "Set"),
  mac(1, macro_middle, "middle"),
  // endregion
  // region atom types
  mac(1, macro_mathop, "mathop"),
  mac(1, macro_mathpunct, "mathpunct"),
  mac(1, macro_mathord, "mathord"),
  mac(1, macro_mathrel, "mathrel"),
  mac(1, macro_mathinner, "mathinner"),
  mac(1, macro_mathbin, "mathbin"),
  mac(1, macro_mathopen, "mathopen"),
  mac(1, macro_mathclose, "mathclose"),
  // endregion
  // region math and text styles
  mac(1, macro_oldstylenums, "oldstylenums"),
  mac(1, macro_mathfont, "mathnormal"),
  mac(1, macro_mathfont, "mathrm"),
  mac(1, macro_mathfont, "mathbf"),
  mac(1, macro_mathfont, "mathit"),
  mac(1, macro_mathfont, "mathcal"),
  mac(1, macro_mathfont, "mathscr"),
  mac(1, macro_mathfont, "mathfrak"),
  mac(1, macro_mathfont, "mathbb"),
  mac(1, macro_mathfont, "mathsf"),
  mac(1, macro_mathfont, "mathtt"),
  mac(1, macro_mathfont, "mathbfit"),
  mac(1, macro_mathfont, "mathbfcal"),
  mac(1, macro_mathfont, "mathbffrak"),
  mac(1, macro_mathfont, "mathsfbf"),
  mac(1, macro_mathfont, "mathbfsf"),
  mac(1, macro_mathfont, "mathsfit"),
  mac(1, macro_mathfont, "mathsfbfit"),
  mac(1, macro_mathfont, "mathbfsfit"),
  mac(1, macro_Bbb, "Bbb"),
  mac(1, macro_mathds, "mathds"),
  mac(1, macro_bold, "bold"),
  mac(1, macro_bold, "boldsymbol"),
  // \bm (from the bm package) and \pmb (from amsbsy) are the canonical
  // bold-math commands in modern LaTeX. Both alias to the same
  // bold-font switch as \boldsymbol so real-world source compiles
  // without needing user-side rewrites. The visual output is identical
  // to \mathbf for the glyphs we ship; that's a closer match to LaTeX
  // semantics than the previous behaviour of leaving them undefined.
  mac(1, macro_bold, "bm"),
  mac(1, macro_bold, "pmb"),
  // endregion
  // region nested styles
  mac(1, macro_text, "mbox"),
  mac(1, macro_text, "text"),
  mac(1, macro_intertext, "intertext"),
  mac(1, macro_textit, "textit"),
  mac(1, macro_textbf, "textbf"),
  mac(1, macro_textsf, "textsf"),
  mac(1, macro_texttt, "texttt"),
  mac(1, macro_textrm, "textrm"),
  // endregion
  // region text accents
  mac(1, macro_accentbiss, "^"),
  mac(1, macro_accentbiss, "\'"),
  mac(1, macro_accentbiss, "\""),
  mac(1, macro_accentbiss, "`"),
  mac(1, macro_accentbiss, "="),
  mac(1, macro_accentbiss, "."),
  mac(1, macro_accentbiss, "~"),
  mac(1, macro_accentbiss, "t"),
  mac(1, macro_accentbiss, "u"),
  mac(1, macro_accentbiss, "v"),
  mac(1, macro_accentbiss, "r"),
  // endregion
  // region math accents
  mac(1, macro_accents, "not"),
  mac(1, macro_accents, "hat"),
  mac(1, macro_accents, "widehat"),
  mac(1, macro_accents, "check"),
  mac(1, macro_accents, "tilde"),
  mac(1, macro_accents, "widetilde"),
  mac(1, macro_accents, "acute"),
  mac(1, macro_accents, "grave"),
  mac(1, macro_accents, "dot"),
  mac(1, macro_accents, "ddot"),
  mac(1, macro_accents, "dddot"),
  mac(1, macro_accents, "ddddot"),
  mac(1, macro_accents, "breve"),
  mac(1, macro_accents, "bar"),
  mac(1, macro_accents, "vec"),
  mac(1, macro_accents, "mathring"),
  mac(2, macro_accentset, "accentset"),  // fake accents
  mac(2, macro_overset, "overset"),
  mac(2, macro_underset, "underset"),
  mac(2, macro_underaccent, "underaccent"),
  mac(1, macro_undertilde, "undertilde"),
  // endregion
  // region microtex styles
  mac(1, macro_everymath, "everymath"),
  mac(1, macro_atexstyle, "dnomstyle"),
  mac(1, macro_atexstyle, "numstyle"),
  mac(1, macro_atexstyle, "substyle"),
  mac(1, macro_atexstyle, "supstyle"),
  // endregion
  // region colors
  mac(3, macro_definecolor, "definecolor"),
  mac(2, macro_fgcolor, "fgcolor"),
  mac(2, macro_bgcolor, "bgcolor"),
  mac(2, macro_textcolor, "textcolor"),
  mac(2, macro_colorbox, "colorbox"),
  mac(3, macro_fcolorbox, "fcolorbox"),
  // endregion
  // region spaces
  mac(0, macro_muskips, ","),
  mac(0, macro_muskips, ":"),
  mac(0, macro_muskips, ";"),
  mac(0, macro_muskips, "thinspace"),
  mac(0, macro_muskips, "medspace"),
  mac(0, macro_muskips, "thickspace"),
  mac(0, macro_muskips, "!"),
  mac(0, macro_muskips, "negthinspace"),
  mac(0, macro_muskips, "negmedspace"),
  mac(0, macro_muskips, "negthickspace"),
  mac(0, macro_quad, "quad"),
  // endregion
  // region boxes
  mac(1, macro_reflectbox, "reflectbox"),
  mac(3, macro_resizebox, "resizebox"),
  mac(1, macro_shadowbox, "shadowbox"),
  mac(1, macro_ovalbox, "ovalbox"),
  mac(1, macro_cornersize, "cornersize"),
  mac(1, macro_doublebox, "doublebox"),
  mac(1, macro_fbox, "fbox"),
  mac(1, macro_fbox, "boxed"),
  // endregion
  // region clr lap
  mac(1, macro_clrlap, "llap"),
  mac(1, macro_clrlap, "rlap"),
  mac(1, macro_clrlap, "clap"),
  mac(1, macro_mathclrlap, "mathllap"),
  mac(1, macro_mathclrlap, "mathrlap"),
  mac(1, macro_mathclrlap, "mathclap"),
  // endregion
  // region limits
  // endregion
  mac(1, macro_romannumeral, "roman"),
  mac(1, macro_romannumeral, "Roman"),
  mac(0, macro_surd, "surd"),
  mac(0, macro_lmoustache, "lmoustache"),
  mac(0, macro_rmoustache, "rmoustache"),
  mac(0, macro_breakmark, "-"),
  mac(1, macro_st, "st"),
  mac(2, macro_longdiv, "longdiv"),
  mac(1, macro_cancel, "cancel"),
  mac(1, macro_bcancel, "bcancel"),
  mac(1, macro_xcancel, "xcancel"),
  mac(1, macro_sout, "sout"),
  mac(6, macro_zstack, "stackinset"),
  mac(0, macro_nbsp, "nbsp"),
  mac(1, macro_sqrt, "sqrtsign"),
  mac(0, macro_joinrel, "joinrel"),
  mac(1, macro_hvspace, "hspace"),
  mac(1, macro_hvspace, "vspace"),
  mac(0, macro_underscore, "underscore"),
  mac(2, macro_binom, "binom"),
  mac(1, macro_phantom, "phantom"),
  mac(1, macro_hphantom, "hphantom"),
  mac(1, macro_vphantom, "vphantom"),
  mac(0, macro_spATbreve, "sp@breve"),
  mac(0, macro_nokern, "nokern"),
};

namespace {

// The registry above is a static-duration container holding raw `new`ed
// pointers. At process exit the container is destroyed but its values are
// not, so a leak checker reports "definitely lost" records pointing at
// defMac() static initialisation. Hosts that never call MicroTeX::release()
// -- anything embedding the library and relying on process teardown --
// leak all of it.
//
// This object is defined last in this translation unit, so it is
// constructed last and destroyed *first*, ahead of _commands above. That is
// the only point at which _free_() can still run against a live container.
// _free_() clears it as it goes, so an explicit MicroTeX::release()
// beforehand makes this a no-op rather than a double free.
struct MacroRegistryCleanup {
  ~MacroRegistryCleanup() {
    MacroInfo::_free_();
  }
};

const MacroRegistryCleanup _macro_registry_cleanup;

}  // namespace
