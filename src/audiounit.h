#ifndef AUDIOUNIT_H
#define AUDIOUNIT_H

#include <iostream>
#include <algorithm>
#include <map>

#include <jack/jack.h>

typedef jack_default_audio_sample_t sample_t;

enum
{
   MSG_INT,
   MSG_TIME,
   MSG_ARG
};

typedef std::map<std::string,double*> controlMap_t;
typedef std::map<std::string,double*>::iterator controlIter_t;

class AudioUnit
{
   private:
      virtual double f(uint64_t t, double in) = 0;

   protected:
      uint64_t t0;
      controlMap_t controls;
      void addCtl(std::string s, double *ptr);

   public:
      AudioUnit();
      AudioUnit(uint64_t t);
      virtual ~AudioUnit() {};

      virtual int process(jack_nframes_t nframes, sample_t *out, uint64_t t);

      void setCtl(std::string control, double value);
      double getCtl(std::string control);
      virtual void onControlUpdate();
      controlIter_t ctlListIter();
      controlIter_t ctlListEnd();
};

#endif
