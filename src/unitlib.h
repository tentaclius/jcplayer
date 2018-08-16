#ifndef _LIB_H_
#define _LIB_H_

#include <stdint.h>

#include <queue>
#include <list>

#include "script.h"

extern uint64_t SampleRate;

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

      virtual double operator() (uint64_t t, double in = 0) = 0;
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

      double operator()(uint64_t t, double in = 0);

      void start();
      void stop();
};

/*=================================================================================*/

class SinOsc : public Generator
{
   public:
      SinOsc();
      SinOsc(double f);
      SinOsc(uint64_t t1);

      double operator()(uint64_t t, double in = 0);

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

      double operator()(uint64_t t, double in = 0);

      double phase;
      double freq;
};

#endif
