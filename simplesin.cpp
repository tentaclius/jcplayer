void setup()
{
}

double f(uint64_t t, double in)
{
   return sin((double)t / SampleRate * 440 * 2 * M_PI);
}

