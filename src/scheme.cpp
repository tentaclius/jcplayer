#include <iostream>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "script.h"
#include "unitlib.h"

#include "s7/s7.h"

class MySynth : public AudioUnit
{
   private:
      s7_scheme *s7;
      s7_pointer f;

   public:
      MySynth()
      {
         s7 = s7_init();
         s7_load(s7, "scheme.scm");
         f = s7_eval_c_string(s7, "f");
      }

      double operator()(uint64_t sampleNum, double in)
      {
         s7_pointer t = s7_make_real(s7, T(sampleNum));
         s7_pointer i = s7_make_real(s7, in);
         s7_pointer args = s7_list(s7, 2, t, i);

         s7_pointer r = s7_call(s7, f, args);
         return s7_number_to_real(s7, r);
      }
};

AudioUnit* init()
{
   return new MySynth();
}
