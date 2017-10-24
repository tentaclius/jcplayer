struct Bass : public Generator
{
   ADSR adsr;
   SinOsc sin1;
   SinOsc sinMod;
   uint64_t t;

   double freq;

   Bass()
   {
      t = 0;
      freq = 440;
      adsr.set(0.02, 0.2, 0.4, 1);
      sin1.freq = freq * 4;
      sinMod.freq = freq;
   }

   double operator() (uint64_t t)
   {

   }

};

void setup()
{
   gain = 1;
   freq = 440;

   addCtl("gain", &gain);
   addCtl("freq", &freq);

   adsr.set(0.02, 0.2, 0.4, 1);
   sinOsc.freq = 440;
}

double f(uint64_t t, double in)
{
   return in + sinOsc() * gain;
}

void onControlUpdate()
{
   sinOsc.freq = freq;
}
