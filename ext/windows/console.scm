;;;
;;; console.stub - console-related apis
;;;
;;;   Copyright (c) 2010-2025  Shiro Kawai  <shiro@acm.org>
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

;; NB: It appears that, on MinGW, you cannot obtain an usable console
;; output handle by GetStdHandle() if you allocated a console by
;; AllocConsole().  Maybe it is caused from the difference of startup
;; code between MinGW and MSVC.  The fact makes these procedures
;; little usable.  Nevertheless, I put it here hoping someday the
;; issue is addressed.

(in-module os.windows)

(inline-stub
(.include "gauche/extend.h"
          "gauche/class.h")
(.when (defined GAUCHE_WINDOWS)

;; ConsoleBufferHandle
;(define-cfn make_console_buffer (h::HANDLE) :static
;  (return (Scm_MakeWinHandle h 'console-buffer)))
;(define-cfn console_buffer (h) ::HANDLE :static
;  (return (Scm_WinHandle h 'console-buffer)))

(define-cise-stmt handle
  [(_ type expr)
   (let1 r (gensym)
     `(let* ([,r :: HANDLE ,expr])
        (when (== ,r INVALID_HANDLE_VALUE)
          (Scm_SysError ,#`",(car expr) failed"))
        (result (Scm_MakeWinHandle ,r ,type))))])

(define-cise-stmt check
  [(_ expr)
   (let1 r (gensym)
     `(let* ([,r :: BOOL ,expr])
        (when (== ,r 0) (Scm_SysError ,#`",(car expr) failed"))))])

(define-cise-expr DWORD [(_ expr) `(cast DWORD ,expr)])

;;
;; Console procedures
;;
(define-cproc sys-alloc-console () ::<void> (check (AllocConsole)))
(define-cproc sys-free-console () ::<void>  (check (FreeConsole)))

(define-enum CTRL_C_EVENT)
(define-enum CTRL_BREAK_EVENT)

(define-cproc sys-generate-console-ctrl-event (event::<int> pgid::<uint>)
  ::<void>
  (check (GenerateConsoleCtrlEvent (DWORD event) (DWORD pgid))))

;;
;; Console Buffers
;;

(define-enum GENERIC_READ)
(define-enum GENERIC_WRITE)
(define-enum FILE_SHARE_READ)
(define-enum FILE_SHARE_WRITE)

(define-cproc sys-create-console-screen-buffer (desired-access::<int>
                                                share-mode::<uint>
                                                inheritable::<boolean>)
  (let* ([sa::SECURITY_ATTRIBUTES])
    (when inheritable
      (= (ref sa nLength) (sizeof SECURITY_ATTRIBUTES)
         (ref sa lpSecurityDescriptor) NULL
         (ref sa bInheritHandle) TRUE))
    (handle 'console-buffer
            (CreateConsoleScreenBuffer desired-access share-mode
                                       (?: inheritable (& sa) NULL)
                                       CONSOLE_TEXTMODE_BUFFER NULL))))

(define-cproc sys-set-console-active-screen-buffer (h) ::<void>
  (check (SetConsoleActiveScreenBuffer (Scm_WinHandle h '#f))))

(define-cproc sys-scroll-console-screen-buffer (handle
                                                scroll-rectangle::<s16vector>
                                                clip-rectangle::<s16vector>?
                                                x::<short> y::<short>
                                                fill::<ulong>)
  ::<void>
  (when (< (SCM_UVECTOR_SIZE scroll-rectangle) 4)
    (Scm_Error "s16vector of minimum length 4 required for scroll-rectangle: %S"
               scroll-rectangle))
  (when (and clip-rectangle (< (SCM_UVECTOR_SIZE clip-rectangle) 4))
    (Scm_Error "s16vector of minimum length 4 required for clip-rectangle: %S"
               clip-rectangle))
  (let* ([c::COORD] [ci::CHAR_INFO])
    (= (ref c X) x (ref c Y) y)
    (memcpy (& ci) (& fill) (sizeof CHAR_INFO))
    (check (ScrollConsoleScreenBuffer
            (Scm_WinHandle handle '#f)
            (cast (SMALL_RECT*) (SCM_UVECTOR_ELEMENTS scroll-rectangle))
            (?: clip-rectangle (cast (SMALL_RECT*) (SCM_UVECTOR_ELEMENTS clip-rectangle)) NULL)
            c (& ci)))))

;;
;; Console Code Page
;;
(define-cproc sys-get-console-cp () ::<uint> GetConsoleCP)
(define-cproc sys-get-console-output-cp () ::<uint> GetConsoleOutputCP)
(define-cproc sys-set-console-cp (cp::<uint>) ::<void>
  (check (SetConsoleCP cp)))
(define-cproc sys-set-console-output-cp (cp::<uint>) ::<void>
  (check (SetConsoleOutputCP cp)))


;;
;; Console Cursor Info
;;
(define-cproc sys-get-console-cursor-info (h) ::(<int> <boolean>)
  (let* ([ci::CONSOLE_CURSOR_INFO])
    (check (GetConsoleCursorInfo (Scm_WinHandle h '#f) (& ci)))
    (result (cast int (ref ci dwSize)) (ref ci bVisible))))
(define-cproc sys-set-console-cursor-info (h size::<int> visible::<boolean>)
  ::<void>
  (let* ([ci::CONSOLE_CURSOR_INFO])
    (= (ref ci dwSize) size (ref ci bVisible) visible)
    (check (SetConsoleCursorInfo (Scm_WinHandle h '#f) (& ci)))))

(define-cproc sys-set-console-cursor-position (h x::<short> y::<short>)
  ::<void>
  (let* ([c::COORD])
    (= (ref c X) x (ref c Y) y)
    (check (SetConsoleCursorPosition (Scm_WinHandle h '#f) c))))

;;
;; Console Mode
;;
(define-enum ENABLE_LINE_INPUT)
(define-enum ENABLE_ECHO_INPUT)
(define-enum ENABLE_PROCESSED_INPUT)
(define-enum ENABLE_WINDOW_INPUT)
(define-enum ENABLE_MOUSE_INPUT)
(define-enum ENABLE_PROCESSED_OUTPUT)
(define-enum ENABLE_WRAP_AT_EOL_OUTPUT)

(define-cproc sys-get-console-mode (h) ::<uint>
  (let* ([m::DWORD 0])
    (check (GetConsoleMode (Scm_WinHandle h '#f) (& m)))
    (result m)))

(define-cproc sys-set-console-mode (h mode::<uint>) ::<void>
  (check (SetConsoleMode (Scm_WinHandle h '#f) mode)))


;;
;; Console Screen Buffer Info
;;
(define-ctype ScmWinConsoleScreenBufferInfo
  ::(.struct ScmWinConsoleScreenBufferInfoRec
             (SCM_HEADER::||
              info::CONSOLE_SCREEN_BUFFER_INFO)))

(declcode (SCM_CLASS_DECL Scm_WinConsoleScreenBufferInfoClass))

(.define SCM_WIN_CONSOLE_SCREEN_BUFFER_INFO_P (obj)
         (SCM_XTYPEP obj (& Scm_WinConsoleScreenBufferInfoClass)))
(.define SCM_WIN_CONSOLE_SCREEN_BUFFER_INFO (obj)
         (cast ScmWinConsoleScreenBufferInfo* obj))

(define-cfn make-console-screen-buffer-info () :static
  (let* ([z::ScmWinConsoleScreenBufferInfo*
          (SCM_NEW ScmWinConsoleScreenBufferInfo)])
    (SCM_SET_CLASS z (& Scm_WinConsoleScreenBufferInfoClass))
    (return (SCM_OBJ z))))

(define-cfn allocate-console-screen-buffer-info (_::ScmClass* _) :static
  (return (make-console-screen-buffer-info)))

(declare-stub-type <win:console-screen-buffer-info>
  "ScmWinConsoleScreenBufferInfo*" "CONSOLE_SCREEN_BUFFER_INFO"
  "SCM_WIN_CONSOLE_SCREEN_BUFFER_INFO_P"
  "SCM_WIN_CONSOLE_SCREEN_BUFFER_INFO")

(define-cclass <win:console-screen-buffer-info>
  "ScmWinConsoleScreenBufferInfo*" "Scm_WinConsoleScreenBufferInfoClass" ()
  ((size.x :c-name "info.dwSize.X" :type <short>)
   (size.y :c-name "info.dwSize.Y" :type <short>)
   (cursor-position.x :c-name "info.dwCursorPosition.X" :type <short>)
   (cursor-position.y :c-name "info.dwCursorPosition.Y" :type <short>)
   (attributes :c-name "info.wAttributes" :type <uint>)
   (window.left   :c-name "info.srWindow.Left" :type <short>)
   (window.top    :c-name "info.srWindow.Top" :type <short>)
   (window.right  :c-name "info.srWindow.Right" :type <short>)
   (window.bottom :c-name "info.srWindow.Bottom" :type <short>)
   (maximum-window-size.x :c-name "info.dwMaximumWindowSize.X" :type <short>)
   (maximum-window-size.y :c-name "info.dwMaximumWindowSize.Y" :type <short>))
  (allocator (c "allocate_console_screen_buffer_info")))

(define-enum FOREGROUND_BLUE)
(define-enum FOREGROUND_GREEN)
(define-enum FOREGROUND_RED)
(define-enum FOREGROUND_INTENSITY)
(define-enum BACKGROUND_BLUE)
(define-enum BACKGROUND_GREEN)
(define-enum BACKGROUND_RED)
(define-enum BACKGROUND_INTENSITY)

(define-cproc sys-get-console-screen-buffer-info (h)
  (let* ([info (make-console-screen-buffer-info)])
    (check (GetConsoleScreenBufferInfo
            (Scm_WinHandle h '#f)
            (& (-> (SCM_WIN_CONSOLE_SCREEN_BUFFER_INFO info) info))))
    (result info)))

(define-cproc sys-get-largest-console-window-size (h) ::(<int> <int>)
  (let* ([c::COORD (GetLargestConsoleWindowSize (Scm_WinHandle h '#f))])
    (result (ref c X) (ref c Y))))

(define-cproc sys-set-screen-buffer-size (h x::<short> y::<short>) ::<void>
  (let* ([c::COORD])
    (= (ref c X) x (ref c Y) y)
    (check (SetConsoleScreenBufferSize (Scm_WinHandle h '#f) c))))

;;
;; Console input
;;

(define-ctype ScmWinInputRecord
  ::(.struct ScmWinInputRecordRec
             (SCM_HEADER::||
              rec::INPUT_RECORD)))

(declcode (SCM_CLASS_DECL Scm_WinInputRecordClass))

(.define SCM_WIN_INPUT_RECORD_P (obj)
         (SCM_XTYPEP obj (& Scm_WinInputRecordClass)))
(.define SCM_WIN_INPUT_RECORD (obj)
         (cast ScmWinInputRecord* obj))

(define-cfn make-input-record () :static
  (let* ([z::ScmWinInputRecord* (SCM_NEW ScmWinInputRecord)])
    (SCM_SET_CLASS z (& Scm_WinInputRecordClass))
    (return (SCM_OBJ z))))

(define-cfn allocate-input-record (_::ScmClass* _) :static
  (return (make-input-record)))

(declare-stub-type <win:input-record>
  "ScmWinInputRecord*" "INPUT_RECORD"
  "SCM_WIN_INPUT_RECORD_P"
  "SCM_WIN_INPUT_RECORD")

(define-cclass <win:input-record>
  "ScmWinInputRecord*" "Scm_WinInputRecordClass" ()
  ((event-type              :c-name "rec.EventType" :type <int>)
   (key.down                :c-name "rec.Event.KeyEvent.bKeyDown"
                            :type <boolean>)
   (key.repeat-count        :c-name "rec.Event.KeyEvent.wRepeatCount"
                            :type <int>)
   (key.virtual-key-code    :c-name "rec.Event.KeyEvent.wVirtualKeyCode"
                            :type <int>)
   (key.virtual-scan-code   :c-name "rec.Event.KeyEvent.wVirtualScanCode"
                            :type <int>)
   (key.unicode-char        :c-name "rec.Event.KeyEvent.uChar.UnicodeChar"
                            :type <uint>)
   (key.ascii-char          :c-name "rec.Event.KeyEvent.uChar.AsciiChar"
                            :type <uint>)
   (key.control-key-state   :c-name "rec.Event.KeyEvent.dwControlKeyState"
                            :type <uint>)
   (mouse.x                 :c-name "rec.Event.MouseEvent.dwMousePosition.X"
                            :type <short>)
   (mouse.y                 :c-name "rec.Event.MouseEvent.dwMousePosition.Y"
                            :type <short>)
   (mouse.button-state      :c-name "rec.Event.MouseEvent.dwButtonState"
                            :type <uint>)
   (mouse.control-key-state :c-name "rec.Event.MouseEvent.dwControlKeyState"
                            :type <uint>)
   (mouse.event-flags       :c-name "rec.Event.MouseEvent.dwEventFlags"
                            :type <uint>)
   (window-buffer-size.x    :c-name "rec.Event.WindowBufferSizeEvent.dwSize.X"
                            :type <short>)
   (window-buffer-size.y    :c-name "rec.Event.WindowBufferSizeEvent.dwSize.Y"
                            :type <short>)
   (menu.command-id         :c-name "rec.Event.MenuEvent.dwCommandId"
                            :type <uint>)
   (focus.set-focus         :c-name "rec.Event.FocusEvent.bSetFocus"
                            :type <boolean>))
  (allocator (c "allocate_input_record")))

(define-cproc sys-get-number-of-console-input-events (h) ::<uint>
  (let* ([num::DWORD 0])
    (check (GetNumberOfConsoleInputEvents (Scm_WinHandle h '#f) (& num)))
    (result num)))

(define-cproc sys-get-number-of-console-mouse-buttons () ::<uint>
  (let* ([num::DWORD 0])
    (check (GetNumberOfConsoleMouseButtons (& num)))
    (result num)))

;; Later we may support passing input record buffers and/or
;; retrieving multiple input records at once.
(define-cise-stmt peek/read-console-input
  [(_ proc)
   `(let* ([rec (make-input-record)] [cnt::DWORD 0])
      (check (,proc (Scm_WinHandle h '#f)
                    (& (-> (SCM_WIN_INPUT_RECORD rec) rec))
                    1 (& cnt)))
      (if (== cnt 0) (result SCM_NIL) (result (list rec))))])

(define-cproc sys-peek-console-input (h)
  (peek/read-console-input PeekConsoleInput))
(define-cproc sys-read-console-input (h)
  (peek/read-console-input ReadConsoleInput))

(define-cproc sys-read-console (h buf::<uvector>) ::<uint>
  (unless (or (SCM_U8VECTORP buf) (SCM_U16VECTORP buf))
    (Scm_TypeError "buf" "u8vector or u16vector" (SCM_OBJ buf)))
  (SCM_UVECTOR_CHECK_MUTABLE buf)
  (let* ([nc::DWORD (/ (Scm_UVectorSizeInBytes buf) (sizeof TCHAR))]
         [nread::DWORD 0])
    (check (ReadConsole (Scm_WinHandle h '#f)
                        (SCM_UVECTOR_ELEMENTS buf)
                        nc (& nread) NULL))
    (result nread)))

(define-cproc sys-read-console-output (handle
                                       buf::<u32vector>
                                       w::<short> h::<short>
                                       x::<short> y::<short>
                                       region::<s16vector>)
  (when (< (SCM_UVECTOR_SIZE buf) (* w h))
    (Scm_Error "ReadConsoleOutput: buffer argument too small \
                (required at least %dx%d)" w h))
  (when (< (SCM_UVECTOR_SIZE region) 4)
    (Scm_Error "ReadConsoleOutput: region argument must be at least \
                4 elements long: %S"  region))
  (SCM_UVECTOR_CHECK_MUTABLE buf)
  (SCM_UVECTOR_CHECK_MUTABLE region)
  (let* ([siz::COORD] [coord::COORD])
    (= (ref siz X) w (ref siz Y) h
       (ref coord X) x (ref coord Y) y)
    (check (ReadConsoleOutput (Scm_WinHandle handle '#f)
                              (cast PCHAR_INFO (SCM_UVECTOR_ELEMENTS buf))
                              siz coord
                              (cast PSMALL_RECT (SCM_UVECTOR_ELEMENTS region))))
    (result (SCM_OBJ buf))))

(define-cproc sys-read-console-output-attribute (handle
                                                 buf::<u16vector>
                                                 x::<short> y::<short>)
  ::<uint>
  (let* ([len::DWORD (SCM_UVECTOR_SIZE buf)] [nread::DWORD] [coord::COORD])
    (= (ref coord X) x (ref coord Y) y)
    (check (ReadConsoleOutputAttribute (Scm_WinHandle handle '#f)
                                       (cast LPWORD (SCM_UVECTOR_ELEMENTS buf))
                                       len coord (& nread)))
    (result nread)))


(define-cproc sys-read-console-output-character (handle
                                                 len::<uint>
                                                 x::<short> y::<short>)
  ::<const-cstring>
  (when (> len USHRT_MAX)
    (Scm_Error "ReadConsoleOutputCharacter: length argument too large"))
  (let* ([coord::COORD] [nread::DWORD 0]
         [pbuf::LPTSTR (SCM_NEW_ATOMIC_ARRAY TCHAR (+ len 1))])
    (= (ref coord X) x (ref coord Y) y)
    (check (ReadConsoleOutputCharacter (Scm_WinHandle handle '#f) pbuf len coord (& nread)))
    (= (aref pbuf nread) 0)
    (result (SCM_WCS2MBS pbuf))))

(define-cproc sys-set-console-text-attribute (h attr::<ushort>) ::<void>
  (check (SetConsoleTextAttribute (Scm_WinHandle h '#f) attr)))

(define-cproc sys-set-console-window-info (h absolute::<boolean>
                                             window::<s16vector>)
  ::<void>
  (when (< (SCM_UVECTOR_SIZE window) 4)
    (Scm_Error "s16vector of minimum length 4 required for window: %S"
               window))
  (check (SetConsoleWindowInfo (Scm_WinHandle h '#f) absolute
                               (cast (SMALL_RECT*)
                                     (SCM_UVECTOR_ELEMENTS window)))))

(define-cproc sys-write-console (h s::<string>) ::<uint>
  (let* ([wcs::LPCTSTR (SCM_MBS2WCS (Scm_GetStringConst s))]
         [nwritten::DWORD 0])
    (check (WriteConsole (Scm_WinHandle h '#f)
                         wcs
                         (_tcslen wcs)
                         (& nwritten) NULL))
    (result nwritten)))

(define-cproc sys-write-console-output-character (h s::<string>
                                                    x::<short> y::<short>)
  ::<uint>
  (let* ([wcs::LPCTSTR (SCM_MBS2WCS (Scm_GetStringConst s))]
         [c::COORD] [nwritten::DWORD 0])
    (= (ref c X) x (ref c Y) y)
    (check (WriteConsoleOutputCharacter (Scm_WinHandle h '#f)
                                        wcs
                                        (_tcslen wcs)
                                        c (& nwritten)))
    (result nwritten)))

(define-cproc sys-fill-console-output-character (h c::<char> len::<uint>
                                                   x::<short> y::<short>)
  ::<uint>
  (let* ([ch::ScmChar (Scm_CharToUcs c)]
         [coord::COORD] [nwritten::DWORD 0])
    (= (ref coord X) x (ref coord Y) y)
    (check (FillConsoleOutputCharacter (Scm_WinHandle h '#f)
                                        (cast TCHAR ch)
                                        len
                                        coord (& nwritten)))
    (result nwritten)))

(define-cproc sys-fill-console-output-attribute (h attr::<ushort> len::<uint>
                                                   x::<short> y::<short>)
  ::<uint>
  (let* ([c::COORD] [nwritten::DWORD 0])
    (= (ref c X) x (ref c Y) y)
    (check (FillConsoleOutputAttribute (Scm_WinHandle h '#f)
                                        attr
                                        len
                                        c (& nwritten)))
    (result nwritten)))

(define-cproc sys-flush-console-input-buffer (h) ::<void>
  (check (FlushConsoleInputBuffer (Scm_WinHandle h '#f))))

;;
;; Console Title
;;
(define-cproc sys-get-console-title () ::<const-cstring>
  (let* ([buf::(.array TCHAR (1024))]
         [r::DWORD (GetConsoleTitle buf 1023)])
    (when (== r 0) (Scm_SysError "GetConsoleTitle failed"))
    (= (aref buf 1023) 0)
    (result (SCM_WCS2MBS buf))))

(define-cproc sys-set-console-title (s::<string>) ::<void>
  (let* ([wcs::LPCTSTR (SCM_MBS2WCS (Scm_GetStringConst s))])
    (when (>= (_tcslen wcs) 1024)
      (Scm_Error "SetConsoleTitle: string argument too long"))
    (check (SetConsoleTitle wcs))))

;;
;; Std Handles
;;
(define-enum STD_INPUT_HANDLE)
(define-enum STD_OUTPUT_HANDLE)
(define-enum STD_ERROR_HANDLE)

(define-cproc sys-get-std-handle (which::<int>)
  (let* ([h::HANDLE (GetStdHandle (DWORD which))])
    (when (== h INVALID_HANDLE_VALUE)
      (Scm_SysError "GetStdHandle failed"))
    (let* ([p::HANDLE (GetCurrentProcess)]
           [h2::HANDLE INVALID_HANDLE_VALUE])
      (unless (DuplicateHandle p h p (& h2) 0 TRUE DUPLICATE_SAME_ACCESS)
        (Scm_SysError "DuplicateHandle failed"))
      (result (Scm_MakeWinHandle h2 '#f)))))
(define-cproc sys-set-std-handle (which::<int> handle) ::<void>
  (check (SetStdHandle (DWORD which) (Scm_WinHandle handle '#f))))

;; not supported yet
;;  SetConsoleHandler - Windows API doesn't allow passing extra user data
;;  WriteConsoleInput
;;  WriteConsoleOutput
;;  WriteConsoleOutputAttribute

) ;; defined(GAUCHE_WINDOWS)
) ;; inline-stub

;; Local variables:
;; mode: scheme
;; end:
