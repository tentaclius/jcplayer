double f(uint64_t t, double in)
{
   t %= 440 * SampleRate;
   return sin((double)t / SampleRate * 440 * 2 * M_PI);
}
