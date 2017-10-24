#include <iostream>

#include <stdio.h>
#include <math.h>
#include <stdlib.h>

#include "script.h"
#include "unitlib.cpp"

class MySynth : public AudioUnit
{
   public:
      JJ_MODULE_SOURCE
      MySynth() { setup(); }
};

AudioUnit* init()
{
   return new MySynth();
}
