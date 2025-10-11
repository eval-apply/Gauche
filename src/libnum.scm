;;;
;;; libnum.stub - builtin number libraries
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

(select-module gauche.internal)

(inline-stub
 (.include "gauche/priv/configP.h"
           "gauche/vminsn.h"
           "gauche/priv/bignumP.h"
           "gauche/priv/writerP.h"
           <stdlib.h>
           <float.h>
           <math.h>)
 (.when (not (defined M_PI))
   (.define M_PI 3.1415926535897932384)))

;;
;; Predicates
;;

(select-module scheme)
(define-cproc number? (obj)  ::<boolean> :fast-flonum :constant
  (inliner NUMBERP) SCM_NUMBERP)
(define-cproc complex? (obj) ::<boolean> :fast-flonum :constant
  (inliner NUMBERP) SCM_NUMBERP)
(define-cproc real? (obj)    ::<boolean> :fast-flonum :constant
  (inliner REALP) SCM_REALP)
(define-cproc rational? (obj)::<boolean> :fast-flonum :constant
  (return (and (SCM_REALP obj) (Scm_FiniteP obj))))
(define-cproc integer? (obj) ::<boolean> :fast-flonum :constant
  (return (and (SCM_NUMBERP obj) (Scm_IntegerP obj))))

(define-cproc exact? (obj)   ::<boolean> :fast-flonum :constant SCM_EXACTP)
(define-cproc inexact? (obj) ::<boolean> :fast-flonum :constant SCM_INEXACTP)

(define-cproc zero? (obj::<number>) ::<boolean> :fast-flonum :constant
  (return (and (SCM_REALP obj) (== (Scm_Sign obj) 0))))

(define-cproc positive? (obj) ::<boolean> :fast-flonum :constant
  (return (> (Scm_Sign obj) 0)))
(define-cproc negative? (obj) ::<boolean> :fast-flonum :constant
  (return (and (not (Scm_NanP obj)) (< (Scm_Sign obj) 0))))
(define-cproc odd? (obj)  ::<boolean> :fast-flonum :constant Scm_OddP)
(define-cproc even? (obj) ::<boolean> :fast-flonum :constant
  (return (not (Scm_OddP obj))))

(select-module gauche)
;; fixnum? and bignum? is not :constant, since it is platform-dependent.
(define-cproc fixnum? (x) ::<boolean> :fast-flonum SCM_INTP)
(define-cproc bignum? (x) ::<boolean> :fast-flonum SCM_BIGNUMP)
(define-cproc flonum? (x) ::<boolean> :fast-flonum :constant SCM_FLONUMP)
(define-cproc ratnum? (x) ::<boolean> :fast-flonum :constant SCM_RATNUMP)

(define-cproc finite?   (x::<number>) ::<boolean> :fast-flonum Scm_FiniteP)
(define-cproc infinite? (x::<number>) ::<boolean> :fast-flonum Scm_InfiniteP)
(define-cproc nan?      (x::<number>) ::<boolean> :fast-flonum Scm_NanP)
(define-cproc negative-zero? (x::<number>) ::<boolean> :fast-flonum :constant
  (return (and (SCM_FLONUMP x)          ;only flonums have -0.0
               (== (SCM_FLONUM_VALUE x) 0.0)
               (!= (signbit (SCM_FLONUM_VALUE x)) 0))))

(define-cproc exact-integer? (obj) ::<boolean> :fast-flonum :constant
  SCM_INTEGERP)

;;
;; Platform introspection
;; TODO: These are not :constant for now, because of cross-compilation.
;; We may need the means to provide target-specific constants during
;; cross compilation.

(select-module gauche)
;; Names are from R6RS.
(define-cproc fixnum-width ()    ::<int> (return (+ SCM_SMALL_INT_SIZE 1)))
(define-cproc least-fixnum ()    ::<long> (return SCM_SMALL_INT_MIN))
(define-cproc greatest-fixnum () ::<long> (return SCM_SMALL_INT_MAX))

(inline-stub
 (initcode
  ;; default-endian is defined in number.c.  This call ensures it is
  ;; initialized.
  (Scm_DefaultEndian)))

(define-cproc native-endian () Scm_NativeEndian)

;; DBL_EPSILON, etc.
(define-cproc flonum-epsilon ()
  (let* ([x::(static ScmObj) SCM_UNBOUND])
    (when (== x SCM_UNBOUND)
      (set! x (Scm_MakeFlonum DBL_EPSILON)))
    (return x)))
(define-cproc least-positive-normalized-flonum ()
  (let* ([x::(static ScmObj) SCM_UNBOUND])
    (when (== x SCM_UNBOUND)
      (set! x (Scm_MakeFlonum DBL_MIN)))
    (return x)))
(define-cproc least-positive-flonum ()
  (let* ([x::(static ScmObj) SCM_UNBOUND])
    (when (== x SCM_UNBOUND)
      (let* ([m::double (Scm_EncodeFlonum (SCM_MAKE_INT 1) -1074 1)])
        (if (== m 0.0) ; architecture doesn't support denormalized number
          (set! x (Scm_MakeFlonum DBL_MIN))
          (set! x (Scm_MakeFlonum m)))))
    (return x)))
(define-cproc greatest-positive-flonum ()
  (return (Scm_MakeFlonum DBL_MAX)))

;; For backward compatibility
(define flonum-min-normalized least-positive-normalized-flonum)
(define flonum-min-denormalized least-positive-flonum)

(select-module gauche.internal)
(define-cproc %bignum-dump (obj) ::<void>
  (when (SCM_BIGNUMP obj)
    (Scm_BignumDump (SCM_BIGNUM obj) SCM_CUROUT)))

;;
;; Comparison
;;

(select-module scheme)
(inline-stub
 ;; NB: numeric procedures =, <, <=, >, >=, +, -, * and / have inliners
 ;; defined in compile.scm.   When one of these operators appears at the
 ;; operator position of an expression, it is inlined so that the following
 ;; SUBRs won't be called.  N-ary arithmetic operations (N>2) are expanded
 ;; to a series of binary arithmetic operations in the compiler.
 ;; N-ary comparison operations (N>2) are NOT expanded by the compiler,
 ;; and these SUBRs are called.  In order to avoid extra consing for those
 ;; N-ary comparison expressions, we use :optarray to receive the first few
 ;; arguments on stack.
 ;;
 ;; SUBRs are always called when those numeric procedures are invoked via
 ;; apply, or as a result of expression in the operator position,
 ;; such as ((if x + *) 2 3).

 (define-cise-stmt numcmp
   [(_ compar)
    `(begin
       (cond [(not (,compar arg0 arg1)) (return FALSE)]
             [(== optcnt 0) (return TRUE)]
             [(not (,compar arg1 (aref oarg 0))) (return FALSE)]
             [(== optcnt 1) (return TRUE)]
             [(not (,compar (aref oarg 0) (aref oarg 1))) (return FALSE)]
             [(and (== optcnt 2) (SCM_NULLP args)) (return TRUE)]
             [else
              (set! arg0 (aref oarg 1)
                    arg1 (SCM_CAR args)
                    args (SCM_CDR args))
              (loop (cond [(not (,compar arg0 arg1)) (return FALSE)]
                          [(SCM_NULLP args) (return TRUE)]
                          [else (set! arg0 arg1
                                      arg1 (SCM_CAR args)
                                      args (SCM_CDR args))]))]))])
 )

(define-cproc =  (arg0 arg1 :optarray (oarg optcnt 2) :rest args)
  ::<boolean> :fast-flonum :constant (numcmp Scm_NumEq))
(define-cproc <  (arg0 arg1 :optarray (oarg optcnt 2) :rest args)
  ::<boolean> :fast-flonum :constant (numcmp Scm_NumLT))
(define-cproc <= (arg0 arg1 :optarray (oarg optcnt 2) :rest args)
  ::<boolean> :fast-flonum :constant (numcmp Scm_NumLE))
(define-cproc >  (arg0 arg1 :optarray (oarg optcnt 2) :rest args)
  ::<boolean> :fast-flonum :constant (numcmp Scm_NumGT))
(define-cproc >= (arg0 arg1 :optarray (oarg optcnt 2) :rest args)
  ::<boolean> :fast-flonum :constant (numcmp Scm_NumGE))

(define-cproc max (arg0 :rest args) ::<number> :constant
  (Scm_MinMax arg0 args NULL (& SCM_RESULT)))
(define-cproc min (arg0 :rest args) ::<number> :constant
  (Scm_MinMax arg0 args (& SCM_RESULT) NULL))

(select-module gauche)
(define-cproc min&max (arg0 :rest args) ::(<top> <top>)
  (Scm_MinMax arg0 args (& SCM_RESULT0) (& SCM_RESULT1)))

;;
;; Conversions
;;

(select-module scheme)
(define-cproc exact->inexact (obj) :fast-flonum :constant Scm_Inexact)
(define-cproc inexact->exact (obj) :fast-flonum :constant Scm_Exact)

(select-module gauche)
(define-inline exact   inexact->exact)           ;R6RS
(define-inline inexact exact->inexact)           ;R6RS

(select-module scheme)
(define-cproc number->string (obj :optional (control-or-base #f)
                                            (flags #f)
                                            (precision::<fixnum> -1))
  :fast-flonum :constant
  (let* ([fmt::ScmNumberFormat]
         [pfmt::ScmNumberFormat* (& fmt)]
         [o::ScmPort* (SCM_PORT (Scm_MakeOutputStringPort TRUE))])
    (cond
     [(SCM_WRITE_CONTROLS_P control-or-base)
      (let* ([c::ScmWriteControls* (SCM_WRITE_CONTROLS control-or-base)])
        (set! pfmt (& (-> c numberFormat))))]
     [(SCM_INTP control-or-base)
      (let* ([f::u_long 0]
             [base::ScmSmallInt (SCM_INT_VALUE control-or-base)])
        (when (or (< base SCM_RADIX_MIN) (> base SCM_RADIX_MAX))
          (Scm_Error "base must be an integer between %d and %d, but got %d"
                     SCM_RADIX_MIN SCM_RADIX_MAX base))
        (cond [(or (SCM_FALSEP flags) (SCM_NULLP flags)) (set! f 0)]
              [(SCM_TRUEP flags) (set! f SCM_NUMBER_FORMAT_USE_UPPER)];compatibility
              [(SCM_PAIRP flags)
               (unless (SCM_FALSEP (Scm_Memq 'uppercase flags))
                 (logior= f SCM_NUMBER_FORMAT_USE_UPPER))
               (unless (SCM_FALSEP (Scm_Memq 'plus flags))
                 (logior= f SCM_NUMBER_FORMAT_SHOW_PLUS))
               (unless (SCM_FALSEP (Scm_Memq 'radix flags))
                 (logior= f SCM_NUMBER_FORMAT_ALT_RADIX))
               (unless (SCM_FALSEP (Scm_Memq 'notational flags))
                 (logior= f SCM_NUMBER_FORMAT_ROUND_NOTATIONAL))]
              [else
               (Scm_Error "flags argument must be a list of symbols (uppercase, \
                                plus, radix, notational) or a boolean, but got: %S"
                          flags)])
        (Scm_NumberFormatInit pfmt)
        (set! (-> pfmt base) base)
        (set! (-> pfmt flags) f)
        (set! (-> pfmt precision) precision))]
     [(SCM_FALSEP control-or-base)     ;use default
      (set! pfmt (& fmt))
      (Scm_NumberFormatInit pfmt)]
     [else
      (Scm_Error "<write-controls> or fixnum expected, but got: %S"
                 control-or-base)])
    (Scm_PrintNumber o obj pfmt)
    (return (Scm_GetOutputString o 0))))

(define-cproc string->number (obj::<string>
                              :optional (radix::<fixnum> 10)
                                        (default-exactness #f))
  (let* ([flags::u_long 0])
    (cond
     [(SCM_EQ default-exactness 'exact)
      (set! flags SCM_NUMBER_FORMAT_EXACT)]
     [(SCM_EQ default-exactness 'inexact)
      (set! flags SCM_NUMBER_FORMAT_INEXACT)]
     [(SCM_FALSEP default-exactness)]
     [else (Scm_Error "default-exactness must be either #f, exact or inexact, but got: %S" default-exactness)])
    (return (Scm_StringToNumber obj radix flags))))

(select-module gauche)
(define-cproc floor->exact (num) :fast-flonum :constant
  (return (Scm_RoundToExact num SCM_ROUND_FLOOR)))
(define-cproc ceiling->exact (num) :fast-flonum :constant
  (return (Scm_RoundToExact num SCM_ROUND_CEIL)))
(define-cproc truncate->exact (num) :fast-flonum :constant
  (return (Scm_RoundToExact num SCM_ROUND_TRUNC)))
(define-cproc round->exact (num) :fast-flonum :constant
  (return (Scm_RoundToExact num SCM_ROUND_ROUND)))

(define-cproc decode-float (num)        ;from ChezScheme
  (cond [(SCM_FLONUMP num)
         (let* ([exp::int] [sign::int]
                [f (Scm_DecodeFlonum (SCM_FLONUM_VALUE num) (& exp) (& sign))]
                [v (Scm_MakeVector 3 '#f)])
           (set! (SCM_VECTOR_ELEMENT v 0) f
                 (SCM_VECTOR_ELEMENT v 1) (Scm_MakeInteger exp)
                 (SCM_VECTOR_ELEMENT v 2) (Scm_MakeInteger sign))
           (return v))]
        [(SCM_INTP num)
         (let* ([v (Scm_MakeVector 3 '#f)])
           (set! (SCM_VECTOR_ELEMENT v 0) (Scm_Abs num)
                 (SCM_VECTOR_ELEMENT v 1) (Scm_MakeInteger 0)
                 (SCM_VECTOR_ELEMENT v 2) (Scm_MakeInteger (Scm_Sign num)))
           (return v))]
        [else (SCM_TYPE_ERROR num "real number") (return SCM_UNDEFINED)]))

(define-cproc fmod (x::<double> y::<double>)::<double> :constant fmod)

(define-cproc frexp (d::<double>) ::(<double> <int>) :constant
  (set! SCM_RESULT0 (frexp d (& SCM_RESULT1))))

(define-cproc modf (x::<double>) ::(<double> <double>) :constant
  (set! SCM_RESULT0 (modf x (& SCM_RESULT1))))

(define-cproc ldexp (x::<double> exp::<int>) ::<double> :constant ldexp)

(define-cproc log10 (x::<double>) ::<double> :constant log10)

;; NB: Alternative implemenation of gamma and log-abs-gamma functions are
;; provided in Scheme (lib/gauche/numerical.scm).
(select-module gauche.internal)
(inline-stub
 (.when (defined HAVE_TGAMMA)
   (define-cproc %gamma (x::<double>) ::<double> :fast-flonum :constant
     tgamma))
 (.when (defined HAVE_LGAMMA)
   (define-cproc %lgamma (x::<double>) ::<double> :fast-flonum :constant
     lgamma)))
;; Returns the HalfFloat representation as integer.  For now,
;; we keep it in gauche.internal.
(define-cproc flonum->f16bits (x::<double>) ::<int> :constant Scm_DoubleToHalf)

;;
;; Arithmetics
;;

;; NB: Compile-time constant folding for these four procedures are
;; handled in the compiler, so we don't need :constant flag here.

;; For - and /, one-argument case is handled by separate C procedures.
;; (Scm_Negate(), Scm_Reciprocal()).  For + and *, we return the argument
;; as is if it is a number, or delegate it to unary object-+ / object-*
;; method.
(select-module scheme)
(define-cproc * (:rest args) ::<number> :fast-flonum
  (cond [(not (SCM_PAIRP args)) (return (SCM_MAKE_INT 1))]
        [(and (not (SCM_PAIRP (SCM_CDR args)))
              (not (SCM_NUMBERP (SCM_CAR args))))
         (let* ([unary-proc SCM_UNDEFINED])
           (SCM_BIND_PROC unary-proc "%apply-unary-*"
                          (Scm_GaucheInternalModule))
           (return (Scm_ApplyRec1 unary-proc (SCM_CAR args))))]
        [else (let* ([r::ScmObj (SCM_CAR args)])
                (dolist [v (SCM_CDR args)] (set! r (Scm_Mul r v)))
                (return r))]))

(define-cproc + (:rest args) ::<number> :fast-flonum
  (cond [(not (SCM_PAIRP args)) (return (SCM_MAKE_INT 0))]
        [(and (not (SCM_PAIRP (SCM_CDR args)))
              (not (SCM_NUMBERP (SCM_CAR args))))
         (let* ([unary-proc SCM_UNDEFINED])
           (SCM_BIND_PROC unary-proc "%apply-unary-+"
                          (Scm_GaucheInternalModule))
           (return (Scm_ApplyRec1 unary-proc (SCM_CAR args))))]
        [else (let* ([r::ScmObj (SCM_CAR args)])
                (dolist [v (SCM_CDR args)] (set! r (Scm_Add r v)))
                (return r))]))

(define-cproc - (arg1 :rest args) ::<number> :fast-flonum
  (if (SCM_NULLP args)
    (return (Scm_VMNegate arg1))
    (begin (dolist [v args] (set! arg1 (Scm_Sub arg1 v)))
           (return arg1))))

(define-cproc / (arg1 :rest args) ::<number> :fast-flonum
  (if (SCM_NULLP args)
    (return (Scm_VMReciprocal arg1))
    (begin (dolist [v args] (set! arg1 (Scm_Div arg1 v)))
           (return arg1))))

(select-module gauche.internal)
(define (%apply-unary-generic gf who obj)
  ;; TRANSIENT: Up to 0.9.14, we return the argument as is from unary + and *
  ;; unconditionally.  For the backward compatibility if there's no unary
  ;; object-+ / object-*, we warn so and returns the argument instead of
  ;; raising an error.  Should remove that part after a few releases.
  ;; https://github.com/shirok/Gauche/issues/1012
  (if (applicable? gf (class-of obj))
    (gf obj)
    (begin
      (warn "operation ~a is not defined on ~s.  \
             This will be an error in future releases.\n" who obj)
      obj)))
(define (%apply-unary-+ obj) (%apply-unary-generic object-+ '+ obj))
(define (%apply-unary-* obj) (%apply-unary-generic object-* '* obj))

(select-module scheme)

(define-cproc abs (obj) :fast-flonum :constant Scm_VMAbs)

(define-cproc quotient (n1 n2) :fast-flonum :constant
  (return (Scm_Quotient n1 n2 NULL)))
(define-cproc remainder (n1 n2) :fast-flonum :constant
  (return (Scm_Modulo n1 n2 TRUE)))
(define-cproc modulo (n1 n2)    :fast-flonum :constant
  (return (Scm_Modulo n1 n2 FALSE)))

(select-module gauche)
;; gcd, lcm: these are the simplest ones.  If you need efficiency, consult
;; Knuth: "The Art of Computer Programming" Chap. 4.5.2
(define-in-module scheme (gcd . args)
  (define (recn arg args)
    (if (null? args)
      arg
      (recn ((with-module gauche.internal %gcd) arg (car args)) (cdr args))))
  (let1 args (map (^[arg] (unless (integer? arg)
                            (error "integer required, but got" arg))
                    (abs arg))
                  args)
    (cond [(null? args) 0]
          [(null? (cdr args)) (car args)]
          [else (recn (car args) (cdr args))])))

(define-in-module scheme (lcm . args)
  (define (lcm2 u v)
    (let1 g ((with-module gauche.internal %gcd) u v)
      (if (zero? u) 0 (* (quotient u g) v))))
  (define (recn arg args)
    (if (null? args)
      arg
      (recn (lcm2 arg (car args)) (cdr args))))
  (let1 args (map (^[arg] (unless (integer? arg)
                            (error "integer required, but got" arg))
                    (abs arg))
                  args)
    (cond [(null? args) 1]
          [(null? (cdr args)) (car args)]
          [else (recn (car args) (cdr args))])))

(select-module gauche.internal)
(define-cproc %gcd (n1 n2) :fast-flonum :constant Scm_Gcd)

(select-module scheme)
(define-cproc numerator (n)   :fast-flonum :constant Scm_Numerator)
(define-cproc denominator (n) :fast-flonum :constant Scm_Denominator)

(select-module gauche)
(define-in-module scheme (rationalize x e)
  ;; NB: real->rational is in gauche/numerical.scm
  (cond
   [(< e 0) (error "rationalize needs nonnegative error bound, but got" e)]
   [(or (nan? x) (nan? e)) +nan.0]
   [(infinite? e) (if (infinite? x) +nan.0 0.0)]
   [(infinite? x) x]
   [(or (inexact? x) (inexact? e)) (inexact (real->rational x e e))]
   [else (real->rational x e e)]))

(select-module scheme)
(define-cproc floor (v) ::<number> :fast-flonum :constant
  (return (Scm_Round v SCM_ROUND_FLOOR)))
(define-cproc ceiling (v) ::<number> :fast-flonum :constant
  (return (Scm_Round v SCM_ROUND_CEIL)))
(define-cproc truncate (v) ::<number> :fast-flonum :constant
  (return (Scm_Round v SCM_ROUND_TRUNC)))
(define-cproc round (v) ::<number> :fast-flonum :constant
  (return (Scm_Round v SCM_ROUND_ROUND)))

;; Transcedental functions.   First, real-only versions.
;; The name real-* is coined in SRFI-94.
;; NB: We don't support 'real-log' of SRFI-94.  It takes base number first,
;; which is reverse of R7RS 'log'.  It would be too confusing.
(select-module gauche)
(define-cproc real-exp (x::<double>) ::<double> :fast-flonum :constant exp)
(define-cproc real-ln (x::<number>) ::<double> :fast-flonum :constant
  (unless (SCM_REALP x) (SCM_TYPE_ERROR x "real number"))
  (when (< (Scm_Sign x) 0)
    (Scm_Error "Argument must be nonnegative real number: %S" x))
  ;; We can't simply cast x to double, for x can be a large bignum
  ;; outside of double.
  (let* ([d::double (Scm_GetDouble x)])
    (when (== d SCM_DBL_POSITIVE_INFINITY)
      (if (SCM_BIGNUMP x)
        (let* ([z::ScmBits* (cast ScmBits* (-> (SCM_BIGNUM x) values))]
               [scale::long (- (Scm_BitsHighest1 z 0 (* (SCM_BIGNUM_SIZE x) SCM_WORD_BITS)) 53)])
          (return (+ (log (Scm_GetDouble
                           (Scm_DivInexact x (Scm_Ash (SCM_MAKE_INT 1) scale))))
                     (* scale (log 2.0)))))
        (return SCM_DBL_POSITIVE_INFINITY)))
    (return (log d))))

(select-module gauche)
(define-cproc real-sin (x::<double>) ::<double> :fast-flonum :constant sin)
(define-cproc real-cos (x::<double>) ::<double> :fast-flonum :constant cos)
(define-cproc real-tan (x::<double>) ::<double> :fast-flonum :constant tan)

(define-cproc real-sinpi (x::<double>) ::<double> :fast-flonum :constant Scm_SinPi)
(define-cproc real-cospi (x::<double>) ::<double> :fast-flonum :constant Scm_CosPi)
(define-cproc real-tanpi (x::<double>) ::<double> :fast-flonum :constant Scm_TanPi)

(define-cproc real-asin (x::<double>) ::<number> :fast-flonum :constant
  (cond [(> x 1.0)
         (return (Scm_MakeComplex (/ M_PI 2.0)
                                  (- (log (+ x (sqrt (- (* x x) 1.0)))))))]
        [(< x -1.0)
         (return (Scm_MakeComplex (/ (- M_PI) 2.0)
                                  (- (log (- (- x) (sqrt (- (* x x) 1.0)))))))]
        [else (return (Scm_VMReturnFlonum (asin x)))]))

(define-cproc real-acos (x::<double>) ::<number> :fast-flonum :constant
  (cond [(> x 1.0)
         (return (Scm_MakeComplex 0 (log (+ x (sqrt (- (* x x) 1.0))))))]
        [(< x -1.0)
         (return (Scm_MakeComplex 0 (log (+ x (sqrt (- (* x x) 1.0))))))]
        [else (return (Scm_VMReturnFlonum (acos x)))]))

(define-cproc real-atan (z::<double> :optional x) ::<double> :fast-flonum :constant
  (cond [(SCM_UNBOUNDP x) (return (atan z))]
        [else (unless (SCM_REALP x) (SCM_TYPE_ERROR x "real number"))
              (return (atan2 z (Scm_GetDouble x)))]))

(define-cproc real-sinh (x::<double>) ::<double> :fast-flonum :constant sinh)
(define-cproc real-cosh (x::<double>) ::<double> :fast-flonum :constant cosh)
(define-cproc real-tanh (x::<double>) ::<double> :fast-flonum :constant tanh)
(define-cproc real-asinh (x::<double>) ::<double> :fast-flonum :constant asinh)
(define-cproc real-acosh (x::<double>) ::<double> :fast-flonum :constant acosh)
(define-cproc real-atanh (x::<double>) ::<double> :fast-flonum :constant atanh)

(define-cproc real-sqrt (x::<double>) :fast-flonum :constant
  (if (< x 0)
    (return (Scm_MakeComplex 0.0 (sqrt (- x))))
    (return (Scm_VMReturnFlonum (sqrt x)))))

;; Fast path for typical case of sqrt.  Handles positive flonum
;; and exact integer between 0 and 2^52.
;; If input is outside of the range, returns #f to fallback for
;; expensive path.
(select-module gauche.internal)
(define-cproc %sqrt-fast-path (x) :fast-flonum :constant
  (cond [(and (SCM_FLONUMP x) (>= (Scm_Sign x) 0))
         (return (Scm_VMReturnFlonum (sqrt (SCM_FLONUM_VALUE x))))]
        [(and (SCM_INTEGERP x)
              (>= (Scm_Sign x) 0)
              (>= (Scm_NumCmp SCM_2_52 x) 0))
         (let* ([d::double (Scm_GetDouble x)]
                [q::double (sqrt d)]
                [qq::double (floor q)]
                [dd::double (* qq qq)])
           ;; NB: The result is in [0, 2^26], so we know it fits in fixnum.
           (if (== d dd)
             (return (SCM_MAKE_INT (cast long q)))
             (return (Scm_VMReturnFlonum q))))]
        [else (return SCM_FALSE)]))

(select-module gauche)
(define-cproc real-expt (x y) :fast-flonum :constant Scm_Expt)
(define-cproc exact-expt (x y::<integer>) :constant
  (unless (SCM_EXACTP x) (SCM_TYPE_ERROR x "exact real number"))
  (return (Scm_ExactIntegerExpt x y)))
(define-cproc integer-expt (x::<integer> y::<integer>) ::<integer> :constant
  Scm_ExactIntegerExpt)

;; Now, handles complex numbers.
;;  Cf. Teiji Takagi: "Kaiseki Gairon" pp.193--198
(define-in-module scheme (exp z)
  (cond [(real? z) (real-exp z)]
        [(complex? z) (make-polar (real-exp (real-part z)) (imag-part z))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (log z . base)
  (if (null? base)
    (cond [(real? z)
           (if (<= 0 z)
             (real-ln z)
             ;; The constant pi is not avaialble here, but (* 4 (real-atan 1))
             ;; is constand-foled to pi.
             (make-rectangular (real-ln (- z)) (* 4 (real-atan 1))))]
          [(complex? z) (make-rectangular (real-ln (magnitude z)) (angle z))]
          [else (error "number required, but got" z)])
    (/ (log z) (log (car base)))))  ; R6RS addition

(select-module gauche.internal)
(define-in-module scheme (sqrt z)
  (cond
   [(%sqrt-fast-path z)] ; fast-path check
   [(and (exact? z) (>= z 0))
    ;; Gauche doesn't have exact complex, so we have real z.
    (if (integer? z)
      (receive (s r) (%exact-integer-sqrt z)
        (if (= r 0) s (real-sqrt z)))
      (let ([n (numerator z)]
            [d (denominator z)])
        (if-let1 nq (%sqrt-fast-path n)
          (if-let1 dq (%sqrt-fast-path d)
            (/ nq dq)
            (receive (ds dr) (%exact-integer-sqrt d)
              (if (= dr 0)
                (/ nq ds)
                (real-sqrt z))))
          (receive (ns nr) (%exact-integer-sqrt n)
            (if (= nr 0)
              (receive (ds dr) (%exact-integer-sqrt d)
                (if (= dr 0)
                 (/ ns ds)
                  (real-sqrt z)))
              (real-sqrt z))))))]
   [(real? z) (real-sqrt z)]
   [(complex? z) (make-polar (real-sqrt (magnitude z)) (/ (angle z) 2.0))]
   [else (error "number required, but got" z)]))

(define-in-module gauche.internal (%exact-integer-sqrt k) ; k >= 0
  (if (< k 4503599627370496)            ;2^52
    ;; k can be converted to a double without loss.
    (let1 s (floor->exact (real-sqrt k))
      (values s (- k (* s s))))
    ;; use Newton-Rhapson
    ;; If k is representable with double, we use (real-sqrt k) as the initial
    ;; estimate, for calculating double sqrt is fast.  If k is too large,
    ;; we use 2^floor((log2(k)+1)/2) as the initial value.
    (let loop ([s (let1 ik (real-sqrt k)
                    (if (finite? ik)
                      (floor->exact (real-sqrt k))
                      (ash 1 (quotient (integer-length k) 2))))])
      (let1 s2 (* s s)
        (if (< k s2)
          (loop (quotient (+ s2 k) (* 2 s)))
          (let1 s2+ (+ s2 (* 2 s) 1)
            (if (< k s2+)
              (values s (- k s2))
              (loop (quotient (+ s2 k) (* 2 s))))))))))

(define-in-module scheme (expt x y)
  (cond [(and (exact? x) (exact? y))
         ((with-module gauche.internal %exact-expt) x y)]
        [(real? x)
         (cond [(real? y) (real-expt x y)]
               [(number? y)
                (let1 ry (real-part y)
                  (if (and (zero? x) (positive? ry))
                    (if (exact? x) 0 0.0)
                    (* (real-expt x ry)
                       (exp (* +i (imag-part y) (real-ln x))))))]
               [else (error "number required, but got" y)])]
        [(number? x) (exp (* y (log x)))]
        [else (error "number required, but got" x)]))

(select-module gauche.internal)
(define (%exact-expt x y) ;; x, y :: exact
  (cond [(integer? y) (exact-expt x y)]
        [(< x 0) (real-expt x y)] ; we don't have exact compnum
        [(< y 0) (/ (%exact-expt x (- y)))]
        [(integer? x)
         (let ([a (numerator y)]
               [b (denominator y)])
           ;; Calculate b-th root of x by Newton-Rhapson.
           ;;
           ;;   expt(x, 1/b) ==  expt(2, (log_2 x)/b)
           ;;                =:= ash(1, round((log_2 x)/b))
           ;;
           ;; So we start from a initial value r_0 = ash(1, round((log_2 x)/b)),
           ;; then refine it by
           ;;
           ;;   r_{N+1} = r_N - ceil( (r_N^b - x)/br_N^{b-1} )
           ;;
           ;; Since we're looking for an integer solution, we can give up
           ;; when the following condition is met:
           ;;
           ;;     ( r_N+1 - r_N == 1 AND r_N^b < x < r_{N+1}^b )
           ;;  OR ( r_N - r_N+1 == 1 AND r_{N+1}^b < x < r_N^b )
           ;;
           ;; In that case, we fall back to inexact calculation by real-expt.
           (let* ([r (ash 1 (round->exact (/ (log x 2) b)))]
                  [s (integer-expt r b)])
             (if (= s x)
               (integer-expt r a)
               (let loop ([r r] [s s])
                 (let* ([deliv (* b (integer-expt r (- b 1)))]
                        [err   (- s x)]
                        [delta (if (> err 0)
                                 (quotient (+ err deliv -1) deliv)
                                 (quotient (- err deliv -1) deliv))]
                        [r2 (- r delta)]
                        [s2 (integer-expt r2 b)])
                   (if (= s2 x)
                     (integer-expt r2 a)
                     (if (or (and (= delta 1)  (< s2 x s))
                             (and (= delta -1) (< s x s2)))
                       (real-expt x y)
                       (loop r2 s2))))))))]
        [else
         ;; x is rational
         (or (and-let* ([xn (%exact-expt (numerator x) y)]
                        [ (exact? xn) ]
                        [xd (%exact-expt (denominator x) y)]
                        [ (exact? xd) ])
               (/ xn xd))
             (real-expt x y))]))

(select-module gauche)
(define-in-module scheme (cos z)
  (cond [(real? z) (real-cos z)]
        [(number? z)
         (let ([x (real-part z)]
               [y (imag-part z)])
           (make-rectangular (* (real-cos x) (real-cosh y))
                             (- (* (real-sin x) (real-sinh y)))))]
        [else (error "number required, but got" z)]))

(define (cosh z)
  (cond [(real? z) (real-cosh z)]
        [(number? z)
         (let ([x (real-part z)]
               [y (imag-part z)])
           (make-rectangular (* (real-cosh x) (real-cos y))
                             (* (real-sinh x) (real-sin y))))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (sin z)
  (cond [(real? z) (real-sin z)]
        [(number? z)
         (let ([x (real-part z)]
               [y (imag-part z)])
           (make-rectangular (* (real-sin x) (real-cosh y))
                             (* (real-cos x) (real-sinh y))))]
        [else (error "number required, but got" z)]))

(define (sinh z)
  (cond [(real? z) (real-sinh z)]
        [(number? z)
         (let ([x (real-part z)]
               [y (imag-part z)])
           (make-rectangular (* (real-sinh x) (real-cos y))
                             (* (real-cosh x) (real-sin y))))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (tan z)
  (cond [(real? z) (real-tan z)]
        [(number? z)
         (let1 iz (* +i z)
           (* -i
              (/ (- (exp iz) (exp (- iz)))
                 (+ (exp iz) (exp (- iz))))))]
        [else (error "number required, but got" z)]))

(define (tanh z)
  (cond [(real? z) (real-tanh z)]
        [(number? z) (/ (- (exp z) (exp (- z)))
                        (+ (exp z) (exp (- z))))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (asin z)
  (cond [(real? z) (real-asin z)]
        [(number? z)
         ;; The definition of asin is
         ;;   (* -i (log (+ (* +i z) (sqrt (- 1 (* z z))))))
         ;; This becomes unstable when the term in the log is reaching
         ;; toward 0.0.  The term, k = (+ (* +i z) (sqrt (- 1 (* z z)))),
         ;; gets closer to zero when |z| gets bigger, but for large |z|,
         ;; k is prone to lose precision and starts drifting around
         ;; the point zero.
         ;; For now, I let asin to return NaN in such cases.
         (let1 zz (+ (* +i z) (sqrt (- 1 (* z z))))
           (if (< (/. (magnitude zz) (magnitude z)) 1.0e-8)
             (make-rectangular +nan.0 +nan.0)
             (* -i (log zz))))]
        [else (error "number required, but got" z)]))

(define (asinh z)
  (cond [(real? z) (real-asinh z)]
        [(number? z)
         (let1 zz (+ z (sqrt (+ (* z z) 1)))
           (if (< (/. (magnitude zz) (magnitude z)) 1.0e-8)
             (make-rectangular +nan.0 +nan.0)
             (log zz)))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (acos z)
  (cond [(real? z) (real-acos z)]
        [(number? z)
         ;; The definition of acos is
         ;;  (* -i (log (+ z (* +i (sqrt (- 1 (* z z)))))))))
         ;; This also falls in the victim of numerical unstability; worse than
         ;; asin, sometimes the real part of marginal value "hops" between
         ;; +pi and -pi.  It's rather stable to use asin.
         (- 1.5707963267948966 (asin z))]
        [else (error "number required, but got" z)]))

(define (acosh z)
  (cond [(real? z) (real-acosh z)]
        [(number? z);; See the discussion of CLtL2, pp. 313-314
         (* 2 (log (+ (sqrt (/ (+ z 1) 2))
                      (sqrt (/ (- z 1) 2)))))]
        [else (error "number required, but got" z)]))

(define-in-module scheme (atan z . x)
  (if (null? x)
    (cond [(real? z) (real-atan z)]
          [(number? z)
           (let1 iz (* z +i)
             (/ (- (log (+ 1 iz))
                   (log (- 1 iz)))
                +2i))]
          [else (error "number required, but got" z)])
    (cond [(and (eq? (car x) 0) (zero? z))
           ;; Special case.  See R7RS.
           (cond [(eq? z 0) +nan.0]
                 [(negative-zero? z) -1.5707963267948966]
                 [else +1.5707963267948966])]
          [(and (real? z) (real? (car x))) (real-atan z (car x))]
          [else (error "two-argument atan requires real numbers, but got"
                       (list z (car x)))])))

(define (atanh z)
  (cond [(real? z) (real-atanh z)]
        [(number? z) (/ (- (log (+ 1 z)) (log (- 1 z))) 2)]
        [else (error "number required, but got" z)]))

(select-module gauche)
(define-cproc radians->degrees (r::<double>) ::<double> :constant :fast-flonum
  (return (* r (/ 180 M_PI))))
(define-cproc degrees->radians (d::<double>) ::<double> :constant :fast-flonum
  (return (* d (/ M_PI 180))))

(select-module gauche)

(define-cproc ash (num cnt::<fixnum>) ::<integer> :constant Scm_Ash)

(define-cproc lognot (x::<integer>) ::<integer>  :constant Scm_LogNot)

(inline-stub
 (define-cise-stmt logop
   [(_ fn ident)
    `(cond [(== optcnt 0) (return ,ident)]
           [(== optcnt 1)
            (unless (SCM_INTEGERP (aref arg2 0))
              (Scm_Error "Exact integer required, but got %S" (aref arg2 0)))
            (return (aref arg2 0))]
           [else
            (let* ([r (,fn (aref arg2 0) (aref arg2 1))])
              (for-each (lambda (v) (set! r (,fn r v))) args)
              (return r))])])
 )

(define-cproc logand (:optarray (arg2 optcnt 2) :rest args) ::<integer> :constant
  (logop Scm_LogAnd (SCM_MAKE_INT -1)))
(define-cproc logior (:optarray (arg2 optcnt 2) :rest args) ::<integer> :constant
  (logop Scm_LogIor (SCM_MAKE_INT 0)))
(define-cproc logxor (:optarray (arg2 optcnt 2) :rest args) ::<integer> :constant
  (logop Scm_LogXor (SCM_MAKE_INT 0)))

(define-cproc logcount (n::<integer>) ::<int> :constant
  (cond [(SCM_EQ n (SCM_MAKE_INT 0)) (return 0)]
        [(SCM_INTP n)
         (let* ([z::ScmBits (cast ScmBits (cast long (SCM_INT_VALUE n)))])
           (if (> (SCM_INT_VALUE n) 0)
             (return (Scm_BitsCount1 (& z) 0 SCM_WORD_BITS))
             (return (Scm_BitsCount0 (& z) 0 SCM_WORD_BITS))))]
        [(SCM_BIGNUMP n) (return (Scm_BignumLogCount (SCM_BIGNUM n)))]
        [else (SCM_TYPE_ERROR n "exact integer") (return 0)]))

(define-cproc logset+clear (n::<integer> sets::<integer> clears::<integer>)
  ::<integer> :constant
  (return (Scm_LogAnd (Scm_LogIor n sets) (Scm_LogNot clears))))

(define-cproc integer-length (n::<integer>) ::<ulong> :constant Scm_IntegerLength)

;; Returns maximum s where (expt 2 s) is a factor of n.
;; This can be (- (integer-length (logxor n (- n 1))) 1), but we can save
;; creating intermediate numbers by providing this natively.
(define-cproc twos-exponent-factor (n::<integer>) ::<int> :constant
  (cond [(SCM_EQ n (SCM_MAKE_INT 0)) (return -1)]
        [(SCM_INTP n)
         (let* ([z::ScmBits (cast ScmBits (cast long (SCM_INT_VALUE n)))])
           (return (Scm_BitsLowest1 (& z) 0 SCM_WORD_BITS)))]
        [(SCM_BIGNUMP n)
         (let* ([z::ScmBits* (cast ScmBits* (-> (SCM_BIGNUM n) values))]
                [k::int (SCM_BIGNUM_SIZE n)])
           (return (Scm_BitsLowest1 z 0 (* k SCM_WORD_BITS))))]
        [else (SCM_TYPE_ERROR n "exact integer") (return 0)]))

(define-cproc twos-exponent (n::<integer>) ::<integer>? :constant
  (let* ([i::long (Scm_TwosPower n)])
    (return (?: (>= i 0) (Scm_MakeInteger i) SCM_FALSE))))

;; As of 0.8.8 we started to support exact rational numbers.  Some existing
;; code may count on exact integer division to be coerced to flonum
;; if it isn't produce a whole number, and such programs start
;; running very slowly on 0.8.8 by introducing unintentional exact
;; rational arithmetic.
;;
;; For the smooth transition, we provide the original behavior as
;; inexact-/.  If the program uses compat.no-rational, '/' is overridden
;; by inexact-/ and the old code behaves the same.
(define-cproc inexact-/ (arg1 :rest args)
  (cond [(SCM_NULLP args) (return (Scm_ReciprocalInexact arg1))]
        [else (dolist [x args] (set! arg1 (Scm_DivCompat arg1 x)))
              (return arg1)]))

;; Inexact arithmetics.  Useful for speed-sensitive code to avoid
;; accidental use of bignum or ratnum.   We might want to optimize
;; these more, even adding special VM insns for them.
(define-cproc +. (:rest args) :constant
  (let* ([a '0.0])
    (dolist [x args] (set! a (Scm_Add a (Scm_Inexact x))))
    (return a)))
(define-cproc *. (:rest args) :constant
  (let* ([a '1.0])
    (dolist [x args] (set! a (Scm_Mul a (Scm_Inexact x))))
    (return a)))
(define-cproc -. (arg1 :rest args) :constant
  (cond
   [(SCM_NULLP args) (return (Scm_Negate (Scm_Inexact arg1)))]
   [else (dolist [x args] (set! arg1 (Scm_Sub arg1 (Scm_Inexact x))))
         (return arg1)]))
(define-cproc /. (arg1 :rest args) :constant
  (cond
   [(SCM_NULLP args) (return (Scm_Reciprocal (Scm_Inexact arg1)))]
   [else (dolist [x args] (set! arg1 (Scm_DivInexact arg1 x)))
         (return arg1)]))

(define-cproc clamp (x :optional (min #f) (max #f)) :fast-flonum :constant
  (let* ([r x] [maybe_exact::int (SCM_EXACTP x)])
    (unless (SCM_REALP x) (SCM_TYPE_ERROR x "real number"))
    (cond [(SCM_EXACTP min) (when (< (Scm_NumCmp x min) 0) (set! r min))]
          [(SCM_FLONUMP min)
           (set! maybe_exact FALSE)
           (when (< (Scm_NumCmp x min) 0) (set! r min))]
          [(not (SCM_FALSEP min)) (SCM_TYPE_ERROR min "real number or #f")])
    (cond [(SCM_EXACTP max) (when (> (Scm_NumCmp x max) 0) (set! r max))]
          [(SCM_FLONUMP max)
           (set! maybe_exact FALSE)
           (when (> (Scm_NumCmp x max) 0) (set! r max))]
          [(not (SCM_FALSEP max)) (SCM_TYPE_ERROR max "real number or #f")])
    (if (and (not maybe_exact) (SCM_EXACTP r))
      (return (Scm_Inexact r))
      (return r))))

(define-cproc quotient&remainder (n1 n2) ::(<top> <top>)
  (set! SCM_RESULT0 (Scm_Quotient n1 n2 (& SCM_RESULT1))))

;;
;; Complex numbers
;;

(select-module scheme)
(define-cproc make-rectangular (a::<double> b::<double>) ::<number> :constant
  Scm_MakeComplex)
(define-cproc make-polar (r::<double> t::<double>) ::<number> :constant
  Scm_MakeComplexPolar)

;; we don't use Scm_RealPart and Scm_ImagPart, for preserving exactness
;; and avoiding extra allocation.
(define-cproc real-part (z::<number>) :fast-flonum :constant
  (if (SCM_REALP z)
    (return z)
    (return (Scm_VMReturnFlonum (SCM_COMPNUM_REAL z)))))

(define-cproc imag-part (z::<number>) :fast-flonum :constant
  (cond [(SCM_EXACTP z) (return (SCM_MAKE_INT 0))]
        [(SCM_REALP z)  (return (Scm_VMReturnFlonum 0.0))]
        [else (return (Scm_VMReturnFlonum (SCM_COMPNUM_IMAG z)))]))

(define-cproc magnitude (z::<number>) ::<number> :fast-flonum :constant Scm_VMAbs)
(define-cproc angle (z::<number>) ::<double> :fast-flonum :constant Scm_Angle)

;; Utility to recognize clamp mode symbols and returns C enum
;; (Mostly used in uvector-related apis)

(inline-stub
 (define-cfn Scm_ClampMode (clamp) ::int
   (cond [(SCM_EQ clamp 'both) (return SCM_CLAMP_BOTH)]
         [(SCM_EQ clamp 'low)  (return SCM_CLAMP_LO)]
         [(SCM_EQ clamp 'high) (return SCM_CLAMP_HI)]
         [(SCM_EQ clamp 'wraparound) (return SCM_CLAMP_WRAPAROUND)]
         [(not (or (SCM_FALSEP clamp) (SCM_UNBOUNDP clamp)))
          (Scm_Error "clamp argument must be either 'both, 'high, 'low, \
                      'wraparound or #f, but got %S" clamp)])
   (return SCM_CLAMP_ERROR)))
