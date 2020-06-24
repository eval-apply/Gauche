;;;
;;; gauche.uvector - uniform vectors
;;;
;;;   Copyright (c) 2000-2020  Shiro Kawai  <shiro@acm.org>
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

;; This module defines the superset of SRFI-4, homogeneous numeric vector
;; types.   Most of basic operations are defined in the DSO module libuvector.
;; Besides defining functions, the DSO sets up a reader hook to enable
;; extended syntax such as #s8(1 2 3).
;; This module also defines methods for collection and sequence frameworks.

;; NB: The following identifiers are defined in core now:
;;   uvector? ${TAG}vector-ref  ${TAG}vector-set!  uvector-ref  uvector-set!
;;   uvector-length    uvector-immutable?

(define-module gauche.uvector
  (use gauche.collection)
  (use gauche.sequence)

  (include "./exports")
  (export make-uvector

          open-output-uvector get-output-uvector port->uvector
          read-block! read-uvector read-uvector! referencer

          string->s8vector string->s8vector!
          string->u8vector string->u8vector!
          string->s32vector string->s32vector! 
          string->u32vector string->u32vector!
          s8vector->string u8vector->string
          s32vector->string u32vector->string

          uvector-alias uvector-binary-search uvector-class-element-size
          uvector-copy uvector-copy! uvector-ref uvector-set! uvector-size
          uvector->list uvector->vector uvector-swap-bytes uvector-swap-bytes!

          write-block write-uvector

          ;; R7RS compatibility (scheme base) and (scheme bytevector)
          bytevector bytevector? make-bytevector bytevector-fill!
          bytevector-length bytevector-u8-ref bytevector-u8-set!
          bytevector-s8-ref bytevector-s8-set!
          bytevector->u8-list u8-list->bytevector
          bytevector-copy bytevector-copy! bytevector-copy!-r6
          bytevector-append bytevector=?
          ))
(select-module gauche.uvector)

;; gauche.vport is used by port->uvector.  Technically it's on top
;; of uniform vector ports provided by gauche.vport, but logically
;; it is expected to belong gauche.uvector.
(autoload gauche.vport open-output-uvector get-output-uvector)

;; referenced from srfi-160 make-TAGvector-generator
(autoload gauche.generator uvector->generator)

(inline-stub
 (declcode
  (.include <math.h>)
  (.define EXTUVECTOR_EXPORTS)
  (.include "gauche/uvector.h")
  (.include "gauche/priv/vectorP.h")
  (.include "gauche/priv/bytesP.h")
  (.include "uvectorP.h")))

;; uvlib.scm is generated by uvlib.scm.tmpl
(include "./uvlib.scm")

;;;
;;; Generic procedures
;;;

;; uvector-alias
(inline-stub
 (define-cproc uvector-alias
   (klass::<class> v::<uvector> :optional (start::<fixnum> 0) (end::<fixnum> -1))
   Scm_UVectorAlias)
 )

;; byte swapping
(inline-stub
 (define-cise-stmt swap-bytes-common
   [(_ c-fn v type)
    `(let* ([opt::int SWAPB_STD])
       (cond [(== ,type NULL)]
             [(SCM_EQ (SCM_OBJ ,type) 'le:arm-le) (= opt SWAPB_ARM_LE)]
             [(SCM_EQ (SCM_OBJ ,type) 'be:arm-le) (= opt SWAPB_ARM_BE)]
             [else (Scm_TypeError "type" "#f or a symbol le:arm-le or be:arm-le"
                                  (SCM_OBJ ,type))])
       (,c-fn ,v opt))])

 (define-cproc uvector-swap-bytes (v::<uvector> :optional (type::<symbol>? #f)) ::<void>
   (swap-bytes-common Scm_UVectorSwapBytes v type))

 (define-cproc uvector-swap-bytes! (v::<uvector> :optional (type::<symbol>? #f)) ::<void>
   (swap-bytes-common Scm_UVectorSwapBytesX v type))
 )

;; uvector-size
(inline-stub
 (define-cproc uvector-size (v::<uvector>
                             :optional (start::<int> 0) (end::<int> -1))
   ::<int>
   (let* ([len::int (SCM_UVECTOR_SIZE v)])
     (SCM_CHECK_START_END start end len)
     (return (* (- end start)
                (Scm_UVectorElementSize (Scm_ClassOf (SCM_OBJ v)))))))

 (define-cproc uvector-class-element-size (c::<class>) ::<fixnum>
   (let* ([r::int (Scm_UVectorElementSize c)])
     (when (< r 0)
       (Scm_Error "A class of uvector is required, but got: %S" c))
     (return r)))
 )

;; uvector->list
;; uvector->vector
(inline-stub
 (define-cproc uvector->list (v::<uvector> 
                              :optional (start::<fixnum> 0)
                                        (end::<fixnum> -1))
   (case (Scm_UVectorType (Scm_ClassOf (SCM_OBJ v)))
     [(SCM_UVECTOR_S8) (return (Scm_S8VectorToList (SCM_S8VECTOR v)
                                                   start end))]
     [(SCM_UVECTOR_U8) (return (Scm_U8VectorToList (SCM_U8VECTOR v)
                                                   start end))]
     [(SCM_UVECTOR_S16) (return (Scm_S16VectorToList (SCM_S16VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_U16) (return (Scm_U16VectorToList (SCM_U16VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_S32) (return (Scm_S32VectorToList (SCM_S32VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_U32) (return (Scm_U32VectorToList (SCM_U32VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_S64) (return (Scm_S64VectorToList (SCM_S64VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_U64) (return (Scm_U64VectorToList (SCM_U64VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_F16) (return (Scm_F16VectorToList (SCM_F16VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_F32) (return (Scm_F32VectorToList (SCM_F32VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_F64) (return (Scm_F64VectorToList (SCM_F64VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_C32) (return (Scm_C32VectorToList (SCM_C32VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_C64) (return (Scm_C64VectorToList (SCM_C64VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_C128) (return (Scm_C128VectorToList (SCM_C128VECTOR v)
                                                       start end))]
     [else (Scm_Error "[internal] Invalid uvector type: %S" v)
           (return SCM_UNDEFINED)]))
 (define-cproc uvector->vector (v::<uvector> 
                                :optional (start::<fixnum> 0)
                                          (end::<fixnum> -1))
   (case (Scm_UVectorType (Scm_ClassOf (SCM_OBJ v)))
     [(SCM_UVECTOR_S8) (return (Scm_S8VectorToVector (SCM_S8VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_U8) (return (Scm_U8VectorToVector (SCM_U8VECTOR v)
                                                     start end))]
     [(SCM_UVECTOR_S16) (return (Scm_S16VectorToVector (SCM_S16VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_U16) (return (Scm_U16VectorToVector (SCM_U16VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_S32) (return (Scm_S32VectorToVector (SCM_S32VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_U32) (return (Scm_U32VectorToVector (SCM_U32VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_S64) (return (Scm_S64VectorToVector (SCM_S64VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_U64) (return (Scm_U64VectorToVector (SCM_U64VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_F16) (return (Scm_F16VectorToVector (SCM_F16VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_F32) (return (Scm_F32VectorToVector (SCM_F32VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_F64) (return (Scm_F64VectorToVector (SCM_F64VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_C32) (return (Scm_C32VectorToVector (SCM_C32VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_C64) (return (Scm_C64VectorToVector (SCM_C64VECTOR v)
                                                       start end))]
     [(SCM_UVECTOR_C128) (return (Scm_C128VectorToVector (SCM_C128VECTOR v)
                                                         start end))]
     [else (Scm_Error "[internal] Invalid uvector type: %S" v)
           (return SCM_UNDEFINED)])))   

;; allocation by class
(inline-stub
 (define-cproc make-uvector (klass::<class> size::<fixnum>
                             :optional (init 0))
   (unless (>= size 0) (Scm_Error "invalid uvector size: %d" size))
   (let* ([v (Scm_MakeUVector klass size NULL)])
     (case (Scm_UVectorType klass)
       [(SCM_UVECTOR_S8)
        (Scm_S8VectorFill (SCM_S8VECTOR v) 
                          (Scm_GetInteger8Clamp init SCM_CLAMP_ERROR NULL)
                          0 -1)]
       [(SCM_UVECTOR_U8)
        (Scm_U8VectorFill (SCM_U8VECTOR v) 
                          (Scm_GetIntegerU8Clamp init SCM_CLAMP_ERROR NULL)
                          0 -1)]
       [(SCM_UVECTOR_S16)
        (Scm_S16VectorFill (SCM_S16VECTOR v) 
                           (Scm_GetInteger16Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_U16)
        (Scm_U16VectorFill (SCM_U16VECTOR v) 
                           (Scm_GetIntegerU16Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_S32)
        (Scm_S32VectorFill (SCM_S32VECTOR v) 
                           (Scm_GetInteger32Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_U32)
        (Scm_U32VectorFill (SCM_U32VECTOR v) 
                           (Scm_GetIntegerU32Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_S64)
        (Scm_S64VectorFill (SCM_S64VECTOR v) 
                           (Scm_GetInteger64Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_U64)
        (Scm_U64VectorFill (SCM_U64VECTOR v) 
                           (Scm_GetIntegerU64Clamp init SCM_CLAMP_ERROR NULL)
                           0 -1)]
       [(SCM_UVECTOR_F16)
        (Scm_F16VectorFill (SCM_F16VECTOR v)
                           (Scm_DoubleToHalf (Scm_GetDouble init))
                           0 -1)]
       [(SCM_UVECTOR_F32)
        (Scm_F32VectorFill (SCM_F32VECTOR v)
                           (cast float (Scm_GetDouble init))
                           0 -1)]
       [(SCM_UVECTOR_F64)
        (Scm_F64VectorFill (SCM_F64VECTOR v)
                           (Scm_GetDouble init)
                           0 -1)]
       [(SCM_UVECTOR_C32)
        (Scm_C32VectorFill (SCM_C32VECTOR v) (Scm_GetHalfComplex init) 0 -1)]
       [(SCM_UVECTOR_C64)
        (Scm_C64VectorFill (SCM_C64VECTOR v) (Scm_GetFloatComplex init) 0 -1)]
       [(SCM_UVECTOR_C128)
        (Scm_C128VectorFill (SCM_C128VECTOR v) 
                            (Scm_GetDoubleComplex init) 0 -1)]
       [else (Scm_Error "[internal] Invalid uvector class: %S" klass)])
     (return v))))

;; generic copy
(inline-stub
 (define-cproc uvector-copy (v::<uvector>
                             :optional (start::<fixnum> 0)
                                       (end::<fixnum> -1))
   (return (Scm_UVectorCopy v start end)))
 )

;; search
;; rounding can be #f, 'floor or 'ceiling  (srfi-114 also uses symbols
;; for rounding.  we don't use 'round and 'truncate, though, for
;; it doesn't make much sense.)
(inline-stub
 ;; aux fn to deal with optional fixnum arg.  we should make genstub handle
 ;; this in future.
 (define-cfn get-fixnum-arg (arg fallback::ScmSmallInt name::(const char*))
   ::ScmSmallInt :static
   (cond [(SCM_INTP arg) (return (SCM_INT_VALUE arg))]
         [(SCM_FALSEP arg) (return fallback)]
         [else (Scm_Error "%s expects fixnum or #f, but got: %S" name arg)
               (return 0)])) ; dummy
 
 (define-cproc uvector-binary-search (v::<uvector> key::<number>
                                                   :optional
                                                   (start #f)
                                                   (end   #f)
                                                   (skip  #f)
                                                   (rounding #f))
   (let* ([len::ScmSize (SCM_UVECTOR_SIZE v)]
          [s::ScmSmallInt (get-fixnum-arg start 0 "start")]
          [e::ScmSmallInt (get-fixnum-arg end -1 "end")]
          [p::ScmSmallInt (get-fixnum-arg skip 0 "skip")])
     (SCM_CHECK_START_END s e len)
     (unless (== (% (- e s) (+ p 1)) 0)
       (Scm_Error "uvector size (%d) isn't multiple of record size (%d)"
                  (- e s) (+ p 1)))
     (let* ([r::size_t (cast (size_t) -1)]
            [lb::size_t]
            [ub::size_t])
       (case (Scm_UVectorType (Scm_ClassOf (SCM_OBJ v)))
         [(SCM_UVECTOR_S8)
          (let* ([k::int8_t
                  (Scm_GetInteger8Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchS8 (+ (SCM_S8VECTOR_ELEMENTS v) s)
                                        (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_U8)
          (let* ([k::uint8_t
                  (Scm_GetIntegerU8Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchU8 (+ (SCM_U8VECTOR_ELEMENTS v) s)
                                        (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_S16)
          (let* ([k::int16_t
                  (Scm_GetInteger16Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchS16 (+ (SCM_S16VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_U16)
          (let* ([k::uint16_t
                  (Scm_GetIntegerU16Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchU16 (+ (SCM_U16VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_S32)
          (let* ([k::int32_t
                  (Scm_GetInteger32Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchS32 (+ (SCM_S32VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_U32)
          (let* ([k::uint32_t
                  (Scm_GetIntegerU32Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchU32 (+ (SCM_U32VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_S64)
          (let* ([k::int64_t
                  (Scm_GetInteger64Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchS64 (+ (SCM_S64VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_U64)
          (let* ([k::uint64_t
                  (Scm_GetIntegerU64Clamp key SCM_CLAMP_ERROR NULL)])
            (set! r (Scm_BinarySearchU64 (+ (SCM_U64VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_F16)
          (let* ([k::ScmHalfFloat (Scm_DoubleToHalf (Scm_GetDouble key))])
            (set! r (Scm_BinarySearchF16 (+ (SCM_F16VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]         
         [(SCM_UVECTOR_F32)
          (let* ([k::float (Scm_GetDouble key)])
            (set! r (Scm_BinarySearchF32 (+ (SCM_F32VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]         
         [(SCM_UVECTOR_F64)
          (let* ([k::double (Scm_GetDouble key)])
            (set! r (Scm_BinarySearchF64 (+ (SCM_F64VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]
         [(SCM_UVECTOR_C32)
          (let* ([k::ScmHalfComplex (Scm_GetHalfComplex key)])
            (set! r (Scm_BinarySearchC32 (+ (SCM_C32VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]         
         [(SCM_UVECTOR_C64)
          (let* ([k::(ScmFloatComplex) (Scm_GetFloatComplex key)])
            (set! r (Scm_BinarySearchC64 (+ (SCM_C64VECTOR_ELEMENTS v) s)
                                         (- e s) k p (& lb) (& ub))))]         
         [(SCM_UVECTOR_C128)
          (let* ([k::(ScmDoubleComplex) (Scm_GetDoubleComplex key)])
            (set! r (Scm_BinarySearchC128 (+ (SCM_C128VECTOR_ELEMENTS v) s)
                                          (- e s) k p (& lb) (& ub))))] 
         [else (Scm_Error "[internal] Invalid uvector type: %S" v)]
         )
       (when (== r (cast (size_t) -1))
         (cond
          [(SCM_EQ rounding 'floor)   (set! r lb)]
          [(SCM_EQ rounding 'ceiling) (set! r ub)]
          [(not (SCM_FALSEP rounding))
           (Scm_Error "Rounding argument must be either #f, floor \
                       or ceiling, but got: %S" rounding)]))
       (if (== r (cast (size_t) -1))
         (return SCM_FALSE)
         (return (Scm_MakeIntegerU (+ r s)))))))
 )

;; block i/o
(inline-stub
 (define-cproc read-uvector! (v::<uvector>
                              :optional (port::<input-port> (current-input-port))
                                        (start::<fixnum> 0)
                                        (end::<fixnum> -1)
                                        (endian::<symbol>? #f))
   Scm_ReadBlockX)

 (define-cproc read-uvector (klass::<class> size::<fixnum> 
                             :optional (port::<input-port> (current-input-port))
                                       (endian::<symbol>? #f))
   (unless (Scm_SubtypeP klass SCM_CLASS_UVECTOR)
     (Scm_TypeError "class" "uniform vector class" (SCM_OBJ klass)))
   (let* ([v::ScmUVector* (cast ScmUVector* (Scm_MakeUVector klass size NULL))]
          [r (Scm_ReadBlockX v port 0 size endian)])
     (if (SCM_EOFP r)
       (return r)
       (begin
         (SCM_ASSERT (SCM_INTP r))
         (let* ([n::ScmSmallInt (SCM_INT_VALUE r)])
           (SCM_ASSERT (and (<= n size) (<= 0 n)))
           ;; NB: If read size is a lot shorter than requested size, we may
           ;; want to copy it instead of just keeping the rest of vector
           ;; unused.
           (if (< n size)
             (return (Scm_UVectorAlias klass v 0 n))
             (return (SCM_OBJ v))))))))

 (define-cproc write-uvector (v::<uvector>
                              :optional (port::<output-port> (current-output-port))
                                        (start::<fixnum> 0)
                                        (end::<fixnum> -1)
                                        (endian::<symbol>? #f))
   Scm_WriteBlock)
 )

;; copy
(inline-stub
 (define-cproc uvector-copy! (dest::<uvector> dstart::<int> src::<uvector>
                              :optional (sstart::<fixnum> 0)
                                        (send::<fixnum> -1))
   ::<void>
   (SCM_UVECTOR_CHECK_MUTABLE dest)
   (SCM_CHECK_START_END sstart send (SCM_UVECTOR_SIZE src))
   (let* ([deltsize::int (Scm_UVectorElementSize (Scm_ClassOf (SCM_OBJ dest)))]
          [doff::ScmSmallInt (* dstart deltsize)]
          [seltsize::int (Scm_UVectorElementSize (Scm_ClassOf (SCM_OBJ src)))]
          [soff::ScmSmallInt (* sstart seltsize)]
          [size::ScmSmallInt (- (* send seltsize) soff)])
     (memmove (+ (cast char* (SCM_UVECTOR_ELEMENTS dest)) doff)
              (+ (cast (const char*) (SCM_UVECTOR_ELEMENTS src)) soff)
              size)))
 )

;; String operations
(inline-stub
 ;; A common operation to extract range of char* from the input string S.
 ;; START and END may be adjusted.
 ;; SP and EP are const char* variable that gets start and end pointers.
 (define-cise-stmt with-input-string-pointers
   [(_ (s start end sp ep) . body)
    (let ([sb (gensym)] [size (gensym)] [len (gensym)] [ss (gensym)])
      `(let* ([,sb :: (const ScmStringBody*) (SCM_STRING_BODY ,s)]
              [,size :: ScmSize (SCM_STRING_BODY_SIZE ,sb)]
              [,len :: ScmSize (SCM_STRING_BODY_LENGTH ,sb)]
              [,ss :: (const char*) (SCM_STRING_BODY_START ,sb)])
         (SCM_CHECK_START_END ,start ,end (cast int ,len))
         (let* ([,sp :: (const char*)
                     (?: (== ,start 0)
                         ,ss
                         (Scm_StringBodyPosition ,sb ,start))]
                [,ep :: (const char*)
                     (?: (== ,end ,len)
                         (+ ,ss ,size)
                         (Scm_StringBodyPosition ,sb ,end))])
           ,@body)))])
 
 (define-cfn string->bytevector (klass::ScmClass* 
                                 s::ScmString* 
                                 start::ScmSmallInt
                                 end::ScmSmallInt
                                 immutable::int)
   :static
   (with-input-string-pointers (s start end sp ep)
     (let* ([buf::char* NULL])
       (if immutable
         (set! buf (cast char* sp))  ; Eek! drop const qualifier
         (begin
           (set! buf (SCM_NEW_ATOMIC2 (char*) (- ep sp)))
           (memcpy buf sp (- ep sp))))
       (return (Scm_MakeUVectorFull klass (cast ScmSmallInt (- ep sp)) buf
                                    immutable NULL)))))

 (define-cproc string->s8vector
   (s::<string>
    :optional (start::<fixnum> 0) (end::<fixnum> -1) (immutable?::<boolean> #f))
   (return (string->bytevector SCM_CLASS_S8VECTOR s start end immutable?)))

 (define-cproc string->u8vector
   (s::<string>
    :optional (start::<fixnum> 0) (end::<fixnum> -1) (immutable?::<boolean> #f))
   (return (string->bytevector SCM_CLASS_U8VECTOR s start end immutable?)))

 (define-cfn string->bytevector! (v::ScmUVector* 
                                  tstart::ScmSmallInt
                                  s::ScmString*
                                  start::ScmSmallInt 
                                  end::ScmSmallInt)
   :static
   (let* ([tlen::ScmSmallInt (SCM_UVECTOR_SIZE v)])
     (when (and (>= tstart 0) (< tstart tlen))
       (SCM_UVECTOR_CHECK_MUTABLE v)
       (with-input-string-pointers (s start end sp ep)
         (let* ([buf::(char*) (+ (cast char* (SCM_UVECTOR_ELEMENTS v)) tstart)])
           (if (> (- tlen tstart) (- ep sp))
             (memcpy buf sp (- ep sp))
             (memcpy buf sp (- tlen tstart))))))
     (return (SCM_OBJ v))))

 (define-cproc string->s8vector! (v::<s8vector>
                                  tstart::<fixnum>
                                  s::<string>
                                  :optional (start::<fixnum> 0)
                                  (end::<fixnum> -1))
   (return (string->bytevector! (SCM_UVECTOR v) tstart s start end)))

 (define-cproc string->u8vector! (v::<u8vector>
                                  tstart::<fixnum>
                                  s::<string>
                                  :optional (start::<fixnum> 0)
                                  (end::<fixnum> -1))
   (return (string->bytevector! (SCM_UVECTOR v) tstart s start end)))

 (define-cfn bytevector->string (v::ScmUVector* 
                                 start::ScmSmallInt 
                                 end::ScmSmallInt
                                 term)
   :static
   (let* ([len::ScmSmallInt (SCM_UVECTOR_SIZE v)])
     ;; We automatically avoid copying the string contents when the
     ;; following conditions are met:
     ;; * The source vector is immutable
     ;; * The owner of source vector is NULL (If there's an owner such as
     ;;   mmap handle, it isn't desirable if a string points to the memory
     ;;   without keeping ownership info.)
     ;; * The resulting string is not a small fraction of a large vector.
     ;;   If so, we may waste space by retaining large chunk of memory
     ;;   most of which won't be ever used.  Here we use some heuristics:
     ;;   - If the source vector is not small (>= 256)
     ;;   - and the string covers only a fraction (1/5) or less,
     ;;   - then we copy the content.
     ;; NB: We may add a flag that force the content to be shared, for
     ;; the programs that really want to avoid allocation.
     (SCM_CHECK_START_END start end len)
     (let* ([flags::int (?: (and (SCM_UVECTOR_IMMUTABLE_P v)
                                 (== (-> v owner) NULL)
                                 (not (and (>= len 256)
                                           (<= (- end start) (/ len 5)))))
                            0
                            SCM_STRING_COPYING)])
       (when (SCM_INTP term)
         (let* ([terminator::u_char (logand #xff (SCM_INT_VALUE term))]
                [i::ScmSmallInt])
           (for [(set! i start) (< i end) (post++ i)]
             (when (== terminator
                       (aref (cast u_char* (SCM_UVECTOR_ELEMENTS v)) i))
               (set! end i)
               (break)))))
       (return (Scm_MakeString (+ (cast char* (SCM_UVECTOR_ELEMENTS v)) start)
                               (- end start) -1 flags)))))

 (define-cproc s8vector->string (v::<s8vector>
                                 :optional (start::<fixnum> 0)
                                           (end::<fixnum> -1)
                                           (terminator #f))
   (return (bytevector->string (SCM_UVECTOR v) start end terminator)))

 (define-cproc u8vector->string (v::<u8vector>
                                 :optional (start::<fixnum> 0)
                                           (end::<fixnum> -1)
                                           (terminator #f))
   (return (bytevector->string (SCM_UVECTOR v) start end terminator)))

 (define-cfn string->wordvector (klass::ScmClass* s::ScmString*
                                 start::ScmSmallInt end::ScmSmallInt
                                 endian::ScmObj)
   :static
   (unless (SCM_SYMBOLP endian)
     (set! endian (Scm_DefaultEndian)))
   (with-input-string-pointers (s start end sp ep)
     (let* ([v (Scm_MakeUVector klass (- end start) NULL)]
            [eltp::uint32_t* (cast uint32_t* (SCM_UVECTOR_ELEMENTS v))]
            [i::ScmSmallInt 0]
            [do_swap::int (SWAP_REQUIRED endian)])
       (for [() (< sp ep) (post++ i)]
         (let* ([ch::ScmChar])
           (SCM_CHAR_GET sp ch)
           (if do_swap
             (let* ([v::swap_u32_t])
               (set! (ref v val) (cast uint32_t ch))
               (SWAP_4 v)
               (set! (aref eltp i) (ref v val)))
             (set! (aref eltp i) (cast uint32_t ch)))
           (+= sp (SCM_CHAR_NBYTES ch))))
       (return v))))

 (define-cproc string->s32vector (s::<string>
                                  :optional (start::<fixnum> 0)
                                            (end::<fixnum> -1)
                                            endian)
   (return (string->wordvector SCM_CLASS_S32VECTOR s start end endian)))

 (define-cproc string->u32vector (s::<string>
                                  :optional (start::<fixnum> 0)
                                            (end::<fixnum> -1)
                                            endian)
   (return (string->wordvector SCM_CLASS_U32VECTOR s start end endian)))

 (define-cfn string->wordvector! (v::ScmUVector*
                                  tstart::ScmSmallInt
                                  s::ScmString*
                                  start::ScmSmallInt
                                  end::ScmSmallInt
                                  endian::ScmObj)
   :static
   (unless (SCM_SYMBOLP endian)
     (set! endian (Scm_DefaultEndian)))
   (let* ([tlen::ScmSmallInt (SCM_UVECTOR_SIZE v)])
     (when (and (>= tstart 0) (< tstart tlen))
       (SCM_UVECTOR_CHECK_MUTABLE v)
       (with-input-string-pointers (s start end sp ep)
         (let* ([buf::uint32_t* (cast uint32_t* (SCM_UVECTOR_ELEMENTS v))]
                [i::ScmSmallInt tstart]
                [do_swap::int (SWAP_REQUIRED endian)])
           (for [() (and (< sp ep) (< i tlen)) (post++ i)]
             (let* ([ch::ScmChar])
               (SCM_CHAR_GET sp ch)
               (if do_swap
                 (let* ([v::swap_u32_t])
                   (set! (ref v val) (cast uint32_t ch))
                   (SWAP_4 v)
                   (set! (aref buf i) (ref v val)))
                 (set! (aref buf i) (cast uint32_t ch)))
               (+= sp (SCM_CHAR_NBYTES ch)))))))
     (return (SCM_OBJ v))))

 (define-cproc string->s32vector! (v::<s32vector>
                                   tstart::<fixnum>
                                   s::<string>
                                   :optional (start::<fixnum> 0)
                                             (end::<fixnum> -1)
                                             endian)
   (return (string->wordvector! (SCM_UVECTOR v) tstart s start end endian)))

 (define-cproc string->u32vector! (v::<u32vector>
                                   tstart::<fixnum>
                                   s::<string>
                                   :optional (start::<fixnum> 0)
                                             (end::<fixnum> -1)
                                             endian)
   (return (string->wordvector! (SCM_UVECTOR v) tstart s start end endian)))

 (define-cfn wordvector->string (v::ScmUVector* 
                                 start::ScmSmallInt
                                 end::ScmSmallInt
                                 term
                                 endian)
   :static
   (unless (SCM_SYMBOLP endian)
     (set! endian (Scm_DefaultEndian)))
   (let* ([len::ScmSmallInt (SCM_UVECTOR_SIZE v)]
          [s (Scm_MakeOutputStringPort FALSE)]
          [do_swap::int (SWAP_REQUIRED endian)])
     (SCM_CHECK_START_END start end len)
     (let* ([eltp::int32_t* (cast int32_t* (SCM_UVECTOR_ELEMENTS v))])
       (while (< start end)
         (let* ([ch::ScmChar]
                [val::uint32_t (aref eltp (post++ start))])
           (if do_swap
             (let* ([v::swap_u32_t])
               (set! (ref v val) val)
               (SWAP_4 v)
               (set! ch (cast ScmChar (ref v val))))
             (set! ch (cast ScmChar val)))
           (when (and (SCM_INTP term)
                      (== (SCM_INT_VALUE term) ch))
             (break))
           (Scm_PutcUnsafe ch (SCM_PORT s)))))
     (return (Scm_GetOutputStringUnsafe (SCM_PORT s) 0))))

 (define-cproc s32vector->string (v::<s32vector>
                                  :optional (start::<fixnum> 0)
                                            (end::<fixnum> -1)
                                            (terminator #f)
                                            endian)
   (return (wordvector->string (SCM_UVECTOR v) start end terminator endian)))

 (define-cproc u32vector->string (v::<u32vector>
                                  :optional (start::<fixnum> 0)
                                            (end::<fixnum> -1)
                                            (terminator #f)
                                            endian)
   (return (wordvector->string (SCM_UVECTOR v) start end terminator endian)))
 )

;; for the bakcward compatibility
(define read-block! read-uvector!)
(define write-block write-uvector)

;; for symmetry of port->string, etc.
;; This actually uses uvector port in gauche.vport, but that's inner
;; details users shouldn't need to care.
;; TODO: Optional endian argument
(define (port->uvector iport :optional (class <u8vector>))
  (let1 p (open-output-uvector (make-uvector class 0) :extendable #t)
    (copy-port iport p)
    (get-output-uvector p :shared #t)))

;;-------------------------------------------------------------
;; Element range check (srfi-160)
;;

(define-cproc u8? (v) ::<boolean>
  (return (and (SCM_INTP v)
               (<= 0 (SCM_INT_VALUE v))
               (<  (SCM_INT_VALUE v) 256))))

(define-cproc s8? (v) ::<boolean>
  (return (and (SCM_INTP v)
               (<= -128 (SCM_INT_VALUE v))
               (<  (SCM_INT_VALUE v) 128))))

(define-cproc u16? (v) ::<boolean>
  (return (and (SCM_INTP v)
               (<= 0 (SCM_INT_VALUE v))
               (<  (SCM_INT_VALUE v) 65536))))

(define-cproc s16? (v) ::<boolean>
  (return (and (SCM_INTP v)
               (<= -32768 (SCM_INT_VALUE v))
               (<  (SCM_INT_VALUE v) 32768))))

(define-cproc u32? (v) ::<boolean>
  (let* ([oor::int])
    (cast void (Scm_GetIntegerU32Clamp v SCM_CLAMP_NONE (& oor)))
    (return oor)))

(define-cproc s32? (v) ::<boolean>
  (let* ([oor::int])
    (cast void (Scm_GetInteger32Clamp v SCM_CLAMP_NONE (& oor)))
    (return oor)))

(define-cproc u64? (v) ::<boolean>
  (let* ([oor::int])
    (cast void (Scm_GetIntegerU64Clamp v SCM_CLAMP_NONE (& oor)))
    (return oor)))

(define-cproc s64? (v) ::<boolean>
  (let* ([oor::int])
    (cast void (Scm_GetInteger64Clamp v SCM_CLAMP_NONE (& oor)))
    (return oor)))

(define-cproc f16? (v) ::<boolean> :fast-flonum (return (SCM_REALP v)))
(define-cproc f32? (v) ::<boolean> :fast-flonum (return (SCM_REALP v)))
(define-cproc f64? (v) ::<boolean> :fast-flonum (return (SCM_REALP v)))

(define-cproc c32? (v) ::<boolean> :fast-flonum (return (SCM_NUMBERP v)))
(define-cproc c64? (v) ::<boolean> :fast-flonum (return (SCM_NUMBERP v)))
(define-cproc c128? (v) ::<boolean> :fast-flonum (return (SCM_NUMBERP v)))

;;-------------------------------------------------------------
;; special coercers (most sequence methods are in uvlib.scm.tmpl
;;

(define-method coerce-to ((dst <string-meta>) (src <u8vector>))
  (u8vector->string src))
(define-method coerce-to ((dst <string-meta>) (src <s8vector>))
  (s8vector->string src))
(define-method coerce-to ((dst <u8vector-meta>) (src <string>))
  (string->u8vector src))
(define-method coerce-to ((dst <s8vector-meta>) (src <string>))
  (string->s8vector src))
(define-method coerce-to ((dst <string-meta>) (src <u32vector>))
  (u32vector->string src))
(define-method coerce-to ((dst <string-meta>) (src <s32vector>))
  (s32vector->string src))
(define-method coerce-to ((dst <u32vector-meta>) (src <string>))
  (string->u32vector src))
(define-method coerce-to ((dst <s32vector-meta>) (src <string>))
  (string->s32vector src))

;;-------------------------------------------------------------
;; Bytevector aliases (R7RS compatibility)
;;

(define (%adjust-fill-arg fill)
  (cond [(<= 0 fill 255) fill]
        [(<= -128 fill -1) (logand fill #xff)]
        [else (error "fill argument out of range" fill)]))

(define (make-bytevector len :optional (fill 0))
  (make-u8vector len (%adjust-fill-arg fill)))
(define (bytevector-fill! v fill)       ; scheme.bytevector
  (u8vector-fill! v (%adjust-fill-arg fill)))

(define-inline bytevector         u8vector)
(define-inline bytevector?        u8vector?)
(define-inline bytevector-length  u8vector-length)
(define-inline bytevector-u8-ref  u8vector-ref)
(define-inline bytevector-u8-set! u8vector-set!)
(define-inline bytevector-copy    u8vector-copy)
(define-inline bytevector-copy!   u8vector-copy!)
(define-inline bytevector-append  u8vector-append)
(define-inline bytevector=?       u8vector=?)

(define (bytevector-s8-set! v k b)      ; scheme.bytevector
  (bytevector-u8-set! v k (logand b #xff)))
(define bytevector-s8-ref         ; scheme.bytevector
  (letrec ([bytevector-s8-ref 
            (^[v k]
              (let1 b (bytevector-u8-ref v k)
                (if (>= b 128) (- b 256) b)))])
    (getter-with-setter bytevector-s8-ref bytevector-s8-set!)))
   
(define (bytevector-copy!-r6 src sstart target tstart len) ; scheme.bytevector
  (u8vector-copy! target tstart src sstart (+ sstart len)))

(define (bytevector->u8-list v) (u8vector->list v))     ; scheme.bytevector
(define (u8-list->bytevector lis) (list->u8vector lis)) ; scheme.bytevector

