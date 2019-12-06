(define pi 3.141592653589793)
(define (seconds t) (/ t (sample-rate)))

(define (f t)
  (* 0.3 (sin (+ (* 2 pi 440 (seconds t))
                 (* 40 (mouse-x) (sin (* 2 pi (* 100 (mouse-y)) (seconds t))))))) )
