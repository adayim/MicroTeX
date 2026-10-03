#include "front/prelude.h"

namespace microtex::front {

// The engine's commands and environments that are written in LaTeX (they
// were string macros in the old parser's macro_def.cpp). Environments built
// in C++ (matrix, array, align, ...) are not here: the parser reads them.
// `\|` is LaTeX's \Vert (‖); the engine's symbol table has it as a single bar.
//
// Then the document commands a grob has no use for, which used to be
// rewritten in R: the preamble, title and cross-reference metadata, and
// alignment declarations, dropped with their arguments; skips and \hfill as
// fixed space (a grob has no glue to fill); \emph, \em, \newline and
// booktabs' rules as their nearest equivalents. (\caption is
// the parser's, which knows whether it is in a document.) article's
// abstract, and a bibliography as the list of [n] it sets, whose keys have
// nothing to point at; both are transparent, as their text is a document's
// (isBlockEnvironment()).
// document, table, figure and center have no code of their own, so a label
// lays their content out as if they were not there; a document sets a
// float, or center's lines, apart (lower.cpp).
std::string_view preludeSource() {
  static constexpr std::string_view source = R"TEX(
\newenvironment{pmatrix}{\left(\begin{matrix}}{\end{matrix}\right)}
\newenvironment{bmatrix}{\left[\begin{matrix}}{\end{matrix}\right]}
\newenvironment{Bmatrix}{\left\{\begin{matrix}}{\end{matrix}\right\}}
\newenvironment{vmatrix}{\left|\begin{matrix}}{\end{matrix}\right|}
\newenvironment{Vmatrix}{\left\|\begin{matrix}}{\end{matrix}\right\|}
\newenvironment{cases}{\left\{\begin{array}{@{}ll@{\,}}}{\end{array}\right.}
\newenvironment{rcases}{\left.\begin{array}{@{}ll@{\,}}}{\end{array}\right\}}
\newenvironment{dcases}{\left\{\begin{array}{@{}>{\displaystyle}l>{\displaystyle}l@{\,}}}{\end{array}\right.}
\newenvironment{subarray}[1]{\scriptstyle\begin{array}{@{}#1@{}}}{\end{array}}
\newenvironment{split}{\begin{array}{r@{\;}l}}{\end{array}}
\newenvironment{math}{\(}{\)}
\newenvironment{displaymath}{\[}{\]}
\newenvironment{equation}{\begin{align}}{\end{align}}
\newenvironment{equation*}{\begin{align*}}{\end{align*}}
\newcommand{\operatorname}[1]{\mathop{\mathrm{#1}}\nolimits }
\newcommand{\substack}[1]{{\scriptstyle\begin{array}{@{}c@{}}#1\end{array}}}
\newcommand{\shortintertext}[1]{\intertext{#1}}
\newcommand{\mspace}[1]{\hspace{#1}}
\newcommand{\leftroot}[1]{}
\newcommand{\uproot}[1]{}
\newcommand{\dfrac}[2]{\genfrac{}{}{1}{}{#1}{#2}}
\newcommand{\tfrac}[2]{\genfrac{}{}{1}{1}{#1}{#2}}
\newcommand{\dbinom}[2]{\genfrac{(}{)}{0pt}{}{#1}{#2}}
\newcommand{\tbinom}[2]{\genfrac{(}{)}{0pt}{1}{#1}{#2}}
\newcommand{\pmod}[1]{\pod{\mathrm{mod}\mkern6mu #1}}
\newcommand{\mod}[1]{\mathchoice{\mkern18mu}{\mkern12mu}{\mkern12mu}{\mkern12mu}\mathrm{mod}\,\,#1}
\newcommand{\pod}[1]{\mathchoice{\mkern18mu}{\mkern8mu}{\mkern8mu}{\mkern8mu}(#1)}
\newcommand{\boxed}[1]{\fbox{\displaystyle #1}}
\newcommand{\spbreve}{^{\makeatletter\sp@breve\makeatother}}
\newcommand{\spcheck}{^{\vee}}
\newcommand{\spdot}{^{\displaystyle.}}
\newcommand{\d}[1]{\underaccent{\dot}{#1}}
\newcommand{\b}[1]{\underaccent{\bar}{#1}}
\newcommand{\|}{\Vert}
\newcommand{\Bra}[1]{\left\langle{#1}\right\vert}
\newcommand{\Ket}[1]{\left\vert{#1}\right\rangle}
\newcommand{\textsuperscript}[1]{\ensuremath{{}^{\text{#1}}}}
\newcommand{\textsubscript}[1]{\ensuremath{{}_{\text{#1}}}}
\newcommand{\overbrack}[1]{\overbracket{#1}}
\newcommand{\underbrack}[1]{\underbracket{#1}}
\newcommand{\degree}{\ensuremath{^\circ}}
\newcommand{\with}{\mathbin{\&}}
\newcommand{\parr}{\mathbin{\rotatebox[origin=c]{180}{\&}}}
\newcommand{\documentclass}[2][]{}
\newcommand{\usepackage}[2][]{}
\newcommand{\bibliographystyle}[1]{}
\newenvironment{document}{}{}
\newenvironment{table}[1][]{}{}
\newenvironment{table*}[1][]{}{}
\newenvironment{figure}[1][]{}{}
\newenvironment{figure*}[1][]{}{}
\newenvironment{center}{}{}
\newenvironment{flushleft}{}{}
\newenvironment{flushright}{}{}
\newcommand{\maketitle}{}
\newcommand{\title}[1]{}
\newcommand{\author}[1]{}
\newcommand{\DeclareGraphicsExtensions}[1]{}
\newcommand{\flushleft}{}
\newcommand{\arraybackslash}{}
\newcommand{\extracolsep}[1]{}
\newcommand{\flushright}{}
\newcommand{\relax}{}
\newcommand{\smallskip}{\vspace{0.25em}}
\newcommand{\medskip}{\vspace{0.5em}}
\newcommand{\bigskip}{\vspace{1em}}
\newcommand{\hfill}{\quad}
\newcommand{\vfill}{\vspace{1em}}
\newcommand{\emph}[1]{\textit{#1}}
\newcommand{\textscale}[2]{{\relscale{#1}#2}}
\newcommand{\em}{\it}
\newenvironment{abstract}{\small\begin{center}\textbf{Abstract}\end{center}}{\par}
\newenvironment{proof}[1][Proof]{\par\textit{#1.}\ }{\hfill\ensuremath{\square}\par}
\newenvironment{thebibliography}[1]{\section*{References}\begin{enumerate}[{[}\arabic*{]}]}{\end{enumerate}}
\newcommand{\bibitem}[2][]{\item}
\newcommand{\newblock}{}
\newcommand{\newline}{\\}
\newcommand{\bfseries}{\bf}
\newcommand{\itshape}{\it}
\newcommand{\ttfamily}{\tt}
\newcommand{\sffamily}{\sf}
\newcommand{\rmfamily}{\rm}
\newcommand{\normalfont}{\rm}
\newcommand{\upshape}{\rm}
\newcommand{\mdseries}{\rm}
\newcommand{\selectfont}{}
\newcommand{\makecell}[2][c]{\begin{tabular}{#1}#2\end{tabular}}
\newcommand{\thead}[2][c]{\makecell[#1]{\bfseries #2}}
\newcommand{\hhline}[1]{\hline}
\newcommand{\tnote}[1]{\textsuperscript{#1}}
\newenvironment{threeparttable}{}{}
\newenvironment{tablenotes}[1][]{\small}{}
\newcommand{\tinytableDefineColor}[3]{\definecolor{#1}{#2}{#3}}
\newcommand{\toprule}{\thickhline}
\newcommand{\bottomrule}{\thickhline}
\newcommand{\midrule}{\hline}
\newcommand{\sfrac}[2]{\scalebox{.8}{\raisebox{.5ex}{\raisebox{.45ex}{\numstyle{#1}}\kern-.4ex\nokern\mathslash\nokern\kern-.4ex\raisebox{-.45ex}{\dnomstyle{#2}}}}}
)TEX";
  return source;
}

// Names that LaTeX does not define, or that documents often define for
// themselves (KaTeX has them, as aliases): a document that makes its own
// \R or \set takes the place of these, and no \newcommand complains.
std::string_view preludeSoftSource() {
  static constexpr std::string_view source = R"TEX(
\newcommand{\textsection}{\S}
\newcommand{\textparagraph}{\P}
\newcommand{\textcopyright}{\copyright}
\newcommand{\texttrademark}{\ensuremath{{}^{\mathrm{TM}}}}
\newcommand{\textquotedbl}{"}
\newcommand{\@firstoftwo}[2]{#1}
\newcommand{\@secondoftwo}[2]{#2}
\newcommand{\ldots}{\ensuremath{\mathinner{\mathpunct{.}\mathpunct{.}\mathpunct{.}}}}
\newcommand{\cdots}{\ensuremath{\mathinner{\mathpunct{\cdot}\mathpunct{\cdot}\mathpunct{\cdot}}}}
\newcommand{\arraystretch}{1}
\newcommand{\strut}{\rule[-0.36em]{0pt}{1.2em}}
\newcommand{\ordinarycolon}{:}
\newcommand{\angln}{{\angl n}}
\newcommand{\overgroup}[1]{\overparen{#1}}
\newcommand{\undergroup}[1]{\underparen{#1}}
\newcommand{\alef}{\aleph}
\newcommand{\alefsym}{\aleph}
\newcommand{\Dagger}{\ddagger}
\newcommand{\thetasym}{\vartheta}
\newcommand{\weierp}{\wp}
\newcommand{\Complex}{\mathbb{C}}
\newcommand{\cnums}{\mathbb{C}}
\newcommand{\Reals}{\mathbb{R}}
\newcommand{\reals}{\mathbb{R}}
\newcommand{\R}{\mathbb{R}}
\newcommand{\natnums}{\mathbb{N}}
\newcommand{\N}{\mathbb{N}}
\newcommand{\Z}{\mathbb{Z}}
\newcommand{\Bbb}[1]{\mathbb{#1}}
\newcommand{\real}{\Re}
\newcommand{\image}{\Im}
\newcommand{\empty}{\emptyset}
\newcommand{\exist}{\exists}
\newcommand{\isin}{\in}
\newcommand{\infin}{\infty}
\newcommand{\sub}{\subset}
\newcommand{\sube}{\subseteq}
\newcommand{\supe}{\supseteq}
\newcommand{\sdot}{\cdot}
\newcommand{\bull}{\bullet}
\newcommand{\plusmn}{\pm}
\newcommand{\clubs}{\clubsuit}
\newcommand{\diamonds}{\diamondsuit}
\newcommand{\hearts}{\heartsuit}
\newcommand{\spades}{\spadesuit}
\newcommand{\larr}{\leftarrow}
\newcommand{\rarr}{\rightarrow}
\newcommand{\uarr}{\uparrow}
\newcommand{\darr}{\downarrow}
\newcommand{\harr}{\leftrightarrow}
\newcommand{\lrarr}{\leftrightarrow}
\newcommand{\Larr}{\Leftarrow}
\newcommand{\lArr}{\Leftarrow}
\newcommand{\Rarr}{\Rightarrow}
\newcommand{\rArr}{\Rightarrow}
\newcommand{\Uarr}{\Uparrow}
\newcommand{\uArr}{\Uparrow}
\newcommand{\Darr}{\Downarrow}
\newcommand{\dArr}{\Downarrow}
\newcommand{\Harr}{\Leftrightarrow}
\newcommand{\hArr}{\Leftrightarrow}
\newcommand{\Lrarr}{\Leftrightarrow}
\newcommand{\lrArr}{\Leftrightarrow}
\newcommand{\>}{\:}
\newcommand{\enspace}{\hspace{0.5em}}
\newcommand{\nobreakspace}{~}
\newcommand{\space}{\ }
\newcommand{\allowbreak}{}
\newcommand{\nobreak}{}
\newcommand{\long}{}
\newcommand{\hbox}[1]{\mbox{#1}}
\newcommand{\htmlClass}[2]{#2}
\newcommand{\htmlId}[2]{#2}
\newcommand{\htmlData}[2]{#2}
\newcommand{\htmlStyle}[2]{#2}
\newcommand{\KaTeX}{\text{KaTeX}}
\newcommand{\argmax}{\operatorname*{arg\,max}}
\newcommand{\argmin}{\operatorname*{arg\,min}}
\newcommand{\plim}{\operatorname*{plim}}
\newcommand{\operatornamewithlimits}[1]{\operatorname*{#1}}
\newcommand{\arctg}{\operatorname{arctg}}
\newcommand{\arcctg}{\operatorname{arcctg}}
\newcommand{\cosec}{\operatorname{cosec}}
\newcommand{\cotg}{\operatorname{cotg}}
\newcommand{\ctg}{\operatorname{ctg}}
\newcommand{\cth}{\operatorname{cth}}
\newcommand{\tg}{\operatorname{tg}}
\newcommand{\th}{\operatorname{th}}
\newcommand{\sh}{\operatorname{sh}}
\newcommand{\ch}{\operatorname{ch}}
\newcommand{\bigm}[1]{\mathrel{\big#1}}
\newcommand{\Bigm}[1]{\mathrel{\Big#1}}
\newcommand{\biggm}[1]{\mathrel{\bigg#1}}
\newcommand{\Biggm}[1]{\mathrel{\Bigg#1}}
\newcommand{\intop}{\mathop{\int}\nolimits}
\newcommand{\smallint}{\mathop{\textstyle\int}\nolimits}
\newcommand{\Box}{\square}
\newcommand{\bigcirc}{\mathord{\lgwhtcircle}}
\newcommand{\centerdot}{\cdot}
\newcommand{\ldotp}{\mathpunct{.}}
\newcommand{\And}{\mathbin{\&}}
\newcommand{\mathellipsis}{\ldots}
\newcommand{\dotsm}{\cdots}
\newcommand{\dblcolon}{\mathrel{::}}
\newcommand{\vcentcolon}{\mathrel{:}}
\newcommand{\Eqcolon}{\mathrel{=::}}
\newcommand{\models}{\mathrel{\mathrel{\vert}\joinrel=}}
\newcommand{\Eqqcolon}{\mathrel{=::}}
\newcommand{\Colonapprox}{\mathrel{::\approx}}
\newcommand{\Colonsim}{\mathrel{::\sim}}
\newcommand{\thickapprox}{\approx}
\newcommand{\thicksim}{\sim}
\newcommand{\varpropto}{\propto}
\newcommand{\shortmid}{\mid}
\newcommand{\shortparallel}{\parallel}
\newcommand{\smallfrown}{\frown}
\newcommand{\smallsmile}{\smile}
\newcommand{\nshortmid}{\nmid}
\newcommand{\nshortparallel}{\nparallel}
\newcommand{\ngeqq}{\not\geqq}
\newcommand{\nleqq}{\not\leqq}
\newcommand{\nsubseteqq}{\not\subseteqq}
\newcommand{\nsupseteqq}{\not\supseteqq}
\newcommand{\lvertneqq}{\lneqq}
\newcommand{\gvertneqq}{\gneqq}
\newcommand{\varsubsetneqq}{\subsetneqq}
\newcommand{\varsupsetneq}{\supsetneq}
\newcommand{\varsupsetneqq}{\supsetneqq}
\newcommand{\mathreflectbox}[1]{\reflectbox{$#1$}}
\newcommand{\bra}[1]{\left\langle #1\right|}
\newcommand{\ket}[1]{\left| #1\right\rangle}
\newcommand{\braket}[1]{\left\langle #1\right\rangle}
\newcommand{\set}[1]{\left\{ #1\right\}}
\newcommand{\Set}[1]{\left\{ #1\right\}}
\newcommand{\VERT}{\mathrel{|}}
\newcommand{\textmd}[1]{\textnormal{#1}}
\newcommand{\textup}[1]{\textnormal{#1}}
\newcommand{\textasciitilde}{\char126{}}
\newcommand{\textasciicircum}{\char94{}}
\newcommand{\textbackslash}{\char92{}}
\newcommand{\textbar}{\char124{}}
\newcommand{\textbraceleft}{\char123{}}
\newcommand{\textbraceright}{\char125{}}
\newcommand{\textdollar}{\char36{}}
\newcommand{\textless}{\char60{}}
\newcommand{\textgreater}{\char62{}}
\newcommand{\textunderscore}{\char95{}}
\newcommand{\textbardbl}{‖}
\newcommand{\textdagger}{†}
\newcommand{\textdaggerdbl}{‡}
\newcommand{\textdegree}{°}
\newcommand{\textellipsis}{…}
\newcommand{\textemdash}{—}
\newcommand{\textendash}{–}
\newcommand{\textquotedblleft}{“}
\newcommand{\textquotedblright}{”}
\newcommand{\textquoteleft}{‘}
\newcommand{\textquoteright}{’}
\newcommand{\textregistered}{®}
\newcommand{\textsterling}{£}
\newcommand{\lq}{‘}
\newcommand{\rq}{’}
\newcommand{\AE}{Æ}
\newcommand{\ae}{æ}
\newcommand{\OE}{Œ}
\newcommand{\oe}{œ}
\newcommand{\O}{Ø}
\newcommand{\o}{ø}
\newcommand{\ss}{ß}
\newcommand{\i}{ı}
\newcommand{\j}{ȷ}
\newcommand{\S}{§}
\newcommand{\sect}{§}
\newcommand{\P}{¶}
\newcommand{\diagdown}{\mathord{\bcancel{\phantom{x}}}}
\newcommand{\diagup}{\mathord{\cancel{\phantom{x}}}}
\newcommand{\circledS}{\mathord{Ⓢ}}
\newcommand{\minuso}{\mathbin{⦵}}
)TEX";
  return source;
}

}  // namespace microtex::front
