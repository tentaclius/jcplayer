#ifndef _LIB_H_
#define _LIB_H_

#include <stdint.h>

#include <queue>
#include <list>

#include "script.h"

typedef void (*ScheduleFn)(void);

struct Schedule
{
   double startTime;
   ScheduleFn fn;

   Schedule(double time, ScheduleFn fn)
   {
      this->startTime = time;
      this->fn = fn;
   }
};

struct SchedulePtrLess
{
   bool operator() (Schedule *a, Schedule *b);
};

class Scheduler
{
   private:
      std::priority_queue<Schedule*,
                          std::vector<Schedule*>,
                          SchedulePtrLess> mQueue;

   public:
      void schedule(double startTime, ScheduleFn fn);
      void run(double t);
};

/*=================================================================================*/

class Generator
{
   protected:
      uint64_t t, t0;

   public:
      Generator();
      Generator(uint64_t t1);

      virtual double operator() (uint64_t t) = 0;
      double operator() ();
};

/*=================================================================================*/

class ADSR : public AudioUnit 
{
   private:
      uint64_t t;
      uint64_t A, D, R;
      double S;
      enum
      {
         ON,
         OFF,
         INACTIVE
      } state;

   public:
      ADSR();
      ADSR(uint64_t t1);
      ADSR(double a, double d, double s, double r);

      ADSR* set(double a, double d, double s, double r);

      double f(uint64_t t, double in = 0);

      void start();
      void start(uint64_t t);
      void stop();
};

/*=================================================================================*/

class SinOsc : public Generator
{
   public:
      SinOsc();
      SinOsc(double f);
      SinOsc(uint64_t t1);

      using Generator::operator();
      double operator() (uint64_t t);

      double freq;
      double phase;
};

/*=================================================================================*/

class SqrOsc : public Generator
{
   public:
      SqrOsc();
      SqrOsc(double f);
      SqrOsc(uint64_t t);

      using Generator::operator();
      double operator() (uint64_t t);

      double phase;
      double freq;
};

#endif
