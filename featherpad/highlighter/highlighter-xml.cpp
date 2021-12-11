/*
 * Copyright (C) Pedram Pourang (aka Tsu Jan) 2021 <tsujan2000@gmail.com>
 *
 * FeatherPad is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FeatherPad is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * @license GPL-3.0+ <https://spdx.org/licenses/GPL-3.0+.html>
 */

#include "highlighter.h"

namespace FeatherPad {

// The following three methods are only called for quote, comment and value signs,
// i.e., ", ', <, >, <!-- and --> (plus "&lt;", as an exception).
// "start" means that nothing interesting comes before it. An appropriate value
// for it can save lots of CPU time in some cases.

bool Highlighter::isXmlQuoted (const QString &text, const int index, const int start) const
{
    if (start < 0 || index < start) return false;
    QTextCharFormat fi = format (index);
    if (fi == quoteFormat || fi == altQuoteFormat
        || fi == errorFormat // only when this function is called for "<!--" or "<"
        || fi == regexFormat) // only with "&lt;", as an exception
    {
        return true;
    }

    int pos = start - 1;
    bool res = false;
    int N = 0;
    QRegularExpression quoteExpression;
    if (pos == -1)
    {
        int prevState = previousBlockState();
        if (prevState != doubleQuoteState
            && prevState != singleQuoteState)
        {
            quoteExpression = mixedQuoteMark;
        }
        else
        {
            N = 1;
            res = true;
            if (prevState == doubleQuoteState)
                quoteExpression = quoteMark;
            else
                quoteExpression = singleQuoteMark;
        }
    }
    else
        quoteExpression = mixedQuoteMark;

    int nxtPos;
    while ((nxtPos = text.indexOf (quoteExpression, pos + 1)) >= 0)
    {
        if (N % 2 == 0)
        {
            /* WARNING: An infinite loop will be prevented only if the methods isXmlQuoted(),
                        isXxmlComment() and isXmlValue() call each other at successively
                        smaller positions. */
            if (index <= nxtPos) // can never be equal here
                return false;
            if (isXxmlComment (text, nxtPos, start) || isXmlValue (text, nxtPos, start))
            {
                pos = nxtPos;
                continue;
            }
        }

        ++N;

        if (index < nxtPos)
        {
            if (N % 2 == 0) res = true;
            else res = false;
            break;
        }

        if (N % 2 == 0) res = false;
        else res = true;

        if (N % 2 != 0)
        {
            if (text.at (nxtPos) == '\"')
                quoteExpression = quoteMark;
            else
                quoteExpression = singleQuoteMark;
        }
        else
            quoteExpression = mixedQuoteMark;
        pos = nxtPos;
    }

    return res;
}
/*************************/
bool Highlighter::isXxmlComment (const QString &text, const int index, const int start) const
{
    if (start < 0 || index < start) return false;
    if (format (index) == commentFormat) return true;

    bool res = false;
    int pos = start - 1;
    int N = 0;
    QRegularExpressionMatch match;
    QRegularExpression commentExpression;
    if (pos >= 0 || previousBlockState() != commentState)
    {
        commentExpression = commentStartExpression;
    }
    else
    {
        N = 1;
        res = true;
        commentExpression = commentEndExpression;
    }

    while ((pos = text.indexOf (commentExpression, pos + 1, &match)) >= 0)
    {
        if (N % 2 == 0)
        { // "<!--"
            if (index < pos) return false;
            /* can be equal when this function is called for "<",
               which means we're inside a value and a comment is allowed */
            if (index == pos) return true;
            /* comments can only be inside values
               (which means they can't be quoted) */
            if (!isXmlValue (text, pos, start))
                continue;
        }

        ++N;

        if (index < pos + (N % 2 == 0 ? match.capturedLength() : 0))
        {
            if (N % 2 == 0) res = true;
            else res = false;
            break;
        }

        if (N % 2 != 0)
        {
            commentExpression = commentEndExpression;
            res = true;
        }
        else
        {
            commentExpression = commentStartExpression;
            res = false;
        }
    }

    return res;
}
/*************************/
bool Highlighter::isXmlValue (const QString &text, const int index, const int start) const
{
    if (start < 0 || index < start) return false;
    if (format (index) == neutralFormat) return true;

    bool res = false;
    int pos = start - 1;
    int N = 0;
    QRegularExpressionMatch match;
    QRegularExpression xmlSign;
    if (pos >= 0
        || (previousBlockState() != xmlValueState
            && previousBlockState() != commentState))
    {
        xmlSign = xmlGt;
    }
    else
    {
        N = 1;
        res = true;
        xmlSign = xmlLt;
    }

    int newStart = start;
    while ((pos = text.indexOf (xmlSign, pos + 1, &match)) >= 0)
    {
        if (N % 2 == 0)
        { // ">"
            if (index <= pos) // can never be equal here
                return false;
            /* no need to check for a comment because
               comments can start only after this position */
            if (isXmlQuoted (text, pos, newStart))
                continue;
        }
        else
        { // "<"
            if (index <= pos) // can be equal when this function is called for "<!--"
                return true;
            if (isXxmlComment (text, pos, newStart)) // a comment can be inside this value
                continue;
        }

        ++N;

        if (index < pos + (N % 2 == 0 ? match.capturedLength() : 0))
        {
            if (N % 2 == 0) res = true;
            else res = false;
            break;
        }

        if (N % 2 != 0)
        {
            xmlSign = xmlLt;
            res = true;
        }
        else
        {
            newStart = pos + 1; // nothing interesting before this position
            xmlSign = xmlGt;
            res = false;
        }
    }

    return res;
}
/*************************/
// Starting with ">" and ending with "<" (or "&lt;", as an exception).
void Highlighter::xmlValues (const QString &text)
{
    int prevState = previousBlockState();
    int index = 0;
    QRegularExpressionMatch startMatch;
    QRegularExpressionMatch endMatch;

    if (prevState != xmlValueState && prevState != commentState)
    {
        index = text.indexOf (xmlGt, index, &startMatch);
        /* skip quotes (comments can start only after this position) */
        while (isXmlQuoted (text, index, 0))
            index = text.indexOf (xmlGt, index + 1, &startMatch);
    }

    while (index >= 0)
    {
        int endIndex, indx;
        if (index == 0
            && (prevState == xmlValueState || prevState == commentState))
        {
            indx = 0;
        }
        else
            indx = index + startMatch.capturedLength();
        endIndex = text.indexOf (xmlLt, indx, &endMatch);

        /* skip comments */
        while (isXxmlComment (text, endIndex, indx))
            endIndex = text.indexOf (xmlLt, endIndex + 1, &endMatch);

        int valueLength;
        if (endIndex == -1)
        {
            setCurrentBlockState (xmlValueState);
            valueLength = text.length() - index;
        }
        else
            valueLength = endIndex - index + endMatch.capturedLength();
        setFormat (index, valueLength, neutralFormat);

        index = text.indexOf (xmlGt, index + valueLength, &startMatch);
        while (isXmlQuoted (text, index, endIndex + endMatch.capturedLength()))
            index = text.indexOf (xmlGt, index + 1, &startMatch);
    }
}
/*************************/
// For both double and single quotes, only outside values.
void Highlighter::xmlQuotes (const QString &text)
{
    static const QRegularExpression xmlQuoteError ("&|<");
    static const QRegularExpression xmlAmpersand ("&(#[0-9]+|#(x|X)[a-fA-F0-9]+|[\\w\\.\\-:]+);");

    int index = 0;
    QRegularExpressionMatch quoteMatch;
    QRegularExpression quoteExpression;
    int quote = doubleQuoteState;

    /* find the start quote */
    int prevState = previousBlockState();
    if (prevState != doubleQuoteState
        && prevState != singleQuoteState)
    {
        index = text.indexOf (mixedQuoteMark);
        /* skip values and comments */
        while (format (index) == neutralFormat || isXxmlComment (text, index, 0))
            index = text.indexOf (mixedQuoteMark, index + 1);
        /* if the start quote is found... */
        if (index >= 0)
        {
            /* ... distinguish between double and single quotes */
            if (text.at (index) == '\"')
            {
                quoteExpression = quoteMark;
                quote = doubleQuoteState;
            }
            else
            {
                quoteExpression = singleQuoteMark;
                quote = singleQuoteState;
            }
        }
    }
    else // but if we're inside a quotation...
    {
        /* ... distinguish between the two quote kinds
           by checking the previous line */
        quote = prevState;
        if (quote == doubleQuoteState)
            quoteExpression = quoteMark;
        else
            quoteExpression = singleQuoteMark;
    }

    while (index >= 0)
    {
        int endIndex;
        if (index == 0
            && (prevState == doubleQuoteState || prevState == singleQuoteState))
        {
            endIndex = text.indexOf (quoteExpression, 0, &quoteMatch);
        }
        else
            endIndex = text.indexOf (quoteExpression, index + 1, &quoteMatch);

        int quoteLength;
        if (endIndex == -1)
        {
            setCurrentBlockState (quote);
            quoteLength = text.length() - index;
        }
        else
            quoteLength = endIndex - index + quoteMatch.capturedLength();
        setFormat (index, quoteLength,
                   quote == doubleQuoteState ? quoteFormat : altQuoteFormat);

        /* format valid ampersand strings and errors inside quotes (with
           "regexFormat" and "errorFormat" respectively, to prevent overrides) */
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
        QString str = text.mid (index, quoteLength);
#else
        QString str = text.sliced (index, quoteLength);
#endif
        int indx = 0;
        QRegularExpressionMatch match;
        while ((indx = str.indexOf (xmlQuoteError, indx)) > -1)
        {
            if (str.at (indx) == '&' && indx == str.indexOf (xmlAmpersand, indx, &match))
            {
                setFormat (indx + index, match.capturedLength(), regexFormat);
                indx += match.capturedLength();
            }
            else
            {
                setFormat (indx + index, 1, errorFormat);
                ++ indx;
            }
        }

        /* the next quote may be different */
        index = text.indexOf (mixedQuoteMark, index + quoteLength);
        while (format (index) == neutralFormat
               || isXxmlComment (text, index, endIndex + quoteMatch.capturedLength()))
        {
            index = text.indexOf (mixedQuoteMark, index + 1);
        }
        /* if the search is continued... */
        if (index >= 0)
        {
            /* ... distinguish between double and single quotes
               again because the quote mark may have changed */
            if (text.at (index) == '\"')
            {
                quoteExpression = quoteMark;
                quote = doubleQuoteState;
            }
            else
            {
                quoteExpression = singleQuoteMark;
                quote = singleQuoteState;
            }
        }
        else break;
    }
}
/*************************/
// Starting with "<!--" and ending with "-->" only inside values.
void Highlighter::xmlComment (const QString &text)
{
    int prevState = previousBlockState();
    int index = 0;
    QRegularExpressionMatch startMatch;
    QRegularExpressionMatch endMatch;

    if (prevState != commentState)
    {
        index = text.indexOf (commentStartExpression, index, &startMatch);
        while (format (index) == errorFormat // it's an error
               // comments should be inside values
               || (index > -1 && format (index) != neutralFormat))
        {
            index = text.indexOf (commentStartExpression, index + 1, &startMatch);
        }
    }

    while (index >= 0)
    {
        int endIndex;
        if (index == 0 && prevState == commentState)
        {
            endIndex = text.indexOf (commentEndExpression, 0, &endMatch);
        }
        else
            endIndex = text.indexOf (commentEndExpression,
                                     index + startMatch.capturedLength(),
                                     &endMatch);

        int commentLength;
        if (endIndex == -1)
        {
            setCurrentBlockState (commentState);
            commentLength = text.length() - index;
        }
        else
            commentLength = endIndex - index
                            + endMatch.capturedLength();
        setFormat (index, commentLength, commentFormat);

        index = text.indexOf (commentStartExpression, index + commentLength, &startMatch);
        while (format (index) == errorFormat
               || (index > -1 && format (index) != neutralFormat))
        {
            index = text.indexOf (commentStartExpression, index + 1, &startMatch);
        }
    }
}
/*************************/
void Highlighter::highlightXmlBlock (const QString &text)
{
    TextBlockData *data = new TextBlockData;
    setCurrentBlockUserData (data);
    setCurrentBlockState (0);

    xmlValues (text);
    xmlQuotes (text);
    xmlComment (text);

    /*********************************************
     * Parentheses, Braces and Brackets Matching *
     *********************************************/

    /* left parenthesis */
    int index = text.indexOf ('(');
    QTextCharFormat fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf ('(', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        ParenthesisInfo *info = new ParenthesisInfo;
        info->character = '(';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf ('(', index + 1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf ('(', index + 1);
            fi = format (index);
        }
    }
    /* right parenthesis */
    index = text.indexOf (')');
    fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf (')', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        ParenthesisInfo *info = new ParenthesisInfo;
        info->character = ')';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf (')', index +1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf (')', index + 1);
            fi = format (index);
        }
    }
    /* left brace */
    index = text.indexOf ('{');
    fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf ('{', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        BraceInfo *info = new BraceInfo;
        info->character = '{';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf ('{', index + 1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf ('{', index + 1);
            fi = format (index);
        }
    }
    /* right brace */
    index = text.indexOf ('}');
    fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf ('}', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        BraceInfo *info = new BraceInfo;
        info->character = '}';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf ('}', index +1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf ('}', index + 1);
            fi = format (index);
        }
    }
    /* left bracket */
    index = text.indexOf ('[');
    fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf ('[', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        BracketInfo *info = new BracketInfo;
        info->character = '[';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf ('[', index + 1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf ('[', index + 1);
            fi = format (index);
        }
    }
    /* right bracket */
    index = text.indexOf (']');
    fi = format (index);
    while (index >= 0
           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
    {
        index = text.indexOf (']', index + 1);
        fi = format (index);
    }
    while (index >= 0)
    {
        BracketInfo *info = new BracketInfo;
        info->character = ']';
        info->position = index;
        data->insertInfo (info);

        index = text.indexOf (']', index +1);
        fi = format (index);
        while (index >= 0
               && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat))
        {
            index = text.indexOf (']', index + 1);
            fi = format (index);
        }
    }

    /*******************
     * Main Formatting *
     *******************/

    int bn = currentBlock().blockNumber();
    if (bn >= startCursor.blockNumber() && bn <= endCursor.blockNumber())
    {
        data->setHighlighted(); // completely highlighted
        QRegularExpressionMatch match;
        for (const HighlightingRule &rule : qAsConst (highlightingRules))
        {
            index = text.indexOf (rule.pattern, 0, &match);
            fi = format (index);
            /* skip quotes and comments (and errors and correct ampersands inside quotes) */
            if (rule.format != whiteSpaceFormat && rule.format != urlFormat)
            {
                while (index >= 0
                       && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat
                           || fi == regexFormat || fi == errorFormat))
                {
                    index = text.indexOf (rule.pattern, index + match.capturedLength(), &match);
                    fi = format (index);
                }
            }

            while (index >= 0)
            {
                int length = match.capturedLength();
                if (rule.format == urlFormat
                    && (fi == quoteFormat || fi == altQuoteFormat))
                { // urls inside quotes
                    setFormat (index, length, urlInsideQuoteFormat);
                }
                else
                    setFormat (index, length, rule.format);
                index = text.indexOf (rule.pattern, index + length, &match);

                fi = format (index);
                if (rule.format != whiteSpaceFormat && rule.format != urlFormat)
                {
                    while (index >= 0
                           && (fi == quoteFormat || fi == altQuoteFormat || fi == commentFormat
                               || fi == regexFormat || fi == errorFormat))
                    {
                        index = text.indexOf (rule.pattern, index + match.capturedLength(), &match);
                        fi = format (index);
                    }
                }
            }
        }
    }

    setCurrentBlockUserData (data);
}

}
