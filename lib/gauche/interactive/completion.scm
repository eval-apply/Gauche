;;;
;;; gauche.interactive.completion
;;;
;;;   Copyright (c) 2021-2022  Shiro Kawai  <shiro@acm.org>
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

;; This module is autoloaded from gauche.interactive.editable-reader.

(define-module gauche.interactive.completion
  (use srfi-13)
  (use text.gap-buffer)
  (export list-completions))
(select-module gauche.interactive.completion)

;; Completion (EXPERIMENTAL)
;; Some questions to consider
;;   - Should we build a trie for quick access to prefix-matching symbols?
;;     If we do so, how to keep it updated?
;;   - Do we want to complete w-i-f-f to with-input-from-file?
;;   - We need to access runtime 'current-module' info, which currently isn't
;;     a public API.  Should we have an official API for that?
;;   - The routine is pretty similar to apropos.  Should we refactor?
(define (list-completions word gbuf start end)
  (%complete-symbol word))

(define (%complete-symbol word)
  (let ([mod ((with-module gauche.internal vm-current-module))]
        [visited '()]
        [hits (make-hash-table 'string=?)])
    (define (search m)
      (unless (memq m visited)
        (push! visited m)
        ($ hash-table-for-each (module-table m)
           (^[sym _]
             (let1 s (symbol->string sym)
               (when (string-prefix? word s)
                 (hash-table-put! hits s #t)))))))
    (search mod)
    (dolist [m (module-imports mod)]
      (for-each search (module-precedence-list m)))
    (sort (hash-table-keys hits))))
               
