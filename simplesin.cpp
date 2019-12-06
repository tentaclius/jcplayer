double operator() (uint64_t t, double in)
{
   return sin(T(t) * 440 * 2 * M_PI);
}
