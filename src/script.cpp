// This file is a skeleton for a user library.
// Only to be used by build.sh script.

#include <iostream>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "script.h"
#include "unitlib.h"

class MySynth : public AudioUnit
{
   public:
      JJ_MODULE_SOURCE
      MySynth() {}
};

AudioUnit* init()
{
   return new MySynth();
}
