;;;
;;; netaux.scm - network interface
;;;  
;;;   Copyright (c) 2000-2007 Shiro Kawai (shiro@acm.org)
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
;;;  $Id: netaux.scm,v 1.6 2007-02-21 22:27:37 shirok Exp $
;;;

(select-module gauche.net)
(use srfi-1)

;; default backlog value for socket-listen
(define-constant DEFAULT_BACKLOG 5)

(define ipv6-capable (global-variable-bound? 'gauche.net 'sys-getaddrinfo))

(define (make-sys-addrinfo . args)
  (if ipv6-capable
    (let-keywords args ((flags    0)
                        (family   |AF_UNSPEC|)
                        (socktype 0)
                        (protocol 0))
      (make <sys-addrinfo>
        :flags (if (list? flags) (apply logior flags) flags)
        :family family :socktype socktype :protocol protocol))
    (error "make-sys-addrinfo is available on IPv6-enabled platform")))

;; Utility
(define (address->protocol-family addr)
  (case (sockaddr-family addr)
    ((unix)  |PF_UNIX|)
    ((inet)  |PF_INET|)
    ((inet6) |PF_INET6|) ;;this can't happen if !ipv6-capable
    (else (error "unknown family of socket address" addr))))

;; High-level interface.  We need some hardcoded heuristics here.

(define (make-client-socket proto . args)
  (cond ((eq? proto 'unix)
         (let-optionals* args ((path #f))
           (unless (string? path)
             (error "unix socket requires pathname, but got" path))
           (make-client-socket-unix path)))
        ((eq? proto 'inet)
         (let-optionals* args ((host #f) (port #f))
           (unless (and (string? host) (or (integer? port) (string? port)))
             (errorf "inet socket requires host name and port, but got ~s and ~s"
                     host port))
           (make-client-socket-inet host port)))
        ((is-a? proto <sockaddr>)
         ;; caller provided sockaddr
         (make-client-socket-from-addr proto))
        ((and (string? proto)
              (pair? args)
              (integer? (car args)))
         ;; STk compatibility
         (make-client-socket-inet proto (car args)))
        (else
         (error "unsupported protocol:" proto))))

(define (make-client-socket-from-addr addr)
  (let1 socket (make-socket (address->protocol-family addr) |SOCK_STREAM|)
    (socket-connect socket addr)
    socket))

(define (make-client-socket-unix path)
  (let ((address (make <sockaddr-un> :path path))
        (socket  (make-socket |PF_UNIX| |SOCK_STREAM|)))
    (socket-connect socket address)
    socket))

(define (make-client-socket-inet host port)
  (let1 err #f
    (define (try-connect address)
      (guard (e (else (set! err e) #f))
        (let1 socket (make-socket (address->protocol-family address)
                                  |SOCK_STREAM|)
          (socket-connect socket address)
          socket)))
    (let1 socket (any try-connect (make-sockaddrs host port))
      (unless socket (raise err))
      socket)))

(define (make-server-socket proto . args)
  (cond ((eq? proto 'unix)
         (let-optionals* args ((path #f))
           (unless (string? path)
             (error "unix socket requires pathname, but got" path))
           (apply make-server-socket-unix path (cdr args))))
        ((eq? proto 'inet)
         (let-optionals* args ((port #f))
           (unless (or (integer? port) (string? port))
             (error "inet socket requires port, but got" port))
           (apply make-server-socket-inet port (cdr args))))
        ((is-a? proto <sockaddr>)
         ;; caller provided sockaddr
         (apply make-server-socket-from-addr proto args))
        ((integer? proto)
         ;; STk compatibility
         (apply make-server-socket-inet proto args))
        (else
         (error "unsupported protocol:" proto))))

(define (make-server-socket-from-addr addr . args)
  (let-keywords args ((reuse-addr? #f)
                      (sock-init #f)
                      (backlog DEFAULT_BACKLOG))
    (let1 socket (make-socket (address->protocol-family addr) |SOCK_STREAM|)
      (when (procedure? sock-init)
	(sock-init socket addr))
      (when reuse-addr?
	(socket-setsockopt socket |SOL_SOCKET| |SO_REUSEADDR| 1))
      (socket-bind socket addr)
      (socket-listen socket backlog))))

(define (make-server-socket-unix path . args)
  (let-keywords args ((backlog DEFAULT_BACKLOG))
    (let ((address (make <sockaddr-un> :path path))
          (socket (make-socket |PF_UNIX| |SOCK_STREAM|)))
      (socket-bind socket address)
      (socket-listen socket backlog))))

(define (make-server-socket-inet port . args)
  (let1 addr (car (make-sockaddrs #f port))
    (apply make-server-socket-from-addr addr args)))

(define (make-server-sockets host port . args)
  (map (lambda (sockaddr) (apply make-server-socket sockaddr args))
       (make-sockaddrs host port)))

(define (make-sockaddrs host port . maybe-proto)
  (let1 proto (get-optional maybe-proto 'tcp)
    (cond (ipv6-capable
           (let* ((socktype (case proto
                              ((tcp) |SOCK_STREAM|)
                              ((udp) |SOCK_DGRAM|)
                              (else (error "unsupported protocol:" proto))))
                  (port (x->string port))
                  (hints (make-sys-addrinfo :flags |AI_PASSIVE|
                                            :socktype socktype)))
             (map (lambda (ai) (slot-ref ai 'addr))
                  (sys-getaddrinfo host port hints))))
          (else
           (let* ((proto (symbol->string proto))
                  (port (cond ((number? port) port)
                              ((sys-getservbyname port proto)
                               => (cut slot-ref <> 'port))
                              (else
                               (error "couldn't find a port number of service:"
                                      port)))))
             (if host
               (let ((hh (sys-gethostbyname host)))
                 (unless hh (error "couldn't find host: " host))
                 (map (cut make <sockaddr-in> :host <> :port port)
                      (slot-ref hh 'addresses)))
               (list (make <sockaddr-in> :host :any :port port))))))))

(define (call-with-client-socket socket proc)
  (guard (e (else (socket-close socket) (raise e)))
    (begin0
     (proc (socket-input-port socket) (socket-output-port socket))
     (socket-close socket))))



  
