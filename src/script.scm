(load "lib.scm")

(define (seconds t)
  (/ t (sample-rate)) )

(define (f t)
  (if (= 0 (modulo (floor seconds t) 2)) 1 0) )
