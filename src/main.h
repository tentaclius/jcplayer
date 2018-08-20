#ifndef _JACKCLIENT_H_
#define _JACKCLIENT_H_

#include <iostream>
#include <memory>
#include <sys/types.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "unitlib.h"

class JackEngine;
class UnitLoader;

typedef jack_default_audio_sample_t sample_t;
typedef AudioUnit* (*externalInit_t) ();
typedef std::vector<std::shared_ptr<UnitLoader>> Synthesizers;

/* An envelope class to load and store details for a given plugin */
class UnitLoader
{
   void *mDlHandle;
   std::string mFileName;
   JackEngine *mJackEngine;

   public:
   UnitLoader(std::string fileName, JackEngine *jEngine);
   ~UnitLoader();

   std::string getName();

   AudioUnit *unit;
};

class SynthChain
{

};

class JackEngine
{
   private:
      Synthesizers mUnitLoaders;

   public:
      jack_port_t *input_port;
      jack_port_t *output_port;
      jack_client_t *client;
      jack_ringbuffer_t *ringbuffer;

      jack_nframes_t sampleRate;

      void init();
      void shutdown();

      size_t addSynth(std::shared_ptr<UnitLoader> synth);
      void delNthSynth(size_t n);
      void replaceNthSynth(size_t n, std::shared_ptr<UnitLoader> synth);
      void swapSynths(size_t n1, size_t n2);
      Synthesizers& getSynths();
      std::shared_ptr<UnitLoader> nthSynth(size_t n);
      size_t getSynthCount();

      friend int jack_process_cb(jack_nframes_t nframes, void *arg);
      friend int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
      friend void jack_shutdown_cb(void *arg);
      friend void* jack_thread_func(void *arg);
};
#endif
