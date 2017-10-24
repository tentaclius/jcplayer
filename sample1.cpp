ADSR adsr;
SinOsc sinOsc;
SqrOsc sqrOsc;

double gain;
double freq;

void setup()
{
   gain = 1;
   freq = 440;

   addCtl("gain", &gain);
   addCtl("freq", &freq);

   adsr.set(0.02, 0.2, 0.4, 1);
   sinOsc.freq = 440;
   sqrOsc.freq = 440;
}

double f(uint64_t t, double in)
{
   if (t % (4 * SampleRate) == 0)
      adsr.start();

   if (t % (4 * SampleRate) == SampleRate)
      adsr.stop();

   return in + (sqrOsc() * 0.4 + sinOsc() * 0.4) * adsr.f(t, 0) * gain;
}

void onControlUpdate()
{
   sinOsc.freq = freq;
   sqrOsc.freq = freq;
}
