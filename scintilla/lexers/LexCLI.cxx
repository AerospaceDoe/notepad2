// Lexer for MSIL, CLI

#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "CharacterSet.h"
#include "LexerModule.h"

#ifdef SCI_NAMESPACE
using namespace Scintilla;
#endif

static inline bool IsCLIOp(int ch) {
	return ch == '{' || ch == '}' || ch == '[' || ch == ']' || ch == '(' || ch == ')' || ch == '=' || ch == '&'
		|| ch == ':' || ch == ',' || ch == '+' || ch == '-' || ch == '.';
}
static inline bool IsCLIWordChar(int ch) {
	return iswordchar(ch) || ch == '-' || ch == '/' || ch == '$';
}

#define MAX_WORD_LENGTH	31
static void ColouriseCLIDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, WordList *keywordLists[], Accessor &styler) {
	WordList &keywords = *keywordLists[0];
	WordList &keywords2	= *keywordLists[1];
	WordList &kwInstruction = *keywordLists[10];

	int state = initStyle;
	int ch = 0, chNext = styler[startPos];
	styler.StartAt(startPos);
	styler.StartSegment(startPos);
	Sci_PositionU endPos = startPos + length;
	if (endPos == (Sci_PositionU)styler.Length())
		++endPos;

	Sci_Position lineCurrent = styler.GetLine(startPos);
	char buf[MAX_WORD_LENGTH + 1] = {0};
	int wordLen = 0;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		int chPrev = ch;
		ch = chNext;
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
		case SCE_C_DIRECTIVE:
		case SCE_C_ASM_INSTRUCTION:
			if (!IsCLIWordChar(ch)) {
				styler.ColourTo(i - 1, state);
				state = SCE_L_DEFAULT;
			}
			break;
		case SCE_C_IDENTIFIER:
			if (!(iswordstart(ch) || ch == '$')) {
				buf[wordLen] = 0;
				state = SCE_C_DEFAULT;
				if (keywords.InList(buf)) {
					styler.ColourTo(i - 1, SCE_C_WORD);
				} else if (keywords2.InList(buf)) {
					styler.ColourTo(i - 1, SCE_C_WORD2);
				} else if (kwInstruction.InList(buf)) {
					state = SCE_C_ASM_INSTRUCTION;
				} else if (ch == ':' && chNext != ':') {
					styler.ColourTo(i - 1, SCE_C_LABEL);
				}
				wordLen = 0;
			} else if (wordLen < MAX_WORD_LENGTH) {
				buf[wordLen++] = (char)ch;
			}
			break;
		case SCE_C_STRING:
		case SCE_C_CHARACTER:
			if (atLineStart) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_DEFAULT;
			} else if (ch == '\\' && (chNext == '\\' || chNext == '\"')) {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if ((state == SCE_C_STRING && ch == '\"') || (state == SCE_C_CHARACTER && ch == '\'')) {
				styler.ColourTo(i, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		case SCE_C_COMMENTLINE:
			if (atLineStart) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_DEFAULT;
			}
			break;
		case SCE_C_COMMENT:
			if (ch == '*' && chNext == '/') {
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
				styler.ColourTo(i, state);
				state = SCE_C_DEFAULT;
				continue;
			}
			break;
		}

		if (state == SCE_C_DEFAULT) {
			if (ch == '/' && chNext == '/') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_COMMENTLINE;
			} else if (ch == '/' && chNext == '*') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_COMMENT;
				i++;
				ch = chNext;
				chNext = styler.SafeGetCharAt(i + 1);
			} else if (ch == '\"') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_STRING;
			} else if (ch == '\'') {
				styler.ColourTo(i - 1, state);
				state = SCE_C_CHARACTER;
			} else if (IsADigit(ch) || (ch == '.' && IsADigit(chNext))) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_NUMBER;
			} else if (ch == '.' && IsAlpha(chNext) && (IsCLIOp(chPrev) || IsASpace(chPrev))) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_DIRECTIVE;
			} else if (iswordstart(ch)) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_IDENTIFIER;
				buf[wordLen++] = (char)ch;
			} else if (IsCLIOp(ch)) {
				styler.ColourTo(i - 1, state);
				state = SCE_C_OPERATOR;
			}
		}
	}

	// Colourise remaining document
	styler.ColourTo(endPos - 1, state);
}

#define IsCommentLine(line)			IsLexCommentLine(line, styler, SCE_C_COMMENTLINE)
#define IsStreamCommentStyle(style)	(style == SCE_C_COMMENT)
static void FoldCLIDoc(Sci_PositionU startPos, Sci_Position length, int initStyle, WordList *[], Accessor &styler) {
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

	int chNext = styler[startPos];
	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;

	for (Sci_PositionU i = startPos; i < endPos; i++) {
		int ch = chNext;
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
		if (foldComment && IsStreamCommentStyle(style) && !IsCommentLine(lineCurrent)) {
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext) && !atEOL) {
				levelNext--;
			}
		}

		if (style == SCE_C_OPERATOR) {
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
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

LexerModule lmCLI(SCLEX_CLI, ColouriseCLIDoc, "cli", FoldCLIDoc);
