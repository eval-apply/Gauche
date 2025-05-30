;;;
;;; sysutil - Auxiliary system interface, autoloaded
;;;
;;;   Copyright (c) 2000-2025  Shiro Kawai  <shiro@acm.org>
;;;
;;;   Redistribution and use in source and binary forms, with or without
;;;   modification, are permitted provided that the following conditions
;;;   are met:
;;;
;;;   1. Redistributions of source code must retain the above copyright
;;;      notice, this list of conditions and the following disclaimer.
;;;
;;;   2. Redistributions in binary form must reproduce the above copyright
;;;      notice, this list of conditions and the following disclaimer in the
;;;      documentation and/or other materials provided with the distribution.
;;;
;;;   3. Neither the name of the authors nor the names of its contributors
;;;      may be used to endorse or promote products derived from this
;;;      software without specific prior written permission.
;;;
;;;   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
;;;   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
;;;   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
;;;   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
;;;   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
;;;   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
;;;   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
;;;   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
;;;   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
;;;   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
;;;   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
;;;

(select-module gauche)

;; These are spilled out from src/libsys.scm, because we need
;; to expand cond-expand form on the running machine.
;; Src/libsys.scm is pre-compiled at the time of distribution,
;; so we cannot have platform-dependent cond-expand in it.

(cond-expand
 [gauche.sys.select
  (define (sys-fdset . pfs)
    (list->sys-fdset pfs))
  (define (sys-fdset->list fdset)
    (assume-type fdset <sys-fdset>)
    (do ([i (sys-fdset-max-fd fdset) (- i 1)]
         [fds '() (if (sys-fdset-ref fdset i) (cons i fds) fds)])
        [(< i 0) fds]
      #f))
  (define (list->sys-fdset pfs)
    (rlet1 fdset (make <sys-fdset>)
      (dolist (pf pfs)
        (cond [(or (integer? pf) (port? pf)) (sys-fdset-set! fdset pf #t)]
              [(is-a? pf <sys-fdset>)
               (dotimes (i (+ (sys-fdset-max-fd pf) 1))
                 (when (sys-fdset-ref pf i) (sys-fdset-set! fdset i #t)))]
              [else (error "sys-fdset requires a port, an integer, \
                           or a <sys-fdset> object, but got:" pf)]))))
  ]
 [else
  ;; make autoload happy
  (define sys-fdset #f)
  (define sys-fdset->list #f)
  (define list->sys-fdset #f)
  ])

;; Realpath implementation.
;; POSIX realpath(3) is flawed in a sense that there's no way to get
;; the reasonable and safe buffer size (PATH_MAX can return very large
;; number; see the manpage for the details).  So we implement it in
;; Scheme, making it portable and safe.
;; NB: we can't use utilities in file.util to avoid dependency hell.
(cond-expand
 [gauche.os.windows
  (define (sys-realpath path)
    (rlet1 p (sys-normalize-pathname path :absolute #t :canonicalize #t)
      (sys-stat p)))] ; check if the path exists
 [else
  (define (sys-realpath path)
    (define count 0)
    (define (loop-check!)
      (inc! count)
      (when (> count 100)
        (error "possible cycle in resolving symlinks for path:" path)))
    (define (decompose path) (string-split path "/"))
    (define (absolute? path)
      (and (>= (string-length path) 1) (eqv? (string-ref path 0) #\/)))
    (define (path-concat path)
      (string-append "/" (string-join (reverse path) "/")))
    (define (resolve path comps)
      (cond [(null? comps) (path-concat path)] ; we know path exists
            [(member (car comps) '("" "."))(resolve path (cdr comps))]
            [(string=? (car comps) "..")
             (resolve (if (pair? path) (cdr path) path) (cdr comps))]
            [else
             (let* ((q (cons (car comps) path))
                    (p (path-concat q))
                    (s (sys-lstat p)))    ; may raise ENOENT
               (cond [(eq? (slot-ref s'type) 'symlink)
                      (loop-check!)
                      (let1 p1 (sys-readlink p)
                        (resolve (if (absolute? p1) '() path)
                                 (append! (decompose p1) (cdr comps))))]
                     [(or (null? (cdr comps))
                          (eq? (slot-ref s'type) 'directory))
                      (resolve q (cdr comps))]
                     [else (error "not a directory" p)]))]))

    (resolve '() (append! (if (absolute? path) '() (decompose (sys-getcwd)))
                          (decompose path))))])

;; Find a file that satisfies a predicate in given list of paths.
;; This is a generalization of what execvp does to search executable from PATH.
;; This used to be file.util#find-file-in-paths.  We moved it here so that
;; sys-exec can rely on it, as we switch to execve(2) from execvp(3).
;;
;; The default value handling of pred and extensions are a bit involved.
;; The default of pred is to check the file is executable, and the default of
;; extensions is '().  However, on Windows, if both are omitted, we default
;; extensions to be '("exe" "com" "cmd" "bad"), to mimic the behaivor of
;; execvp().
(define (sys-find-file name
                       :key (paths (cond [(sys-getenv "PATH")
                                          => (cut string-split <>
                                                  (cond-expand
                                                   [gauche.os.windows #\;]
                                                   [else #\:]))]
                                         [else '()]))
                            pred
                            extensions)
  (define real-predicate
    (if (undefined? pred)
      (cute sys-access <> X_OK)
      pred))
  (define real-extensions
    (if (undefined? extensions)
      (cond-expand
       [gauche.os.windows (if (undefined? pred)
                            '("exe" "com" "cmd" "bat")
                            '())]
       [else '()])
      (assume-type extensions (<List> <string>)
                   "List of strings expected, but got:" extensions)))
  (define names
    (if (null? real-extensions)
      `(,name)
      (cons name
            (map (^e (string-append name "." e)) real-extensions))))
  (define (try n) (and (real-predicate n) n))
  (define (abspath? path)               ;dupe of file.util#absolute-path?
    (cond-expand
     [gauche.os.windows (#/^[\/\\]|^[A-Za-z]:/ path)]
     [else              (#/^\// path)]))
  (define (mkpath dir file)             ;dupe of file.util#build-path
    (string-append (sys-normalize-pathname dir)
                   (cond-expand
                    [gauche.os.windows "\\"]
                    [else              "/"])
                   file))

  (if (abspath? name)
    (any try names)
    (and-let1 found
        (let loop ((paths paths))
          (and (not (null? paths))
               (or (any try (map (cute mkpath (car paths) <>) names))
                   (loop (cdr paths)))))
      (sys-normalize-pathname found :absolute #t))))
