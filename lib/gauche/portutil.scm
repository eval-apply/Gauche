;;;
;;; port related utility functions.  to be autoloaded.
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

(define-module gauche.portutil
  (export copy-port))
(select-module gauche.portutil)

;;-----------------------------------------------------
;; copy-port
;;  This is autoladed becuse use may depend on gauche.uvector

;; only load gauche.uvector if we use chunked copy
(autoload gauche.uvector make-u8vector read-block! write-block)

(define-macro (%do-copy reader writer incr)
  `(with-port-locking src
     (^[]
       (with-port-locking dst
         (^[]
           (let loop ([data  ,reader]
                      [count 0])
             (if (eof-object? data)
               count
               (begin ,writer
                      (loop ,reader ,incr)))))))))

(define-macro (%do-copy/limit1 reader writer limit)
  `(with-port-locking src
     (^[]
       (with-port-locking dst
         (^[]
           (let loop ((count 0))
             (if (>= count ,limit)
               count
               (let ((data ,reader))
                 (if (eof-object? data)
                   count
                   (begin ,writer
                          (loop (+ count 1))))))))))))

(define (%do-copy/limitN src dst buf unit limit)
  (with-port-locking src
    (^[]
      (with-port-locking dst
        (^[]
          (let loop ((count 0))
            (if (>= count limit)
              count
              (let1 nr (read-block! buf src 0
                                    (if (>= (+ count unit) limit)
                                      (- limit count)
                                      unit))
                (if (eof-object? nr)
                  count
                  (begin (write-block buf dst 0 nr)
                         (loop (+ count nr))))))))))))

(define (copy-port src dst :key (unit 4096) (size -1))
  (check-arg input-port? src)
  (check-arg output-port? dst)
  (cond [(eq? unit 'byte)
         (if (and (integer? size) (not (negative? size)))
           (%do-copy/limit1 (read-byte src) (write-byte data dst) size)
           (%do-copy (read-byte src) (write-byte data dst) (+ count 1)))]
        [(eq? unit 'char)
         (if (and (integer? size) (not (negative? size)))
           (%do-copy/limit1 (read-char src) (write-char data dst) size)
           (%do-copy (read-char src) (write-char data dst) (+ count 1)))]
        [(integer? unit)
         (let ((buf (make-u8vector (if (zero? unit) 4096 unit))))
           (if (and (integer? size) (not (negative? size)))
             (%do-copy/limitN src dst buf unit size)
             (%do-copy (read-block! buf src) (write-block buf dst 0 data)
                       (+ count data))))]
        [else (error "unit must be 'char, 'byte, or non-negative integer" unit)]
        ))
