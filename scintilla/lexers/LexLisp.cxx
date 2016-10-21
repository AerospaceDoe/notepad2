// Lexer for Lisp.

#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "LexerModule.h"

static inline bool IsLispOp(int ch) {
	return ch == '(' || ch == ')' || ch == '[' || ch == ']'
		|| ch == '\'' || ch == '*' || ch == '?' || ch == '.'
		|| ch == '+' || ch == '-' || ch == '<' || ch == '>'
		|| ch == ':' || ch == '=' || ch == ','
		|| ch == '`' || ch == '#' || ch == '@' || ch == '&';
}

#define MAX_WORD_LENGTH	15
static void ColouriseLispDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, WordList *keywordLists[], Accessor &styler) {
	WordList &keywords = *keywordLists[0];

	int state = initStyle;
	int chNext = styler[startPos];
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	Sci_PositionU endPos = startPos + length;
	if (endPos == (Sci_PositionU)styler.Length())
		++endPos;

	Sci_Position lineCurrent = styler.GetLine(startPos);
	char buf[MAX_WORD_LENGTH + 1] = {0};
	int wordLen = 0;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		int ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);

		const bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');
		const bool atLineStart = i == (Sci_PositionU)styler.LineStart(lineCurrent);
		if (atEOL || i == endPos-1) {
			lineCurrent++;
		}

		switch (state) {
		case SCE_C_OPERATOR:
			styler.ColourTo(i - 1, state);
			state = SCE_C_DEFAULT;
			break;
		case SCE_C_NUMBER:
			if (!(iswordchar(ch) || ((ch == '+' || ch == '-') && IsADigit(chNext)))) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_IDENTIFIER:
			if (!(iswordchar(ch) || ch == '-')) {
				buf[wordLen] = 0;
				if (keywords.InList(buf)) {
					styler.ColourTo(i - 1, SCE_C_WORD);
				}
				state = SCE_C_DEFAULT;
				wordLen = 0;
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = (char)ch;
			}
			break;
		case SCE_C_CHARACTER:
			if (IsASpace(ch) || ch == ')') {
				styler.ColourTo(i-1, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_STRING:
			if (ch == '\\' && (chNext == '\\' || chNext == '\"')) {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if (ch == '\"') {
				styler.ColourTo(i, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		case SCE_C_COMMENTLINE:
			if (atLineStart) {
				styler.ColourTo(i-1, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_COMMENT:
			if (ch == '|' && chNext == '#') {
				i++;
				chNext = styler.SafeGetCharAt(i + 1);
				styler.ColourTo(i, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		}

		if (state == SCE_C_DEFAULT) {
			if (ch == '?' && chNext == '\\') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_CHARACTER;
			} else if (ch == ';') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_COMMENTLINE;
			} else if (ch == '\"') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_STRING;
			} else if (ch == '#' && chNext == '|') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_COMMENT;
				i++;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if (IsADigit(ch) || (ch == '.' && IsADigit(chNext))) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_NUMBER;
			} else if (iswordstart(ch)) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_IDENTIFIER;
				buf[wordLen++] = (char)ch;
			} else if (IsLispOp(ch)) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_OPERATOR;
			}
		}
	}

	// Colourise remaining document
	styler.ColourTo(endPos - 1, state);
}

#define IsCommentLine(line)			IsLexCommentLine(line, styler, SCE_C_COMMENTLINE)
#define IsStreamStyle(style)		(style == SCE_C_STRING)
#define IsStreamCommantStyle(style)	(style == SCE_C_COMMENT)
static void FoldListDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, WordList *[], Accessor &styler) {
	if (styler.GetPropertyInt("fold") == 0)
		return;
	const bool foldComment = styler.GetPropertyInt("fold.comment") != 0;
	const bool foldCompact = styler.GetPropertyInt("fold.compact", 1) != 0;

	Sci_PositionU endPos = startPos + length;
	int visibleChars = 0;
	Sci_Position lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	if (lineCurrent > 0)
		levelCurrent = styler.LevelAt(lineCurrent-1) >> 16;
	int levelNext = levelCurrent;

	char chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		char ch = chNext;
		chNext = styler.SafeGetCharAt(i + 1);
		int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(i + 1);
		bool atEOL = (ch == '\r' && chNext != '\n') || (ch == '\n');

		if (foldComment && atEOL && IsCommentLine(lineCurrent)) {
			if (!IsCommentLine(lineCurrent - 1) && IsCommentLine(lineCurrent + 1))
				levelNext++;
			else if (IsCommentLine(lineCurrent - 1) && !IsCommentLine(lineCurrent + 1))
				levelNext--;
		}
		if (foldComment && IsStreamCommantStyle(style)) {
			if (!IsStreamCommantStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommantStyle(styleNext) && !atEOL) {
				levelNext--;
			}
		}
		if (foldComment && IsStreamStyle(style)) {
			if (!IsStreamStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamStyle(styleNext) && !atEOL) {
				levelNext--;
			}
		}

		if (style == SCE_C_OPERATOR) {
			if (ch == '(')
				levelNext++;
			else if (ch == ')')
				levelNext--;
		}

		if (!isspacechar(ch))
			visibleChars++;

		if (atEOL || (i == endPos-1)) {
			int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (visibleChars == 0 && foldCompact)
				lev |= SC_FOLDLEVELWHITEFLAG;
			if (levelUse < levelNext)
				lev |= SC_FOLDLEVELHEADERFLAG;
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}
			lineCurrent++;
			levelCurrent = levelNext;
			visibleChars = 0;
		}
	}
}

LexerModule lmLisp(SCLEX_LISP, ColouriseLispDoc, "lisp", FoldListDoc);
