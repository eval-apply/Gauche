;;;
;;; gauche.interactive - useful stuff in the interactive session
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

#!no-fold-case

(define-module gauche.interactive
  (export apropos d read-eval-print-loop print-mode
          toplevel-reader-state
          ;; autoloaded symbols follow
          info info-page info-search reload ed
          reload-modified-modules module-reload-rules reload-verbose)
  )
(select-module gauche.interactive)

(autoload gauche.uvector f64vector uvector-alias u64vector-ref)

;;;
;;; Apropos - search bound symbols matching given pattern
;;;
;;;  (apropos 'open)             print bound symbols that contains "open"
;;;                              in its name
;;;  (apropos #/^(open|close)/)  you can use regexp
;;;
;;;  (apropos 'open 'scheme)     search symbols only in a single module
;;;
;;; Apropos is implemented as macro, for it requires to get the current
;;; module which is only available at the compile time.

(define-syntax apropos
  (syntax-rules ()
    [(_ item) (%apropos item (current-module) #f)]
    [(_ item module) (%apropos item module #t)]
    ))

(define (%apropos item module stay-in-module)
  (let ([module (cond [(module? module) module]
                      [(symbol? module)
                       (or (find-module module)
                           (error "No such module: " module))]
                      [else (error "Bad object for module: " module)])]
        [matcher (cond [(symbol? item)
                        (let1 substr (symbol->string item)
                          (^[name] (string-scan name substr)))]
                       [(string? item) (^[name] (string-scan name item))]
                       [(is-a? item <regexp>) (^[name] (rxmatch item name))]
                       [else
                        (error "Bad object for item: " item)])]
        [result '()]
        [searched '()])

    (define (search mod)
      (unless (memq mod searched)
        (set! searched (cons mod searched))
        (hash-table-for-each
         (module-table mod)
         (^[symbol value]
           (when (matcher (symbol->string symbol))
             (found mod symbol))))))

    (define (visible? sym)
      (module-binds? module sym))

    (define (found module symbol)
      (push! result
             (format #f "~30s (~a~a)~%" symbol
                     (if (visible? symbol) "" "*")
                     (module-name module))))

    ;; mimics the Scm_FindBinding
    (if stay-in-module
      (search module)
      (begin (for-each (^m (for-each search (module-precedence-list m)))
                       (module-imports module))
             (for-each search (module-precedence-list module))))
    (for-each display (sort result))
    (values)
    ))

;;;
;;; Describe - describe object
;;;

;; NB: The base methods (describe (obj <top>)) and
;; (describe-slots (obj <top>)) are defined in src/libobj.scm

(define-method describe () (describe *1)) ; for convenience

(define-method describe ((s <symbol>))
  (describe-common s)
  (describe-symbol-bindings s) ;; autoloaded from gauche.modutil
  (values))

(define-method describe ((c <char>))
  (describe-common c)
  (format #t "  (U+~4,'0x, ~a)\n" (char->ucs c) (char-general-category c))
  (values))

(define-method describe ((n <integer>))
  (describe-common n)
  (when (exact? n)
    (format #t "  (#x~,,'_,8:x" n)
    (when (<= 1000 n #e1e26) ; 10^26 is approx to 2^89
      (let loop ([nn n] [unit '(_ Ki Mi Gi Ti Pi Ei Zi Yi)])
        (cond [(null? unit)]
              [(< nn 1000)
               (format #t ", ~~ ~,,,,3a~a" (floor nn) (car unit))]
              ;; I'm not sure how to round in binary-prefix system, but it's
              ;; approximation anyway, so here it goes.
              [(< nn 9950)
               (let* ([N (floor (+ nn 50))]
                      [N0 (quotient N 1000)]
                      [N1 (quotient (modulo N 1000) 100)])
                 (format #t ", ~~ ~d.~d~a" N0 N1 (cadr unit)))]
              [else (loop (/ nn 1024) (cdr unit))])))
    (when (and (<= 0 n #x10ffff)
               (let1 c (ucs->char n)
                 (or (memq (char-general-category c) '(Ll Lm Lo Lt Lu
                                                       Nd Nl No
                                                       Pc Pd Pe Pf Pi Po Ps
                                                       Sc Sk Sm So))
                     (memv c '(#\null #\alarm #\backspace #\tab #\newline
                               #\return #\escape #\space)))))
      (format #t ", ~s as char" (ucs->char n)))
    (when (and (<= 0 n (expt 2 31)))
      (format #t ", ~a as unix-time"
              (sys-strftime "%Y-%m-%dT%H:%M:%SZ" (sys-gmtime n))))
    (format #t ")\n")
    (values)))

(define-method describe ((r <rational>))
  (describe-common r)
  (when (ratnum? r)
    (format #t "  inexact: ~s\n" (inexact r))
    (and-let* ([abs-r (abs r)]
               [ (> abs-r 1) ]
               [intpart (floor abs-r)]
               [mixed `(+ ,intpart ,(- abs-r intpart))]
               [mixed. (if (negative? r) `(- ,mixed) mixed)])
      (format #t "    mixed: ~s\n" mixed.)))
  (values))

(define-method describe ((d <real>))
  (describe-common d)
  (when (flonum? d)
    (let* ([components (decode-float d)]
           [buf (f64vector d)])
      (if (finite? d)
        (format #t "  mantissa: #x~a~14,'0,'_,4:x   exponent: ~d"
                (if (positive? (vector-ref components 2)) "+" "-")
                (vector-ref components 0)
                (vector-ref components 1))
        (format #t "  ~s" d))
      (format #t "    hex: #x~16,'0,'_,4:x\n"
              (u64vector-ref (uvector-alias <u64vector> buf) 0)))
    (when (finite? d)
      (format #t "  exact: ~s\n" (exact d))))
  (values))

(define-method describe ((c <complex>))
  (describe-common c)
  (when (and (finite? (real-part c)) (finite? (imag-part c)))
    (let1 ang (angle c)
      (format #t "  polar: ~s@~s  (@~spi)\n" (magnitude c) ang
              (/ ang (* 4 (atan 1))))))
  (values))

(define-method describe ((g <generic>))
  (describe-common g)
  (describe-slots g)
  (print "methods:")
  (dolist [m (~ g'methods)]
    (let ([spnames (map class-name (~ m'specializers))]
          [has-optional? (~ m'optional)]
          [srcloc (source-location m)])
      (format #t "  ~20s ~a\n"
              (if has-optional?
                (append spnames '_) ; this works even spnames is ()
                spnames)
              (if srcloc
                (format "; ~s:~d" (car srcloc) (cadr srcloc))
                ""))))
  (and-let1 dis ((with-module gauche.object generic-dispatcher-info) g)
    (format #t "dispatcher:\n  ~s\n" dis))
  (values))

(define-method describe ((m <method>))
  (describe-common m)
  (and-let1 source (source-location m)
    (format #t "Defined at ~s:~d\n" (car source) (cadr source)))
  (describe-slots m)
  (values))

(define-method describe ((p <procedure>))
  (describe-common p)
  (and-let1 source (source-location p)
    (format #t "Defined at ~s:~d\n" (car source) (cadr source)))
  (format #t "type: ~s\n" (procedure-type p))
  (describe-slots p)
  (values))

(define-method describe ((m <macro>))
  (describe-common m)
  (and-let1 source (assq-ref (~ m'info-alist) 'source-info)
    (format #t "Defined at ~s:~d\n" (car source) (cadr source)))
  (describe-slots m)
  (values))

(define-method describe ((c <compound-condition>))
  ;; TODO: We might want to customize simple condition as well to
  ;; get better display
  (describe-common c)
  (let loop ([i 0] (cs (~ c'%conditions)))
    (unless (null? cs)
      (format #t "Condition[~d]: " i)
      (describe (car cs))
      (loop (+ i 1) (cdr cs)))))

(define d describe)

;;;
;;; Enhanced REPL
;;;

(autoload gauche.interactive.editable-reader make-editable-reader)

;; Evaluation history.
;; Kludge: We want the history variables to be visible not only in
;; #<module user> but in most other modules, so that the user can switch
;; modules in REPL without losing access to the history.  So we "inject"
;; those variables into #<module gauche>.  It is not generally recommended
;; way, though.
;;
;; We also export those history variables from gauche.base, if it has already
;; been loaded.  Moduels that imports gauche.base should see all variables
;; defined in #<module gauche>.  This runtime hack is also a kludge.

(define-in-module gauche *1 #f)
(define-in-module gauche *1+ '())
(define-in-module gauche *2 #f)
(define-in-module gauche *2+ '())
(define-in-module gauche *3 #f)
(define-in-module gauche *3+ '())
(define-in-module gauche *e #f)
(define-in-module gauche (*history)
  (display "*1: ") (repl-print *1) (newline)
  (display "*2: ") (repl-print *2) (newline)
  (display "*3: ") (repl-print *3) (newline)
  (values))
(and-let1 m (find-module 'gauche.base)
  (eval '(export *1 *1+ *2 *2+ *3 *3+ *e *history) m))

(define (%set-history-expr! r)
  (unless (null? r)
    (set! *3 *2) (set! *3+ *2+)
    (set! *2 *1) (set! *2+ *1+)
    (set! *1 (car r)) (set! *1+ r)))

(define (%set-history-exception! e) (set! *e e))

;; This is kluge for Windows deferred console creation.
(define *line-edit-ctx* #f)

;; Will be extended for fancier printer
(define (repl-print x) (write/ss x) (flush))

(define *repl-name* "gosh")

(define default-prompt-string
  (let1 user-module (find-module 'user)
    (^[:optional (delim ">")]
      (let1 m ((with-module gauche.internal vm-current-module))
        (if (eq? m user-module)
          (format "~a~a " *repl-name* delim)
          (format "~a[~a]~a " *repl-name* (module-name m) delim))))))


;; can be
;;  #f       - read edit isn't available at all
;;  editable - read edit is being used
;;  vanilla  - read edit is available, but turned off temporarily
(define *read-edit-state* #f)

(define toplevel-reader-state
  (case-lambda
    [() *read-edit-state*]
    [(mode) (ecase mode
              [(#f editable vanilla) (set! *read-edit-state* mode)])]))

;; Returns a reader procedure that can handle toplevel command.
;; READ - reads one sexpr from the REPL input
;; READ-LINE - read to the EOL from REPL input and returns a string.
;;             The newline char is read but not included in the string.
;; SKIPPER - consumes trailing whitespaces from REPL input until either
;;           first newline is read, or encounters non-whitespace character.
(define (make-repl-reader read read-line skipper)
  (^[]
    (let1 expr (read)
      (if (and (pair? expr)      ; avoid depending on util.match yet
               (eq? (car expr) 'unquote)
               (pair? (cdr expr))
               (null? (cddr expr)))
        (handle-toplevel-command (cadr expr) (read-line))
        (begin
          (unless (eof-object? expr) (skipper))
          expr)))))

;; History file, used by input editor.
;; The default history file is ~/.gosh_history.  The user can override
;; it with the environment variable GAUCHE_HISTORY_FILE, unless
;; the process is suid/sgid-ed.
;; To prohibit history saving, set an empty string to "GAUCHE_HISTORY_FILE".
(define (get-history-filename)
  (let1 h (sys-getenv "GAUCHE_HISTORY_FILE")
    (cond [(or (equal? h "") (sys-setugid?)) #f] ; do not save history file
          [h    (sys-normalize-pathname h :absolute #t :expand #t)]
          [else (sys-normalize-pathname "~/.gosh_history" :expand #t)])))

;; Returns three values, prompter, reader, and line-edit-context,
;; depending on the editable repl setting, terminal capability, and an
;; optional user-provided prompt generator.
;;
;; Editable repl setting is indicated by the variable *read-edit*.  It is
;; on by default, but can be turned off by the env var GAUCHE_NO_READ_EDIT
;; or -fno-read-edit flag.
;;
(define (make-suitable-prompter/reader/ctx given-prompter)
  ;; fallback reader when we don't use editable repl.  We sill handle
  ;; toplevel commands.
  (define vanilla-reader
    (make-repl-reader (with-module gauche.internal read-code)
                      read-line
                      consume-trailing-whitespaces))

  ;; Returns a prompt string.
  (define prompt-string-editable
    (if given-prompter
      (cut with-output-to-string given-prompter)
      (cut default-prompt-string "$")))
  (define prompt-string-noneditable
    (if given-prompter
      (cut with-output-to-string given-prompter)
      (cut default-prompt-string ">")))

  ;; Try creating editable reader.
  (receive (r rl skipper ctx)
    (if (with-module gauche.internal *read-edit*)
      (make-editable-reader prompt-string-editable
                            (get-history-filename))
      (values #f #f #f #f))
    (if (and r rl skipper ctx)
      (let1 editing-reader (make-repl-reader r rl skipper)
        (set! *read-edit-state* 'editable)
        ;; Editable repl
        (values (^[]
                  ;; We only let prompter write prompt when we're in
                  ;; vanilla mode.
                  (when (eq? *read-edit-state* 'vanilla)
                    (display (prompt-string-noneditable))))
                (^[]
                  (if (eq? *read-edit-state* 'editable)
                    (editing-reader)
                    (vanilla-reader)))
                ctx))
      ;; Non-editabl repl
      (values (^[] (display (prompt-string-noneditable)) (flush))
              vanilla-reader
              #f))))

;; error printing will be handled by the original read-eval-print-loop
(define (%evaluator expr env)
  ;; Kludge - If read edit mode is ON, the final '\n' is consumed by
  ;; the read-line/edit and the column count isn't reset.  To ensure
  ;; proper indentation of output during eval, we forcibly reset the
  ;; column count.
  (set! (~ (current-output-port)'current-column) 0)
  (guard (e [else (%set-history-exception! e) (raise e)])
    (receive r (eval expr env)
      (%set-history-expr! r)
      (apply values r))))

;; <write-controls> used for the printer.
(define-constant *default-controls*
  (make-write-controls :length 50 :level 10 :width 79 :string-length 256
                       :pretty (not (sys-getenv "GAUCHE_REPL_NO_PPRINT"))))

;; Returns printer and print-mode procedure.  Both needs to keep
;; the current write context, and needs to update them according
;; to line-edit-context.
(define (make-printer/print-mode edit-ctx)
  (define (sw)                          ;screen width
    (if edit-ctx (~ edit-ctx'screen-width) 79))
  (let* ([last-width (sw)]
         [controls (write-controls-copy *default-controls*
                                        :width last-width)])
    (define (%update-controls!)
      (unless (= last-width (sw))
        (set! last-width (sw))
        (set! controls (write-controls-copy *default-controls*
                                            :width last-width))))
    (define (printer . exprs)
      (%update-controls!)
      (dolist [expr exprs]
        (write expr controls)
        (newline)))
    (define (print-mode . args)
      (%update-controls!)
      (apply
       (case-lambda
         [() controls]                   ; return the current controls
         [(c)                            ; set controls directly
          (let1 c (if (eq? c 'default)
                    (write-controls-copy *default-controls*
                                         :width last-width)
                    c)
            (assume-type c <write-controls>)
            (rlet1 old controls
              (set! controls c)
              ;; NB: Without this, ^L won't reset the modified width.
              ;; You need to change the actual screen width to make it
              ;; reset.
              ;; However, with this, setting width with print-mode
              ;; is immediately reverted by the next printer call.
              ;; We'll think more of better interaction.
              ;;(set! last-width (~ controls'width))
              ))]
         [kvs
          (rlet1 old controls
            (set! controls (apply write-controls-copy controls kvs))
            ;;(set! last-width (~ controls'width))
            )])
       args))
    (values printer print-mode)))

(define print-mode-proc
  (make-parameter (^ _ *default-controls*)))

(define (print-mode . args)
  (apply (print-mode-proc) args))

;; This shadows gauche#read-eval-print-loop
;; NB: We ignore reader argument,  The whole point of this procedure
;; is to provide an editable reader, so the user doesn't need to customize it.
(define (read-eval-print-loop :optional (reader #f)
                                        (evaluator #f)
                                        (printer #f)
                                        (prompter #f))
  (receive (%prompter %reader ctx)
      (make-suitable-prompter/reader/ctx prompter)
    (set! *line-edit-ctx* ctx)          ;kludge
    (receive (%printer %print-mode)
        (make-printer/print-mode ctx)
      (parameterize ((print-mode-proc %print-mode))
        ((with-module gauche read-eval-print-loop)
         %reader
         (or evaluator %evaluator)
         (or printer %printer)
         %prompter)))))

;;;
;;; Misc. setup
;;;

;; EXPERIMENTAL: windows console code page support for text.line-edit
(cond-expand
 [gauche.os.windows
  (autoload os.windows
            sys-get-console-output-cp
            sys-has-windows-console?
            sys-windows-terminal?
            sys-windows-console-legacy?)
  ;; wide character settings for text.line-edit
  (if-let1 ctx *line-edit-ctx*
    ;; check if we have a windows console
    ;; (except for windows terminal (windows 10))
    (when (and (sys-has-windows-console?)
               (not (sys-windows-terminal?)))
      (case (sys-get-console-output-cp)
        [(65001)
         (set! (~ ctx 'wide-char-disp-setting 'mode) 'Surrogate)
         (set! (~ ctx 'wide-char-pos-setting  'mode) 'Surrogate)
         (set! (~ ctx 'wide-char-disp-setting 'wide-char-width) 2)
         (set! (~ ctx 'wide-char-pos-setting  'wide-char-width)
               (if (sys-windows-console-legacy?) 1 2))
         (set! (~ ctx 'wide-char-disp-setting 'surrogate-char-width) 2)
         (set! (~ ctx 'wide-char-pos-setting  'surrogate-char-width) 2)]
        [else ; 932 etc.
         (set! (~ ctx 'wide-char-disp-setting 'mode) 'Surrogate)
         (set! (~ ctx 'wide-char-pos-setting  'mode) 'Surrogate)
         (set! (~ ctx 'wide-char-disp-setting 'wide-char-width) 2)
         (set! (~ ctx 'wide-char-pos-setting  'wide-char-width) 2)
         (set! (~ ctx 'wide-char-disp-setting 'surrogate-char-width) 4)
         (set! (~ ctx 'wide-char-pos-setting  'surrogate-char-width) 4)])))]
 [else])

;; Autoload online info viewer
(autoload gauche.interactive.info info info-page info-search)

;; Autoload module reloader
(autoload gauche.reload reload reload-modified-modules
                        module-reload-rules reload-verbose)

;; Autoload editor invoker
(autoload text.external-editor ed ed-pick-file)

;; Autoload toplevel command handler
(autoload gauche.interactive.toplevel handle-toplevel-command)

;; See (describe <symbol>) above
(autoload gauche.modutil describe-symbol-bindings)

;; This might help first time users
(define-in-module user help "Type ,help (comma and help) for help")
