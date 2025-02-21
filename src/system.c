/*
 * system.c - system interface
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
#include "gauche/priv/bignumP.h"
#include "gauche/priv/builtin-syms.h"
#include "gauche/priv/fastlockP.h"

#include <locale.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <dirent.h>

#if !defined(GAUCHE_WINDOWS)
#include <grp.h>
#include <pwd.h>
#include <sys/times.h>
#include <sys/wait.h>

# if !defined(HAVE_CRT_EXTERNS_H)
/* POSIX defines environ, and ISO C defines __environ.
   Modern C seems to have the latter declared in unistd.h */
extern char **environ;
# else  /* HAVE_CRT_EXTERNS_H */
/* On newer OSX, we can't directly access global 'environ' variable.
   We need to use _NSGetEnviron(), and this header defines it. */
#include <crt_externs.h>
# endif /* HAVE_CRT_EXTERNS_H */
#else   /* GAUCHE_WINDOWS */
#include <lm.h>
#include <tlhelp32.h>
/* For windows redirection; win_prepare_handles creats and returns
   win_redirects[3].  Each entry contains an inheritable handle for
   the child process' stdin, stdout and stderr, respectively, and the flag
   duped indicates whether the parent process must close the handle. */
typedef struct win_redirects_rec {
    HANDLE *h;
    int duped;
} win_redirects;
static win_redirects *win_prepare_handles(int *fds);
static int win_wait_for_handles(HANDLE *handles, int nhandles, int options,
                                int *status /*out*/);
#endif  /* GAUCHE_WINDOWS */

#ifdef HAVE_GLOB_H
#include <glob.h>
#endif
#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

/*
 * Auxiliary system interface functions.   See libsys.scm for
 * Scheme binding.
 */

/*===============================================================
 * Windows specific - conversion between mbs and wcs.
 */
#if defined(GAUCHE_WINDOWS) && defined(UNICODE)
#include "win-compat.c"

WCHAR *Scm_MBS2WCS(const char *s)
{
    return mbs2wcs(s, TRUE, Scm_Error);
}

const char *Scm_WCS2MBS(const WCHAR *s)
{
    return wcs2mbs(s, TRUE, Scm_Error);
}
#endif /* defined(GAUCHE_WINDOWS) && defined(UNICODE) */

/*===============================================================
 * OBSOLETED: Wrapper to the system call to handle signals.
 * Use SCM_SYSCALL_{I|P} macro instead.
 */
int Scm_SysCall(int r)
{
    Scm_Warn("Obsoleted API Scm_SysCall is called.");
    if (r < 0 && errno == EINTR) {
        ScmVM *vm = Scm_VM();
        errno = 0;
        SCM_SIGCHECK(vm);
    }
    return r;
}

void *Scm_PtrSysCall(void *r)
{
    Scm_Warn("Obsoleted API Scm_PtrSysCall is called.");
    if (r == NULL && errno == EINTR) {
        ScmVM *vm = Scm_VM();
        errno = 0;
        SCM_SIGCHECK(vm);
    }
    return r;
}

/*
 * A utility function for the procedures that accepts either port or
 * integer file descriptor.  Returns the file descriptor.  If port_or_fd
 * is a port that is not associated with the system file, and needfd is
 * true, signals error.  Otherwise it returns -1.
 */
int Scm_GetPortFd(ScmObj port_or_fd, int needfd)
{
    int fd = -1;
    if (SCM_INTP(port_or_fd)) {
        fd = SCM_INT_VALUE(port_or_fd);
    } else if (SCM_PORTP(port_or_fd)) {
        fd = Scm_PortFileNo(SCM_PORT(port_or_fd));
        if (fd < 0 && needfd) {
            Scm_Error("the port is not associated with a system file descriptor: %S",
                      port_or_fd);
        }
    } else {
        Scm_Error("port or small integer required, but got %S", port_or_fd);
    }
    return fd;
}

/*===============================================================
 * Directory primitives (dirent.h)
 *   We don't provide the iterator primitives, but a function which
 *   reads entire directory.
 */

/* Returns a list of directory entries.  If pathname is not a directory,
   or can't be opened by some reason, an error is signalled. */
ScmObj Scm_ReadDirectory(ScmString *pathname)
{
    ScmObj head = SCM_NIL, tail = SCM_NIL;
#if !defined(GAUCHE_WINDOWS)
    ScmVM *vm = Scm_VM();
    struct dirent *dire;
    DIR *dirp = opendir(Scm_GetStringConst(pathname));

    if (dirp == NULL) {
        SCM_SIGCHECK(vm);
        Scm_SysError("couldn't open directory %S", pathname);
    }
    while ((dire = readdir(dirp)) != NULL) {
        ScmObj ent = SCM_MAKE_STR_COPYING(dire->d_name);
        SCM_APPEND1(head, tail, ent);
    }
    SCM_SIGCHECK(vm);
    closedir(dirp);
    return head;
#else  /* GAUCHE_WINDOWS */
    WIN32_FIND_DATA fdata;
    DWORD winerrno;
    ScmObj pattern;

    int pathlen = SCM_STRING_LENGTH(pathname);
    if (pathlen == 0) {
        Scm_Error("Couldn't open directory \"\"");
    }
    ScmChar lastchar = Scm_StringRef(pathname, pathlen-1, FALSE);
    if (lastchar == SCM_CHAR('/') || lastchar == SCM_CHAR('\\')) {
        pattern = Scm_StringAppendC(pathname, "*", 1, 1);
    } else {
        pattern = Scm_StringAppendC(pathname, "\\*", 2, 2);
    }
    const char *path = Scm_GetStringConst(SCM_STRING(pattern));

    HANDLE dirp = FindFirstFile(SCM_MBS2WCS(path), &fdata);
    if (dirp == INVALID_HANDLE_VALUE) {
        if ((winerrno = GetLastError()) != ERROR_FILE_NOT_FOUND) goto err;
        return head;
    }
    const char *tpath = SCM_WCS2MBS(fdata.cFileName);
    SCM_APPEND1(head, tail, SCM_MAKE_STR_COPYING(tpath));
    while (FindNextFile(dirp, &fdata) != 0) {
        tpath = SCM_WCS2MBS(fdata.cFileName);
        SCM_APPEND1(head, tail, SCM_MAKE_STR_COPYING(tpath));
    }
    winerrno = GetLastError();
    FindClose(dirp);
    if (winerrno != ERROR_NO_MORE_FILES) goto err;
    return head;
 err:
    Scm_Error("Searching directory failed by windows error %d",
              winerrno);
    return SCM_UNDEFINED;       /* dummy */
#endif
}

/* getcwd compatibility layer.
   Some implementations of getcwd accepts NULL as buffer to allocate
   enough buffer memory in it, but that's not standardized and we avoid
   relying on it.
 */
ScmObj Scm_GetCwd(void)
{
#if defined(GAUCHE_WINDOWS)&&defined(UNICODE)
#  define CHAR_T wchar_t
#  define GETCWD _wgetcwd
#else  /*!(defined(GAUCHE_WINDOWS)&&defined(UNICODE))*/
#  define CHAR_T char
#  define GETCWD getcwd
#endif /*!(defined(GAUCHE_WINDOWS)&&defined(UNICODE))*/

#define GETCWD_INITIAL_BUFFER_SIZE 1024
    int bufsiz = GETCWD_INITIAL_BUFFER_SIZE;
    CHAR_T sbuf[GETCWD_INITIAL_BUFFER_SIZE];
    CHAR_T *buf = sbuf;
    CHAR_T *r;

    for (;;) {
        SCM_SYSCALL3(r, GETCWD(buf, bufsiz), r == NULL);
        if (r != NULL) break;
        if (errno == ERANGE) {
            bufsiz *= 2;
            buf = SCM_NEW_ATOMIC_ARRAY(CHAR_T, bufsiz);
        } else {
            Scm_SysError("getcwd failed");
        }
    }
#if defined(GAUCHE_WINDOWS) && defined(UNICODE)
    return Scm_MakeString(Scm_WCS2MBS(buf), -1, -1, 0);
#else  /*!(defined(GAUCHE_WINDOWS) && defined(UNICODE))*/
    return Scm_MakeString(buf, -1, -1, SCM_STRING_COPYING);
#endif /*!(defined(GAUCHE_WINDOWS) && defined(UNICODE))*/
#undef CHAR_T
}

/*===============================================================
 * Pathname manipulation
 *
 *  It gets complicated since the byte '/' and '\\' can appear in
 *  the trailing octets of a multibyte character.
 *  Assuming these operations won't be a bottleneck, we use simple and
 *  straightforward code rather than tricky and fast one.
 */

/* Returns the system's native pathname delimiter. */
const char *Scm_PathDelimiter(void)
{
#if !defined(GAUCHE_WINDOWS)
    return "/";
#else  /* GAUCHE_WINDOWS */
    return "\\";
#endif /* GAUCHE_WINDOWS */
}

/* On Windows, '/' is *allowed* to be an alternative separator. */
#if defined(GAUCHE_WINDOWS)
#define SEPARATOR '\\'
#define ROOTDIR   "\\"
#define SEPARATOR_P(c)  ((c) == SEPARATOR || (c) == '/')
#else
#define SEPARATOR '/'
#define ROOTDIR   "/"
#define SEPARATOR_P(c)  ((c) == SEPARATOR)
#endif

/* Returns the pointer to the first path separator character,
   or NULL if no separator is found. */
static const char *get_first_separator(const char *path, const char *end)
{
    const char *p = path;
    while (p < end) {
        if (SEPARATOR_P(*p)) return p;
        p += SCM_CHAR_NFOLLOWS(*p)+1;
    }
    return NULL;
}

/* Returns the pointer to the last path separator character,
   or NULL if no separator is found. */
static const char *get_last_separator(const char *path, const char *end)
{
    const char *p = path, *last = NULL;
    while (p < end) {
        if (SEPARATOR_P(*p)) last = p;
        p += SCM_CHAR_NFOLLOWS(*p)+1;
    }
    return last;
}

static const char *skip_separators(const char *p, const char *end)
{
    while (p < end) {
        if (!SEPARATOR_P(*p)) break;
        p += SCM_CHAR_NFOLLOWS(*p)+1;
    }
    return p;
}

/* Returns the end pointer sans trailing separators. */
static const char *truncate_trailing_separators(const char *path,
                                                const char *end)
{
    const char *p = get_first_separator(path, end);
    if (p == NULL) return end;
    for (;;) {
        const char *q = skip_separators(p, end);
        if (q == end) return p;
        p = get_first_separator(q, end);
        if (p == NULL) return end;
    }
}

/* for keyword arguments */
static ScmObj key_absolute = SCM_FALSE;
static ScmObj key_expand = SCM_FALSE;
static ScmObj key_canonicalize = SCM_FALSE;

ScmObj Scm_NormalizePathname(ScmString *pathname, int flags)
{
    static ScmObj proc = SCM_UNDEFINED;
    SCM_BIND_PROC(proc, "sys-normalize-pathname", Scm_GaucheModule());

    ScmObj h = SCM_NIL, t = SCM_NIL;
    SCM_APPEND1(h, t, SCM_OBJ(pathname));
    if (flags & SCM_PATH_ABSOLUTE) {
        SCM_APPEND1(h, t, key_absolute);
        SCM_APPEND1(h, t, SCM_TRUE);
    }
    if (flags & SCM_PATH_CANONICALIZE) {
        SCM_APPEND1(h, t, key_canonicalize);
        SCM_APPEND1(h, t, SCM_TRUE);
    }
    if (flags & SCM_PATH_EXPAND) {
        SCM_APPEND1(h, t, key_expand);
        SCM_APPEND1(h, t, SCM_TRUE);
    }
    return Scm_ApplyRec(proc, h);
}

/* Returns system's temporary directory. */
ScmObj Scm_TmpDir(void)
{
#if defined(GAUCHE_WINDOWS)
# define TMP_PATH_MAX 1024
    TCHAR buf[TMP_PATH_MAX+1], *tbuf = buf;
    /* According to the windows document, this API checks environment
       variables TMP, TEMP, and USERPROFILE.  Fallback is the Windows
       directory. */
    DWORD r = GetTempPath(TMP_PATH_MAX, buf);
    if (r == 0) Scm_SysError("GetTempPath failed");
    if (r > TMP_PATH_MAX) {
        tbuf = SCM_NEW_ATOMIC_ARRAY(TCHAR, r+1);
        DWORD r2 = GetTempPath(r, tbuf);
        if (r2 != r) Scm_SysError("GetTempPath failed");
    }
    return SCM_MAKE_STR_COPYING(SCM_WCS2MBS(tbuf));
#else  /*!GAUCHE_WINDOWS*/
    const char *s;
    if ((s = Scm_GetEnv("TMPDIR")) != NULL) return SCM_MAKE_STR_COPYING(s);
    if ((s = Scm_GetEnv("TMP")) != NULL) return SCM_MAKE_STR_COPYING(s);
    else return SCM_MAKE_STR("/tmp"); /* fallback */
#endif /*!GAUCHE_WINDOWS*/
}

/* Basename and dirname.
   On Win32, we need to treat drive names specially, e.g.:
   (sys-dirname "C:/a") == (sys-dirname "C:/") == (sys-dirname "C:") == "C:\\"
   (sys-basename "C:/") == (sys-basename "C:) == ""
*/

ScmObj Scm_BaseName(ScmString *filename)
{
    ScmSmallInt size;
    const char *path = Scm_GetStringContent(filename, &size, NULL, NULL);

#if defined(GAUCHE_WINDOWS)
    /* Ignore drive letter, for it can never be a part of basename. */
    if (size >= 2 && path[1] == ':' && isalpha(path[0])) {
        path += 2;
        size -= 2;
    }
#endif /* GAUCHE_WINDOWS) */

    if (size == 0) return SCM_MAKE_STR("");
    const char *endp = truncate_trailing_separators(path, path+size);
    const char *last = get_last_separator(path, endp);
    if (last == NULL) {
        return Scm_MakeString(path, (int)(endp-path), -1, 0);
    } else {
        return Scm_MakeString(last+1, (int)(endp-last-1), -1, 0);
    }
}

ScmObj Scm_DirName(ScmString *filename)
{
    ScmSmallInt size;
    const char *path = Scm_GetStringContent(filename, &size, NULL, NULL);
#if defined(GAUCHE_WINDOWS)
    int drive_letter = -1;
    if (size >= 2 && path[1] == ':' && isalpha(path[0])) {
        drive_letter = path[0];
        path += 2;
        size -= 2;
    }
#endif /* GAUCHE_WINDOWS */

    if (size == 0) { path = NULL; goto finale; }
    const char *endp = truncate_trailing_separators(path, path+size);
    if (endp == path) { path = ROOTDIR, size = 1; goto finale; }
    const char *last = get_last_separator(path, endp);
    if (last == NULL) { path = ".", size = 1; goto finale; }

    /* we have "something/", and 'last' points to the last separator. */
    last = truncate_trailing_separators(path, last);
    if (last == path) {
        path = ROOTDIR, size = 1;
    } else {
        size = (int)(last - path);
    }
 finale:
#if defined(GAUCHE_WINDOWS)
    if (drive_letter > 0) {
        ScmObj z;
        char p[3] = "x:";
        p[0] = (char)drive_letter;
        z = Scm_MakeString(p, 2, 2, SCM_MAKSTR_COPYING);
        if (path) {
            return Scm_StringAppendC(SCM_STRING(z), path, size, -1);
        } else {
            return Scm_StringAppendC(SCM_STRING(z), ROOTDIR, 1, -1);
        }
    }
#endif /* GAUCHE_WINDOWS */
    if (path) return Scm_MakeString(path, size, -1, 0);
    else      return Scm_MakeString(".", 1, 1, 0);
}

#undef ROOTDIR
#undef SEPARATOR


#if !defined(HAVE_MKSTEMP) || !defined(HAVE_MKDTEMP)
/*
 * Helper function to emulate mkstemp or mkdtemp.  FUNC returns 0 on
 * success and non-zero otherwize.  NAME is a name of operation
 * performed by FUNC.  ARG is caller supplied data passed to FUNC.
 */
static void emulate_mkxtemp(char *name, char *templat,
                            int (*func)(char *, void *), void *arg)
{
    /* Emulate mkxtemp. */
    int siz = (int)strlen(templat);
    if (siz < 6) {
        Scm_Error("%s - invalid template: %s", name, templat);
    }
#define MKXTEMP_MAX_TRIALS 65535   /* avoid infinite loop */
    {
        u_long seed = (u_long)time(NULL);
        int numtry, rv;
        char suffix[7];
        for (numtry=0; numtry<MKXTEMP_MAX_TRIALS; numtry++) {
            snprintf(suffix, 7, "%06lx", (seed>>8)&0xffffff);
            memcpy(templat+siz-6, suffix, 7);
            rv = (*func)(templat, arg);
            if (rv == 0) break;
            seed *= 2654435761UL;
        }
        if (numtry == MKXTEMP_MAX_TRIALS) {
            Scm_Error("%s failed", name);
        }
    }
}
#endif /* !defined(HAVE_MKSTEMP) || !defined(HAVE_MKDTEMP) */

#define MKXTEMP_PATH_MAX 1025  /* Geez, remove me */
static void build_template(ScmString *templat, char *name)
{
    ScmSmallInt siz;
    const char *t = Scm_GetStringContent(templat, &siz, NULL, NULL);
    if (siz >= MKXTEMP_PATH_MAX-6) {
        Scm_Error("pathname too long: %S", templat);
    }
    memcpy(name, t, siz);
    memcpy(name + siz, "XXXXXX", 6);
    name[siz+6] = '\0';
}

#if !defined(HAVE_MKSTEMP)
static int create_tmpfile(char *templat, void *arg)
{
    int *fdp = (int *)arg;
    int flags;

#if defined(GAUCHE_WINDOWS)
    flags = O_CREAT|O_EXCL|O_WRONLY|O_BINARY;
#else  /* !GAUCHE_WINDOWS */
    flags = O_CREAT|O_EXCL|O_WRONLY;
#endif /* !GAUCHE_WINDOWS */
    SCM_SYSCALL(*fdp, open(templat, flags, 0600));
    return *fdp < 0;
}
#endif

/* Make mkstemp() work even if the system doesn't have one. */
int Scm_Mkstemp(char *templat)
{
    int fd = -1;
#if defined(HAVE_MKSTEMP)
    SCM_SYSCALL(fd, mkstemp(templat));
    if (fd < 0) Scm_SysError("mkstemp failed");
    return fd;
#else   /*!defined(HAVE_MKSTEMP)*/
    emulate_mkxtemp("mkstemp", templat, create_tmpfile, &fd);
    return fd;
#endif /*!defined(HAVE_MKSTEMP)*/
}


ScmObj Scm_SysMkstemp(ScmString *templat)
{
    char name[MKXTEMP_PATH_MAX];
    build_template(templat, name);
    int fd = Scm_Mkstemp(name);
    ScmObj sname = SCM_MAKE_STR_COPYING(name);
    SCM_RETURN(Scm_Values2(Scm_MakePortWithFd(sname, SCM_PORT_OUTPUT, fd,
                                              SCM_PORT_BUFFER_FULL, TRUE),
                           sname));
}

#if !defined(HAVE_MKDTEMP)
static int create_tmpdir(char *templat, void *arg SCM_UNUSED)
{
    int r;

#if defined(GAUCHE_WINDOWS)
    SCM_SYSCALL(r, mkdir(templat));
#else  /* !GAUCHE_WINDOWS */
    SCM_SYSCALL(r, mkdir(templat, 0700));
#endif /* !GAUCHE_WINDOWS */
    return r < 0;
}
#endif

ScmObj Scm_SysMkdtemp(ScmString *templat)
{
    char name[MKXTEMP_PATH_MAX];
    build_template(templat, name);

#if defined(HAVE_MKDTEMP)
    {
      char *p = NULL;
      SCM_SYSCALL3(p, mkdtemp(name), (p == NULL));
      if (p == NULL) Scm_SysError("mkdtemp failed");
    }
#else   /*!defined(HAVE_MKDTEMP)*/
    emulate_mkxtemp("mkdtemp", name, create_tmpdir, NULL);
#endif /*!defined(HAVE_MKDTEMP)*/

    return SCM_MAKE_STR_COPYING(name);
}

/*===============================================================
 * Stat (sys/stat.h)
 */

static ScmObj stat_allocate(ScmClass *klass, ScmObj initargs SCM_UNUSED)
{
    return SCM_OBJ(SCM_NEW_INSTANCE(ScmSysStat, klass));
}

static ScmSmallInt stat_hash(ScmObj obj, ScmSmallInt salt, u_long flags)
{
    ScmStat *s = SCM_SYS_STAT_STAT(obj);
    ScmSmallInt h = salt;
#define STAT_HASH_UI(name)                                              \
    h = Scm_CombineHashValue(Scm_SmallIntHash((ScmSmallInt)s->SCM_CPP_CAT(st_, name), \
                                              salt, flags), h)
#define STAT_HASH_TIME(name)                                            \
    h = Scm_CombineHashValue(Scm_Int64Hash((int64_t)s->SCM_CPP_CAT(st_, name), \
                                           salt, flags), h)
#define STAT_HASH_TIMESPEC(name) \
    h = Scm_CombineHashValue(Scm_Int64Hash((int64_t)s->SCM_CPP_CAT(st_, name).tv_sec, \
                                           salt, flags), \
        Scm_CombineHashValue(Scm_Int64Hash((int64_t)s->SCM_CPP_CAT(st_, name).tv_nsec, \
                                           salt, flags),h))


    STAT_HASH_UI(mode);
    STAT_HASH_UI(ino);
    STAT_HASH_UI(dev);
    STAT_HASH_UI(rdev);
    STAT_HASH_UI(nlink);
    STAT_HASH_UI(uid);
    STAT_HASH_UI(gid);
#if HAVE_STRUCT_STAT_ST_ATIM
    STAT_HASH_TIMESPEC(atim);
#else
    STAT_HASH_TIME(atime);
#endif
#if HAVE_STRUCT_STAT_ST_MTIM
    STAT_HASH_TIMESPEC(mtim);
#else
    STAT_HASH_TIME(mtime);
#endif
#if HAVE_STRUCT_STAT_ST_CTIM
    STAT_HASH_TIMESPEC(ctim);
#else
    STAT_HASH_TIME(ctime);
#endif
    return h;
}

SCM_DEFINE_BUILTIN_CLASS(Scm_SysStatClass,
                         NULL, NULL, stat_hash,
                         stat_allocate,
                         SCM_CLASS_DEFAULT_CPL);

ScmObj Scm_MakeSysStat(void)
{
    return stat_allocate(&Scm_SysStatClass, SCM_NIL);
}

static ScmObj stat_type_get(ScmSysStat *stat)
{
    if (S_ISDIR(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_DIRECTORY);
    if (S_ISREG(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_REGULAR);
    if (S_ISCHR(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_CHARACTER);
    if (S_ISBLK(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_BLOCK);
    if (S_ISFIFO(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_FIFO);
#ifdef S_ISLNK
    if (S_ISLNK(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_SYMLINK);
#endif
#ifdef S_ISSOCK
    if (S_ISSOCK(SCM_SYS_STAT_STAT(stat)->st_mode)) return (SCM_SYM_SOCKET);
#endif
    return (SCM_FALSE);
}

static ScmObj stat_perm_get(ScmSysStat *stat)
{
    return Scm_MakeIntegerFromUI(SCM_SYS_STAT_STAT(stat)->st_mode & 0777);
}

static ScmObj stat_size_get(ScmSysStat *stat)
{
    return Scm_OffsetToInteger(SCM_SYS_STAT_STAT(stat)->st_size);
}


#define STAT_GETTER_UI(name) \
  static ScmObj SCM_CPP_CAT3(stat_, name, _get)(ScmSysStat *s) \
  { return Scm_MakeIntegerFromUI((u_long)(SCM_SYS_STAT_STAT(s)->SCM_CPP_CAT(st_, name))); }
#define STAT_GETTER_TIME(name) \
  static ScmObj SCM_CPP_CAT3(stat_, name, _get)(ScmSysStat *s) \
  { return Scm_MakeSysTime(SCM_SYS_STAT_STAT(s)->SCM_CPP_CAT(st_, name)); }

STAT_GETTER_UI(mode)
STAT_GETTER_UI(ino)
STAT_GETTER_UI(dev)
STAT_GETTER_UI(rdev)
STAT_GETTER_UI(nlink)
STAT_GETTER_UI(uid)
STAT_GETTER_UI(gid)
STAT_GETTER_TIME(atime)
STAT_GETTER_TIME(mtime)
STAT_GETTER_TIME(ctime)

static ScmObj stat_atim_get(ScmSysStat *s)
{
#if HAVE_STRUCT_STAT_ST_ATIM
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_atim.tv_sec,
                          s->statrec.st_atim.tv_nsec);
#else
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_atime,
                          0);
#endif
}

static ScmObj stat_mtim_get(ScmSysStat *s)
{
#if HAVE_STRUCT_STAT_ST_MTIM
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_mtim.tv_sec,
                          s->statrec.st_mtim.tv_nsec);
#else
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_mtime,
                          0);
#endif
}

static ScmObj stat_ctim_get(ScmSysStat *s)
{
#if HAVE_STRUCT_STAT_ST_CTIM
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_ctim.tv_sec,
                          s->statrec.st_ctim.tv_nsec);
#else
    return Scm_MakeTime64(SCM_SYM_TIME_UTC,
                          (int64_t)s->statrec.st_ctime,
                          0);
#endif
}

static ScmClassStaticSlotSpec stat_slots[] = {
    SCM_CLASS_SLOT_SPEC("type",  stat_type_get,  NULL),
    SCM_CLASS_SLOT_SPEC("perm",  stat_perm_get,  NULL),
    SCM_CLASS_SLOT_SPEC("mode",  stat_mode_get,  NULL),
    SCM_CLASS_SLOT_SPEC("ino",   stat_ino_get,   NULL),
    SCM_CLASS_SLOT_SPEC("dev",   stat_dev_get,   NULL),
    SCM_CLASS_SLOT_SPEC("rdev",  stat_rdev_get,  NULL),
    SCM_CLASS_SLOT_SPEC("nlink", stat_nlink_get, NULL),
    SCM_CLASS_SLOT_SPEC("uid",   stat_uid_get,   NULL),
    SCM_CLASS_SLOT_SPEC("gid",   stat_gid_get,   NULL),
    SCM_CLASS_SLOT_SPEC("size",  stat_size_get,  NULL),
    SCM_CLASS_SLOT_SPEC("atime", stat_atime_get, NULL),
    SCM_CLASS_SLOT_SPEC("mtime", stat_mtime_get, NULL),
    SCM_CLASS_SLOT_SPEC("ctime", stat_ctime_get, NULL),
    SCM_CLASS_SLOT_SPEC("atim",  stat_atim_get, NULL),
    SCM_CLASS_SLOT_SPEC("mtim",  stat_mtim_get, NULL),
    SCM_CLASS_SLOT_SPEC("ctim",  stat_ctim_get, NULL),
    SCM_CLASS_SLOT_SPEC_END()
};

/*===============================================================
 * Time (sys/time.h and time.h)
 */

/* Gauche has two notion of time.  A simple number is used by the low-level
 * system interface (sys-time, sys-gettimeofday).  An object of <time> class
 * is used for higher-level interface, including threads.
 */

/* <time> object */

static ScmObj time_allocate(ScmClass *klass, ScmObj initargs SCM_UNUSED)
{
    ScmTime *t = SCM_NEW_INSTANCE(ScmTime, klass);
    t->type = SCM_SYM_TIME_UTC;
    t->sec = 0;
    t->nsec = 0;
    return SCM_OBJ(t);
}

static void time_print(ScmObj obj, ScmPort *port,
                       ScmWriteContext *ctx SCM_UNUSED)
{
    ScmTime *t = SCM_TIME(obj);
    ScmObj sec = Scm_MakeInteger64(t->sec);
    long nsec = t->nsec;
    /* t->sec can be negative for time-difference. */
    if (Scm_Sign(sec) < 0 && t->nsec > 0) {
        sec = Scm_Abs(Scm_Add(sec, SCM_MAKE_INT(1)));
        nsec = 1000000000L - nsec;
        Scm_Printf(port, "#<%S -%S.%09lu>", t->type, sec, nsec);
    } else {
        Scm_Printf(port, "#<%S %S.%09lu>", t->type, sec, nsec);
    }
}

static int time_compare(ScmObj x, ScmObj y, int equalp)
{
    ScmTime *tx = SCM_TIME(x);
    ScmTime *ty = SCM_TIME(y);

    if (equalp) {
        if (SCM_EQ(tx->type, ty->type)
            && tx->sec == ty->sec
            && tx->nsec == ty->nsec) {
            return 0;
        } else {
            return 1;
        }
    } else {
        if (!SCM_EQ(tx->type, ty->type)) {
            Scm_Error("cannot compare different types of time objects: %S vs %S", x, y);
        }
        if (tx->sec < ty->sec) return -1;
        if (tx->sec == ty->sec) {
            if (tx->nsec < ty->nsec) return -1;
            if (tx->nsec == ty->nsec) return 0;
            else return 1;
        }
        else return 1;
    }
}

static ScmSmallInt time_hash(ScmObj x, ScmSmallInt salt, u_long flags)
{
    ScmTime *t = SCM_TIME(x);
    ScmSmallInt h = salt;
    h = Scm_CombineHashValue(Scm_RecursiveHash(t->type, salt, flags), h);
    h = Scm_CombineHashValue(Scm_Int64Hash(t->sec, salt, flags), h);
    h = Scm_CombineHashValue(Scm_SmallIntHash(t->nsec, salt, flags), h);
    return h;
}

SCM_DEFINE_BUILTIN_CLASS(Scm_TimeClass,
                         time_print, time_compare, time_hash,
                         time_allocate, SCM_CLASS_DEFAULT_CPL);

static ScmTime *make_time_int(ScmObj type)
{
    ScmTime *t = SCM_TIME(time_allocate(SCM_CLASS_TIME, SCM_NIL));
    t->type = SCM_FALSEP(type)? SCM_SYM_TIME_UTC : type;
    return t;
}


ScmObj Scm_MakeTime(ScmObj type, long sec, long nsec)
{
    ScmTime *t = make_time_int(type);
    t->sec = (int64_t)sec;
    t->nsec = nsec;
    return SCM_OBJ(t);
}

ScmObj Scm_MakeTime64(ScmObj type, int64_t sec, long nsec)
{
    ScmTime *t = make_time_int(type);
    t->sec = sec;
    t->nsec = nsec;
    return SCM_OBJ(t);
}

/* Abstract gettimeofday() */
void Scm_GetTimeOfDay(u_long *sec, u_long *usec)
{
#if defined(HAVE_GETTIMEOFDAY)
    struct timeval tv;
    int r;
    SCM_SYSCALL(r, gettimeofday(&tv, NULL));
    if (r < 0) Scm_SysError("gettimeofday failed");
    *sec = (u_long)tv.tv_sec;
    *usec = (u_long)tv.tv_usec;
#elif defined(GAUCHE_WINDOWS)
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    SCM_FILETIME_TO_UNIXTIME(ft, *sec, *usec);
#else  /* !HAVE_GETTIMEOFDAY && !GAUCHE_WINDOWS */
    /* Last resort */
    *sec = (u_long)time(NULL);
    *usec = 0;
#endif /* !HAVE_GETTIMEOFDAY && !GAUCHE_WINDOWS */
}

/* Abstract clock_gettime and clock_getres.
   If the system doesn't have these, those API returns FALSE; the caller
   should make up fallback means.

   NB: XCode8 breaks clock_getres on OSX 10.11---it's only provided in
   OSX 10.12, but the SDK pretends it's available on all platforms.
   For the workaround, we call OSX specific functions.
   Cf. http://developer.apple.com/library/mac/#qa/qa1398/_index.html
 */
#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach.h>
#include <mach/mach_time.h>
static mach_timebase_info_data_t tinfo;
#endif /* __APPLE__ && __MACH__ */

int Scm_ClockGetTimeMonotonic(u_long *sec, u_long *nsec)
{
#if defined(__APPLE__) && defined(__MACH__)
    if (tinfo.denom == 0) {
        (void)mach_timebase_info(&tinfo);
    }
    uint64_t t = mach_absolute_time();
    uint64_t ns = t * tinfo.numer / tinfo.denom;
    *sec = ns / 1000000000;
    *nsec = ns % 1000000000;
    return TRUE;
#elif defined(GAUCHE_WINDOWS)
    /* On MinGW, clock_gettime is in libwinpthread-1.dll; we avoid depending
       on it. */
    LARGE_INTEGER qpf;
    LARGE_INTEGER qpc;
    if (!QueryPerformanceFrequency(&qpf)) {
        Scm_SysError("QueryPerformanceFrequency failed");
    }
    if (!QueryPerformanceCounter(&qpc)) {
        Scm_SysError("QueryPerformanceCounter failed");
    }
    *sec = (u_long)(qpc.QuadPart / qpf.QuadPart);
    *nsec = (u_long)((qpc.QuadPart % qpf.QuadPart) * 1000000000 / qpf.QuadPart);
    return TRUE;
#elif defined(HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
    ScmTimeSpec ts;
    int r;
    SCM_SYSCALL(r, clock_gettime(CLOCK_MONOTONIC, &ts));
    if (r < 0) Scm_SysError("clock_gettime failed");
    *sec = (u_long)ts.tv_sec;
    *nsec = (u_long)ts.tv_nsec;
    return TRUE;
#else  /*!HAVE_CLOCK_GETTIME*/
    *sec = *nsec = 0;
    return FALSE;
#endif /*!HAVE_CLOCK_GETTIME*/
}

int Scm_ClockGetResMonotonic(u_long *sec, u_long *nsec)
{
#if defined(__APPLE__) && defined(__MACH__)
    if (tinfo.denom == 0) {
        (void)mach_timebase_info(&tinfo);
    }
    if (tinfo.numer <= tinfo.denom) {
        /* The precision is finer than nano seconds, but we can only
           represent nanosecond resolution. */
        *sec = 0;
        *nsec = 1;
    } else {
        *sec = 0;
        *nsec = tinfo.numer / tinfo.denom;
    }
    return TRUE;
#elif defined(GAUCHE_WINDOWS)
    /* On MinGW, clock_getres is in libwinpthread-1.dll; we avoid depending
       on it. */
    LARGE_INTEGER qpf;
    if (!QueryPerformanceFrequency(&qpf)) {
        Scm_SysError("QueryPerformanceFrequency failed");
    }
    *sec = 0;
    *nsec = (u_long)(1000000000 / qpf.QuadPart);
    if (*nsec == 0) *nsec = 1;
    return TRUE;
#elif defined(HAVE_CLOCK_GETRES) && defined(CLOCK_MONOTONIC)
    ScmTimeSpec ts;
    int r;
    SCM_SYSCALL(r, clock_getres(CLOCK_MONOTONIC, &ts));
    if (r < 0) Scm_SysError("clock_getres failed");
    *sec = (u_long)ts.tv_sec;
    *nsec = (u_long)ts.tv_nsec;
    return TRUE;
#else  /*!HAVE_CLOCK_GETRES*/
    *sec = *nsec = 0;
    return FALSE;
#endif /*!HAVE_CLOCK_GETRES*/
}


/* Experimental.  This returns the microsecond-resolution time, wrapped
   around the fixnum resolution.  In 32-bit architecture it's a bit more
   than 1000seconds.  Good for micro-profiling, since this guarantees
   no allocation.  Returned value can be negative. */
long Scm_CurrentMicroseconds()
{
    u_long sec, usec;
    Scm_GetTimeOfDay(&sec, &usec);
    /* we ignore overflow */
    usec += sec * 1000000;
    usec &= (1UL<<(SCM_SMALL_INT_SIZE+1)) - 1;
    if (usec > SCM_SMALL_INT_MAX) usec -= (1UL<<(SCM_SMALL_INT_SIZE+1));
    return (long)usec;
}

ScmObj Scm_CurrentTime(void)
{
    u_long sec, usec;
    Scm_GetTimeOfDay(&sec, &usec);
    return Scm_MakeTime(SCM_SYM_TIME_UTC, sec, usec*1000);
}

ScmObj Scm_IntSecondsToTime(long sec)
{
    return Scm_MakeTime(SCM_SYM_TIME_UTC, sec, 0);
}

ScmObj Scm_Int64SecondsToTime(int64_t sec)
{
    return Scm_MakeTime64(SCM_SYM_TIME_UTC, sec, 0);
}

ScmObj Scm_RealSecondsToTime(double sec)
{
    double s;
    double frac = modf(sec, &s);
    int64_t secs = (int64_t)s;
    return Scm_MakeTime64(SCM_SYM_TIME_UTC, secs, (long)(frac * 1.0e9));
}

static ScmObj time_type_get(ScmTime *t)
{
    return t->type;
}

static void time_type_set(ScmTime *t, ScmObj val)
{
    if (!SCM_SYMBOLP(val)) {
        Scm_Error("time type must be a symbol, but got %S", val);
    }
    t->type = val;
}

static ScmObj time_sec_get(ScmTime *t)
{
    return Scm_MakeInteger64(t->sec);
}

static void time_sec_set(ScmTime *t, ScmObj val)
{
    if (!SCM_REALP(val)) {
        Scm_Error("real number required, but got %S", val);
    }
    t->sec = Scm_GetInteger64(val);
}

static ScmObj time_nsec_get(ScmTime *t)
{
    return Scm_MakeInteger(t->nsec);
}

static void time_nsec_set(ScmTime *t, ScmObj val)
{
    if (!SCM_REALP(val)) {
        Scm_Error("real number required, but got %S", val);
    }
    long l = Scm_GetInteger(val);
    if (l >= 1000000000) {
        Scm_Error("nanoseconds out of range: %ld", l);
    }
    t->nsec = l;
}

static ScmClassStaticSlotSpec time_slots[] = {
    SCM_CLASS_SLOT_SPEC("type",       time_type_get, time_type_set),
    SCM_CLASS_SLOT_SPEC("second",     time_sec_get, time_sec_set),
    SCM_CLASS_SLOT_SPEC("nanosecond", time_nsec_get, time_nsec_set),
    SCM_CLASS_SLOT_SPEC_END()
};

/* time_t and conversion routines */
/* NB: I assume time_t is typedefed to either an integral type or
 * a floating point type.  As far as I know it is true on most
 * current architectures.  POSIX doesn't specify so, however; it
 * may be some weird structure.  If you find such an architecture,
 * tweak configure.in and modify the following two functions.
 */
ScmObj Scm_MakeSysTime(time_t t)
{
#ifdef INTEGRAL_TIME_T
    return Scm_MakeIntegerFromUI((unsigned long)t);
#else
    double val = (double)t;
    return Scm_MakeFlonum(val);
#endif
}

time_t Scm_GetSysTime(ScmObj val)
{
    if (SCM_TIMEP(val)) {
#ifdef INTEGRAL_TIME_T
        return (time_t)SCM_TIME(val)->sec;
#else
        return (time_t)(Scm_Int64ToDouble(SCM_TIME(val)->sec) +
                        (double)SCM_TIME(val)->nsec/1.0e9);
#endif
    } else if (SCM_NUMBERP(val)) {
#ifdef INTEGRAL_TIME_T
        return (time_t)Scm_GetUInteger(val);
#else
        return (time_t)Scm_GetDouble(val);
#endif
    } else {
        Scm_Error("bad time value: either a <time> object or a real number is required, but got %S", val);
        return (time_t)0;       /* dummy */
    }
}

/* strftime
 *  On MinGW, strftime() returns a multibyte string in the system's language
 *  setting.  Unfortunately, wcsftime() seems broken and unusable
 *  (cf. https://github.com/shirok/Gauche/pull/809 )
 *  This is the common compatibility routine.   The third argument is
 *  reserved for future extension to specify a locale.
 */
ScmObj Scm_StrfTime(const char *format,
                    const struct tm *tm,
                    ScmObj reserved SCM_UNUSED)
{
#if !defined(GAUCHE_WINDOWS) || !defined(UNICODE)
    const char *format1 = format;
#else  /* defined(GAUCHE_WINDOWS) && defined(UNICODE) */
    /* convert utf-8 to MB string */
    const wchar_t *wformat = Scm_MBS2WCS(format);
    int nb = WideCharToMultiByte(CP_ACP, 0, wformat, -1, NULL, 0, 0, 0);
    if (nb == 0) Scm_Error("strftime() failed (WideCharToMultiByte NULL)");
    char *format1 = SCM_NEW_ATOMIC_ARRAY(char, nb);
    if (WideCharToMultiByte(CP_ACP, 0, wformat, -1, format1, nb, 0, 0) == 0) {
        Scm_Error("strftime() failed (WideCharToMultiByte)");
    }
#endif /* defined(GAUCHE_WINDOWS) && defined(UNICODE) */

    size_t bufsiz = strlen(format1) + 30;
    char *buf = SCM_NEW_ATOMIC2(char*, bufsiz);

    /* NB: Zero return value may mean the buffer size is not enough, OR
       the actual output is an empty string.  We can't know which is the
       case.  Here we give a few tries.  */
    size_t r = 0;
    for (int retry = 0; retry < 3; retry++) {
        r = strftime(buf, bufsiz, format1, tm);
        if (r > 0) break;
        bufsiz *= 2;
        buf = SCM_NEW_ATOMIC2(char*, bufsiz);
    }
    if (r == 0) return SCM_MAKE_STR_IMMUTABLE("");

#if !defined(GAUCHE_WINDOWS) || !defined(UNICODE)
    return Scm_MakeString(buf, r, -1, SCM_STRING_COPYING);
#else  /* defined(GAUCHE_WINDOWS) && defined(UNICODE) */
    /* Here, buf contains MB string in system's language setting.
       Ensure we have utf-8 encoding.
     */
    int nc = MultiByteToWideChar(CP_ACP, 0, buf, -1, NULL, 0);
    if (nc == 0) Scm_Error("strftime() failed (MultiByteToWideChar NULL)");
    wchar_t *wb = SCM_NEW_ATOMIC_ARRAY(wchar_t, nc);
    if (MultiByteToWideChar(CP_ACP, 0, buf, -1, wb, nc) == 0) {
        Scm_Error("strftime() failed (MultiByteToWideChar)");
    }
    return Scm_MakeString(Scm_WCS2MBS(wb), -1, -1, SCM_STRING_COPYING);
#endif /* defined(GAUCHE_WINDOWS) && defined(UNICODE) */
}


ScmObj Scm_TimeToSeconds(ScmTime *t)
{
    if (t->nsec) {
        return Scm_MakeFlonum((double)(t->sec) + (double)t->nsec/1.0e9);
    } else {
        return Scm_MakeInteger64(t->sec);
    }
}

#define NSECS_IN_A_SEC 1000000000 /* 1e9 */

/* Scheme time -> timespec conversion */
ScmTimeSpec *Scm_ToTimeSpec(ScmObj t, ScmTime *t0, ScmTimeSpec *spec)
{
    if (SCM_FALSEP(t)) return NULL;
    if (SCM_TIMEP(t)) {
        if (SCM_EQ(SCM_TIME(t)->type, SCM_SYM_TIME_UTC)) {
            spec->tv_sec = SCM_TIME(t)->sec;
            spec->tv_nsec = SCM_TIME(t)->nsec;
        } else if (SCM_EQ(SCM_TIME(t)->type, SCM_SYM_TIME_DURATION)) {
            ScmTime *ct = t0 ? t0 : SCM_TIME(Scm_CurrentTime());
            spec->tv_sec = ct->sec + SCM_TIME(t)->sec;
            spec->tv_nsec = ct->nsec + SCM_TIME(t)->nsec; /* always positive */
            while (spec->tv_nsec >= NSECS_IN_A_SEC) {
                spec->tv_nsec -= NSECS_IN_A_SEC;
                spec->tv_sec += 1;
            }
        }
    } else if (!SCM_REALP(t)) {
        Scm_Error("bad time spec: <time> object, real number, or #f is required, but got %S", t);
    } else {
        ScmTime *ct = t0? t0 : SCM_TIME(Scm_CurrentTime());
        spec->tv_sec = ct->sec;
        spec->tv_nsec = ct->nsec;
        if (SCM_INTP(t)) {
            spec->tv_sec += Scm_GetInteger(t);
        } else if (!SCM_REALP(t)) {
            Scm_Panic("implementation error: Scm_GetTimeSpec: something wrong");
        } else {
            double s;
            spec->tv_nsec += (long)(modf(Scm_GetDouble(t), &s)*1.0e9);
            spec->tv_sec += (long)s;
            while (spec->tv_nsec >= NSECS_IN_A_SEC) {
                spec->tv_nsec -= NSECS_IN_A_SEC;
                spec->tv_sec += 1;
            }
            while (spec->tv_nsec < 0) {
                spec->tv_nsec += NSECS_IN_A_SEC;
                spec->tv_sec -= 1;
            }
        }
    }
    return spec;
}

/* Backward compatibility */
ScmTimeSpec *Scm_GetTimeSpec(ScmObj t, ScmTimeSpec *spec)
{
    return Scm_ToTimeSpec(t, NULL, spec);
}

/*
 * nanosleep() compatibility layer
 */
int Scm_NanoSleep(const ScmTimeSpec *req, ScmTimeSpec *rem)
{
#if defined(GAUCHE_WINDOWS)
    /* Recent mingw32 includes nanosleep but it seems broken, so we keep
       using this compatibility code for the time being. */
    DWORD msecs = 0;
    time_t sec;
    u_long overflow = 0, c;
    const DWORD MSEC_OVERFLOW = 4294967; /* 4294967*1000 = 0xfffffed8 */

    /* It's very unlikely that we overflow msecs, but just in case... */
    if (req->tv_sec > 0 || (req->tv_sec == 0 && req->tv_nsec > 0)) {
        if ((unsigned)req->tv_sec >= MSEC_OVERFLOW) {
            overflow = req->tv_sec / MSEC_OVERFLOW;
            sec = req->tv_sec % MSEC_OVERFLOW;
        } else {
            sec = req->tv_sec;
        }
        msecs = (sec * 1000 + (req->tv_nsec + 999999)/1000000);
    }
    Sleep (msecs);
    for (c = 0; c < overflow; c++) {
        Sleep(MSEC_OVERFLOW * 1000);
    }
    if (rem) {
        rem->tv_sec = rem->tv_nsec = 0;
    }
    return 0;
#elif defined(HAVE_NANOSLEEP)
    return nanosleep(req, rem);
#else   /* !defined(HAVE_NANOSLEEP) && !defined(GAUCHE_WINDOWS) */
    /* This case should be excluded at the caller site */
    errno = EINVAL;
    return -1;
#endif
}

/*===============================================================
 * Yielding CPU (sched.h, if available)
 */

/* If sched_yield is not available, we make the calling thread sleep
   small amount of time, hoping there are other threads that can run
   in place. */
void
Scm_YieldCPU(void)
{
#if defined(GAUCHE_WINDOWS)
    /* Windows have select(), but it doesn't allow all fds are NULL. */
    Sleep(0);
#elif defined(HAVE_SCHED_YIELD)
    sched_yield();
#elif defined(HAVE_NANOSLEEP)
    /* We can use struct timespec instead of ScmTimeSpec here, for mingw
       won't use this path. */
    struct timespec spec;
    spec.tv_sec = 0;
    spec.tv_nsec = 1;
    nanosleep(&spec, NULL);
#elif defined(HAVE_SELECT)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 1;
    select(0, NULL, NULL, NULL, &tv);
#else /* the last resort */
    sleep(1);
#endif
}

/*===============================================================
 * Groups (grp.h)
 */

static void grp_print(ScmObj obj, ScmPort *port, ScmWriteContext *ctx SCM_UNUSED)
{
    Scm_Printf(port, "#<sys-group %S>",
               SCM_SYS_GROUP(obj)->name);
}

static int grp_compare(ScmObj x, ScmObj y, int equalp)
{
    ScmSysGroup *gx = SCM_SYS_GROUP(x);
    ScmSysGroup *gy = SCM_SYS_GROUP(y);

    if (equalp) {
        return (Scm_EqualP(gx->name, gy->name)
                && Scm_EqualP(gx->gid, gy->gid)
                && Scm_EqualP(gx->passwd, gy->passwd)
                && Scm_EqualP(gx->mem, gy->mem));
    } else {
        /* This is arbitrary, but having some order allows the object
           to be used as a key in treemap. */
        int r = Scm_Compare(gx->gid, gy->gid);
        if (r != 0) return r;
        r = Scm_Compare(gx->name, gy->name);
        if (r != 0) return r;
        r = Scm_Compare(gx->passwd, gy->passwd);
        if (r != 0) return r;
        return Scm_Compare(gx->mem, gy->mem);
    }
}

static ScmSmallInt grp_hash(ScmObj obj, ScmSmallInt salt, u_long flags)
{
    ScmSysGroup *g = SCM_SYS_GROUP(obj);
    ScmSmallInt h = salt;
    h = Scm_CombineHashValue(Scm_RecursiveHash(g->name, salt, flags), h);
    h = Scm_CombineHashValue(Scm_RecursiveHash(g->gid, salt, flags), h);
    h = Scm_CombineHashValue(Scm_RecursiveHash(g->passwd, salt, flags), h);
    h = Scm_CombineHashValue(Scm_RecursiveHash(g->mem, salt, flags), h);
    return h;
}

SCM_DEFINE_BUILTIN_CLASS(Scm_SysGroupClass,
                         grp_print, grp_compare, grp_hash,
                         NULL, SCM_CLASS_DEFAULT_CPL);

static ScmObj make_group(struct group *g)
{
    ScmSysGroup *sg = SCM_NEW(ScmSysGroup);
    SCM_SET_CLASS(sg, SCM_CLASS_SYS_GROUP);

    sg->name = SCM_MAKE_STR_COPYING(g->gr_name);
#ifdef HAVE_STRUCT_GROUP_GR_PASSWD
    sg->passwd = SCM_MAKE_STR_COPYING(g->gr_passwd);
#else
    sg->passwd = SCM_FALSE;
#endif
    sg->gid = Scm_MakeInteger(g->gr_gid);
    sg->mem = Scm_CStringArrayToList((const char**)g->gr_mem, -1,
                                     SCM_MAKSTR_COPYING);
    return SCM_OBJ(sg);
}

ScmObj Scm_GetGroupById(gid_t gid)
{
    struct group *gdata = getgrgid(gid);
    if (gdata == NULL) {
        Scm_SigCheck(Scm_VM());
        return SCM_FALSE;
    } else {
        return make_group(gdata);
    }
}

ScmObj Scm_GetGroupByName(ScmString *name)
{
    struct group *gdata = getgrnam(Scm_GetStringConst(name));
    if (gdata == NULL) {
        Scm_SigCheck(Scm_VM());
        return SCM_FALSE;
    } else {
        return make_group(gdata);
    }
}

#define GRP_GETTER(name) \
  static ScmObj SCM_CPP_CAT3(grp_, name, _get)(ScmSysGroup *s) \
  { return s->name; }

GRP_GETTER(name)
GRP_GETTER(gid)
GRP_GETTER(passwd)
GRP_GETTER(mem)

static ScmClassStaticSlotSpec grp_slots[] = {
    SCM_CLASS_SLOT_SPEC("name",   grp_name_get, NULL),
    SCM_CLASS_SLOT_SPEC("gid",    grp_gid_get, NULL),
    SCM_CLASS_SLOT_SPEC("passwd", grp_passwd_get, NULL),
    SCM_CLASS_SLOT_SPEC("mem",    grp_mem_get, NULL),
    SCM_CLASS_SLOT_SPEC_END()
};

/*===============================================================
 * Passwords (pwd.h)
 *   Patch provided by Yuuki Takahashi (t.yuuki@mbc.nifty.com)
 */

static void pwd_print(ScmObj obj, ScmPort *port,
                      ScmWriteContext *ctx SCM_UNUSED)
{
    Scm_Printf(port, "#<sys-passwd %S>",
               SCM_SYS_PASSWD(obj)->name);
}

SCM_DEFINE_BUILTIN_CLASS_SIMPLE(Scm_SysPasswdClass, pwd_print);

static ScmObj make_passwd(struct passwd *pw)
{
    ScmSysPasswd *sp = SCM_NEW(ScmSysPasswd);
    SCM_SET_CLASS(sp, SCM_CLASS_SYS_PASSWD);

    sp->name = SCM_MAKE_STR_COPYING(pw->pw_name);
    sp->uid = Scm_MakeInteger(pw->pw_uid);
    sp->gid = Scm_MakeInteger(pw->pw_gid);
#ifdef HAVE_STRUCT_PASSWD_PW_PASSWD
    sp->passwd = SCM_MAKE_STR_COPYING(pw->pw_passwd);
#else
    sp->passwd = SCM_FALSE;
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_GECOS
    sp->gecos = SCM_MAKE_STR_COPYING(pw->pw_gecos);
#else
    sp->gecos = SCM_FALSE;
#endif
#ifdef HAVE_STRUCT_PASSWD_PW_CLASS
    sp->pwclass = SCM_MAKE_STR_COPYING(pw->pw_class);
#else
    sp->pwclass = SCM_FALSE;
#endif
    sp->dir = SCM_MAKE_STR_COPYING(pw->pw_dir);
    sp->shell = SCM_MAKE_STR_COPYING(pw->pw_shell);
    return SCM_OBJ(sp);
}

ScmObj Scm_GetPasswdById(uid_t uid)
{
    struct passwd *pdata = getpwuid(uid);
    if (pdata == NULL) {
        Scm_SigCheck(Scm_VM());
        return SCM_FALSE;
    } else {
        return make_passwd(pdata);
    }
}

ScmObj Scm_GetPasswdByName(ScmString *name)
{
    struct passwd *pdata = getpwnam(Scm_GetStringConst(name));
    if (pdata == NULL) {
        Scm_SigCheck(Scm_VM());
        return SCM_FALSE;
    } else {
        return make_passwd(pdata);
    }
}

#define PWD_GETTER(name) \
  static ScmObj SCM_CPP_CAT3(pwd_, name, _get)(ScmSysPasswd *p) \
  { return p->name; }

PWD_GETTER(name)
PWD_GETTER(uid)
PWD_GETTER(gid)
PWD_GETTER(passwd)
PWD_GETTER(gecos)
PWD_GETTER(dir)
PWD_GETTER(shell)
PWD_GETTER(pwclass)

static ScmClassStaticSlotSpec pwd_slots[] = {
    SCM_CLASS_SLOT_SPEC("name",   pwd_name_get, NULL),
    SCM_CLASS_SLOT_SPEC("uid",    pwd_uid_get, NULL),
    SCM_CLASS_SLOT_SPEC("gid",    pwd_gid_get, NULL),
    SCM_CLASS_SLOT_SPEC("passwd", pwd_passwd_get, NULL),
    SCM_CLASS_SLOT_SPEC("gecos",  pwd_gecos_get, NULL),
    SCM_CLASS_SLOT_SPEC("dir",    pwd_dir_get, NULL),
    SCM_CLASS_SLOT_SPEC("shell",  pwd_shell_get, NULL),
    SCM_CLASS_SLOT_SPEC("class",  pwd_pwclass_get, NULL),
    SCM_CLASS_SLOT_SPEC_END()
};

/*
 * Check if we're suid/sgid-ed.
 */

/* We "remember" the initial state, in case issetugid() isn't available.
   This isn't perfect, for the process may change euid/egid before calling
   Scm_Init().  */
static int initial_ugid_differ = FALSE;

int Scm_IsSugid(void)
{
#if HAVE_ISSETUGID
    return issetugid();
#else
    return initial_ugid_differ;
#endif /* GAUCHE_WINDOWS */
}

/*===============================================================
 * Process management
 */

/* Child process management (windows only)
 *   On windows, parent-child relationship is very weak.  The system
 *   records parent's pid (and we can query it in a very twisted way), but
 *   the child's process record is discarded upon child's termination
 *   unless the parent keeps its process handle.   To emulate exec-wait
 *   semantics, we keep the list of child process handles whose status is
 *   unclaimed.
 *   One issue is that we cannot wait() for child processes that
 *   are created by Gauche extension code and not using Scm_SysExec API.
 */
#if defined(GAUCHE_WINDOWS)
static struct process_mgr_rec {
    ScmObj children;
    ScmInternalMutex mutex;
} process_mgr = { SCM_NIL, SCM_INTERNAL_MUTEX_INITIALIZER };

ScmObj win_process_register(ScmObj process)
{
    SCM_ASSERT(Scm_WinProcessP(process));
    ScmObj pair = Scm_Cons(process, SCM_NIL);
    SCM_INTERNAL_MUTEX_LOCK(process_mgr.mutex);
    SCM_SET_CDR_UNCHECKED(pair, process_mgr.children);
    process_mgr.children = pair;
    SCM_INTERNAL_MUTEX_UNLOCK(process_mgr.mutex);
    return process;
}

ScmObj win_process_unregister(ScmObj process)
{
    SCM_INTERNAL_MUTEX_LOCK(process_mgr.mutex);
    process_mgr.children = Scm_DeleteX(process, process_mgr.children,
                                       SCM_CMP_EQ);
    SCM_INTERNAL_MUTEX_UNLOCK(process_mgr.mutex);
    return process;
}

int win_process_active_child_p(ScmObj process)
{
    SCM_INTERNAL_MUTEX_LOCK(process_mgr.mutex);
    ScmObj r = Scm_Member(process, process_mgr.children, SCM_CMP_EQ);
    SCM_INTERNAL_MUTEX_UNLOCK(process_mgr.mutex);
    return !SCM_FALSEP(r);
}

ScmObj *win_process_get_array(int *size /*out*/)
{
    SCM_INTERNAL_MUTEX_LOCK(process_mgr.mutex);
    ScmSize array_size;
    ScmObj *r = Scm_ListToArray(process_mgr.children, &array_size, NULL, TRUE);
    *size = (int)array_size;
    SCM_INTERNAL_MUTEX_UNLOCK(process_mgr.mutex);
    return r;
}

void win_process_cleanup(void *data SCM_UNUSED)
{
    SCM_INTERNAL_MUTEX_LOCK(process_mgr.mutex);
    ScmObj cp;
    SCM_FOR_EACH(cp, process_mgr.children) {
        CloseHandle(Scm_WinHandle(SCM_CAR(cp), SCM_FALSE));
    }
    process_mgr.children = SCM_NIL;
    SCM_INTERNAL_MUTEX_UNLOCK(process_mgr.mutex);
}
#endif /*GAUCHE_WINDOWS*/

/* Command line construction (Windows only)
 *   In order to use CreateProcess we have to concatenate all arguments
 *   into one command line string.  Proper escaping should be considered
 *   when the arguments include whitespaces or double-quotes.
 *   It's pretty silly that we have to do this, since the child process
 *   crt will re-parse the command line again.  Besides, since the parsing
 *   of the command line is up to each application, THERE IS NO WAY TO
 *   GUARANTEE TO QUOTE THE ARGUMENTS PROPERLY.   This is intolerably
 *   broken specification.
 *
 *   If the program to run is .BAT or .CMD file, it is possible to
 *   manufacture an argument that injects undesired command execution.
 *   It is practically impossible to avoid the situation, so we rejects
 *   such an argument in the case.  See:
 *   https://flatt.tech/research/posts/batbadbut-you-cant-securely-execute-commands-on-windows/
 */
#if defined(GAUCHE_WINDOWS)
static _Bool unsafe_program(TCHAR *program_path, int program_path_len)
{
    if (program_path_len < 4) return FALSE;
    TCHAR *extp = program_path + program_path_len - 4;
    if (*extp != '.') return FALSE;
    if ((extp[1] == 'b' || extp[1] == 'B')
        && (extp[2] == 'a' || extp[2] == 'A')
        && (extp[3] == 't' || extp[3] == 'T'))
        return TRUE;
    if ((extp[1] == 'c' || extp[1] == 'C')
        && (extp[2] == 'm' || extp[2] == 'M')
        && (extp[3] == 'd' || extp[3] == 'D'))
        return TRUE;
    return FALSE;
}

static char *win_create_command_line(TCHAR *program_path,
                                     int program_path_len,
                                     ScmObj args)
{
    static ScmObj proc = SCM_UNDEFINED;
    SCM_BIND_PROC(proc, "%sys-escape-windows-command-line", Scm_GaucheModule());

    _Bool unsafep = unsafe_program(program_path, program_path_len);
    ScmObj ostr = Scm_MakeOutputStringPort(TRUE);
    ScmObj ap;
    SCM_FOR_EACH(ap, args) {
        ScmObj escaped = Scm_ApplyRec2(proc, SCM_CAR(ap), SCM_MAKE_BOOL(unsafep));
        Scm_Printf(SCM_PORT(ostr), "%A ", escaped);
    }
    ScmObj out = Scm_GetOutputStringUnsafe(SCM_PORT(ostr), 0);
    return Scm_GetString(SCM_STRING(out));
}
#endif /*GAUCHE_WINDOWS*/

/* Scm_SysExec
 *   execvp(), with optionally setting stdios correctly.
 *
 *   iomap argument, when provided, specifies how the open file descriptors
 *   are treated.  If it is not a pair, nothing will be changed for open
 *   file descriptors.  If it is a pair, it must be a list of
 *   (<to> . <from>), where <tofd> is an integer file descriptor that
 *   executed process will get, and <from> is either an integer file descriptor
 *   or a port.   If a list is passed to iomap, any file descriptors other
 *   than specified in the list will be closed before exec().
 *
 *   If forkp arg is TRUE, this function forks before swapping file
 *   descriptors.  It is more reliable way to fork&exec in multi-threaded
 *   program.  In such a case, this function returns Scheme integer to
 *   show the children's pid.   If fork arg is FALSE, this procedure
 *   of course never returns.
 *
 *   On Windows port, this returns a process handle obejct instead of
 *   pid of the child process in fork mode.  We need to keep handle, or
 *   the process exit status will be lost when the child process terminates.
 */
ScmObj Scm_SysExec(ScmString *file, ScmObj args, ScmObj iomap,
                   ScmSysSigset *mask SCM_UNUSED, ScmString *dir,
                   ScmObj env, u_long flags)
{
    int argc = Scm_Length(args);
    int forkp = flags & SCM_EXEC_WITH_FORK;
    int detachp = flags & SCM_EXEC_DETACHED;

    if (argc < 1) {
        Scm_Error("argument list must have at least one element: %S", args);
    }

    /* make a C array of C strings */
    char **argv = Scm_ListToCStringArray(args, TRUE, NULL);

    /* setting up iomap table */
    int *fds = Scm_SysPrepareFdMap(iomap);

    /* find executable.
       If FILE contains path separators, we don't use path search.
    */
    const char *program;
    if (SCM_FALSEP(Scm_StringScanChar(file, '/', SCM_STRING_SCAN_INDEX))
#if defined(GAUCHE_WINDOWS)
        && SCM_FALSEP(Scm_StringScanChar(file, '\\', SCM_STRING_SCAN_INDEX))
#endif
        ) {
        static ScmObj sys_find_file_proc = SCM_UNDEFINED;
        SCM_BIND_PROC(sys_find_file_proc, "sys-find-file", Scm_GaucheModule());
        ScmObj fullpath = Scm_ApplyRec1(sys_find_file_proc, SCM_OBJ(file));
        if (!SCM_STRINGP(fullpath)) {
            Scm_Error("Can't find executable file %S in PATH.", SCM_OBJ(file));
        }
        program = Scm_GetStringConst(SCM_STRING(fullpath));
    } else {
        program = Scm_GetStringConst(file);
    }

    /*
     * From now on, we have totally different code for Unix and Windows.
     */
#if !defined(GAUCHE_WINDOWS)
    /*
     * Unix path
     */
    const char *cdir = NULL;
    if (dir != NULL) cdir = Scm_GetStringConst(dir);

    /* When requested, call fork() here. */
    pid_t pid = 0;
    if (forkp) {
        SCM_SYSCALL(pid, fork());
        if (pid < 0) Scm_SysError("fork failed");
    }

    if (!forkp || pid == 0) {   /* possibly the child process */

        /* If we're running the daemon, we fork again to detach the parent,
           and also reset the session id. */
        if (detachp) {
            SCM_SYSCALL(pid, fork());
            if (pid < 0) Scm_SysError("fork failed");
            if (pid > 0) exit(0);   /* not Scm_Exit(), for we don't want to
                                       run the cleanup stuff. */
            setsid();
        }

        if (cdir != NULL) {
            if (chdir(cdir) < 0) {
                Scm_Panic("chdir to %s failed before executing %s: %s",
                          cdir, program, strerror(errno));
            }
        }

        Scm_SysSwapFds(fds);
        if (mask) {
            Scm_ResetSignalHandlers(&mask->set);
            Scm_SysSigmask(SIG_SETMASK, mask);
        }

        if (SCM_LISTP(env)) {
            execve(program, (char *const*)argv,
                   Scm_ListToCStringArray(env, TRUE, NULL));
        } else {
            execv(program, (char *const*)argv);
        }
        /* here, we failed */
        Scm_Panic("exec failed: %s: %s", program, strerror(errno));
    }

    /* We come here only when fork is requested. */
    return Scm_MakeInteger(pid);
#else  /* GAUCHE_WINDOWS */
    /*
     * Windows path
     */
    const char *cdir = NULL;
    if (dir != NULL) {
        /* we need full path for CreateProcess. */
        dir = SCM_STRING(Scm_NormalizePathname(dir, SCM_PATH_ABSOLUTE|SCM_PATH_CANONICALIZE));
        cdir = Scm_GetStringConst(dir);

        /* If the program is given in relative pathname,
           it must be adjusted relative to the specified directory. */
        if (program[0] != '/' && program[0] != '\\'
            && !(program[0] && program[1] == ':')) {
            ScmDString ds;
            int c = cdir[strlen(cdir)-1];
            Scm_DStringInit(&ds);
            Scm_DStringPutz(&ds, cdir, -1);
            if (c != '/' && c != '\\') Scm_DStringPutc(&ds, SCM_CHAR('/'));
            Scm_DStringPutz(&ds, program, -1);
            program = Scm_DStringGetz(&ds);
        }
    }

    if (forkp) {
        TCHAR program_path[MAX_PATH+1], *filepart;
        win_redirects *hs = win_prepare_handles(fds);
        PROCESS_INFORMATION pi;
        DWORD creation_flags = 0;

        DWORD pathlen = SearchPath(NULL, SCM_MBS2WCS(program),
                                   _T(".exe"), MAX_PATH, program_path,
                                   &filepart);
        if (pathlen == 0) Scm_SysError("cannot find program '%s'", program);
        program_path[pathlen] = 0;

        STARTUPINFO si;
        GetStartupInfo(&si);
        if (hs != NULL) {
            si.dwFlags |= STARTF_USESTDHANDLES;
            si.hStdInput  = hs[0].h;
            si.hStdOutput = hs[1].h;
            si.hStdError  = hs[2].h;
        }

        LPCTSTR curdir = NULL;
        if (cdir != NULL) curdir = SCM_MBS2WCS(cdir);

        if (detachp) {
            creation_flags |= CREATE_NEW_PROCESS_GROUP;
        }

        char *cmdline = win_create_command_line(program_path, pathlen, args);

        TCHAR *tenvp = NULL;
        if (SCM_NULLP(env)) {
            /* CreateProcess rejects empty emvironemnt block.
               (cf. https://nullprogram.com/blog/2023/08/23/)
               We insert a dummy environemnt variable to workaround it.
            */
            env = SCM_LIST1(SCM_MAKE_STR("AVOID_EMPTY_ENVIRONMENT=1"));
        }
        if (SCM_LISTP(env)) {
            /* We need to consturct a TCHAR[] block that contains NUL
               characters, which gets tricky, for our utility MBS2WCS does
               not handle the case.  This is not a performance critical
               path, so we allocage temporary strings abundantly.
            */
            size_t numenvs = Scm_Length(env);
            TCHAR **envs = SCM_NEW_ATOMIC_ARRAY(TCHAR*, numenvs);
            size_t nc = 0;
            ScmObj ep = env;
            for (size_t i = 0; i < numenvs; i++) {
                if (!(SCM_PAIRP(ep) && SCM_STRINGP(SCM_CAR(ep)))) {
                    Scm_Error("Invalid environment list: %S", env);
                }
                envs[i] =
                    Scm_MBS2WCS(Scm_GetStringConst(SCM_STRING(SCM_CAR(ep))));
                nc += _tcslen(envs[i]) + 1;
                ep = SCM_CDR(ep);
            }
            nc += 1;

            tenvp = SCM_NEW_ATOMIC_ARRAY(TCHAR, nc);
            TCHAR *tp = tenvp;
            for (size_t i = 0; i < numenvs; i++) {
                size_t elen = _tcslen(envs[i])+1;
                memcpy(tp, envs[i], elen * sizeof(TCHAR));
                tp += elen;
            }
            *tp = 0;
            creation_flags |= CREATE_UNICODE_ENVIRONMENT;
        }

        BOOL r = CreateProcess(program_path,
                               SCM_MBS2WCS(cmdline),
                               NULL, /* process attr */
                               NULL, /* thread addr */
                               TRUE, /* inherit handles */
                               creation_flags, /* creation flags */
                               tenvp, /* nenvironment */
                               curdir, /* current dir */
                               &si,  /* startup info */
                               &pi); /* process info */
        if (hs != NULL) {
            for (int i=0; i<3; i++) {
                /* hs[i].h may be a handle duped in win_prepare_handles().
                   We have to close it in parent process or they would be
                   inherited to subsequent child process.  (The higher-level
                   Scheme routine closes the open end of the pipe, but that
                   won't affect the duped one. */
                if (hs[i].duped) CloseHandle(hs[i].h);
            }
        }
        if (r == 0) Scm_SysError("spawning %s failed", program);
        CloseHandle(pi.hThread); /* we don't need it. */
        return win_process_register(Scm_MakeWinProcess(pi.hProcess));
    } else {
        Scm_SysSwapFds(fds);
        if (cdir != NULL) {
            if (_chdir(cdir) < 0) {
                Scm_SysError("Couldn't chdir to %s", cdir);
            }
        }
        /* TODO: We should probably use Windows API to handle various
           options consistently with fork-and-exec case above. */
#if defined(__MINGW64_VERSION_MAJOR)
        execvp(program, (char *const*)argv);
#else  /* !defined(__MINGW64_VERSION_MAJOR) */
        execvp(program, (const char *const*)argv);
#endif /* !defined(__MINGW64_VERSION_MAJOR) */
        Scm_Panic("exec failed: %s: %s", program, strerror(errno));
    }
    return SCM_FALSE; /* dummy */
#endif /* GAUCHE_WINDOWS */
}

/* Two auxiliary functions to support iomap feature.  They are exposed
   so that the library can implement iomap feature as the same way as
   sys-exec.

   The first function, Scm_SysPrepareFdMap, walks iomap structure and
   prepare a table of file descriptors to modify.  The second function,
   Scm_SysSwapFds, takes the table and modifies process's file descriptors.

   We need to split this feature to two function, since it is unsafe
   to raise an error after fork() in multi-threaded environment.
   Scm_SysPrepareFdMap may throw an error if passed iomap contains
   invalid entries.  On the other hand, Scm_SysSwapFds just aborts if
   things goes wrong---not only because of the MT-safety issue, but also
   it is generally impossible to handle errors reasonably since we don't
   even sure we have stdios.   And the client code is supposed to call
   fork() between these functions.

   The client code should treat the returned pointer of Scm_SysPrepareFdMap
   opaque, and pass it to Scm_SysSwapFds as is.
*/
int *Scm_SysPrepareFdMap(ScmObj iomap)
{
    int *fds = NULL;
    if (SCM_PAIRP(iomap)) {
        int iollen = Scm_Length(iomap);

        /* check argument vailidity before duping file descriptors, so that
           we can still use Scm_Error */
        if (iollen < 0) {
            Scm_Error("proper list required for iolist, but got %S", iomap);
        }
        fds    = SCM_NEW_ATOMIC2(int *, 2 * iollen * sizeof(int) + 1);
        fds[0] = iollen;
        int *tofd   = fds + 1;
        int *fromfd = fds + 1 + iollen;

        ScmObj iop;
        int i = 0;
        SCM_FOR_EACH(iop, iomap) {
            ScmObj port, elt = SCM_CAR(iop);
            if (!SCM_PAIRP(elt) || !SCM_INTP(SCM_CAR(elt))
                || (!SCM_PORTP(SCM_CDR(elt)) && !SCM_INTP(SCM_CDR(elt)))) {
                Scm_Error("bad iomap specification: needs (int . int-or-port): %S", elt);
            }
            tofd[i] = SCM_INT_VALUE(SCM_CAR(elt));
            if (SCM_INTP(SCM_CDR(elt))) {
                fromfd[i] = SCM_INT_VALUE(SCM_CDR(elt));
            } else {
                port = SCM_CDAR(iop);
                fromfd[i] = Scm_PortFileNo(SCM_PORT(port));
                if (fromfd[i] < 0) {
                    Scm_Error("iolist requires a port that has associated file descriptor, but got %S",
                              SCM_CDAR(iop));
                }
                if (tofd[i] == 0 && !SCM_IPORTP(port))
                    Scm_Error("input port required to make it stdin: %S",
                              port);
                if (tofd[i] == 1 && !SCM_OPORTP(port))
                    Scm_Error("output port required to make it stdout: %S",
                              port);
                if (tofd[i] == 2 && !SCM_OPORTP(port))
                    Scm_Error("output port required to make it stderr: %S",
                              port);
            }
            i++;
        }
    }
    return fds;
}

void Scm_SysSwapFds(int *fds)
{
    if (fds == NULL) return;

    int maxfd;
    int nfds = fds[0];
    int *tofd   = fds + 1;
    int *fromfd = fds + 1 + nfds;

    /* TODO: use getdtablehi if available */
#if !defined(GAUCHE_WINDOWS)
    if ((maxfd = sysconf(_SC_OPEN_MAX)) < 0) {
        Scm_Panic("failed to get OPEN_MAX value from sysconf");
    }
#else  /*GAUCHE_WINDOWS*/
    maxfd = 256;        /* guess it and cross your finger */
#endif /*GAUCHE_WINDOWS*/

    /* Dup fromfd to the corresponding tofd.  We need to be careful
       not to override the destination fd if it will be used. */
    for (int i=0; i<nfds; i++) {
        if (tofd[i] == fromfd[i]) continue;
        for (int j=i+1; j<nfds; j++) {
            if (tofd[i] == fromfd[j]) {
                int tmp = dup(tofd[i]);
                if (tmp < 0) Scm_Panic("dup failed: %s", strerror(errno));
                fromfd[j] = tmp;
            }
        }
        if (dup2(fromfd[i], tofd[i]) < 0)
            Scm_Panic("dup2 failed: %s", strerror(errno));
    }

    /* Close unused fds */
    for (int fd=0; fd<maxfd; fd++) {
        int j;
        for (j=0; j<nfds; j++) if (fd == tofd[j]) break;
        if (j == nfds) close(fd);
    }
}

#if defined(GAUCHE_WINDOWS)
/* Fds is Scm_SysPrepareFdMap returns. */
static win_redirects *win_prepare_handles(int *fds)
{
    if (fds == NULL) return NULL;

    /* For the time being, we only consider stdin, stdout, and stderr. */
    win_redirects *hs = SCM_NEW_ATOMIC_ARRAY(win_redirects, 3);
    int count = fds[0];

    for (int i=0; i<count; i++) {
        int to = fds[i+1], from = fds[i+1+count];
        if (to >= 0 && to < 3) {
            if (from >= 3) {
                /* FROM may be a pipe.  in that case, it will be closed
                   in the higher-level, so we shouldn't give
                   DUPLICATE_CLOSE_SOURCE here. */
                HANDLE zh;
                if (!DuplicateHandle(GetCurrentProcess(),
                                     (HANDLE)_get_osfhandle(from),
                                     GetCurrentProcess(),
                                     &zh,
                                     0, TRUE,
                                     DUPLICATE_SAME_ACCESS)) {
                    Scm_SysError("DuplicateHandle failed");
                }
                hs[to].h = zh;
                hs[to].duped = TRUE;
            } else {
                hs[to].h = (HANDLE)_get_osfhandle(from);
                hs[to].duped = FALSE;
            }
        }
    }
    for (int i=0; i<3; i++) {
        if (hs[i].h == NULL) {
            hs[i].h = (HANDLE)_get_osfhandle(i);
            hs[i].duped = FALSE;
        }
    }
    return hs;
}
#endif /*GAUCHE_WINDOWS*/

/*===============================================================
 * Kill
 *
 *  It is simple on Unix, but on windows it is a lot more involved,
 *  mainly due to the lack of signals as the means of IPC.
 */
void Scm_SysKill(ScmObj process, int signal)
{
#if !defined(GAUCHE_WINDOWS)
    pid_t pid;
    int r;
    if (!SCM_INTEGERP(process)) SCM_TYPE_ERROR(process, "integer process id");
    pid = Scm_GetInteger(process);
    SCM_SYSCALL(r, kill(pid, signal));
    if (r < 0) Scm_SysError("kill failed");
#else  /*GAUCHE_WINDOWS*/
    /* You cannot really "send" signals to other processes on Windows.
       We try to emulate SIGKILL and SIGINT by Windows API.
       To send a signal to the current process we can use raise(). */
    HANDLE p;
    BOOL r;
    DWORD errcode;
    int pid_given = FALSE;
    pid_t pid = 0;

    if (SCM_INTEGERP(process)) {
        pid_given = TRUE; pid = Scm_GetInteger(process);
    } else if (Scm_WinProcessP(process)) {
        pid = Scm_WinProcessPID(process);
    } else {
        SCM_TYPE_ERROR(process, "process handle or integer process id");
    }

    if (signal == SIGKILL) {
        if (pid_given) {
            p = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (p == NULL) Scm_SysError("OpenProcess failed for pid %d", pid);
        } else {
            p = Scm_WinProcess(process);
        }
        /* We send 0xff00 + KILL, so that the receiving process (if it is
           Gauche) can yield an exit status that indicates it is kill. */
        r = TerminateProcess(p, SIGKILL+0xff00);
        errcode = GetLastError();
        if (pid_given) CloseHandle(p);
        SetLastError(errcode);
        if (r == 0) Scm_SysError("TerminateProcess failed");
        return;
    }
    /* another idea; we may map SIGTERM to WM_CLOSE message. */

    if (signal == 0) {
        /* We're supposed to do the error check without actually sending
           the signal.   For now we just pretend nothing's wrong. */
        return;
    }
    if (pid == getpid()) {
        /* we're sending signal to the current process. */
        int r = raise(signal); /* r==0 is success */
        if (r < 0) Scm_SysError("raise failed");
        return;
    }
    if (signal == SIGINT || signal == SIGABRT) {
        /* we can emulate these signals by console event, although the
           semantics of process group differ from unix significantly.
           Process group id is the same as the pid of the process
           that started the group.  So you cannot send SIGABRT only
           to the process group leader.  OTOH, for SIGINT, the windows
           manual says it always directed to the specified process,
           not the process group, unless pid == 0 */
        if (pid < 0) pid = -pid;
        r = GenerateConsoleCtrlEvent(pid,
                                     (signal == SIGINT)?
                                     CTRL_C_EVENT : CTRL_BREAK_EVENT);
        if (r == 0) {
            Scm_SysError("GenerateConsoleCtrlEvent failed for process %d", pid);
        }
        return;
    }
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
#endif /*GAUCHE_WINDOWS*/
}

/*===============================================================
 * Wait
 *
 *  A wrapper of waitpid.  Returns two values---the process object or pid that
 *  whose status has been taken, and the exit status.
 *  Again, it is simple on Unix, but on windows it is a lot more involved.
 */

ScmObj Scm_SysWait(ScmObj process, int options)
{
#if !defined(GAUCHE_WINDOWS)
    pid_t r;
    int status = 0;
    if (!SCM_INTEGERP(process)) SCM_TYPE_ERROR(process, "integer process id");
    SCM_SYSCALL(r, waitpid(Scm_GetInteger(process), &status, options));
    if (r < 0) Scm_SysError("waitpid() failed");
    return Scm_Values2(Scm_MakeInteger(r), Scm_MakeInteger(status));
#else  /* GAUCHE_WINDOWS */
    /* We have four cases
       process is integer and < -1   -> not supported.
       process is -1 or 0 -> wait for all children (we ignore process group)
       process is integer and > 0  -> wait for specific pid
       process is #<win:process-handle> -> wait for specified process
       The common op is factored out in win_wait_for_handles. */
    int r, status = 0;

    if (SCM_INTEGERP(process)) {
        pid_t pid = Scm_GetInteger(process);
        if (pid < -1) {
            /* Windows doesn't have the concept of "process group id" */
            SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
            Scm_SysError("waitpid cannot wait for process group on Windows.");
        }
        if (pid > 0) {
            /* wait for specific pid */
            HANDLE handle = OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION,
                                        FALSE, pid);
            DWORD errcode;
            if (handle == NULL) {
                Scm_SysError("OpenProcess failed for pid %d", pid);
            }
            r = win_wait_for_handles(&handle, 1, options, &status);
            errcode = GetLastError();
            CloseHandle(handle);
            SetLastError(errcode);
            if (r == -2) goto timeout;
            if (r == -1) goto error;
            return Scm_Values2(Scm_MakeInteger(pid), Scm_MakeInteger(status));
        }
        else {
            /* wait for any children. */
            ScmObj *children;
            int num_children, i;
            HANDLE *handles;
            children = win_process_get_array(&num_children);
            if (num_children == 0) {
                SetLastError(ERROR_WAIT_NO_CHILDREN);
                Scm_SysError("waitpid failed");
            }
            handles = SCM_NEW_ATOMIC_ARRAY(HANDLE, num_children);
            for (i=0; i<num_children; i++) {
                handles[i] = Scm_WinProcess(children[i]);
            }
            r = win_wait_for_handles(handles, num_children, options, &status);
            if (r == -2) goto timeout;
            if (r == -1) goto error;
            win_process_unregister(children[r]);
            return Scm_Values2(children[r], Scm_MakeInteger(status));
        }
    } else if (Scm_WinProcessP(process)) {
        /* wait for the specified process */
        HANDLE handle;
        if (!win_process_active_child_p(process)) {
            SetLastError(ERROR_WAIT_NO_CHILDREN);
            Scm_SysError("waitpid failed");
        }
        handle = Scm_WinProcess(process);
        r = win_wait_for_handles(&handle, 1, options, &status);
        if (r == -2) goto timeout;
        if (r == -1) goto error;
        win_process_unregister(process);
        return Scm_Values2(process, Scm_MakeInteger(status));
    }
  timeout:
    return Scm_Values2(SCM_MAKE_INT(0), SCM_MAKE_INT(0));
  error:
    Scm_SysError("waitpid failed");
    return SCM_UNDEFINED;  /* dummy */
#endif /* GAUCHE_WINDOWS */
}

#if defined(GAUCHE_WINDOWS)
/* aux fn. */
static int win_wait_for_handles(HANDLE *handles, int nhandles, int options,
                                int *status /*out*/)
{
    DWORD r = MsgWaitForMultipleObjects(nhandles,
                                        handles,
                                        FALSE,
                                        (options&WNOHANG)? 0 : INFINITE,
                                        0);
    if (r == WAIT_FAILED) return -1;
    if (r == WAIT_TIMEOUT) return -2;
    if ((int)r >= (int)WAIT_OBJECT_0 && (int)r < (int)WAIT_OBJECT_0 + nhandles) {
        DWORD exitcode;
        int index = r - WAIT_OBJECT_0;
        r = GetExitCodeProcess(handles[index], &exitcode);
        if (r == 0) return -1;
        *status = exitcode;
        return index;
    }
    return -1;
}
#endif /*GAUCHE_WINDOWS*/

/*===============================================================
 * select
 */

#ifdef HAVE_SELECT
static ScmObj fdset_allocate(ScmClass *klass, ScmObj initargs SCM_UNUSED)
{
    ScmSysFdset *set = SCM_NEW_INSTANCE(ScmSysFdset, klass);
    set->maxfd = -1;
    FD_ZERO(&set->fdset);
    return SCM_OBJ(set);
}

static ScmSysFdset *fdset_copy(ScmSysFdset *fdset)
{
    ScmSysFdset *set = SCM_NEW(ScmSysFdset);
    SCM_SET_CLASS(set, SCM_CLASS_SYS_FDSET);
    set->maxfd = fdset->maxfd;
    set->fdset = fdset->fdset;
    return set;
}

SCM_DEFINE_BUILTIN_CLASS(Scm_SysFdsetClass, NULL, NULL, NULL,
                         fdset_allocate, SCM_CLASS_DEFAULT_CPL);

static ScmSysFdset *select_checkfd(ScmObj fds)
{
    if (SCM_FALSEP(fds)) return NULL;
    if (!SCM_SYS_FDSET_P(fds))
        Scm_Error("sys-fdset object or #f is required, but got %S", fds);
    return SCM_SYS_FDSET(fds);
}

static struct timeval *select_timeval(ScmObj timeout, struct timeval *tm)
{
    if (SCM_FALSEP(timeout)) return NULL;
    if (SCM_INTP(timeout)) {
        int val = SCM_INT_VALUE(timeout);
        if (val < 0) goto badtv;
        tm->tv_sec = val / 1000000;
        tm->tv_usec = val % 1000000;
        return tm;
    } else if (SCM_BIGNUMP(timeout)) {
        long usec;
        ScmObj sec;
        if (Scm_Sign(timeout) < 0) goto badtv;
        sec = Scm_BignumDivSI(SCM_BIGNUM(timeout), 1000000, &usec);
        tm->tv_sec = Scm_GetInteger(sec);
        tm->tv_usec = usec;
        return tm;
    } else if (SCM_FLONUMP(timeout)) {
        long val = Scm_GetInteger(timeout);
        if (val < 0) goto badtv;
        tm->tv_sec = val / 1000000;
        tm->tv_usec = val % 1000000;
        return tm;
    } else if (SCM_PAIRP(timeout) && SCM_PAIRP(SCM_CDR(timeout))) {
        ScmObj sec = SCM_CAR(timeout);
        ScmObj usec = SCM_CADR(timeout);
        long isec, iusec;
        if (!Scm_IntegerP(sec) || !Scm_IntegerP(usec)) goto badtv;
        isec = Scm_GetInteger(sec);
        iusec = Scm_GetInteger(usec);
        if (isec < 0 || iusec < 0) goto badtv;
        tm->tv_sec = isec;
        tm->tv_usec = iusec;
        return tm;
    }
  badtv:
    Scm_Error("timeval needs to be a real number (in microseconds) or a list of two integers (seconds and microseconds), but got %S", timeout);
    return NULL;                /* dummy */
}

static ScmObj select_int(ScmSysFdset *rfds, ScmSysFdset *wfds,
                         ScmSysFdset *efds, ScmObj timeout)
{
    int numfds, maxfds = 0;
    struct timeval tm;
    if (rfds) maxfds = rfds->maxfd;
    if (wfds && wfds->maxfd > maxfds) maxfds = wfds->maxfd;
    if (efds && efds->maxfd > maxfds) maxfds = efds->maxfd;

    SCM_SYSCALL(numfds,
                select(maxfds+1,
                       (rfds? &rfds->fdset : NULL),
                       (wfds? &wfds->fdset : NULL),
                       (efds? &efds->fdset : NULL),
                       select_timeval(timeout, &tm)));
    if (numfds < 0) Scm_SysError("select failed");
    return Scm_Values4(Scm_MakeInteger(numfds),
                       (rfds? SCM_OBJ(rfds) : SCM_FALSE),
                       (wfds? SCM_OBJ(wfds) : SCM_FALSE),
                       (efds? SCM_OBJ(efds) : SCM_FALSE));
}

ScmObj Scm_SysSelect(ScmObj rfds, ScmObj wfds, ScmObj efds, ScmObj timeout)
{
    ScmSysFdset *r = select_checkfd(rfds);
    ScmSysFdset *w = select_checkfd(wfds);
    ScmSysFdset *e = select_checkfd(efds);
    return select_int((r? fdset_copy(r) : NULL),
                      (w? fdset_copy(w) : NULL),
                      (e? fdset_copy(e) : NULL),
                      timeout);
}

ScmObj Scm_SysSelectX(ScmObj rfds, ScmObj wfds, ScmObj efds, ScmObj timeout)
{
    ScmSysFdset *r = select_checkfd(rfds);
    ScmSysFdset *w = select_checkfd(wfds);
    ScmSysFdset *e = select_checkfd(efds);
    return select_int(r, w, e, timeout);
}

#endif /* HAVE_SELECT */

/*===============================================================
 * Environment
 */

/* We provide a compatibility layer for getenv/setenv stuff, whose semantics
   slightly differ among platforms.

   POSIX putenv() has a flaw that passed string can't be freed reliably;
   the system may retain the pointer, so the caller can't free it afterwards,
   while putenv() itself can't know if the passed pointer is malloc-ed or
   static.  Some Unixes appears to change the semantics, guaranteeing
   the system copies the passed string so that the caller can free it;
   however, it's not easy to check which semantics the given platform uses.

   What POSIX suggests is setenv() when you want to pass malloc-ed
   strings.  Unfortunately it is a newer addition and not all platforms
   supports it.  Windows doesn't, either, but it offers _[w]putenv_s
   as an alternative.  Unfortunately again, current MinGW doesn't include
   _[w]putenv_s in its headers and import libraries.

   So, for those platforms, we use putenv/_wputenv.  We track allocated
   memory in env_string table, keyed by names of envvars, and free them
   whenever we put a new definition of envvars we've inserted before.

   Another merit of this compatibility layer is to guarantee MT-safeness;
   Putenv/setenv aren't usually MT-safe, neither is getenv when environment
   is being modified.
*/

static ScmInternalMutex env_mutex;
static ScmHashCore env_strings; /* name -> malloc-ed mem.
                                   used with putenv()/_wputenv() to prevent
                                   leak. */

const char *Scm_GetEnv(const char *name)
{
#if defined(GAUCHE_WINDOWS) && defined(UNICODE)
    const wchar_t *wname = Scm_MBS2WCS(name);
    const char *value = NULL;
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    const wchar_t *wvalue = _wgetenv(wname);
    if (wvalue != NULL) {
        value = Scm_WCS2MBS(wvalue);
    }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    return value;
#else  /*!(defined(GAUCHE_WINDOWS) && defined(UNICODE))*/
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    const char *value = SCM_STRDUP(getenv(name));
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    return value;
#endif /*!(defined(GAUCHE_WINDOWS) && defined(UNICODE))*/
}

void Scm_SetEnv(const char *name, const char *value, int overwrite)
{
#if defined(GAUCHE_WINDOWS) && defined(UNICODE)
    /* We need to use _wputenv for wide-character support.  Since we pass
       the converted strings to OS, we have to allocate them by malloc.
       To prevent leak, we register the allocated memory to the global
       hash table, and free it when Scm_SetEnv is called with the same NAME
       again. */
    wchar_t *wname = Scm_MBS2WCS(name);
    wchar_t *wvalue = Scm_MBS2WCS(value);
    int nlen = wcslen(wname);
    int vlen = wcslen(wvalue);
    wchar_t *wnameval = (wchar_t*)malloc((nlen+vlen+2)*sizeof(wchar_t));
    if (wnameval == NULL) {
        Scm_Error("sys-setenv: out of memory");
    }
    wcscpy(wnameval, wname);
    wcscpy(wnameval+nlen, L"=");
    wcscpy(wnameval+nlen+1, wvalue);

    ScmObj sname = Scm_MakeString(name, -1, -1, SCM_STRING_COPYING);

    int result = 0;
    wchar_t *prev_mem = NULL;

    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    if (overwrite || _wgetenv(wname) == NULL) {
        result = _wputenv(wnameval);
        if (result >= 0) {
            ScmDictEntry *e = Scm_HashCoreSearch(&env_strings,
                                                 (intptr_t)sname,
                                                 SCM_DICT_CREATE);
            /* SCM_DICT_VALUE is only for ScmObj, so we directly access value
               field here. */
            prev_mem = (wchar_t*)e->value;
            e->value = (intptr_t)wnameval;
        }
    }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);

    if (result < 0) {
        free(wnameval);
        Scm_SysError("setenv failed on '%s=%s'", name, value);
    }
    if (prev_mem != NULL) {
        free(prev_mem);
    }
#elif defined(HAVE_SETENV)
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    int r = setenv(name, value, overwrite);
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    if (r < 0) Scm_SysError("setenv failed on '%s=%s'", name, value);
#elif defined(HAVE_PUTENV)
    int nlen = (int)strlen(name);
    int vlen = (int)strlen(value);
    char *nameval = (char*)malloc(nlen+vlen+2);
    if (nameval == NULL) {
        Scm_Error("sys-setenv: out of memory");
    }
    strcpy(nameval, name);
    strcpy(nameval+nlen, "=");
    strcpy(nameval+nlen+1, value);

    ScmObj sname = Scm_MakeString(name, -1, -1, SCM_STRING_COPYING);

    int result = 0;
    char *prev_mem = NULL;

    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    if (overwrite || getenv(name) == NULL) {
        result = putenv(nameval);
        if (result >= 0) {
            ScmDictEntry *e = Scm_HashCoreSearch(&env_strings,
                                                 (intptr_t)sname,
                                                 SCM_DICT_CREATE);
            /* SCM_DICT_VALUE is only for ScmObj, so we directly access value
               field here. */
            prev_mem = (char*)e->value;
            e->value = (intptr_t)nameval;
        }
    }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    if (result < 0) {
        free (nameval);
        Scm_SysError("putenv failed on '%s=%s'", name, value);
    }
    if (prev_mem != NULL) {
        free(prev_mem);
    }
#else /* !HAVE_SETENV && !HAVE_PUTENV */
    /* We can't do much.  we may replace environ by ourselves, but
       it is unlikely that the system have extern environ and not putenv.
    */
    Scm_Error("neither setenv nor putenv is supported on this platform.");
#endif
}

/* Returns the system's environment table as a list of strings.
   Each string is in the format of "key=value". */
ScmObj Scm_Environ(void)
{
#if defined(GAUCHE_WINDOWS)
#define ENV_BUFSIZ 64
    LPVOID ss = GetEnvironmentStrings();
    ScmObj h = SCM_NIL, t = SCM_NIL;
    TCHAR *cp = (TCHAR*)ss, *pp;
    TCHAR sbuf[ENV_BUFSIZ], *buf=sbuf;
    int bsize = ENV_BUFSIZ, size;

    do {
        for (pp=cp; *pp; pp++) /*proceed ptr*/;
        size = (int)(pp - cp) + 1;
        if (size >= bsize) {
            buf = SCM_NEW_ATOMIC_ARRAY(TCHAR, size);
            bsize = size;
        }
        memcpy(buf, cp, size*sizeof(TCHAR));
        SCM_APPEND1(h, t, SCM_MAKE_STR_COPYING(SCM_WCS2MBS(buf)));
        cp = pp+1;
    } while (pp[1] != 0);
    FreeEnvironmentStrings(ss);
    return h;
#else
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
#  if defined(HAVE_CRT_EXTERNS_H)
    char **environ = *_NSGetEnviron();  /* OSX Hack*/
#  endif
    ScmObj r = (environ == NULL
                ? SCM_NIL
                : Scm_CStringArrayToList((const char**)environ, -1,
                                         SCM_STRING_COPYING));
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    return r;
#endif /*!GAUCHE_WINDOWS*/
}

void Scm_UnsetEnv(const char *name)
{
#if defined(HAVE_UNSETENV)
    /* NB: If we HAVE_SETENV, we don't have any entries in env_strings,
       so the lookup of snv_strings is a waste; but the result is always
       NULL and it won't harm the operation, and we expect sys-unsetenv
       is rarely used, so we just let it waste cpu cycles. */
    char *prev_mem = NULL;
    ScmObj sname = Scm_MakeString(name, -1, -1, SCM_STRING_COPYING);
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    int r = unsetenv(name);
    ScmDictEntry *e = Scm_HashCoreSearch(&env_strings,
                                         (intptr_t)sname,
                                         SCM_DICT_DELETE);
    if (e != NULL) { prev_mem = (char*)e->value; e->value = (intptr_t)NULL; }
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    if (r < 0) Scm_SysError("unsetenv failed on %s", name);
    if (prev_mem != NULL) free(prev_mem);
#else  /*!HAVE_UNSETENV*/
    (void)name; /* suppress unused var warning */
    Scm_Error("sys-unsetenv is not supported on this platform.");
#endif /*!HAVE_UNSETENV*/
}

void Scm_ClearEnv()
{
#if defined(HAVE_CLEARENV)
    /* As in Scm_UnsetEnv, we don't need env_strings business if
       we HAVE_SETENV, but it does no harm either. */
    (void)SCM_INTERNAL_MUTEX_LOCK(env_mutex);
    int r = clearenv();
    ScmHashIter iter;
    Scm_HashIterInit(&iter, &env_strings);
    ScmDictEntry *e;
    while ((e = Scm_HashIterNext(&iter)) != NULL) {
        free((void*)e->value);
        e->value = (intptr_t)NULL;
    }
    Scm_HashCoreClear(&env_strings);
    (void)SCM_INTERNAL_MUTEX_UNLOCK(env_mutex);
    if (r < 0) Scm_SysError("clearenv failed");
#else  /*!HAVE_UNSETENV*/
    Scm_Error("sys-clearenv is not supported on this platform.");
#endif /*!HAVE_UNSETENV*/
}

/*===============================================================
 * Closer-to-metal thingy
 */

/* Try to find # of available processors.  If we don't know how to
   find that info on the platform, we fall back to 1.
   If GAUCHE_AVAILABLE_PROCESSORS environment variable is defined and
   has the value interpreted as a positive integer, we use that value
   instead.
*/
int Scm_AvailableProcessors()
{
    const char *env = Scm_GetEnv("GAUCHE_AVAILABLE_PROCESSORS");
    if (env && env[0] != '\0') {
        char *ep;
        long v = strtol(env, &ep, 10);
        if (v > 0 && *ep == '\0') return (int)v;
    }
#if !defined(GAUCHE_WINDOWS)
#if   defined(_SC_NPROCESSORS_ONLN)
    return (int)sysconf(_SC_NPROCESSORS_ONLN);
#else  /*!defined(_SC_NPROCESSORS_ONLN)*/
    return 1;                   /* fallback */
#endif /*!defined(_SC_NPROCESSORS_ONLN)*/
#else  /*defined(GAUCHE_WINDOWS)*/
    SYSTEM_INFO sysinfo;
    GetSystemInfo( &sysinfo );
    return (int)sysinfo.dwNumberOfProcessors;
#endif /*defined(GAUCHE_WINDOWS)*/
}

/*===============================================================
 * Emulation layer for Windows
 */
#if defined(GAUCHE_WINDOWS)

/* Dynamically obtain an entry point that may not be available on
   all Windows versions.  If throw_error is TRUE, throws an error
   if DLL mapping failed, or entry cannot be found.  Otherwise,
   returns NULL on error. */
static void *get_api_entry(const TCHAR *module, const char *proc,
                           int throw_error)
{
    void *entry;
    HMODULE m = LoadLibrary(module);
    if (m == NULL) {
        if (throw_error)
            Scm_SysError("LoadLibrary(%s) failed", SCM_WCS2MBS(module));
        else
            return NULL;
    }
    entry = (void*)GetProcAddress(m, proc);
    if (entry == NULL) {
        DWORD errcode = GetLastError();
        FreeLibrary(m);
        SetLastError(errcode);
        if (throw_error)
            Scm_SysError("GetProcAddress(%s) failed", proc);
        else
            return NULL;
    }
    return entry;
}

/* Scan the processes to find out either the parent process, or the
   child processes of the current process.  I cannot imagine why we
   need such a hassle to perform this kind of simple task, but this
   is the way the MS document suggests.
   Returns a single Scheme integer of the parent process id if childrenp
   is FALSE; returns a list of Scheme integers of child process ids if
   childrenp is TRUE. */
static ScmObj get_relative_processes(int childrenp)
{
    HANDLE snapshot;
    PROCESSENTRY32 entry;
    DWORD myid = GetCurrentProcessId(), parentid = 0;
    int found = FALSE;
    ScmObj h = SCM_NIL, t = SCM_NIL; /* children pids */

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        Scm_Error("couldn't take process snapshot in getppid()");
    }
    entry.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(snapshot, &entry)) {
        CloseHandle(snapshot);
        Scm_Error("Process32First failed in getppid()");
    }
    do {
        if (childrenp) {
            if (entry.th32ParentProcessID == myid) {
                SCM_APPEND1(h, t, Scm_MakeInteger(entry.th32ProcessID));
            }
        } else {
            if (entry.th32ProcessID == myid) {
                parentid = entry.th32ParentProcessID;
                found = TRUE;
                break;
            }
        }
    } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);

    if (childrenp) {
        return h;
    } else {
        if (!found) {
            Scm_Error("couldn't find the current process entry in getppid()");
        }
        return Scm_MakeInteger(parentid);
    }
}

/* Retrieve PID from windows process handle wrapper.  */

pid_t Scm_WinProcessPID(ScmObj handle)
{
    /* GetProcessId seems very primitive procedure, but somehow Windows
       only provides it in XP SP1 or after.  Before that it seems you
       can only map pid -> handle by OpenProcess but you can't do the
       reverse (except you enumerate all process ids, calling OpenProcess
       on each and look for one whose handle matches the given handle.
       Sounds expensive. */
    static DWORD (WINAPI *pGetProcessId)(HANDLE) = NULL;
    static int queried = FALSE;

    if (!Scm_WinProcessP(handle)) {
        SCM_TYPE_ERROR(handle, "<win:handle process>");
    }

    if (pGetProcessId == NULL) {
        if (queried) return (pid_t)-1;
        pGetProcessId = get_api_entry(_T("kernel32.dll"), "GetProcessId",
                                      FALSE);
        if (pGetProcessId == NULL) {
            queried = TRUE;
            return (pid_t)-1;
        }
    }
    return pGetProcessId(Scm_WinProcess(handle));
}

/*
 * Users and groups
 * Kinda Kluge, since we don't have "user id" associated with each
 * user.  (If a domain server is active, Windows security manager seems
 * to assign an unique user id for every user; but it doesn't seem available
 * for stand-alone machine.)
 */

static void convert_user(const USER_INFO_2 *wuser, struct passwd *res)
{
    res->pw_name    = (const char*)SCM_WCS2MBS(wuser->usri2_name);
    res->pw_passwd  = "*";
    res->pw_uid     = 0;
    res->pw_gid     = 0;
    res->pw_comment = (const char*)SCM_WCS2MBS(wuser->usri2_comment);
    res->pw_gecos   = (const char*)SCM_WCS2MBS(wuser->usri2_full_name);
    res->pw_dir     = (const char*)SCM_WCS2MBS(wuser->usri2_home_dir);
    res->pw_shell   = "";
}

/* Arrgh! thread unsafe!  just for the time being...*/
static struct passwd pwbuf = { "dummy", "", 0, 0, "", "", "", "" };

struct passwd *getpwnam(const char *name)
{
    USER_INFO_2 *res;
    if (NetUserGetInfo(NULL, (LPCWSTR)SCM_MBS2WCS(name), 2, (LPBYTE*)&res)
        != NERR_Success) {
        return NULL;
    }
    convert_user(res, &pwbuf);
    NetApiBufferFree(res);
    return &pwbuf;
}

struct passwd *getpwuid(uid_t uid SCM_UNUSED)
{
    /* for the time being, we just ignore uid and returns the current
       user info. */
#define NAMELENGTH 256
    TCHAR buf[NAMELENGTH];
    DWORD len = NAMELENGTH;
    if (GetUserName(buf, &len) == 0) {
        return NULL;
    }
    return getpwnam(SCM_WCS2MBS(buf));
}

static struct group dummy_group = {
    "dummy",
    "",
    100,
    NULL
};

struct group *getgrgid(gid_t gid SCM_UNUSED)
{
    return &dummy_group;
}

struct group *getgrnam(const char *name SCM_UNUSED)
{
    return &dummy_group;
}

/* Kluge kluge kluge */
uid_t getuid(void)
{
    return 0;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

pid_t getppid(void)
{
    ScmObj ppid = get_relative_processes(FALSE);
    return Scm_GetInteger(ppid);
}

const char *getlogin(void)
{
    static TCHAR buf[256]; /* this isn't thread-safe, but getlogin() is
                              inherently thread-unsafe call anyway */
    DWORD size = sizeof(buf)/sizeof(TCHAR);
    BOOL r;
    r = GetUserName(buf, &size);
    if (r) {
        return SCM_WCS2MBS(buf);
    } else {
        return NULL;
    }
}

clock_t times(struct tms *info)
{
    HANDLE process = GetCurrentProcess();
    FILETIME ctime, xtime, utime, stime;
    int64_t val;
    const int factor = 10000000/CLK_TCK;
    const int bias   = factor/2;

    if (!GetProcessTimes(process, &ctime, &xtime, &stime, &utime)) {
        Scm_SysError("GetProcessTimes failed");
    }
    val = ((int64_t)stime.dwHighDateTime << 32) + stime.dwLowDateTime;
    info->tms_stime = (u_int)((val+bias) / factor);
    val = ((int64_t)utime.dwHighDateTime << 32) + utime.dwLowDateTime;
    info->tms_utime = (u_int)((val+bias) / factor);

    info->tms_cstime = 0;
    info->tms_cutime = 0;
    return 0;
}



/*
 * Other obscure stuff
 */

int fork(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return -1;
}

int pipe(int fd[])
{
#define PIPE_BUFFER_SIZE 512
    /* NB: We create pipe with NOINHERIT to avoid complication when spawning
       child process.  Scm_SysExec will dups the handle with inheritable flag
       for the children.  */
    int r = _pipe(fd, PIPE_BUFFER_SIZE, O_BINARY|O_NOINHERIT);
    return r;
}

/* If the given handle points to a pipe, returns its name.
   As of Oct 2016, mingw headers does not include
   GetFileInformationByHandleEx API, so we provide alternative. */

typedef struct {
    DWORD FileNameLength;
    WCHAR FileName[1];
} X_FILE_NAME_INFO;

typedef enum {
    X_FileNameInfo = 2
} X_FILE_INFO_BY_HANDLE_CLASS;

ScmObj Scm_WinGetPipeName(HANDLE h)
{
    if (GetFileType(h) != FILE_TYPE_PIPE) return SCM_FALSE;
    static BOOL (WINAPI *pGetFileInformationByHandleEx)(HANDLE,
                                                        X_FILE_INFO_BY_HANDLE_CLASS,
                                                        LPVOID, DWORD) = NULL;

    if (pGetFileInformationByHandleEx == NULL) {
        pGetFileInformationByHandleEx =
            get_api_entry(_T("kernel32.dll"),
                          "GetFileInformationByHandleEx",
                          FALSE);
    }
    if (pGetFileInformationByHandleEx == NULL) return SCM_FALSE;

    DWORD size = sizeof(X_FILE_NAME_INFO) + sizeof(WCHAR)*MAX_PATH;
    X_FILE_NAME_INFO *info = SCM_MALLOC_ATOMIC(size);
    BOOL r = pGetFileInformationByHandleEx(h, X_FileNameInfo, info, size);
    if (!r) return SCM_FALSE;

    info->FileName[info->FileNameLength / sizeof(WCHAR)] = 0;
    return SCM_MAKE_STR_COPYING(SCM_WCS2MBS(info->FileName));
}

char *ttyname(int desc SCM_UNUSED)
{
    return NULL;
}

#if !HAVE_UTIMENSAT
/* Emulate utimensat() by utime().  For MinGW. */
int utimensat(int dirfd SCM_UNUSED,
              const char *path,
              const ScmTimeSpec times[2],
              int flags SCM_UNUSED)
{
    struct utimbuf buf;
    buf.actime = times[0].tv_sec;
    buf.modtime = times[1].tv_sec;

    if (times[0].tv_nsec == UTIME_NOW) {
        buf.actime = time(NULL);
    }
    if (times[1].tv_nsec == UTIME_NOW) {
        buf.modtime = time(NULL);
    }
    /* TODO: UTIME_OMIT case */

    return utime(path, &buf);
}
#endif /*!HAVE_UTIMENSAT*/

#ifndef __MINGW64_VERSION_MAJOR /* MinGW64 has truncate and ftruncate */

static int win_truncate(HANDLE file, off_t len)
{
    typedef BOOL (WINAPI *pSetEndOfFile_t)(HANDLE);
    typedef BOOL (WINAPI *pSetFilePointer_t)(HANDLE, LONG, PLONG, DWORD);

    static pSetEndOfFile_t pSetEndOfFile = NULL;
    static pSetFilePointer_t pSetFilePointer = NULL;

    DWORD r1;
    BOOL  r2;

    if (pSetEndOfFile == NULL) {
        pSetEndOfFile = (pSetEndOfFile_t)get_api_entry(_T("kernel32.dll"),
                                                       "SetEndOfFile",
                                                       FALSE);
        if (pSetEndOfFile == NULL) return -1;
    }
    if (pSetFilePointer == NULL) {
        pSetFilePointer = (pSetFilePointer_t)get_api_entry(_T("kernel32.dll"),
                                                           "SetFilePointer",
                                                           FALSE);
        if (pSetFilePointer == NULL) return -1;
    }

    /* TODO: 64bit size support! */
    r1 = pSetFilePointer(file, (LONG)len, NULL, FILE_BEGIN);
    if (r1 == INVALID_SET_FILE_POINTER) return -1;
    r2 = pSetEndOfFile(file);
    if (r2 == 0) return -1;
    return 0;
}

int truncate(const char *path, off_t len)
{
    HANDLE file;
    int r;

    file = CreateFile(SCM_MBS2WCS(path), GENERIC_WRITE,
                      FILE_SHARE_READ|FILE_SHARE_WRITE,
                      NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return -1;
    r = win_truncate(file, len);
    if (r < 0) {
        DWORD errcode = GetLastError();
        CloseHandle(file);
        SetLastError(errcode);
        return -1;
    }
    CloseHandle(file);
    return 0;
}

int ftruncate(int fd, off_t len)
{
    HANDLE h = (HANDLE)_get_osfhandle(fd);
    int r;
    if (h == INVALID_HANDLE_VALUE) return -1;
    r = win_truncate(h, len);
    if (r < 0) return -1;
    return 0;
}

#endif /* __MINGW64_VERSION_MAJOR */

unsigned int alarm(unsigned int seconds SCM_UNUSED)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    Scm_SysError("alarm");
    return 0;
}

/* file links */
int link(const char *existing, const char *newpath)
{
    /* CreateHardLink only exists in WinNT or later.  Officially we don't
       support anything before, but let's try to be kind for the legacy
       system ...*/
    typedef BOOL (WINAPI *pCreateHardLink_t)(LPTSTR, LPTSTR,
                                             LPSECURITY_ATTRIBUTES);
    static pCreateHardLink_t pCreateHardLink = NULL;
    BOOL r;
#if defined(UNICODE)
#define CREATEHARDLINK  "CreateHardLinkW"
#else
#define CREATEHARDLINK  "CreateHardLinkA"
#endif

    if (pCreateHardLink == NULL) {
        pCreateHardLink = (pCreateHardLink_t)get_api_entry(_T("kernel32.dll"),
                                                           CREATEHARDLINK,
                                                           TRUE);
    }
    r = pCreateHardLink((LPTSTR)SCM_MBS2WCS(newpath),
                        (LPTSTR)SCM_MBS2WCS(existing), NULL);
    return r? 0 : -1;
}

/* Winsock requires some obscure initialization.
   We perform initialization here, since winsock module is used
   in both gauche.net and gauche.auxsys. */
static WSADATA wsaData;

static void init_winsock(void)
{
    int opt;
    int r = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (r != 0) {
        SetLastError(r);
        Scm_SysError("WSAStartup failed");
    }
    /* windows voodoo to make _open_osfhandle magic work */
    opt = SO_SYNCHRONOUS_NONALERT;
    r = setsockopt(INVALID_SOCKET, SOL_SOCKET,
                   SO_OPENTYPE, (char*)&opt, sizeof(opt));
    if (r == SOCKET_ERROR) {
        Scm_SysError("winsock initialization failed");
    }
}

static void fini_winsock(void *data SCM_UNUSED)
{
    (void)WSACleanup();
}

/* Win32 thread support.  See also gauche/wthread.h */

#if defined(GAUCHE_USE_WTHREADS)

HANDLE Scm__WinCreateMutex()
{
    HANDLE m = CreateMutex(NULL, FALSE, NULL);
    if (m == NULL) Scm_SysError("couldn't create a mutex");
    return m;
}

int Scm__WinMutexLock(HANDLE mutex)
{
    DWORD r = WaitForSingleObject(mutex, INFINITE);
    if (r == WAIT_OBJECT_0) return 0;
    else return 1;              /* TODO: proper error handling */
}

/* Windows fast lock */

int Scm__WinFastLockInit(ScmInternalFastlock *spin)
{
    *spin = SCM_NEW(struct win_spinlock_rec);
    Scm_AtomicStore(&(*spin)->lock_state, 0);
    return 0;
}

int Scm__WinFastLockLock(ScmInternalFastlock spin)
{
    /* spin may be NULL when FASTLOCK_LOCK is called on already-closed port. */
    if (spin != NULL) {
        ScmAtomicWord idle = 0;
        while (!Scm_AtomicCompareExchange(&spin->lock_state, &idle, 1)) {
            /* idle might be changed */
            idle = 0;
            /* it might be slow */
            Sleep(0);
        }
    }
    return 0;
}

int Scm__WinFastLockUnlock(ScmInternalFastlock spin)
{
    /* spin may be NULL when FASTLOCK_LOCK is called on already-closed port. */
    if (spin != NULL) {
        Scm_AtomicStoreFull(&spin->lock_state, 0);
    }
    return 0;
}

int Scm__WinFastLockDestroy(ScmInternalFastlock *spin)
{
    *spin = NULL;
    return 0;
}

/* Win32 conditional variable emulation.
   Native condition variable support is only available on Windows Vista
   and later.  We don't want to drop XP support (yet), so we avoid using
   it.  Instead we emulate posix condition variable semantics.
   We enhanced the implementation described as the SignalObjectAndWait
   solution shown in
   <http://www1.cse.wustl.edu/~schmidt/win32-cv-1.html>
 */
void Scm__InternalCondInit(ScmInternalCond *cond)
{
    cond->numWaiters = 0;
    cond->broadcast = FALSE;
    cond->mutex = NULL;         /* set by the first CondWait */
    cond->sem = CreateSemaphore(NULL,          /* no security */
                                0, 0x7fffffff, /* initial and max val */
                                NULL);         /* name */
    if (cond->sem == NULL) {
        Scm_SysError("couldn't create a semaphore for a condition variable");
    }
    cond->done = CreateEvent(NULL,  /* no security */
                             FALSE, /* auto-reset */
                             FALSE, /* initially non-signalled */
                             NULL); /* name */
    if (cond->done == NULL) {
        DWORD err = GetLastError();
        CloseHandle(cond->sem);
        SetLastError(err);
        Scm_SysError("couldn't create event for a condition variable");
    }
    InitializeCriticalSection(&cond->numWaitersLock);
}

int Scm__InternalCondWait(ScmInternalCond *cond, ScmInternalMutex *mutex,
                          ScmTimeSpec *pts)
{
    DWORD r0, r1;
    DWORD timeout_msec;
    int badMutex = FALSE, lastWaiter;

    if (pts) {
        u_long now_sec, now_usec;
        u_long target_sec, target_usec;
        Scm_GetTimeOfDay(&now_sec, &now_usec);
        target_sec = pts->tv_sec;
        target_usec = pts->tv_nsec / 1000;
        if (target_sec < now_sec
            || (target_sec == now_sec && target_usec <= now_usec)) {
            timeout_msec = 0;
        } else if (target_usec >= now_usec) {
            timeout_msec = ceil((target_sec - now_sec) * 1000
                                + (target_usec - now_usec)/1000.0);
        } else {
            timeout_msec = ceil((target_sec - now_sec - 1) * 1000
                                + (1.0e6 + target_usec - now_usec)/1000.0);
        }
    } else {
        timeout_msec = INFINITE;
    }

    EnterCriticalSection(&cond->numWaitersLock);
    /* If we're the first one to wait on this cond var, set cond->mutex.
       We don't allow to use multiple mutexes together with single cond var.
     */
    if (cond->mutex != NULL && cond->mutex != mutex) {
        badMutex = TRUE;
    } else {
        cond->numWaiters++;
        if (cond->mutex == NULL) cond->mutex = mutex;
    }
    LeaveCriticalSection(&cond->numWaitersLock);

    if (badMutex) {
        Scm_Error("Attempt to wait on condition variable %p with different"
                  " mutex %p\n", cond, mutex);
    }

    /* Signals mutex and atomically waits on the semaphore */
    r0 = SignalObjectAndWait(*mutex, cond->sem, timeout_msec, FALSE);

    /* We're signaled, or timed out.   There can be a case that cond is
       broadcasted between the timeout of SignalObjectAndWait and the
       following EnterCriticalSection.  So we should check lastWaiter
       anyway. */
    EnterCriticalSection(&cond->numWaitersLock);
    cond->numWaiters--;
    lastWaiter = cond->broadcast && cond->numWaiters == 0;
    LeaveCriticalSection(&cond->numWaitersLock);

    if (lastWaiter) {
        /* tell the broadcaster that all the waiters have gained
           control, and wait to acquire mutex. */
        r1 = SignalObjectAndWait(cond->done, *mutex, INFINITE, FALSE);
    } else {
        /* Acquire mutex */
        r1 = WaitForSingleObject(*mutex, INFINITE);
    }
    if (r0 == WAIT_TIMEOUT) return SCM_INTERNAL_COND_TIMEDOUT;
    if (r0 != WAIT_OBJECT_0 || r1 != WAIT_OBJECT_0) return -1;
    return 0;
}

int Scm__InternalCondSignal(ScmInternalCond *cond)
{
    int haveWaiters;
    BOOL r = TRUE;

    if (!cond->mutex) return 0; /* nobody ever waited on this cond var. */

    SCM_INTERNAL_MUTEX_SAFE_LOCK_BEGIN(cond->mutex);

    EnterCriticalSection(&cond->numWaitersLock);
    haveWaiters = (cond->numWaiters > 0);
    LeaveCriticalSection(&cond->numWaitersLock);

    if (haveWaiters) {
        r = ReleaseSemaphore(cond->sem, 1, 0);
    }

    SCM_INTERNAL_MUTEX_SAFE_LOCK_END();
    if (!r) return -1;
    return 0;
}

int Scm__InternalCondBroadcast(ScmInternalCond *cond)
{
    int haveWaiters;
    DWORD err = 0;
    BOOL r0 = TRUE;
    DWORD r1 = WAIT_OBJECT_0;

    if (!cond->mutex) return 0; /* nobody ever waited on this cond var. */

    SCM_INTERNAL_MUTEX_SAFE_LOCK_BEGIN(cond->mutex);

    EnterCriticalSection(&cond->numWaitersLock);
    cond->broadcast = haveWaiters = (cond->numWaiters > 0);

    if (haveWaiters) {
        r0 = ReleaseSemaphore(cond->sem, cond->numWaiters, 0);
        if (!r0) err = GetLastError();
        LeaveCriticalSection(&cond->numWaitersLock);

        if (r0) {
            /* Each waiter aquires mutex in turn, until the last waiter
               who will signal on 'done'. */
            r1 = WaitForSingleObject(cond->done, INFINITE);
            cond->broadcast = FALSE; /* safe; nobody will check this */
        }
    } else {
        /* nobody's waiting */
        LeaveCriticalSection(&cond->numWaitersLock);
    }

    SCM_INTERNAL_MUTEX_SAFE_LOCK_END();

    if (!r0) { SetLastError(err); return -1; }
    if (r1 != WAIT_OBJECT_0) return -1;
    return 0;
}

void Scm__InternalCondDestroy(ScmInternalCond *cond)
{
    CloseHandle(cond->sem);
    cond->sem = NULL;
    CloseHandle(cond->done);
    cond->done = NULL;
}

void Scm__WinThreadExit()
{
    ScmVM *vm = Scm_VM();
    ScmWinCleanup *cup = vm->winCleanup;
    while (cup) {
        cup->cleanup(cup->data);
        cup = cup->prev;
    }
    GC_ExitThread(0);
}

#endif /* GAUCHE_USE_WTHREADS */

#endif /* GAUCHE_WINDOWS */

/*===============================================================
 * Initialization
 */
void Scm__InitSystem(void)
{
    ScmModule *mod = Scm_GaucheModule();
    Scm_InitStaticClass(&Scm_SysStatClass, "<sys-stat>", mod, stat_slots, 0);
    Scm_InitStaticClass(&Scm_TimeClass, "<time>", mod, time_slots, 0);
    Scm_InitStaticClass(&Scm_SysGroupClass, "<sys-group>", mod, grp_slots, 0);
    Scm_InitStaticClass(&Scm_SysPasswdClass, "<sys-passwd>", mod, pwd_slots, 0);
#ifdef HAVE_SELECT
    Scm_InitStaticClass(&Scm_SysFdsetClass, "<sys-fdset>", mod, NULL, 0);
#endif
    SCM_INTERNAL_MUTEX_INIT(env_mutex);
    Scm_HashCoreInitSimple(&env_strings, SCM_HASH_STRING, 0, NULL);

    key_absolute = SCM_MAKE_KEYWORD("absolute");
    key_expand = SCM_MAKE_KEYWORD("expand");
    key_canonicalize = SCM_MAKE_KEYWORD("canonicalize");

    initial_ugid_differ = (geteuid() != getuid() || getegid() != getgid());

#ifdef GAUCHE_WINDOWS
    init_winsock();
    SCM_INTERNAL_MUTEX_INIT(process_mgr.mutex);
    Scm_AddCleanupHandler(fini_winsock, NULL);
    Scm_AddCleanupHandler(win_process_cleanup, NULL);
#endif
}
