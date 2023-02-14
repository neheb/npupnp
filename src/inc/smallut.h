/* Copyright (C) 2006-2022 J.F.Dockes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *   02110-1301 USA
 */
#ifndef _SMALLUT_H_INCLUDED_
#define _SMALLUT_H_INCLUDED_

#include <stdint.h>
#include <time.h>
#include <string>
#include <vector>
#include <map>
#include <functional>

struct tm;

namespace MedocUtils {

// Miscellaneous mostly string-oriented small utilities
// Note that none of the following code knows about utf-8.

// Call this before going multithread.
void smallut_init_mt();

#ifndef SMALLUT_DISABLE_MACROS
#ifndef MIN
#define MIN(A,B) (((A)<(B)) ? (A) : (B))
#endif
#ifndef MAX
#define MAX(A,B) (((A)>(B)) ? (A) : (B))
#endif
#ifndef deleteZ
#define deleteZ(X) {delete X;X = nullptr;}
#endif
#ifndef PRETEND_USE
#define PRETEND_USE(var) ((void)(var))
#endif
#ifndef VERSION_AT_LEAST
#define VERSION_AT_LEAST(LIBMAJ,LIBMIN,LIBREV,TARGMAJ,TARGMIN,TARGREV)  \
    ((LIBMAJ) > (TARGMAJ) ||                                            \
     ((LIBMAJ) == (TARGMAJ) &&                                          \
      ((LIBMIN) > (TARGMIN) ||                                          \
       ((LIBMIN) == (TARGMIN) && (LIBREV) >= (TARGREV)))))
#endif
#endif /* SMALLUT_DISABLE_MACROS */

// Case-insensitive compare. ASCII ONLY !
extern int stringicmp(const std::string& s1, const std::string& s2);

// For find_if etc.
struct StringIcmpPred {
    explicit StringIcmpPred(const std::string& s1)
        : m_s1(s1) {
    }
    bool operator()(const std::string& s2) {
        return stringicmp(m_s1, s2) == 0;
    }
    const std::string& m_s1;
};

extern int stringlowercmp(const std::string& s1, // already lower
                          const std::string& s2);
extern int stringuppercmp(const std::string& s1, // already upper
                          const std::string& s2);

extern void stringtolower(std::string& io);
extern std::string stringtolower(const std::string& io);
extern void stringtoupper(std::string& io);
extern std::string stringtoupper(const std::string& io);
extern bool beginswith(const std::string& big, const std::string& small);

// Parse date interval specifier into pair of y,m,d dates.  The format
// for the time interval is based on a subset of iso 8601 with
// the addition of open intervals, and removal of all time indications.
// 'P' is the Period indicator, it's followed by a length in
// years/months/days (or any subset thereof)
// Dates: YYYY-MM-DD YYYY-MM YYYY
// Periods: P[nY][nM][nD] where n is an integer value.
// At least one of YMD must be specified
// The separator for the interval is /. Interval examples
// YYYY/ (from YYYY) YYYY-MM-DD/P3Y (3 years after date) etc.
// This returns a pair of y,m,d dates.
struct DateInterval {
    int y1;
    int m1;
    int d1;
    int y2;
    int m2;
    int d2;
};
extern bool parsedateinterval(const std::string& s, DateInterval *di);
extern int monthdays(int mon, int year);


/** Note for all templated functions: 
 * By default, smallut.cpp has explicit instantiations for common
 * containers (list, vector, set, etc.). If this is not enough, or
 * conversely, if you want to minimize the module size, you can chose
 * the instantiations by defining the SMALLUT_EXTERNAL_INSTANTIATIONS
 * compilation flag, and defining the instances in a file named
 * smallut_instantiations.h
 */

/**
 * Parse input string into list of strings. See instantiation note above.
 *
 * Token delimiter is " \t\n" except inside dquotes. dquote inside
 * dquotes can be escaped with \ etc...
 * Input is handled a byte at a time, things will work as long as
 * space tab etc. have the ascii values and can't appear as part of a
 * multibyte char. utf-8 ok but so are the iso-8859-x and surely
 * others. addseps do have to be single-bytes
 */
template <class T> bool stringToStrings(const std::string& s, T& tokens,
                                        const std::string& addseps = "");

/**
 * Inverse operation. See instantiation note above.
 */
template <class T> void stringsToString(const T& tokens, std::string& s);
template <class T> std::string stringsToString(const T& tokens);

/**
 * Strings to CSV string. tokens containing the separator are quoted (")
 * " inside tokens is escaped as "" ([word "quote"] =>["word ""quote"""]
 * See instantiation note above.
 */
template <class T> void stringsToCSV(const T& tokens, std::string& s, char sep = ',');

/** Find longest common prefix for bunch of strings */
template <class T> std::string commonprefix(const T& values);

/**
 * Split input string. No handling of quoting.
 */
extern void stringToTokens(const std::string& s,
                           std::vector<std::string>& tokens,
                           const std::string& delims = " \t",
                           bool skipinit = true, bool allowempty = false);

/** Like toTokens but with multichar separator */
extern void stringSplitString(const std::string& str,
                              std::vector<std::string>& tokens,
                              const std::string& sep);

/** Convert string to boolean */
extern bool stringToBool(const std::string& s);

/** Remove instances of characters belonging to set (default {space,
    tab}) at beginning and end of input string */
extern void trimstring(std::string& s, const char *ws = " \t");
extern void rtrimstring(std::string& s, const char *ws = " \t");
extern void ltrimstring(std::string& s, const char *ws = " \t");

/** Escape things like < or & by turning them into entities */
extern std::string escapeHtml(const std::string& in);

/** Double-quote and escape to produce C source code string (prog generation) */
extern std::string makeCString(const std::string& in);

/** Replace some chars with spaces (ie: newline chars). */
extern std::string neutchars(const std::string& str, const std::string& chars,
                             char rep = ' ');
extern void neutchars(const std::string& str, std::string& out,
                      const std::string& chars, char rep = ' ');

/** Turn string into something that won't be expanded by a shell. In practise
 *  quote with double-quotes and escape $`\ */
extern std::string escapeShell(const std::string& in);

/** Truncate a string to a given maxlength, avoiding cutting off midword
 *  if reasonably possible. */
extern std::string truncate_to_word(const std::string& input, std::string::size_type maxlen);

void ulltodecstr(uint64_t val, std::string& buf);
void lltodecstr(int64_t val, std::string& buf);
std::string lltodecstr(int64_t val);
std::string ulltodecstr(uint64_t val);

/** Convert byte count into unit (KB/MB...) appropriate for display */
std::string displayableBytes(int64_t size);

/** Break big string into lines */
std::string breakIntoLines(const std::string& in, unsigned int ll = 100, unsigned int maxlines = 50);

/** Small utility to substitute printf-like percents cmds in a string */
bool pcSubst(const std::string& in, std::string& out, const std::map<char, std::string>& subs);
/** Substitute printf-like percents and also %(key) */
bool pcSubst(const std::string& in, std::string& out,
             const std::map<std::string, std::string>& subs);
/** Substitute printf-like percents and %(nm), using result of function call */
bool pcSubst(const std::string& i, std::string& o, const std::function<std::string(const std::string&)>&);

/** Stupid little smart buffer handler avoiding value-initialization when not needed (e.g. for using
    as read buffer **/
class DirtySmartBuf {
public:
    DirtySmartBuf(size_t sz) { m_buf = new char[sz]; }
    ~DirtySmartBuf() { delete [] m_buf; }
    char *buf() { return m_buf; }
  private:
    char *m_buf;
};

/** Append system error message */
void catstrerror(std::string *reason, const char *what, int _errno);

/** Portable timegm. MS C has _mkgmtime, but there is a bug in Gminw which
 * makes it inaccessible */
time_t portable_timegm(struct tm *tm);

inline void leftzeropad(std::string &s, unsigned len) {
    if (s.length() && s.length() < len) {
        s = s.insert(0, len - s.length(), '0');
    }
}

// Print binary string in hexa, separate bytes with character separ if not zero
// (e.g. ac:23:0c:4f:46:fd)
extern std::string hexprint(const std::string& in, char separ= 0);

#ifndef SMALLUT_NO_REGEX
// A class to solve platorm/compiler issues for simple regex
// matches. Uses the appropriate native lib under the hood.
// This always uses extended regexp syntax.
class SimpleRegexp {
public:
    enum Flags {SRE_NONE = 0, SRE_ICASE = 1, SRE_NOSUB = 2};
    /// @param nmatch must be >= the number of parenthesed subexp in exp
    SimpleRegexp(const std::string& exp, int flags, int nmatch = 0);
    ~SimpleRegexp();
    SimpleRegexp(const SimpleRegexp&) = delete;
    SimpleRegexp& operator=(const SimpleRegexp&) = delete;
    /// Match input against exp, return true if matches
    bool simpleMatch(const std::string& val) const;
    /// After simpleMatch success, get nth submatch, 0 is the whole
    /// match, 1 first parentheses, etc.
    std::string getMatch(const std::string& val, int i) const;
    /// Calls simpleMatch()
    bool operator() (const std::string& val) const;

    /// Replace the first occurrence of regexp. 
    std::string simpleSub(const std::string& input, const std::string& repl);

    /// Check after construction
    bool ok() const;

    class Internal;
private:
    std::unique_ptr<Internal> m;
};
#endif // SMALLUT_NO_REGEX

/// Utilities for printing names for defined values (Ex: O_RDONLY->"O_RDONLY")

/// Entries for the descriptive table
struct CharFlags {
    CharFlags(int v, const char *y, const char *n=nullptr)
        : value(v), yesname(y), noname(n) {}
    unsigned int value; // Flag or value
    const char *yesname;// String to print if flag set or equal
    const char *noname; // String to print if flag not set (unused for values)
};

/// Helper macro for the common case where we want to print the
/// flag/value defined name
#define CHARFLAGENTRY(NM) {NM, #NM}

/// Translate a bitfield into string description
extern std::string flagsToString(const std::vector<CharFlags>&, unsigned int val);

/// Translate a value into a name
extern std::string valToString(const std::vector<CharFlags>&, unsigned int val);

/// Decode percent-encoded URL
extern std::string url_decode(const std::string&);

} // End namespace MedocUtils

using namespace MedocUtils;

#endif /* _SMALLUT_H_INCLUDED_ */
