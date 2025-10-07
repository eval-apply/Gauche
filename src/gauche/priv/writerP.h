/*
 * writerP.h - Writer private API
 *
 *   Copyright (c) 2013-2025  Shiro Kawai  <shiro@acm.org>
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

#ifndef GAUCHE_PRIV_WRITERP_H
#define GAUCHE_PRIV_WRITERP_H

#include <gauche/number.h>      /* for ScmNumberFormat */

/* Writer control parameters */
struct ScmWriteControlsRec {
    SCM_HEADER;
    int printLength;            /* -1 for no limit */
    int printLevel;             /* -1 for no limit */
    int printWidth;             /* -1 for no limit */
    int printBase;              /* 2-36 */
    int printRadix;             /* boolean, #t to print radix for all numbers */
    int printPretty;            /* boolean, #t to use pretty printer */
    int printIndent;            /* >=0 extra indent to be added after each
                                   newline when pretty printing. */
    int bytestring;             /* boolean, #t to use bytestring repr for
                                   u8vector (srfi-207) */
    int stringLength;           /* -1 for no limit.  Length of literal string */
    int exactDecimal;           /* #t to use decimal point for exact numbers
                                   whenever possible. */
    int arrayFormat;            /* enum ScmWriteArrayFormat */
    int complexFormat;          /* enum ScmWriteComplexFormat */
    ScmNumberFormat numberFormat; /* number formatting */
};

SCM_CLASS_DECL(Scm_WriteControlsClass);
#define SCM_CLASS_WRITE_CONTROLS  (&Scm_WriteControlsClass)
#define SCM_WRITE_CONTROLS(obj)   ((ScmWriteControls*)(obj))
#define SCM_WRITE_CONTROLS_P(obj) SCM_XTYPEP(obj, SCM_CLASS_WRITE_CONTROLS)

enum ScmWriteArrayFormat {
    SCM_WRITE_ARRAY_COMPACT,     /* #2a(...) */
    SCM_WRITE_ARRAY_DIMENSIONS,  /* #2a:3:3(...) */
    SCM_WRITE_ARRAY_READER_CTOR, /* #,(<array> (0 3 0 3) ...) */
};

enum ScmWriteComplexFormat {
    SCM_WRITE_COMPLEX_RECTANGULAR, /* a+bi */
    SCM_WRITE_COMPLEX_POLAR,       /* a@b */
    SCM_WRITE_COMPLEX_POLAR_PI,    /* a@bpi */
    SCM_WRITE_COMPLEX_COMMON_LISP, /* #c(a b) */
};

/*
 * NB: Flip the following condition to use ellipsis (U+2026) to indicate
 * truncated output, instead of three periods.  This is turned off because,
 * on Windows environment, ellipsis may not be displayable depending on
 * terminal settings.  To our astonishment, they are not fullly
 * unicode-capable by default.
 * We may probe the terminal at runtime to switch, but I don't want to
 * clutter the code.  So for now, no ellipsis.
 */
#if 0
#define SCM_WRITTEN_ELLIPSIS "\xe2\x80\xa6"
#else
#define SCM_WRITTEN_ELLIPSIS "..."
#endif


/* WriteContext and WriteState

   WriteContext affects write operation below the current subtree.
   WriteState is created at the root of write-family call and carried
   around during the entire write operation.

   WriteState is ScmObj and will be accessed from Scheme world as well.
 */

struct ScmWriteContextRec {
    short mode;                 /* print mode */
    short flags;                /* internal */
    int limit;                  /* used in WriteLimited */
    const ScmWriteControls *controls;
};

#define SCM_WRITE_CONTEXT(obj)    ((ScmWriteContext*)(obj))

struct ScmWriteStateRec {
    SCM_HEADER;
    ScmHashTable *sharedTable;  /* track shared structure.  can be NULL */
    const ScmWriteControls *controls; /* saving writecontext->controls
                                         for recursive call */
    int sharedCounter;          /* counter to emit #n= and #n# */
    int currentLevel;
};

SCM_CLASS_DECL(Scm_WriteStateClass);
#define SCM_CLASS_WRITE_STATE  (&Scm_WriteStateClass)
#define SCM_WRITE_STATE(obj)   ((ScmWriteState*)(obj))
#define SCM_WRITE_STATE_P(obj) SCM_XTYPEP(obj, SCM_CLASS_WRITE_STATE)

SCM_EXTERN ScmWriteState *Scm_MakeWriteState(ScmWriteState *proto);


#define SCM_WRITE_MODE_MASK  0x03
#define SCM_WRITE_CASE_MASK  0x0c

#define SCM_WRITE_MODE(ctx)   ((ctx)->mode & SCM_WRITE_MODE_MASK)
#define SCM_WRITE_CASE(ctx)   ((ctx)->mode & SCM_WRITE_CASE_MASK)

SCM_EXTERN ScmObj Scm__WritePrimitive(ScmObj obj, ScmPort *port,
                                      ScmWriteContext *ctx);

#endif /*GAUCHE_PRIV_WRITERP_H*/
