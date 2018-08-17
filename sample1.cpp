ADSR adsr;
SinOsc sinOsc;
SqrOsc sqrOsc;

double gain;
double freq;

void setup()
{
   printf("Sample rate: %lu\n", SampleRate);

   gain = 1;
   freq = 440;

   addCtl("gain", &gain);
   addCtl("freq", &freq);

   adsr.set(0.02, 0.2, 0.4, 1);
   sinOsc.freq = 440;
   sqrOsc.freq = 440;
}

double operator()(uint64_t t, double in)
{
   /*
   if (t % (4 * SampleRate) == 0)
      adsr.start();

   if (t % (4 * SampleRate) == SampleRate)
      adsr.stop();
      */

   //return in + (sqrOsc(t) * 0.4 + sinOsc(t) * 0.4) * adsr(t, 0) * gain;

   return in + sin(T(t) * 220 * 2 * M_PI) * 0.3;
}

void onControlUpdate()
{
   sinOsc.freq = freq;
   sqrOsc.freq = freq;
}
