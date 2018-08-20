(load "lib.scm")

;; square wave function (sort-of)
(define (sqr freq t) (if (> 0 (sin (* t freq 2 pi))) 1 0))
(define (rnd) (- (random 2.) 1))

(define (my-sound f t0)
  (let ((freq f)
        (l (line-down t0 0.3)))
    (lambda (t)
      (+ (* (sqr freq t) 0.5)
         (* (rnd) (* (l t) 0.5)) ))))

(define sequencer
  (make-loop-seq :A4 :E4 :G4 :D4 :F4 :C4 :E4 :B3))

(define t1 0)
(define dt 0.7)   
(define cur-freq 0)

(define my-snd (my-sound 0 0))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
(define (f t in)
  (if (>= t (+ t1 dt))
    (begin
      (set! my-snd (my-sound (note->freq (sequencer)) t))
      (set! t1 t)))

  (my-snd t))
