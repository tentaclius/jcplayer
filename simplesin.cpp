double f(uint64_t t, double in)
{
   return sin(t / SampleRate * 440 * 2 * M_PI);
}
