;;;
;;;  gauche.test.generative - Generative tests
;;;
;;;   Copyright (c) 2013-2022  Shiro Kawai  <shiro@acm.org>
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

;; Generative tests, inspired by Haskell's QuickCheck and Clojure's
;; test.generative.

;; TODO: Use multiple cores when available

(define-module gauche.test.generative
  (use gauche.test)
  (use gauche.generator)
  (use data.random)
  (export check check-seed check-amount make-checker ensure)
  )
(select-module gauche.test.generative)

;; Configurable parameters
(define check-seed   (make-parameter 42))
(define check-amount (make-parameter 100))

;; `Check' is syntactically like `let'.
;;
;;    (check ([var gen-expr] ...)
;;       body ...)
;;
;;  where VAR is a variable, and GEN-EXPR is an expression that yields
;;  a generator.  This macro simply evaluates BODY ... many times
;;  (determined by check-count parameter) while binding VARs to the
;;  values generated by GEN-EXPR each time.   If nothings happens,
;;  `check' macro returns #t.
;;
;;  Within BODY..., you can use ensure macro:
;;
;;    (ensure EXPECTED EXPR :optional (COMPARE test-check))
;;
;;  The argument of ensure is just like the arguments to test* macro,
;;  except that ensure doesn't take msg argument.
;;
;;  The ensure macro evaluates EXPR, and compares its result with EXPECTED,
;;  using COMPARE.  If it doesn't match, CHECK macro stops the execution
;;  and returns the following datum:
;;
;;    ("ensure failed on EXPR: expects EXPECTED, got <actual-value>"
;;     :count <the-current-count>
;;     :seed <the-seed-used>)
;;
;;  More key-value data may be added in the future versions, but it is
;;  always (<string> . kv-list).
;;
;;  You can use check and ensure combined with test*
;;
;;  (test* "check-it" #t
;;         (check ([a fixnum] [b fixnum])
;;           (ensure (+ a b) (my-own-add-function a b))
;;           ...))
;;
;;  Another way is to create a checker procedure by `make-checker'.
;;
;;  (define check-my-own-add-function
;;    (make-checker ([a fixnum] [b fixnum])
;;      (ensure (+ a b) (my-own-add-function a b))
;;      ...)))
;;
;;  `Make-checker' returns a procedure that does what `check' does when invoked.
;;  The checker procedure created by make-checker accepts the following
;;  keyword arguments:
;;
;;    seed    - random seed to be used for data generation.  useful to
;;              reproduce a failure case
;;    amount  - override the default check amount
;;    skip-to - don't execute body for SKIP-TO times (just generating
;;              data), then start checking.  also useful to reproduce
;;              a failure case.
;;
;;  When skip-to is given and amount isn't given, we just run the body
;;  once.

(define-condition-type <check-failure> <condition> #f
  (expr)      ; Original expression
  (expected)  ; Expected result
  (actual)    ; Actual result (can be a #<test-error>)
  (var-alist) ; Assoc-list of vars name and generated data
  (seed)      ; Current random seed
  (count)     ; Current count
  )

;; API
(define-syntax make-checker
  (syntax-rules ()
    [(_ ([var gen] ...) body ...)
     (^[:key [seed (check-seed)]
             [skip-to 0]
             [amount (if (zero? skip-to) (check-amount) 1)]]
       (run-checker (^[var ...] body ...)
                    '(var ...) (list gen ...)
                    seed amount skip-to))]))

;; API
(define-syntax check
  (syntax-rules ()
    [(_ ([var gen] ...) body ...)
     ((make-checker ([var gen] ...) body ...))]))

;; API
(define-syntax ensure
  (syntax-rules ()
    [(_ expected expr . args)
     (apply %ensure 'expr expected (^[] expr) args)]))

;; aux proc

(define %current-vars  (make-parameter '()))
(define %current-data  (make-parameter '()))
(define %current-count (make-parameter 0))

(define (run-checker proc vars gens seed amount skip-to)
  (parameterize ([%current-vars vars])
    (guard (e [(<check-failure> e) (format-check-failure e)])
      (with-random-data-seed seed
        (^[]
          (let1 c (+ skip-to amount)
            (dotimes [n c]
              (let1 data (map (^g (g)) gens)
                (when (>= n skip-to)
                  (parameterize ([%current-data data]
                                 [%current-count n])
                    (apply proc data)))))
            (format #t "Passes ~a check~:p.\n" c)
            #t))))))

(define (%ensure qexpr expected thunk :optional (cmp test-check))
  (define (throw-failure result)
    (error <check-failure>
           :expr qexpr :expected expected :actual result
           :var-alist (map cons (%current-vars) (%current-data))
           :seed (check-seed) :count (%current-count)))
  (let1 result
      (guard (e [else (throw-failure (test-error
                                      (class-of e)
                                      (condition-message e)))])
        (thunk))
    (unless (cmp expected result)
      (throw-failure result))))

(define (format-check-failure e)
  (let1 msg (format "Ensure failed on ~s: expects ~s, got ~s"
                    (~ e 'expr) (~ e 'expected) (~ e 'actual))
    (format #t "~a\nat count=~s with seed=~s.\n" msg
            (~ e 'count) (~ e 'seed))
    `(,msg
      :count ,(~ e 'count)
      :seed ,(~ e 'seed)
      :vars ,(~ e 'var-alist))))
