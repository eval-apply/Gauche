/*
 * string.c - string implementation
 *
 *   Copyright (c) 2000-2025  Shiro Kawai  <shiro@acm.org>
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the authors nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 *   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LIBGAUCHE_BODY
#include "gauche.h"
#include "gauche/priv/configP.h"
#include "gauche/priv/stringP.h"
#include "gauche/priv/writerP.h"
#include "gauche/char_attr.h"

#include <string.h>
#include <ctype.h>

void Scm_DStringDump(FILE *out, ScmDString *dstr);
static ScmObj make_string_cursor(ScmString *src, const char *cursor);

static void string_print(ScmObj obj, ScmPort *port, ScmWriteContext *ctx);
SCM_DEFINE_BUILTIN_CLASS(Scm_StringClass, string_print, NULL, NULL, NULL,
                         SCM_CLASS_SEQUENCE_CPL);

#define CHECK_SIZE(siz)                                         \
    do {                                                        \
        if ((siz) > SCM_STRING_MAX_SIZE) {                      \
            Scm_Error("string size too big: %ld", (siz));       \
        }                                                       \
    } while (0)

/* Internal primitive constructor.   LEN can be negative if the string
   is incomplete. */
static ScmString *make_str(ScmSmallInt len, ScmSmallInt siz,
                           const char *p, u_long flags,
                           const void *index)
{
    if (len < 0) flags |= SCM_STRING_INCOMPLETE;
    if (flags & SCM_STRING_INCOMPLETE) len = siz;

    if (siz > SCM_STRING_MAX_SIZE) {
        Scm_Error("string size too big: %ld", siz);
    }
    if (len > siz) {
        Scm_Error("string length (%ld) exceeds size (%ld)", len, siz);
    }

    ScmString *s = SCM_NEW(ScmString);
    SCM_SET_CLASS(s, SCM_CLASS_STRING);
    s->body = NULL;
    s->initialBody.flags = flags & SCM_STRING_FLAG_MASK;
    s->initialBody.length = len;
    s->initialBody.size = siz;
    s->initialBody.start = p;
    s->initialBody.index = index;
    return s;
}

#define DUMP_LENGTH   50

/* for debug */
void Scm_StringDump(FILE *out, ScmObj str)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    ScmSmallInt s = SCM_STRING_BODY_SIZE(b);
    const char *p = SCM_STRING_BODY_START(b);

    fprintf(out, "STR(len=%ld,siz=%ld) \"", SCM_STRING_BODY_LENGTH(b), s);
    for (int i=0; i < DUMP_LENGTH && s > 0;) {
        int n = SCM_CHAR_NFOLLOWS(*p) + 1;
        for (; n > 0 && s > 0; p++, n--, s--, i++) {
            putc(*p, out);
        }
    }
    if (s > 0) {
        fputs("...\"\n", out);
    } else {
        fputs("\"\n", out);
    }
}

/* Like GC_strndup, but we don't require the source string to be
   NUL-terminated (instead, we trust the caller that the size
   argument is in valid range.) */
char *Scm_StrdupPartial(const char *src, size_t size)
{
    char *dst = SCM_NEW_ATOMIC_ARRAY(char, size+1);
    memcpy(dst, src, size);
    dst[size] = '\0';
    return dst;
}

/*
 * Multibyte length calculation
 */

/* We have multiple similar functions, due to performance reasons. */

/* Calculate both length and size of C-string str.
   If str is incomplete, *plen gets -1. */
static inline ScmSmallInt count_size_and_length(const char *str,
                                                ScmSmallInt *psize, /* out */
                                                ScmSmallInt *plen)  /* out */
{
    char c;
    int incomplete = FALSE;
    const char *p = str;
    ScmSmallInt size = 0, len = 0;
    while ((c = *p++) != 0) {
        int i = SCM_CHAR_NFOLLOWS(c);
        len++;
        size += i+1;

        ScmChar ch;
        SCM_CHAR_GET(p-1, ch);
        if (ch == SCM_CHAR_INVALID) incomplete = TRUE;
        /* Check every octet to avoid skipping over terminating NUL. */
        while (i-- > 0) {
            if (!*p++) { incomplete = TRUE; goto eos; }
        }
    }
  eos:
    if (incomplete) len = -1;
    *psize = size;
    *plen = len;
    return len;
}

/* Calculate length of known size string.  str can contain NUL character. */
static inline ScmSmallInt count_length(const char *str, ScmSmallInt size)
{
    ScmSmallInt count = 0;
    while (size-- > 0) {
        unsigned char c = (unsigned char)*str;
        int i = SCM_CHAR_NFOLLOWS(c);
        if (i < 0 || i > size) return -1;
        ScmChar ch;
        SCM_CHAR_GET(str, ch);
        if (ch == SCM_CHAR_INVALID) return -1;
        count++;
        str += i+1;
        size -= i;
    }
    return count;
}

/* Returns length of string, starts from str and end at stop.
   If stop is NULL, str is regarded as C-string (NUL terminated).
   If the string is incomplete, returns -1. */
ScmSmallInt Scm_MBLen(const char *str, const char *stop)
{
    ScmSmallInt size = (stop == NULL)? (ScmSmallInt)strlen(str) : (stop - str);
    ScmSmallInt len = count_length(str, size);
    if (len > SCM_STRING_MAX_LENGTH) {
        Scm_Error("Scm_MBLen: length too big: %ld", len);
    }
    return len;
}

/*----------------------------------------------------------------
 * Cursors
 */

static void cursor_print(ScmObj obj, ScmPort *port,
                         ScmWriteContext *mode SCM_UNUSED)
{
    Scm_Printf(port, "#<string-cursor-large %ld>",
               SCM_STRING_CURSOR_LARGE_OFFSET(obj));
}

static ScmClass *cursor_cpl[] = {
    SCM_CLASS_STATIC_PTR(Scm_StringCursorClass),
    SCM_CLASS_STATIC_PTR(Scm_TopClass),
    NULL
};

SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_StringCursorClass, NULL);
SCM_DEFINE_BUILTIN_CLASS(Scm_StringCursorLargeClass, cursor_print, NULL, NULL,
                         NULL, cursor_cpl);

/* Common routine to get hold of the pointer from string cursor.
   Returns NULL if SC isn't a string cursor.
   Raise an error if sc is not in the range. */
static inline const char *string_cursor_ptr(const ScmStringBody *sb, ScmObj sc)
{
    const char *ptr = NULL;
    if (SCM_STRING_CURSOR_LARGE_P(sc)) {
        if (SCM_STRING_BODY_START(sb) != SCM_STRING_CURSOR_LARGE_START(sc)) {
            Scm_Error("invalid cursor (made for string '%s'): %S",
                      SCM_STRING_CURSOR_LARGE_START(sc), sc);
        }
        ptr = SCM_STRING_CURSOR_LARGE_POINTER(sb, sc);
    } else if (SCM_STRING_CURSOR_SMALL_P(sc)) {
        ptr = SCM_STRING_CURSOR_SMALL_POINTER(sb, sc);
    } else {
        return NULL;
    }
    if (ptr < SCM_STRING_BODY_START(sb) ||
        ptr > SCM_STRING_BODY_END(sb)) {
        Scm_Error("cursor out of range: %S", sc);
    }
    return ptr;
}

/* Returns -1 if sc isn't a cursor.  No range check performed. */
static inline ScmSmallInt string_cursor_offset(ScmObj sc) {
    if (SCM_STRING_CURSOR_LARGE_P(sc)) {
        return SCM_STRING_CURSOR_LARGE_OFFSET(sc);
    } else if (SCM_STRING_CURSOR_SMALL_P(sc)) {
        return SCM_STRING_CURSOR_SMALL_OFFSET(sc);
    } else {
        return -1;
    }
}

/*----------------------------------------------------------------
 * Constructors
 */

/* General constructor. */
ScmObj Scm_MakeString(const char *str, ScmSmallInt size, ScmSmallInt len,
                      u_long flags)
{
    flags &= ~SCM_STRING_TERMINATED;

    if (size < 0) {
        count_size_and_length(str, &size, &len);
        flags |= SCM_STRING_TERMINATED;
    } else {
        if (len < 0) len = count_length(str, size);
    }
    /* Range of size and len will be checked in make_str */

    ScmString *s;
    if (flags & SCM_STRING_COPYING) {
        flags |= SCM_STRING_TERMINATED; /* SCM_STRDUP_PARTIAL terminates the result str */
        s = make_str(len, size, SCM_STRDUP_PARTIAL(str, size), flags, NULL);
    } else {
        s = make_str(len, size, str, flags, NULL);
    }
    return SCM_OBJ(s);
}

ScmObj Scm_MakeFillString(ScmSmallInt len, ScmChar fill)
{
    if (len < 0) Scm_Error("length out of range: %ld", len);
    ScmSmallInt csize = SCM_CHAR_NBYTES(fill);
    CHECK_SIZE(csize*len);
    char *ptr = SCM_NEW_ATOMIC2(char *, csize*len+1);
    char *p = ptr;
    for (ScmSmallInt i=0; i<len; i++, p+=csize) {
        SCM_CHAR_PUT(p, fill);
    }
    ptr[csize*len] = '\0';
    return SCM_OBJ(make_str(len, csize*len, ptr, SCM_STRING_TERMINATED, NULL));
}

ScmObj Scm_ListToString(ScmObj chars)
{
    ScmSmallInt size = 0, len = 0;

    ScmObj cp;
    SCM_FOR_EACH(cp, chars) {
        if (!SCM_CHARP(SCM_CAR(cp)))
            Scm_Error("character required, but got %S", SCM_CAR(cp));
        ScmChar ch = SCM_CHAR_VALUE(SCM_CAR(cp));
        size += SCM_CHAR_NBYTES(ch);
        len++;
        CHECK_SIZE(size);
    }
    char *buf = SCM_NEW_ATOMIC2(char *, size+1);
    char *bufp = buf;
    SCM_FOR_EACH(cp, chars) {
        ScmChar ch = SCM_CHAR_VALUE(SCM_CAR(cp));
        SCM_CHAR_PUT(bufp, ch);
        bufp += SCM_CHAR_NBYTES(ch);
    }
    *bufp = '\0';
    return Scm_MakeString(buf, size, len, 0);
}

/* Extract string as C-string.  This one guarantees to return
   mutable string (we always copy) */
char *Scm_GetString(ScmString *str)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    return SCM_STRDUP_PARTIAL(SCM_STRING_BODY_START(b), SCM_STRING_BODY_SIZE(b));
}

/* Common routine for Scm_GetStringConst and Scm_GetStringContent */
static const char *get_string_from_body(const ScmStringBody *b)
{
    ScmSmallInt size = SCM_STRING_BODY_SIZE(b);
    if (SCM_STRING_BODY_HAS_FLAG(b, SCM_STRING_TERMINATED)) {
        /* we can use string data as C-string */
        return SCM_STRING_BODY_START(b);
    } else {
        char *p = SCM_STRDUP_PARTIAL(SCM_STRING_BODY_START(b), size);
        /* kludge! This breaks 'const' qualification, but we know
           this is an idempotent operation from the outside.  Note that
           this is safe even multiple threads execute this part
           simultaneously. */
        ((ScmStringBody*)b)->start = p; /* discard const qualifier */
        ((ScmStringBody*)b)->flags |= SCM_STRING_TERMINATED;
        return p;
    }
}

/* Extract string as C-string.  Returned string is immutable,
   so we can directly return the body of the string.  We do not
   allow string containing NUL to be passed to C world, for it
   would be a security risk.
   TODO: Let the string body have a flag so that we don't need
   to scan the string every time.
*/
const char *Scm_GetStringConst(ScmString *str)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    if (memchr(SCM_STRING_BODY_START(b), 0, SCM_STRING_BODY_SIZE(b))) {
        Scm_Error("A string containing NUL character is not allowed: %S",
                  SCM_OBJ(str));
    }
    return get_string_from_body(b);
}

/* Atomically extracts C-string, length, size, and incomplete flag.
   MT-safe. */
/* NB: Output parameters are int's for the ABI compatibility. */
const char *Scm_GetStringContent(ScmString *str,
                                 ScmSmallInt *psize,   /* out */
                                 ScmSmallInt *plength, /* out */
                                 u_long *pflags)       /* out */
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    if (psize)   *psize = SCM_STRING_BODY_SIZE(b);
    if (plength) *plength = SCM_STRING_BODY_LENGTH(b);
    if (pflags) *pflags = SCM_STRING_BODY_FLAGS(b);
    return get_string_from_body(b);
}


/* Copy string.  You can modify the flags of the newly created string
   by FLAGS and MASK arguments; for the bits set in MASK, corresponding
   bits in FLAGS are copied to the new string, and for other bits, the
   original flags are copied.

   The typical semantics of copy-string is achieved by passing 0 to
   FLAGS and SCM_STRING_IMMUTABLE to MASK (i.e. reset IMMUTABLE flag,
   and keep other flags intact.

   NB: This routine doesn't check whether specified flag is valid
   with the string content, i.e. you can drop INCOMPLETE flag with
   copying, while the string content won't be checked if it consists
   valid complete string. */
ScmObj Scm_CopyStringWithFlags(ScmString *x, u_long flags, u_long mask)
{
    const ScmStringBody *b = SCM_STRING_BODY(x);
    ScmSmallInt size = SCM_STRING_BODY_SIZE(b);
    ScmSmallInt len  = SCM_STRING_BODY_LENGTH(b);
    const char *start = SCM_STRING_BODY_START(b);
    const void *index = b->index;
    u_long newflags = ((SCM_STRING_BODY_FLAGS(b) & ~mask)
                       | (flags & mask));

    return SCM_OBJ(make_str(len, size, start, newflags, index));
}

/* OBSOLETED */
ScmObj Scm_StringCompleteToIncomplete(ScmString *x)
{
    Scm_Warn("Obsoleted C API Scm_StringCompleteToIncomplete called");
    ScmObj proc = SCM_UNDEFINED;
    SCM_BIND_PROC(proc, "string-complete->incomplete", Scm_GaucheModule());
    return Scm_ApplyRec1(proc, SCM_OBJ(x));
}

/* OBSOLETED */
ScmObj Scm_StringIncompleteToComplete(ScmString *x,
                                      int handling,
                                      ScmChar substitute)
{
    Scm_Warn("Obsoleted C API Scm_StringIncompleteToComplete called");
    ScmObj proc = SCM_UNDEFINED;
    SCM_BIND_PROC(proc, "string-incomplete->complete", Scm_GaucheModule());
    ScmObj r;
    if (handling == SCM_ILLEGAL_CHAR_REJECT) {
        r = Scm_ApplyRec1(proc, SCM_OBJ(x));
    } else if (handling == SCM_ILLEGAL_CHAR_OMIT) {
        r = Scm_ApplyRec2(proc, SCM_OBJ(x), SCM_MAKE_KEYWORD("omit"));
    } else {
        r = Scm_ApplyRec2(proc, SCM_OBJ(x), SCM_MAKE_CHAR(substitute));
    }
    return r;
}

/*----------------------------------------------------------------
 * Comparison
 */

/* TODO: merge Equal and Cmp API; required generic comparison protocol */
int Scm_StringEqual(ScmString *x, ScmString *y)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    const ScmStringBody *yb = SCM_STRING_BODY(y);
    if ((SCM_STRING_BODY_FLAGS(xb)^SCM_STRING_BODY_FLAGS(yb))&SCM_STRING_INCOMPLETE) {
        return FALSE;
    }
    if (SCM_STRING_BODY_SIZE(xb) != SCM_STRING_BODY_SIZE(yb)) {
        return FALSE;
    }
    return (memcmp(SCM_STRING_BODY_START(xb),
                   SCM_STRING_BODY_START(yb),
                   SCM_STRING_BODY_SIZE(xb)) == 0? TRUE : FALSE);
}

int Scm_StringCmp(ScmString *x, ScmString *y)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    const ScmStringBody *yb = SCM_STRING_BODY(y);
    ScmSmallInt sizx = SCM_STRING_BODY_SIZE(xb);
    ScmSmallInt sizy = SCM_STRING_BODY_SIZE(yb);
    ScmSmallInt siz = (sizx < sizy)? sizx : sizy;
    int r = memcmp(SCM_STRING_BODY_START(xb), SCM_STRING_BODY_START(yb), siz);
    if (r == 0) {
        if (sizx == sizy) {
            if (SCM_STRING_BODY_INCOMPLETE_P(xb)) {
                if (SCM_STRING_BODY_INCOMPLETE_P(yb)) return 0;
                else                                  return 1;
            } else {
                if (SCM_STRING_BODY_INCOMPLETE_P(yb)) return -1;
                else                                  return 0;
            }
        }
        if (sizx < sizy)  return -1;
        else              return 1;
    } else if (r < 0) {
        return -1;
    } else {
        return 1;
    }
}

/* single-byte case insensitive comparison */
static int sb_strcasecmp(const char *px, ScmSmallInt sizx,
                         const char *py, ScmSmallInt sizy)
{
    for (; sizx > 0 && sizy > 0; sizx--, sizy--, px++, py++) {
        char cx = tolower((u_char)*px);
        char cy = tolower((u_char)*py);
        if (cx == cy) continue;
        return (cx - cy);
    }
    if (sizx > 0) return 1;
    if (sizy > 0) return -1;
    return 0;
}

/* multi-byte case insensitive comparison */
static int mb_strcasecmp(const char *px, ScmSmallInt lenx,
                         const char *py, ScmSmallInt leny)
{
    int ix, iy;
    for (; lenx > 0 && leny > 0; lenx--, leny--, px+=ix, py+=iy) {
        int cx, cy;
        SCM_CHAR_GET(px, cx);
        SCM_CHAR_GET(py, cy);
        int ccx = SCM_CHAR_UPCASE(cx);
        int ccy = SCM_CHAR_UPCASE(cy);
        if (ccx != ccy) return (ccx - ccy);
        ix = SCM_CHAR_NBYTES(cx);
        iy = SCM_CHAR_NBYTES(cy);
    }
    if (lenx > 0) return 1;
    if (leny > 0) return -1;
    return 0;
}

int Scm_StringCiCmp(ScmString *x, ScmString *y)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    const ScmStringBody *yb = SCM_STRING_BODY(y);

    if ((SCM_STRING_BODY_FLAGS(xb)^SCM_STRING_BODY_FLAGS(yb))&SCM_STRING_INCOMPLETE) {
        Scm_Error("cannot compare incomplete strings in case-insensitive way: %S, %S",
                  SCM_OBJ(x), SCM_OBJ(y));
    }
    ScmSmallInt sizx = SCM_STRING_BODY_SIZE(xb);
    ScmSmallInt lenx = SCM_STRING_BODY_LENGTH(xb);
    ScmSmallInt sizy = SCM_STRING_BODY_SIZE(yb);
    ScmSmallInt leny = SCM_STRING_BODY_LENGTH(yb);
    const char *px = SCM_STRING_BODY_START(xb);
    const char *py = SCM_STRING_BODY_START(yb);

    if (sizx == lenx && sizy == leny) {
        return sb_strcasecmp(px, sizx, py, sizy);
    } else {
        return mb_strcasecmp(px, lenx, py, leny);
    }
}

/*----------------------------------------------------------------
 * Reference
 */

/* Advance ptr for NCHARS characters.  Args assumed in boundary. */
static inline const char *forward_pos(const ScmStringBody *body,
                                      const char *current,
                                      ScmSmallInt nchars)
{
    if (body && (SCM_STRING_BODY_SINGLE_BYTE_P(body) ||
                 SCM_STRING_BODY_INCOMPLETE_P(body))) {
        return current + nchars;
    }

    while (nchars--) {
        int n = SCM_CHAR_NFOLLOWS(*current);
        current += n + 1;
    }
    return current;
}

/* Index -> ptr.  Args assumed in boundary. */
static const char *index2ptr(const ScmStringBody *body,
                             ScmSmallInt nchars)
{
    if (body->index == NULL) {
        return forward_pos(body, SCM_STRING_BODY_START(body), nchars);
    }
    ScmStringIndex *index = STRING_INDEX(body->index);
    ScmSmallInt off = 0;
    ScmSmallInt array_off = (nchars>>STRING_INDEX_SHIFT(index))+1;
    /* If array_off is 1, we don't need lookup - the character is in the
       first segment. */
    if (array_off > 1) {
        switch (STRING_INDEX_TYPE(index)) {
        case STRING_INDEX8:
            SCM_ASSERT(array_off < (ScmSmallInt)index->index8[1]);
            off = index->index8[array_off];
            break;
        case STRING_INDEX16:
            SCM_ASSERT(array_off < (ScmSmallInt)index->index16[1]);
            off = index->index16[array_off];
            break;
        case STRING_INDEX32:
            SCM_ASSERT(array_off < (ScmSmallInt)index->index32[1]);
            off = index->index32[array_off];
            break;
        case STRING_INDEX64:
            SCM_ASSERT(array_off < (ScmSmallInt)index->index64[1]);
            off = index->index64[array_off];
            break;
        default:
            Scm_Panic("String index contains unrecognized signature (%02x). "
                      "Possible memory corruption.  Aborting...",
                      index->signature);
        }
    }
    return forward_pos(body,
                       SCM_STRING_BODY_START(body) + off,
                       nchars & (STRING_INDEX_INTERVAL(index)-1));
}


/* string-ref.
 * If POS is out of range,
 *   - returns SCM_CHAR_INVALID if range_error is FALSE
 *   - raise error otherwise.
 * This differs from Scheme version, which takes an optional 'fallback'
 * argument which will be returned when POS is out-of-range.  We can't
 * have the same semantics since the return type is limited.
 */
ScmChar Scm_StringRef(ScmString *str, ScmSmallInt pos, int range_error)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    ScmSmallInt len = SCM_STRING_BODY_LENGTH(b);

    /* we can't allow string-ref on incomplete strings, since it may yield
       invalid character object. */
    if (SCM_STRING_BODY_INCOMPLETE_P(b)) {
        Scm_Error("incomplete string not allowed : %S", str);
    }
    if (pos < 0 || pos >= len) {
        if (range_error) {
            Scm_Error("argument out of range: %ld", pos);
        } else {
            return SCM_CHAR_INVALID;
        }
    }

    const char *p = NULL;
    if (SCM_STRING_BODY_SINGLE_BYTE_P(b)) {
        p = SCM_STRING_BODY_START(b) + pos;
    } else {
        p = index2ptr(b, pos);
    }

    if (SCM_STRING_BODY_SINGLE_BYTE_P(b)) {
        return (ScmChar)(*(unsigned char *)p);
    } else {
        ScmChar c;
        SCM_CHAR_GET(p, c);
        return c;
    }
}

/* The meaning and rationale of range_error is the same as Scm_StringRef.
 * Returns -1 if OFFSET is out-of-range and RANGE_ERROR is FALSE.
 * (Because of this, the return type is not ScmByte but int.
 */
int Scm_StringByteRef(ScmString *str, ScmSmallInt offset, int range_error)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    if (offset < 0 || offset >= SCM_STRING_BODY_SIZE(b)) {
        if (range_error) {
            Scm_Error("argument out of range: %ld", offset);
        } else {
            return -1;
        }
    }
    return (ScmByte)SCM_STRING_BODY_START(b)[offset];
}

/* External interface of index2ptr.  Returns the pointer to the
   offset-th character in str. */
/* NB: this function allows offset == length of the string; in that
   case, the return value points the location past the string body,
   but it is necessary sometimes to do a pointer arithmetic with the
   returned values. */
const char *Scm_StringBodyPosition(const ScmStringBody *b, ScmSmallInt offset)
{
    if (offset < 0 || offset > SCM_STRING_BODY_LENGTH(b)) {
        Scm_Error("argument out of range: %ld", offset);
    }
    return index2ptr(b, offset);
}

/* This is old API and now DEPRECATED.  It's difficult to use this safely,
   since you don't have a way to get the string length consistent at the
   moment you call this function.   Use Scm_StringBodyPosition instead. */
const char *Scm_StringPosition(ScmString *str, ScmSmallInt offset)
{
    return Scm_StringBodyPosition(SCM_STRING_BODY(str), offset);
}

/*----------------------------------------------------------------
 * Concatenation
 */

ScmObj Scm_StringAppend2(ScmString *x, ScmString *y)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    const ScmStringBody *yb = SCM_STRING_BODY(y);
    ScmSmallInt sizex = SCM_STRING_BODY_SIZE(xb);
    ScmSmallInt lenx = SCM_STRING_BODY_LENGTH(xb);
    ScmSmallInt sizey = SCM_STRING_BODY_SIZE(yb);
    ScmSmallInt leny = SCM_STRING_BODY_LENGTH(yb);
    CHECK_SIZE(sizex+sizey);
    u_long flags = 0;
    char *p = SCM_NEW_ATOMIC2(char *,sizex + sizey + 1);

    memcpy(p, xb->start, sizex);
    memcpy(p+sizex, yb->start, sizey);
    p[sizex + sizey] = '\0';
    flags |= SCM_STRING_TERMINATED;

    if (SCM_STRING_BODY_INCOMPLETE_P(xb) || SCM_STRING_BODY_INCOMPLETE_P(yb)) {
        flags |= SCM_STRING_INCOMPLETE; /* yields incomplete string */
    }
    return SCM_OBJ(make_str(lenx+leny, sizex+sizey, p, flags, NULL));
}

ScmObj Scm_StringAppendC(ScmString *x, const char *str,
                         ScmSmallInt sizey, ScmSmallInt leny)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    ScmSmallInt sizex = SCM_STRING_BODY_SIZE(xb);
    ScmSmallInt lenx = SCM_STRING_BODY_LENGTH(xb);
    u_long flags = 0;

    if (sizey < 0) count_size_and_length(str, &sizey, &leny);
    else if (leny < 0) leny = count_length(str, sizey);
    CHECK_SIZE(sizex+sizey);

    char *p = SCM_NEW_ATOMIC2(char *, sizex + sizey + 1);
    memcpy(p, xb->start, sizex);
    memcpy(p+sizex, str, sizey);
    p[sizex+sizey] = '\0';
    flags |= SCM_STRING_TERMINATED;

    if (SCM_STRING_BODY_INCOMPLETE_P(xb) || leny < 0) {
        flags |= SCM_STRING_INCOMPLETE;
    }
    return SCM_OBJ(make_str(lenx + leny, sizex + sizey, p, flags, NULL));
}

ScmObj Scm_StringAppend(ScmObj strs)
{
#define BODY_ARRAY_SIZE 32
    ScmSmallInt size = 0, len = 0;
    u_long flags = 0;
    const ScmStringBody *bodies_s[BODY_ARRAY_SIZE], **bodies;

    /* It is trickier than it appears, since the strings may be modified
       by another thread during we're dealing with it.  So in the first
       pass to sum up the lengths of strings, we extract the string bodies
       and save it.  */
    ScmSmallInt numstrs = Scm_Length(strs);
    if (numstrs < 0) Scm_Error("improper list not allowed: %S", strs);
    if (numstrs > BODY_ARRAY_SIZE) {
        bodies = SCM_NEW_ARRAY(const ScmStringBody*, numstrs);
    } else {
        bodies = bodies_s;
    }

    ScmSmallInt i = 0;
    ScmObj cp;
    SCM_FOR_EACH(cp, strs) {
        const ScmStringBody *b;
        if (!SCM_STRINGP(SCM_CAR(cp))) {
            Scm_Error("string required, but got %S", SCM_CAR(cp));
        }
        b = SCM_STRING_BODY(SCM_CAR(cp));
        size += SCM_STRING_BODY_SIZE(b);
        len += SCM_STRING_BODY_LENGTH(b);
        CHECK_SIZE(size);
        if (SCM_STRING_BODY_INCOMPLETE_P(b)) {
            flags |= SCM_STRING_INCOMPLETE;
        }
        bodies[i++] = b;
    }

    char *buf = SCM_NEW_ATOMIC2(char *, size+1);
    char *bufp = buf;
    for (i=0; i<numstrs; i++) {
        const ScmStringBody *b = bodies[i];
        memcpy(bufp, SCM_STRING_BODY_START(b), SCM_STRING_BODY_SIZE(b));
        bufp += SCM_STRING_BODY_SIZE(b);
    }
    *bufp = '\0';
    bodies = NULL;              /* to help GC */
    flags |= SCM_STRING_TERMINATED;
    return SCM_OBJ(make_str(len, size, buf, flags, NULL));
#undef BODY_ARRAY_SIZE
}

ScmObj Scm_StringJoin(ScmObj strs, ScmString *delim, int grammar)
{
#define BODY_ARRAY_SIZE 32
    ScmSmallInt size = 0, len = 0;
    u_long flags = 0;
    const ScmStringBody *bodies_s[BODY_ARRAY_SIZE], **bodies;

    ScmSmallInt nstrs = Scm_Length(strs);
    if (nstrs < 0) Scm_Error("improper list not allowed: %S", strs);
    if (nstrs == 0) {
        if (grammar == SCM_STRING_JOIN_STRICT_INFIX) {
            Scm_Error("can't join empty list of strings with strict-infix grammar");
        }
        return SCM_MAKE_STR("");
    }

    if (nstrs > BODY_ARRAY_SIZE) {
        bodies = SCM_NEW_ARRAY(const ScmStringBody *, nstrs);
    } else {
        bodies = bodies_s;
    }

    const ScmStringBody *dbody = SCM_STRING_BODY(delim);
    ScmSmallInt dsize = SCM_STRING_BODY_SIZE(dbody);
    ScmSmallInt dlen  = SCM_STRING_BODY_LENGTH(dbody);
    if (SCM_STRING_BODY_INCOMPLETE_P(dbody)) {
        flags |= SCM_STRING_INCOMPLETE;
    }

    ScmSmallInt i = 0, ndelim;
    ScmObj cp;
    SCM_FOR_EACH(cp, strs) {
        const ScmStringBody *b;
        if (!SCM_STRINGP(SCM_CAR(cp))) {
            Scm_Error("string required, but got %S", SCM_CAR(cp));
        }
        b = SCM_STRING_BODY(SCM_CAR(cp));
        size += SCM_STRING_BODY_SIZE(b);
        len  += SCM_STRING_BODY_LENGTH(b);
        CHECK_SIZE(size);
        if (SCM_STRING_BODY_INCOMPLETE_P(b)) {
            flags |= SCM_STRING_INCOMPLETE;
        }
        bodies[i++] = b;
    }
    if (grammar == SCM_STRING_JOIN_INFIX
        || grammar == SCM_STRING_JOIN_STRICT_INFIX) {
        ndelim = nstrs - 1;
    } else {
        ndelim = nstrs;
    }
    size += dsize * ndelim;
    len += dlen * ndelim;
    CHECK_SIZE(size);

    char *buf = SCM_NEW_ATOMIC2(char *, size+1);
    char *bufp = buf;
    if (grammar == SCM_STRING_JOIN_PREFIX) {
        memcpy(bufp, SCM_STRING_BODY_START(dbody), dsize);
        bufp += dsize;
    }
    for (i=0; i<nstrs; i++) {
        const ScmStringBody *b = bodies[i];
        memcpy(bufp, SCM_STRING_BODY_START(b), SCM_STRING_BODY_SIZE(b));
        bufp += SCM_STRING_BODY_SIZE(b);
        if (i < nstrs-1) {
            memcpy(bufp, SCM_STRING_BODY_START(dbody), dsize);
            bufp += dsize;
        }
    }
    if (grammar == SCM_STRING_JOIN_SUFFIX) {
        memcpy(bufp, SCM_STRING_BODY_START(dbody), dsize);
        bufp += dsize;
    }
    *bufp = '\0';
    bodies = NULL;              /* to help GC */
    flags |= SCM_STRING_TERMINATED;
    return SCM_OBJ(make_str(len, size, buf, flags, NULL));
#undef BODY_ARRAY_SIZE
}

/*----------------------------------------------------------------
 * Mutation
 */

/*
 * String mutation is extremely heavy operation in Gauche,
 * and only provided for compatibility to RnRS.  At C API level
 * there's no point in using string mutation at all.  A single
 * API, which replaces the string body, is provided at C level.
 */

ScmObj Scm_StringReplaceBody(ScmString *str, const ScmStringBody *newbody)
{
    if (SCM_STRING_IMMUTABLE_P(str)) {
        Scm_Error("attempted to modify an immutable string: %S", str);
    }

    /* Atomically replaces the str's body (no MT hazard) */
    str->body = newbody;

    /* TODO: If the initialBody of str isn't shared,
       nullify str->initialBody.start so that the original string is
       GCed.  It should be done after implementing 'shared' flag
       into the string body. */
    return SCM_OBJ(str);
}

/*----------------------------------------------------------------
 * Substring
 */

static ScmObj substring(const ScmStringBody *xb,
                        ScmSmallInt start, ScmSmallInt end,
                        int byterange, int immutable)
{
    ScmSmallInt len = byterange? SCM_STRING_BODY_SIZE(xb) : SCM_STRING_BODY_LENGTH(xb);
    u_long flags = SCM_STRING_BODY_FLAGS(xb);
    if (!immutable) flags &= ~SCM_STRING_IMMUTABLE;

    SCM_CHECK_START_END(start, end, len);

    if (byterange) {
        if (end != len) flags &= ~SCM_STRING_TERMINATED;
        flags |= SCM_STRING_INCOMPLETE;
        return SCM_OBJ(make_str(end - start,
                                end - start,
                                SCM_STRING_BODY_START(xb) + start,
                                flags, NULL));
    } else {
        const char *s, *e;
        s = index2ptr(xb, start);
        if (len == end) {
            e = SCM_STRING_BODY_END(xb);
        } else {
            /* kludge - if we don't have index, forward_pos is faster. */
            if (start > 0 && xb->index == NULL) {
                e = forward_pos(xb, s, end - start);
            } else {
                e = index2ptr(xb, end);
            }
            flags &= ~SCM_STRING_TERMINATED;
        }
        return SCM_OBJ(make_str(end - start,
                                (ScmSmallInt)(e - s), s, flags, NULL));
    }
}

static ScmObj substring_cursor(const ScmStringBody *xb,
                               const char *start,
                               const char *end,
                               int immutable)
{
    u_long flags = SCM_STRING_BODY_FLAGS(xb);
    if (!immutable) flags &= ~SCM_STRING_IMMUTABLE;

    if (start < SCM_STRING_BODY_START(xb) ||
        start > SCM_STRING_BODY_END(xb)) {
        Scm_Error("start argument out of range: %S", start);
    }
    else if (end > SCM_STRING_BODY_END(xb)) {
        Scm_Error("end argument out of range: %S", end);
    } else if (end < start) {
        Scm_Error("end argument must be greater than or "
                  "equal to the start argument: %S vs %S", end, start);
    }

    if (end != SCM_STRING_BODY_END(xb)) {
        flags &= ~SCM_STRING_TERMINATED;
    }

    ScmSmallInt len;
    if (SCM_STRING_BODY_SINGLE_BYTE_P(xb)) {
        len = (ScmSmallInt)(end - start);
    } else {
        len = Scm_MBLen(start, end);
    }

    return SCM_OBJ(make_str(len,
                            (ScmSmallInt)(end - start),
                            start, flags, NULL));
}

ScmObj Scm_Substring(ScmString *x, ScmSmallInt start, ScmSmallInt end,
                     int byterangep)
{
    return substring(SCM_STRING_BODY(x), start, end, byterangep, FALSE);
}

/* Auxiliary procedure to support optional start/end parameter specified
   in lots of SRFI-13 functions.   If start and end is specified and restricts
   string range, call substring.  Otherwise returns x itself.
   If input string is immutable, the result is also immutable.  If the caller
   needs a mutable string it should call CopyString anyway, for the caller
   doesn't know if the input string is just passed through.
*/
ScmObj Scm_MaybeSubstring(ScmString *x, ScmObj start, ScmObj end)
{
    const ScmStringBody *xb = SCM_STRING_BODY(x);
    int no_start = SCM_UNBOUNDP(start) || SCM_UNDEFINEDP(start) || SCM_FALSEP(start);
    int no_end = SCM_UNBOUNDP(end) || SCM_UNDEFINEDP(end) || SCM_FALSEP(end);
    ScmSmallInt istart = -1, iend = -1, ostart = -1, oend = -1;

    int immutable = SCM_STRING_BODY_HAS_FLAG(xb, SCM_STRING_IMMUTABLE);

    if (no_start)
        istart = 0;
    else if (SCM_STRING_CURSOR_P(start))
        ostart = string_cursor_offset(start);
    else if (SCM_INTP(start))
        istart = SCM_INT_VALUE(start);
    else
        Scm_Error("exact integer or cursor required for start, but got %S", start);

    if (no_end) {
        if (istart == 0 || ostart == 0) {
            return SCM_OBJ(x);
        }
        iend = SCM_STRING_BODY_LENGTH(xb);
    } else if (SCM_STRING_CURSOR_P(end))
        oend = string_cursor_offset(end);
    else if (SCM_INTP(end))
        iend = SCM_INT_VALUE(end);
    else
        Scm_Error("exact integer or cursor required for end, but got %S", end);

    if (no_start && oend != -1) {
        return substring_cursor(xb,
                                SCM_STRING_BODY_START(xb),
                                SCM_STRING_BODY_START(xb) + oend,
                                immutable);
    }
    if (ostart != -1 && oend != -1) {
        return substring_cursor(xb,
                                SCM_STRING_BODY_START(xb) + ostart,
                                SCM_STRING_BODY_START(xb) + oend,
                                immutable);
    }
    if (ostart != -1 && no_end) {
        return substring_cursor(xb,
                                SCM_STRING_BODY_START(xb) + ostart,
                                SCM_STRING_BODY_END(xb),
                                immutable);
    }

    if (ostart != -1) {
        istart = Scm_GetInteger(Scm_StringCursorIndex(x, start));
    }
    if (oend != -1) {
        iend = Scm_GetInteger(Scm_StringCursorIndex(x, end));
    }

    return substring(xb, istart, iend, FALSE, immutable);
}

/*----------------------------------------------------------------
 * Search & parse
 */

/* Boyer-Moore string search.  assuming siz1 > siz2, siz2 < 256. */
static ScmSmallInt boyer_moore(const char *ss1, ScmSmallInt siz1,
                               const char *ss2, ScmSmallInt siz2)
{
    unsigned char shift[256];
    for (ScmSmallInt i=0; i<256; i++) { shift[i] = siz2; }
    for (ScmSmallInt j=0; j<siz2-1; j++) {
        shift[(unsigned char)ss2[j]] = siz2-j-1;
    }
    for (ScmSmallInt i=siz2-1; i<siz1; i+=shift[(unsigned char)ss1[i]]) {
        ScmSmallInt j, k;
        for (j=siz2-1, k = i; j>=0 && ss1[k] == ss2[j]; j--, k--)
            ;
        if (j == -1) return k+1;
    }
    return -1;
}

static ScmSmallInt boyer_moore_reverse(const char *ss1, ScmSmallInt siz1,
                                       const char *ss2, ScmSmallInt siz2)
{
    unsigned char shift[256];
    for (ScmSmallInt i=0; i<256; i++) { shift[i] = siz2; }
    for (ScmSmallInt j=siz2-1; j>0; j--) {
        shift[(unsigned char)ss2[j]] = j;
    }
    for (ScmSmallInt i=siz1-siz2+1; i>=0; i-=shift[(unsigned char)ss1[i]]) {
        ScmSmallInt j, k;
        for (j=0, k = i; j<siz2 && ss1[k] == ss2[j]; j++, k++)
            ;
        if (j == siz2) return i;
    }
    return -1;
}

/* Primitive routines to search a substring s2 within s1.
   Returns NOT_FOUND if not found, FOUND_BOTH_INDEX if both byte index
   (*bi) and character index (*ci) is calculted, FOUND_BYTE_INDEX
   if only byte index is calculated.

   With utf-8, we can scan a string as if it si just a bytestring.  However,
   we need to calculate character index after we find the match.  It is still
   a total win, for finding out non-matches using Boyer-Moore is a lot
   faster than naive way.
 */

/* return value of string_scan */
#define NOT_FOUND 0         /* string not found */
#define FOUND_BOTH_INDEX 1  /* string found, and both indexes are calculated */
#define FOUND_BYTE_INDEX 2  /* string found, and only byte index is calc'd */

/* glibc has memrchr, but we need to provide fallback anyway and
   we don't need it to be highly tuned, so we just roll our own. */
static const void *my_memrchr(const void *s, int c, size_t n)
{
    const char *p = (const char*)s + n - 1;
    for (;p >= (const char*)s; p--) {
        if ((int)*p == c) return p;
    }
    return NULL;
}

/* NB: len1 and len2 only used in certain internal CES. */
static int string_search(const char *s1, ScmSmallInt siz1,
                         ScmSmallInt len1 SCM_UNUSED,
                         const char *s2, ScmSmallInt siz2,
                         ScmSmallInt len2 SCM_UNUSED,
                         ScmSmallInt *bi /* out */,
                         ScmSmallInt *ci /* out */)
{
    if (siz2 == 0) {
        *bi = *ci = 0;
        return FOUND_BOTH_INDEX;
    }

    if (siz2 == 1) {
        /* Single ASCII character search case.  This is a huge win. */
        const char *z = memchr(s1, s2[0], siz1);
        if (z) { *bi = *ci = z - s1; return FOUND_BYTE_INDEX; }
        else return NOT_FOUND;
    } else {
        ScmSmallInt i;
        /* Shortcut for single-byte strings */
        if (siz1 < siz2) return NOT_FOUND;
        if (siz1 < 256 || siz2 >= 256) {
            /* brute-force search */
            for (i=0; i<=siz1-siz2; i++) {
                if (memcmp(s2, s1+i, siz2) == 0) break;
            }
            if (i == siz1-siz2+1) return NOT_FOUND;
        } else {
            i = boyer_moore(s1, siz1, s2, siz2);
            if (i < 0) return NOT_FOUND;
        }
        *bi = *ci = i;
        return FOUND_BYTE_INDEX;
    }
}

/* NB: len2 is only used in some internal CES */
static int string_search_reverse(const char *s1, ScmSmallInt siz1,
                                 ScmSmallInt len1,
                                 const char *s2, ScmSmallInt siz2,
                                 ScmSmallInt len2 SCM_UNUSED,
                                 ScmSmallInt *bi /* out */,
                                 ScmSmallInt *ci /* out */)
{
    if (siz2 == 0) {
        *bi = siz1;
        *ci = len1;
        return FOUND_BOTH_INDEX;
    }

    if (siz2 == 1) {
        /* Single ASCII character search case.  This is a huge win. */
        const char *z = my_memrchr(s1, s2[0], siz1);
        if (z) { *bi = *ci = z - s1; return FOUND_BYTE_INDEX; }
        else return NOT_FOUND;
    } else {
        ScmSmallInt i;
        /* short cut for single-byte strings */
        if (siz1 < siz2) return NOT_FOUND;
        if (siz1 < 256 || siz2 >= 256) {
            /* brute-force search */
            for (i=siz1-siz2; i>=0; i--) {
                if (memcmp(s2, s1+i, siz2) == 0) break;
            }
            if (i < 0) return NOT_FOUND;
        } else {
            i = boyer_moore_reverse(s1, siz1, s2, siz2);
            if (i < 0) return NOT_FOUND;
        }
        *bi = *ci = i;
        return FOUND_BYTE_INDEX;
    }
}

/* Scan s2 in s1, and calculates appropriate return value(s) according to
   retmode.  Returns # of values, 1 or 2.

   SCM_STRING_SCAN_INDEX  : v1 <- the index of s1
        s1 = "abcde" and s2 = "cd" => 2
   SCM_STRING_SCAN_CURSOR : v1 <- the cursor of s1
        s1 = "abcde" and s2 = "cd" => #<string-cursor 2>
   SCM_STRING_SCAN_BEFORE : v1 <- substring of s1 before s2
        s1 = "abcde" and s2 = "cd" => "ab"
   SCM_STRING_SCAN_AFTER  : v1 <- substring of s1 after s2
        s1 = "abcde" and s2 = "cd" => "e"
   SCM_STRING_SCAN_BEFORE2 : v1 <- substring of s1 before s2, v2 <- rest
       s1 = "abcde" and s2 = "cd" => "ab" and "cde"
   SCM_STRING_SCAN_AFTER2 : v1 <- substring of s1 up to s2, v2 <- rest
       s1 = "abcde" and s2 = "cd" => "abcd" and "e"
   SCM_STRING_SCAN_BOTH   : v1 <- substring of s1 before, v2 <- after s2
       s1 = "abcde" and s2 = "cd" => "ab" and "e"
*/
static int string_scan(ScmString *ss1, const char *s2,
                       ScmSmallInt siz2, ScmSmallInt len2,
                       int incomplete2,
                       int retmode,
                       int (*searcher)(const char*, ScmSmallInt, ScmSmallInt,
                                       const char*, ScmSmallInt, ScmSmallInt,
                                       ScmSmallInt*, ScmSmallInt*),
                       ScmObj *v1,        /* out */
                       ScmObj *v2)        /* out */
{
    ScmSmallInt bi = 0, ci = 0;
    const ScmStringBody *sb = SCM_STRING_BODY(ss1);
    const char *s1 = SCM_STRING_BODY_START(sb);
    ScmSmallInt siz1 = SCM_STRING_BODY_SIZE(sb);
    ScmSmallInt len1 = SCM_STRING_BODY_LENGTH(sb);

    if (retmode < 0 || retmode >= SCM_STRING_SCAN_NUM_RETMODES) {
        Scm_Error("return mode out fo range: %d", retmode);
    }

    int incomplete =
        (SCM_STRING_BODY_INCOMPLETE_P(sb) || incomplete2)
        ? SCM_STRING_INCOMPLETE : 0;

    /* prefiltering - if both string is complete, and s1 is sbstring
       and s2 is mbstring, we know there's no match.  */
    int retcode =
        (!incomplete && (siz1 == len1) && (siz2 != len2))
        ? NOT_FOUND
        : searcher(s1, siz1, len1, s2, siz2, len2, &bi, &ci);

    if (retcode == NOT_FOUND) {
        switch (retmode) {
        case SCM_STRING_SCAN_INDEX:
        case SCM_STRING_SCAN_CURSOR:
        case SCM_STRING_SCAN_BEFORE:
        case SCM_STRING_SCAN_AFTER:
            *v1 = SCM_FALSE;
            return 1;
        default:
            *v1 = SCM_FALSE;
            *v2 = SCM_FALSE;
            return 2;
        }
    }

    if (retmode != SCM_STRING_SCAN_CURSOR
        && (retcode == FOUND_BYTE_INDEX && !incomplete)) {
        ci = count_length(s1, bi);
    }

    switch (retmode) {
    case SCM_STRING_SCAN_INDEX:
        *v1 = Scm_MakeInteger(ci);
        return 1;
    case SCM_STRING_SCAN_CURSOR:
        *v1 = make_string_cursor(ss1, s1 + bi);
        return 1;
    case SCM_STRING_SCAN_BEFORE:
        *v1 = Scm_MakeString(s1, bi, ci, incomplete);
        return 1;
    case SCM_STRING_SCAN_AFTER:
        *v1 = Scm_MakeString(s1+bi+siz2, siz1-bi-siz2,
                             len1-ci-len2, incomplete);
        return 1;
    case SCM_STRING_SCAN_BEFORE2:
        *v1 = Scm_MakeString(s1, bi, ci, incomplete);
        *v2 = Scm_MakeString(s1+bi, siz1-bi, len1-ci, incomplete);
        return 2;
    case SCM_STRING_SCAN_AFTER2:
        *v1 = Scm_MakeString(s1, bi+siz2, ci+len2, incomplete);
        *v2 = Scm_MakeString(s1+bi+siz2, siz1-bi-siz2,
                             len1-ci-len2, incomplete);
        return 2;
    case SCM_STRING_SCAN_BOTH:
        *v1 = Scm_MakeString(s1, bi, ci, incomplete);;
        *v2 = Scm_MakeString(s1+bi+siz2, siz1-bi-siz2,
                             len1-ci-len2, incomplete);
        return 2;
    }
    return 0;       /* dummy */
}

ScmObj Scm_StringScan(ScmString *s1, ScmString *s2, int retmode)
{
    ScmObj v1, v2;
    const ScmStringBody *s2b = SCM_STRING_BODY(s2);
    int nvals = string_scan(s1,
                            SCM_STRING_BODY_START(s2b),
                            SCM_STRING_BODY_SIZE(s2b),
                            SCM_STRING_BODY_LENGTH(s2b),
                            SCM_STRING_BODY_INCOMPLETE_P(s2b),
                            retmode, string_search, &v1, &v2);
    if (nvals == 1) return v1;
    else return Scm_Values2(v1, v2);
}

ScmObj Scm_StringScanChar(ScmString *s1, ScmChar ch, int retmode)
{
    ScmObj v1, v2;
    char buf[SCM_CHAR_MAX_BYTES];
    SCM_CHAR_PUT(buf, ch);
    int nvals = string_scan(s1, buf, SCM_CHAR_NBYTES(ch), 1, FALSE, retmode,
                            string_search, &v1, &v2);
    if (nvals == 1) return v1;
    else return Scm_Values2(v1, v2);
}

ScmObj Scm_StringScanRight(ScmString *s1, ScmString *s2, int retmode)
{
    ScmObj v1, v2;
    const ScmStringBody *s2b = SCM_STRING_BODY(s2);
    int nvals = string_scan(s1,
                            SCM_STRING_BODY_START(s2b),
                            SCM_STRING_BODY_SIZE(s2b),
                            SCM_STRING_BODY_LENGTH(s2b),
                            SCM_STRING_BODY_INCOMPLETE_P(s2b),
                            retmode, string_search_reverse, &v1, &v2);
    if (nvals == 1) return v1;
    else return Scm_Values2(v1, v2);
}

ScmObj Scm_StringScanCharRight(ScmString *s1, ScmChar ch, int retmode)
{
    ScmObj v1, v2;
    char buf[SCM_CHAR_MAX_BYTES];
    SCM_CHAR_PUT(buf, ch);
    int nvals = string_scan(s1, buf, SCM_CHAR_NBYTES(ch), 1, FALSE, retmode,
                            string_search_reverse, &v1, &v2);
    if (nvals == 1) return v1;
    else return Scm_Values2(v1, v2);
}

#undef NOT_FOUND
#undef FOUND_BOTH_INDEX
#undef FOUND_BYTE_INDEX

/* Split string by char.  Char itself is not included in the result.
   If LIMIT >= 0, up to that number of matches are considered (i.e.
   up to LIMIT+1 strings are returned).   LIMIT < 0 makes the number
   of matches unlimited.
   TODO: If CH is a utf-8 multi-byte char, Boyer-Moore skip table is
   calculated every time we call string_scan, which is a waste.  Some
   mechanism to cache the skip table would be nice.
*/
ScmObj Scm_StringSplitByCharWithLimit(ScmString *str, ScmChar ch, int limit)
{
    char buf[SCM_CHAR_MAX_BYTES];
    int nb = SCM_CHAR_NBYTES(ch);
    ScmObj head = SCM_NIL, tail = SCM_NIL;

    if (limit == 0) return SCM_LIST1(SCM_OBJ(str)); /* trivial case */

    SCM_CHAR_PUT(buf, ch);

    for (;;) {
        ScmObj v1, v2;
        (void)string_scan(str, buf, nb, 1, FALSE, SCM_STRING_SCAN_BOTH,
                          string_search, &v1, &v2);
        if (SCM_FALSEP(v1)) {
            SCM_APPEND1(head, tail, SCM_OBJ(str));
            break;
        } else {
            SCM_APPEND1(head, tail, v1);
            if (--limit == 0) { SCM_APPEND1(head, tail, v2); break; }
        }
        str = SCM_STRING(v2);
    }
    return head;
}

/* For ABI compatibility - On 1.0, let's make this have limit arg and
   drop Scm_StringSplitByCharWithLimit.  */
ScmObj Scm_StringSplitByChar(ScmString *str, ScmChar ch)
{
    return Scm_StringSplitByCharWithLimit(str, ch, -1);
}

/*----------------------------------------------------------------
 * Miscellaneous functions
 */

ScmObj Scm_StringToList(ScmString *str)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    ScmObj start = SCM_NIL, end = SCM_NIL;
    const char *bufp = SCM_STRING_BODY_START(b);
    ScmSmallInt len = SCM_STRING_BODY_LENGTH(b);

    if (SCM_STRING_BODY_INCOMPLETE_P(b))
        Scm_Error("incomplete string not supported: %S", str);
    while (len-- > 0) {
        ScmChar ch;
        SCM_CHAR_GET(bufp, ch);
        bufp += SCM_CHAR_NBYTES(ch);
        SCM_APPEND1(start, end, SCM_MAKE_CHAR(ch));
    }
    return start;
}

/* Convert cstring array to a list of Scheme strings.  Cstring array
   can be NULL terminated (in case size < 0) or its size is explicitly
   specified (size >= 0).  FLAGS is passed to Scm_MakeString. */
ScmObj Scm_CStringArrayToList(const char **array, ScmSmallInt size, u_long flags)
{
    ScmObj h = SCM_NIL, t = SCM_NIL;
    if (size < 0) {
        for (;*array; array++) {
            ScmObj s = Scm_MakeString(*array, -1, -1, flags);
            SCM_APPEND1(h, t, s);
        }
    } else {
        for (ScmSmallInt i=0; i<size; i++, array++) {
            ScmObj s = Scm_MakeString(*array, -1, -1, flags);
            SCM_APPEND1(h, t, s);
        }
    }
    return h;
}

/* common routine for Scm_ListTo[Const]CStringArray */
static ScmSmallInt list_to_cstring_array_check(ScmObj lis, int errp)
{
    ScmObj lp;
    ScmSmallInt len = 0;
    SCM_FOR_EACH(lp, lis) {
        if (!SCM_STRINGP(SCM_CAR(lp))) {
            if (errp) Scm_Error("a proper list of strings is required, but the list contains non-string element: %S", SCM_CAR(lp));
            else return -1;
        }
        len++;
    }
    return len;
}

/* Convert list of Scheme strings into C const char* string array, NULL
   terminated.
   If errp == FALSE, returns NULL on error.
   otherwise, signals an error. */
const char **Scm_ListToConstCStringArray(ScmObj lis, int errp)
{
    ScmSmallInt len = list_to_cstring_array_check(lis, errp);
    if (len < 0) return NULL;
    const char **array = SCM_NEW_ARRAY(const char*, len+1);
    const char **p = array;
    ScmObj lp;
    SCM_FOR_EACH(lp, lis) {
        *p++ = Scm_GetStringConst(SCM_STRING(SCM_CAR(lp)));
    }
    *p = NULL;                  /* termination */
    return array;
}

/* Convert list of Scheme strings into C char* string array, NULL
   terminated.
   If errp == FALSE, returns NULL on error.
   otherwise, signals an error.
   If provided, alloc is used to allocate both a pointer array and char
   arrays.  Otherwise, SCM_ALLOC is used. */
char **Scm_ListToCStringArray(ScmObj lis, int errp, void *(*alloc)(size_t))
{
    char **array, **p;
    ScmSmallInt len = list_to_cstring_array_check(lis, errp);
    if (len < 0) return NULL;

    if (alloc) {
        p = array = (char **)alloc((len+1) * sizeof(char *));
        ScmObj lp;
        SCM_FOR_EACH(lp, lis) {
            const char *s = Scm_GetStringConst(SCM_STRING(SCM_CAR(lp)));
            *p = (char *)alloc(strlen(s) + 1);
            strcpy(*p, s);
            p++;
        }
    } else {
        p = array = SCM_NEW_ARRAY(char*, len+1);
        ScmObj lp;
        SCM_FOR_EACH(lp, lis) {
            *p++ = Scm_GetString(SCM_STRING(SCM_CAR(lp)));
        }
    }
    *p = NULL;                  /* termination */
    return array;
}

/*----------------------------------------------------------------
 * printer
 */

/* ch is single byte if bytemode is true */
static inline void string_putc(ScmChar ch, ScmPort *port, int bytemode)
{
    const int ESCAPE_BUF_MAX = 12;
    char buf[ESCAPE_BUF_MAX];
    switch (ch) {
    case '\\': SCM_PUTZ("\\\\", -1, port); break;
    case '"':  SCM_PUTZ("\\\"", -1, port); break;
    case '\n': SCM_PUTZ("\\n", -1, port); break;
    case '\t': SCM_PUTZ("\\t", -1, port); break;
    case '\r': SCM_PUTZ("\\r", -1, port); break;
    case '\f': SCM_PUTZ("\\f", -1, port); break;
    case '\0': SCM_PUTZ("\\0", -1, port); break;
    default:
        if (ch < 0x80 || bytemode) {
            if (ch < ' ' || ch == 0x7f || bytemode) {
                /* TODO: Should we provide 'legacy-compatible writer mode,
                   which does not use ';' terminator? */
                snprintf(buf, ESCAPE_BUF_MAX, "\\x%02x;", (unsigned char)ch);
                SCM_PUTZ(buf, -1, port);
            } else {
                SCM_PUTC(ch, port);
            }
        } else {
            switch (Scm_CharGeneralCategory(ch)) {
            case SCM_CHAR_CATEGORY_Cc:
            case SCM_CHAR_CATEGORY_Cf:
            case SCM_CHAR_CATEGORY_Cs:
            case SCM_CHAR_CATEGORY_Co:
            case SCM_CHAR_CATEGORY_Cn:
                if (ch < 0x10000) {
                    snprintf(buf, ESCAPE_BUF_MAX, "\\x%04x;", (u_int)ch);
                } else {
                    snprintf(buf, ESCAPE_BUF_MAX, "\\x%x;", (u_int)ch);
                }
                SCM_PUTZ(buf, -1, port);
                break;
            default:
                SCM_PUTC(ch, port);
            }
        }
    }
}

static void string_print(ScmObj obj, ScmPort *port, ScmWriteContext *ctx)
{
    ScmString *str = SCM_STRING(obj);
    int limit = (ctx->controls? ctx->controls->stringLength : -1);
    int trimmed = FALSE;

    if (Scm_WriteContextMode(ctx) == SCM_WRITE_DISPLAY) {
        /* Display mode isn't affected by string-length control */
        SCM_PUTS(str, port);
    } else {
        const ScmStringBody *b = SCM_STRING_BODY(str);
        if (SCM_STRING_BODY_SINGLE_BYTE_P(b)) {
            const char *cp = SCM_STRING_BODY_START(b);
            ScmSmallInt size = SCM_STRING_BODY_SIZE(b);
            if (limit >= 0 && limit < size) {
                trimmed = TRUE; size = limit;
            }

            if (SCM_STRING_BODY_INCOMPLETE_P(b)) {
                /* TODO: Should we provide legacy-compatible writer mode,
                   which puts #*"..." instead? */
                SCM_PUTZ("#**\"", -1, port);
            } else {
                SCM_PUTC('"', port);
            }
            while (--size >= 0) {
                string_putc(*cp++, port, SCM_STRING_BODY_INCOMPLETE_P(b));
            }
        } else {
            const char *cp = SCM_STRING_BODY_START(b);
            ScmSmallInt len = SCM_STRING_BODY_LENGTH(b);
            if (limit >= 0 && limit < len) {
                trimmed = TRUE; len = limit;
            }

            SCM_PUTC('"', port);
            while (--len >= 0) {
                ScmChar ch;
                SCM_CHAR_GET(cp, ch);
                string_putc(ch, port, FALSE);
                cp += SCM_CHAR_NBYTES(ch);
            }
        }
        if (trimmed) {
            SCM_PUTZ(SCM_WRITTEN_ELLIPSIS, -1, port);
        }
        SCM_PUTC('"', port);
    }
}

/*==================================================================
 *
 * String index building
 *
 */

static int string_body_index_needed(const ScmStringBody *sb)
{
    return (!SCM_STRING_BODY_SINGLE_BYTE_P(sb)
            && !SCM_STRING_BODY_INCOMPLETE_P(sb)
            && SCM_STRING_BODY_SIZE(sb) >= 64);
}

int Scm_StringBodyFastIndexableP(const ScmStringBody *sb)
{
    return (!string_body_index_needed(sb)
            || SCM_STRING_BODY_HAS_INDEX(sb));
}

static size_t compute_index_size(const ScmStringBody *sb, int interval)
{
    ScmSmallInt len = SCM_STRING_BODY_LENGTH(sb);
    /* We don't store the first entry (0th character == 0th byte), and
       we use two extra entry for the signature and index_size.  So
       we need +1. */
    return ((len + interval - 1)/interval) + 1;
}

static void *build_index_array(const ScmStringBody *sb)
{
    /* Signature byte is repeated in the first element of the vector */
#define SIG8(type,sig)    (type)(sig)
#define SIG16(type,sig)   ((type)((sig)<<8)|(sig))
#define SIG32(type,sig)   ((type)(SIG16(type,sig)<<16)|SIG16(type,sig))
#define SIG64(type,sig)   ((type)(SIG32(type,sig)<<32)|SIG32(type,sig))

#define BUILD_ARRAY(type_, typeenum_, shift_, sigrep_)                  \
    do {                                                                \
        int interval = 1 << (shift_);                                   \
        size_t index_size = compute_index_size(sb, interval);           \
        type_ *vec = SCM_NEW_ATOMIC_ARRAY(type_, index_size);           \
        u_long sig = STRING_INDEX_SIGNATURE(shift_, typeenum_);         \
        vec[0] = sigrep_(type_,sig);                                    \
        vec[1] = (type_)index_size;                                     \
        const char *p = SCM_STRING_BODY_START(sb);                      \
        for (size_t i = 2; i < index_size; i++) {                       \
            const char *q = forward_pos(sb, p, interval);               \
            vec[i] = (type_)(q - SCM_STRING_BODY_START(sb));            \
            p = q;                                                      \
        }                                                               \
        return vec;                                                     \
    } while (0)

    /* Technically we can use index8 even if size is bigger than 256,
       as long as the last indexed character is within the range.  But
       checking it is too much. */
    if (sb->size < 256) {
        BUILD_ARRAY(uint8_t, STRING_INDEX8, 4, SIG8);
    } else if (sb->size < 8192) {
        /* 32 chars interval */
        BUILD_ARRAY(uint16_t, STRING_INDEX16, 5, SIG16);
    } else if (sb->size < 65536) {
        /* 64 chars interval */
        BUILD_ARRAY(uint16_t, STRING_INDEX16, 6, SIG16);
    }
#if SIZEOF_LONG == 4
    else {
        /* 128 chars interval */
        BUILD_ARRAY(uint32_t, STRING_INDEX32, 7, SIG32);
    }
#else /* SIZEOF_LONG != 4 */
    else if (sb->size < (1L<<32)) {
        /* 128 chars interval */
        BUILD_ARRAY(uint32_t, STRING_INDEX32, 7, SIG32);
    } else {
        /* 256 chars interval */
        BUILD_ARRAY(uint64_t, STRING_INDEX64, 8, SIG64);
    }
#endif
#undef BUILD_ARRAY
}

void Scm_StringBodyBuildIndex(ScmStringBody *sb)
{
    if (!string_body_index_needed(sb) || SCM_STRING_BODY_HAS_INDEX(sb)) return;
    /* This is idempotent, atomic operation; no need to lock.  */
    sb->index = build_index_array(sb);
}

/* For debugging */
void Scm_StringBodyIndexDump(const ScmStringBody *sb, ScmPort *port)
{
    ScmStringIndex *index = STRING_INDEX(sb->index);
    if (index == NULL) {
        Scm_Printf(port, "(nil)\n");
        return;
    }
    int interval = STRING_INDEX_INTERVAL(index);
    size_t index_size = 0;

    switch (STRING_INDEX_TYPE(index)) {
    case STRING_INDEX8:
        Scm_Printf(port, "index8  ");
        index_size = (size_t)index->index8[1];
        break;
    case STRING_INDEX16:
        Scm_Printf(port, "index16 ");
        index_size = (size_t)index->index16[1];
        break;
    case STRING_INDEX32:
        Scm_Printf(port, "index32 ");
        index_size = (size_t)index->index32[1];
        break;
    case STRING_INDEX64:
        Scm_Printf(port, "index64 ");
        index_size = (size_t)index->index64[1];
        break;
    default:
        Scm_Printf(port, "unknown(%02x) ", (uint8_t)STRING_INDEX_TYPE(index));
    }
    Scm_Printf(port, " interval %d  size %d\n", interval, index_size-1);
    Scm_Printf(port, "        0         0\n");
    for (size_t i = 2; i < index_size; i++) {
        switch (STRING_INDEX_TYPE(index)) {
        case STRING_INDEX8:
            Scm_Printf(port, " %8ld  %8u\n", i-1, index->index8[i]); break;
        case STRING_INDEX16:
            Scm_Printf(port, " %8ld  %8u\n", i-1, index->index16[i]); break;
        case STRING_INDEX32:
            Scm_Printf(port, " %8ld  %8u\n", i-1, index->index32[i]); break;
        case STRING_INDEX64:
            Scm_Printf(port, " %8ld  %8lu\n",i-1, index->index64[i]); break;
        }
    }
}


/*==================================================================
 *
 * String cursor API
 *
 */

/* Public interface */
int Scm_StringCursorP(ScmObj obj)
{
    return SCM_STRING_CURSOR_P(obj);
}

static ScmObj make_string_cursor(ScmString *src, const char *ptr)
{
    const ScmStringBody *srcb = SCM_STRING_BODY(src);

    if (ptr < SCM_STRING_BODY_START(srcb) ||
        ptr > SCM_STRING_BODY_END(srcb)) {
        Scm_Error("cursor out of range of %S: %ld",
                  SCM_OBJ(src),
                  (ScmSmallInt)(ptr - SCM_STRING_BODY_START(srcb)));
    }

    ScmSmallInt offset = (ScmSmallInt)(ptr - SCM_STRING_BODY_START(srcb));
    if (!SCM_VM_RUNTIME_FLAG_IS_SET(Scm_VM(), SCM_SAFE_STRING_CURSORS) &&
        SCM_STRING_CURSOR_FITS_SMALL_P(offset)) {
        return SCM_MAKE_STRING_CURSOR_SMALL(offset);
    }

    ScmStringCursorLarge *sc = SCM_NEW(ScmStringCursorLarge);
    SCM_SET_CLASS(sc, SCM_CLASS_STRING_CURSOR_LARGE);
    sc->offset = offset;
    sc->start = SCM_STRING_BODY_START(srcb);
    return SCM_OBJ(sc);
}

ScmObj Scm_MakeStringCursorFromIndex(ScmString *src, ScmSmallInt index)
{
    const ScmStringBody *srcb = SCM_STRING_BODY(src);
    ScmSmallInt len = SCM_STRING_BODY_LENGTH(srcb);
    if (index < 0 || index > len) {
        Scm_Error("index out of range: %ld", index);
    }
    return make_string_cursor(src, index2ptr(srcb, index));
}

ScmObj Scm_MakeStringCursorEnd(ScmString *src)
{
    const ScmStringBody *srcb = SCM_STRING_BODY(src);

    ScmSmallInt offset = SCM_STRING_BODY_END(srcb) - SCM_STRING_BODY_START(srcb);
    if (!SCM_VM_RUNTIME_FLAG_IS_SET(Scm_VM(), SCM_SAFE_STRING_CURSORS) &&
        SCM_STRING_CURSOR_FITS_SMALL_P(offset)) {
        return SCM_MAKE_STRING_CURSOR_SMALL(offset);
    }
    ScmStringCursorLarge *sc = SCM_NEW(ScmStringCursorLarge);
    SCM_SET_CLASS(sc, SCM_CLASS_STRING_CURSOR_LARGE);
    sc->offset = offset;
    sc->start = SCM_STRING_BODY_START(srcb);
    return SCM_OBJ(sc);
}

ScmObj Scm_StringCursorIndex(ScmString *src, ScmObj sc)
{
    if (SCM_INTP(sc) || SCM_BIGNUMP(sc)) {
        return sc;              /* no validation */
    }

    const ScmStringBody *srcb = SCM_STRING_BODY(src);
    const char          *ptr  = NULL;

    if ((ptr = string_cursor_ptr(srcb, sc)) == NULL) {
        Scm_Error("must be either an index or a cursor: %S", sc);
    }

    if (SCM_STRING_BODY_SINGLE_BYTE_P(srcb) ||
        SCM_STRING_BODY_INCOMPLETE_P(srcb)) {
        return SCM_MAKE_INT(ptr - SCM_STRING_BODY_START(srcb));
    }

    const char *current = SCM_STRING_BODY_START(srcb);
    ScmSmallInt len     = SCM_STRING_BODY_LENGTH(srcb);
    ScmSmallInt index   = 0;
    while (index < len && current < ptr) {
        current += SCM_CHAR_NFOLLOWS(*current) + 1;
        index++;
    }
    if (current != ptr) {
        Scm_Error("cursor not pointed at the beginning of a character: %S", sc);
    }

    return SCM_MAKE_INT(index);
}

ScmObj Scm_StringCursorForward(ScmString* s, ScmObj sc, int nchars)
{
    if (nchars < 0) {
        Scm_Error("nchars is negative: %ld", nchars);
    }

    if (SCM_INTEGERP(sc)) {
        return Scm_MakeStringCursorFromIndex(s, Scm_GetInteger(sc) + nchars);
    }

    const ScmStringBody *srcb = SCM_STRING_BODY(s);
    const char *ptr = string_cursor_ptr(srcb, sc);
    if (ptr == NULL) {
        Scm_Error("must be either an index or a cursor: %S", sc);
    }
    return make_string_cursor(s, forward_pos(srcb, ptr, nchars));
}

ScmObj Scm_StringCursorBack(ScmString* s, ScmObj sc, int nchars)
{
    if (nchars < 0) {
        Scm_Error("nchars is negative: %ld", nchars);
    }

    if (SCM_INTP(sc) || SCM_BIGNUMP(sc)) {
        return Scm_MakeStringCursorFromIndex(s, Scm_GetInteger(sc) - nchars);
    }

    const ScmStringBody *srcb = SCM_STRING_BODY(s);
    const char *ptr = string_cursor_ptr(srcb, sc);
    if (ptr == NULL) {
        Scm_Error("must be either an index or a cursor: %S", sc);
    }

    if (SCM_STRING_BODY_SINGLE_BYTE_P(srcb) ||
        SCM_STRING_BODY_INCOMPLETE_P(srcb)) {
        return make_string_cursor(s, ptr - nchars);
    }

    while (nchars--) {
        const char *prev;
        SCM_CHAR_BACKWARD(ptr, SCM_STRING_BODY_START(srcb), prev);
        if (!prev) {
            Scm_Error("nchars out of range: %ld", nchars);
        }
        ptr = prev;
    }

    return make_string_cursor(s, ptr);
}

ScmChar Scm_StringRefCursor(ScmString* s, ScmObj sc, int range_error)
{
    if (SCM_INTP(sc)) {
        return Scm_StringRef(s, SCM_INT_VALUE(sc), range_error);
    }

    const ScmStringBody *srcb = SCM_STRING_BODY(s);

    /* we can't allow string-ref on incomplete strings, since it may yield
       invalid character object. */
    if (SCM_STRING_BODY_INCOMPLETE_P(srcb)) {
        Scm_Error("incomplete string not allowed : %S", s);
    }

    const char *ptr = string_cursor_ptr(srcb, sc);
    if (ptr == NULL) {
        Scm_Error("must be either an index or a cursor: %S", sc);
    }
    if (ptr == SCM_STRING_BODY_END(srcb)) {
        if (range_error) {
            Scm_Error("cursor is at the end: %S", sc);
        }
        return SCM_CHAR_INVALID;
    }
    ScmChar ch;
    SCM_CHAR_GET(ptr, ch);
    return ch;
}

ScmObj Scm_SubstringCursor(ScmString *str,
                           ScmObj start_scm, ScmObj end_scm)
{
    const ScmStringBody *sb = SCM_STRING_BODY(str);
    const char *start = string_cursor_ptr(sb, start_scm);
    const char *end   = string_cursor_ptr(sb, end_scm);

    if (start && end) {
        return substring_cursor(sb, start, end, FALSE);
    }

    return substring(SCM_STRING_BODY(str),
                     Scm_GetInteger(Scm_StringCursorIndex(str, start_scm)),
                     Scm_GetInteger(Scm_StringCursorIndex(str, end_scm)),
                     FALSE, FALSE);
}

int Scm_StringCursorCompare(ScmObj sc1, ScmObj sc2,
                            int (*numcmp)(ScmObj, ScmObj))
{
    /*
     * Handle indexes separately, we can't mix index and cursor
     * because cursor is byte offset, not index.
     */
    if (SCM_INTP(sc1) && SCM_INTP(sc2)) {
        return numcmp(sc1, sc2);
    }

    ScmSmallInt i1 = string_cursor_offset(sc1);
    ScmSmallInt i2 = string_cursor_offset(sc2);
    if (i1 < 0 || i2 < 0) {
        Scm_Error("arguments must be either both cursors or both indexes: %S vs %S", sc1, sc2);
    }
    return numcmp(SCM_MAKE_INT(i1), SCM_MAKE_INT(i2));
}

/*==================================================================
 *
 * Dynamic strings
 *
 */

/* I used to use realloc() to grow the storage; now I avoid it, for
   Boehm GC's realloc almost always copies the original content and
   we don't get any benefit.
   The growing string is kept in the chained chunks.  The size of
   chunk getting bigger as the string grows, until a certain threshold.
   The memory for actual chunks and the chain is allocated separately,
   in order to use SCM_NEW_ATOMIC.
 */

/* NB: it is important that DString functions don't call any
 * time-consuming procedures except memory allocation.   Some of
 * mutex code in other parts relies on that fact.
 */

/* maximum chunk size */
#define DSTRING_MAX_CHUNK_SIZE  8180

void Scm_DStringInit(ScmDString *dstr)
{
    dstr->init.bytes = 0;
    dstr->anchor = dstr->tail = NULL;
    dstr->current = dstr->init.data;
    dstr->end = dstr->current + SCM_DSTRING_INIT_CHUNK_SIZE;
    dstr->lastChunkSize = SCM_DSTRING_INIT_CHUNK_SIZE;
    dstr->length = 0;
}

ScmSmallInt Scm_DStringSize(ScmDString *dstr)
{
    ScmSmallInt size;
    if (dstr->tail) {
        size = dstr->init.bytes;
        dstr->tail->chunk->bytes = dstr->current - dstr->tail->chunk->data;
        for (ScmDStringChain *chain = dstr->anchor; chain; chain = chain->next) {
            size += chain->chunk->bytes;
        }
    } else {
        size = dstr->init.bytes = dstr->current - dstr->init.data;
    }
    if (size > SCM_STRING_MAX_SIZE) {
        Scm_Error("Scm_DStringSize: size exceeded the range: %ld", size);
    }
    return size;
}

static ScmDStringChunk *newChunk(ScmSmallInt size)
{
    return SCM_NEW_ATOMIC2(ScmDStringChunk*,
                           (sizeof(ScmDStringChunk)
                            +size-SCM_DSTRING_INIT_CHUNK_SIZE));
}

void Scm__DStringRealloc(ScmDString *dstr, ScmSmallInt minincr)
{
    /* sets the byte count of the last chunk */
    if (dstr->tail) {
        dstr->tail->chunk->bytes = dstr->current - dstr->tail->chunk->data;
    } else {
        dstr->init.bytes = dstr->current - dstr->init.data;
    }

    /* determine the size of the new chunk.  the increase factor 3 is
       somewhat arbitrary, determined by rudimental benchmarking. */
    ScmSmallInt newsize = dstr->lastChunkSize * 3;
    if (newsize > DSTRING_MAX_CHUNK_SIZE) {
        newsize = DSTRING_MAX_CHUNK_SIZE;
    }
    if (newsize < minincr) {
        newsize = minincr;
    }

    ScmDStringChunk *newchunk = newChunk(newsize);
    newchunk->bytes = 0;
    ScmDStringChain *newchain = SCM_NEW(ScmDStringChain);

    newchain->next = NULL;
    newchain->chunk = newchunk;
    if (dstr->tail) {
        dstr->tail->next = newchain;
        dstr->tail = newchain;
    } else {
        dstr->anchor = dstr->tail = newchain;
    }
    dstr->current = newchunk->data;
    dstr->end = newchunk->data + newsize;
    dstr->lastChunkSize = newsize;
}

/* Retrieve accumulated string. */
static const char *dstring_getz(ScmDString *dstr,
                                ScmSmallInt *psiz,
                                ScmSmallInt *plen,
                                int noalloc)
{
    ScmSmallInt size, len;
    char *buf;
    if (dstr->anchor == NULL) {
        /* we only have one chunk */
        size = dstr->current - dstr->init.data;
        CHECK_SIZE(size);
        len = dstr->length;
        if (noalloc) {
            buf = dstr->init.data;
        } else {
            buf = SCM_STRDUP_PARTIAL(dstr->init.data, size);
        }
    } else {
        ScmDStringChain *chain = dstr->anchor;
        char *bptr;

        size = Scm_DStringSize(dstr);
        CHECK_SIZE(size);
        len = dstr->length;
        bptr = buf = SCM_NEW_ATOMIC2(char*, size+1);

        memcpy(bptr, dstr->init.data, dstr->init.bytes);
        bptr += dstr->init.bytes;
        for (; chain; chain = chain->next) {
            memcpy(bptr, chain->chunk->data, chain->chunk->bytes);
            bptr += chain->chunk->bytes;
        }
        *bptr = '\0';
    }
    if (len < 0) len = count_length(buf, size);
    if (plen) *plen = len;
    if (psiz) *psiz = size;
    return buf;
}

ScmObj Scm_DStringGet(ScmDString *dstr, u_long flags)
{
    ScmSmallInt len, size;
    const char *str = dstring_getz(dstr, &size, &len, FALSE);
    return SCM_OBJ(make_str(len, size, str, flags|SCM_STRING_TERMINATED, NULL));
}

/* For conveninence.   Note that dstr may already contain NUL byte in it,
   in that case you'll get chopped string. */
const char *Scm_DStringGetz(ScmDString *dstr)
{
    ScmSmallInt len, size;
    return dstring_getz(dstr, &size, &len, FALSE);
}

/* Concatenate all chains in DString into one chunk.  Externally nothing
   really changes, but this can be used to optimize allocation. */
void Scm_DStringWeld(ScmDString *dstr)
{
    if (dstr->anchor == NULL) return; /* nothing to do */
    ScmDStringChain *chain = dstr->anchor;
    ScmSmallInt size = Scm_DStringSize(dstr);
    ScmSmallInt bufsiz = size + (dstr->end - dstr->current);
    ScmDStringChunk *newchunk = newChunk(bufsiz);
    newchunk->bytes = size;
    char *bptr = newchunk->data;
    memcpy(bptr, dstr->init.data, dstr->init.bytes);
    bptr += dstr->init.bytes;
    for (; chain; chain = chain->next) {
        memcpy(bptr, chain->chunk->data, chain->chunk->bytes);
        bptr += chain->chunk->bytes;
    }
    dstr->init.bytes = 0;
    dstr->anchor->chunk = newchunk;
    dstr->anchor->next = NULL;
    dstr->tail = dstr->anchor;
    dstr->current = newchunk->data + size;
    dstr->end = newchunk->data + bufsiz;
    dstr->lastChunkSize = bufsiz;
}

/* Returns the current content of DString, along with byte size and character
   length. The returned pointer may not be NUL-terminated.

   Unlike Scm_DStringGet[z], returned pointer can directly points into
   the internal buffer of Scm_DString; especially, this never allocates
   if DString only uses initial buffer.  The caller should be aware that
   the returned content may be altered by further DString operation. */
const char *Scm_DStringPeek(ScmDString *dstr,
                            ScmSmallInt *size, ScmSmallInt *len)
{
    Scm_DStringWeld(dstr);
    if (dstr->anchor == NULL) {
        if (size) *size = dstr->current - dstr->init.data;
        if (len)  *len = dstr->length;
        return dstr->init.data;
    } else {
        if (size) *size = dstr->anchor->chunk->bytes;
        if (len)  *len = dstr->length;
        return dstr->anchor->chunk->data;
    }
}

void Scm_DStringPutz(ScmDString *dstr, const char *str, ScmSmallInt size)
{
    if (size < 0) size = strlen(str);
    if (dstr->current + size > dstr->end) {
        Scm__DStringRealloc(dstr, size);
    }
    memcpy(dstr->current, str, size);
    dstr->current += size;
    if (dstr->length >= 0) {
        ScmSmallInt len = count_length(str, size);
        if (len >= 0) dstr->length += len;
        else dstr->length = -1;
    }
}

void Scm_DStringAdd(ScmDString *dstr, ScmString *str)
{
    const ScmStringBody *b = SCM_STRING_BODY(str);
    ScmSmallInt size = SCM_STRING_BODY_SIZE(b);
    if (size == 0) return;
    if (dstr->current + size > dstr->end) {
        Scm__DStringRealloc(dstr, size);
    }
    memcpy(dstr->current, SCM_STRING_BODY_START(b), size);
    dstr->current += size;
    if (dstr->length >= 0 && !SCM_STRING_BODY_INCOMPLETE_P(b)) {
        dstr->length += SCM_STRING_BODY_LENGTH(b);
    } else {
        dstr->length = -1;
    }
}

void Scm_DStringPutb(ScmDString *ds, char byte)
{
    SCM_DSTRING_PUTB(ds, byte);
}

void Scm_DStringPutc(ScmDString *ds, ScmChar ch)
{
    SCM_DSTRING_PUTC(ds, ch);
}

/* Truncate DString at the specified size.
   Returns after-truncation size (it may be smaller than newsize if
   the original DString isn't as large as newsize. */
ScmSmallInt Scm_DStringTruncate(ScmDString *dstr, ScmSmallInt newsize)
{
    ScmSmallInt origsize = Scm_DStringSize(dstr);

    if (newsize < dstr->init.bytes) {
        dstr->init.bytes = newsize;
        dstr->anchor = NULL;
        dstr->tail = NULL;
        dstr->current = dstr->init.data + newsize;
        dstr->end = dstr->init.data + SCM_DSTRING_INIT_CHUNK_SIZE;
    } else {
        if (newsize >= origsize) return origsize;
        ScmDStringChain *chain = dstr->anchor;
        ScmSmallInt ss = dstr->init.bytes;
        for (; chain; chain = chain->next) {
            if (newsize < ss + chain->chunk->bytes) {
                /* truncate this chunk */
                if (chain == dstr->tail) {
                    chain->chunk->bytes = newsize - ss;
                    dstr->current = chain->chunk->data + newsize - ss;
                } else {
                    dstr->lastChunkSize = chain->chunk->bytes;
                    dstr->end = chain->chunk->data + chain->chunk->bytes;
                    chain->chunk->bytes = newsize - ss;
                    chain->next = NULL;
                    dstr->tail = chain;
                    dstr->current = chain->chunk->data + newsize - ss;
                }
                break;
            }
            ss += chain->chunk->bytes;
        }
        SCM_ASSERT(chain != NULL);
    }

    /* If we accumulated only ASCII, we can adjust length as well. */
    if (dstr->length == origsize || newsize == 0) dstr->length = newsize;
    else                                          dstr->length = -1;
    return newsize;
}


/* for debug */
void Scm_DStringDump(FILE *out, ScmDString *dstr)
{
    fprintf(out, "DString %p\n", dstr);
    if (dstr->anchor) {
        fprintf(out, "  chunk0[%3ld] = \"", dstr->init.bytes);
        SCM_IGNORE_RESULT(fwrite(dstr->init.data, 1, dstr->init.bytes, out));
        fprintf(out, "\"\n");
        ScmDStringChain *chain = dstr->anchor;
        for (int i=1; chain; chain = chain->next, i++) {
            ScmSmallInt size = (chain->next? chain->chunk->bytes : (dstr->current - dstr->tail->chunk->data));
            fprintf(out, "  chunk%d[%3ld] = \"", i, size);
            SCM_IGNORE_RESULT(fwrite(chain->chunk->data, 1, size, out));
            fprintf(out, "\"\n");
        }
    } else {
        ScmSmallInt size = dstr->current - dstr->init.data;
        fprintf(out, "  chunk0[%3ld] = \"", size);
        SCM_IGNORE_RESULT(fwrite(dstr->init.data, 1, size, out));
        fprintf(out, "\"\n");
    }
}
