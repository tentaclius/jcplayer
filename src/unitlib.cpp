/*
 * To Implement:
 * > Saw -- oscillators
 * > Frequency filters
 */

#include <math.h>

#include "unitlib.h"

uint64_t SampleRate;
uint64_t N;
double T;

/*=================================================================================*/
/// general arythmetic functions

inline double signum(double x)
{
   if (x > 0) return 1;
   else if (x < 0) return -1;
   else return 0;
}

inline double line(uint64_t t0, uint64_t t1, uint64_t t, double a, double b)
{
   return a + (b - a) * ((double)(t - t0)) / (t1 - t0);
}

/*=================================================================================*/
/// Scheduler

bool SchedulePtrLess::operator() (Schedule *a, Schedule *b)
{
   return a->startTime > b->startTime;
}

void Scheduler::schedule(double startTime, ScheduleFn fn)
{
   mQueue.push(new Schedule(startTime, fn));
}

void Scheduler::run(double t)
{
   while (!mQueue.empty() && t >= mQueue.top()->startTime)
   {
      mQueue.top()->fn();
      delete mQueue.top();
      mQueue.pop();
   }
}

/*=================================================================================*/
/// Generator - base class for processors

Generator::Generator()
{
   t0 = t = 0;
}

Generator::Generator(uint64_t t1)
{
   t0 = t = t1;
}

/*=================================================================================*/
/// ADSR - simple envelope generator

ADSR::ADSR()
{
   state = INACTIVE;
}

ADSR::ADSR(uint64_t t1) : AudioUnit(t1)
{
   state = INACTIVE;
}

ADSR::ADSR(double a, double d, double s, double r)
{
   set(a, d, s, r);
   state = INACTIVE;
}

ADSR* ADSR::set(double a, double d, double s, double r)
{
   A = a * SampleRate;
   D = d * SampleRate;
   S = s;
   R = r * SampleRate;
   return this;
}

double ADSR::operator()(uint64_t t, double in)
{
   if (state == ON)
   {
      if (t <= A)
         return line(0, A, t, 0, 1);
      if (t > A && t <= D)
         return line(A, D, t, 1, S);
      return S;
   }

   if (state == OFF)
   {
      if (t < R)
         return line(0, R, t, S, 0);
      else
         state = INACTIVE;
   }

   return 0;
}

void ADSR::start()
{
   state = ON;
   t = 0;
}

void ADSR::stop()
{
   state = OFF;
   t = 0;
}

/*=================================================================================*/
/// SinOsc -- sin wave oscillator class

SinOsc::SinOsc()
{
   phase = 0;
}

SinOsc::SinOsc(double f)
{
   phase = 0;
   freq = f;
}

SinOsc::SinOsc(uint64_t t) : Generator(t)
{
   phase = 0;
   freq = 0;
}

double SinOsc::operator()(uint64_t t, double in)
{
   return sin(T(t - t0) * freq * 2 * M_PI + phase);
}

/*=================================================================================*/
/// SqrOsc -- square wave oscillator

SqrOsc::SqrOsc()
{
   phase = 0;
}

SqrOsc::SqrOsc(double f)
{
   freq = f;
   phase = 0;
}

SqrOsc::SqrOsc(uint64_t t) : Generator(t)
{
   freq = 0;
   phase = 0;
}

double SqrOsc::operator()(uint64_t t, double in)
{
   return signum(sin(T(t - t0) / SampleRate * freq * 2 * M_PI + phase));
}
