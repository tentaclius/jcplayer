double gain;
double freq;

void setup()
{
   printf("Sample rate: %lu\n", SampleRate);

   gain = 0.2;
   freq = 440;

   addCtl("gain", &gain);
   addCtl("freq", &freq);
}

double operator()(uint64_t t, double in)
{
   return in + sin(T(t) * freq * 2 * M_PI) * gain;
}

void onControlUpdate()
{
}
