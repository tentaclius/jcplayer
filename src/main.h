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
typedef std::vector<std::unique_ptr<UnitLoader>> Synthesizers;

// Purely virtual class/interface for a unit loader.
class UnitLoader
{
   private:
      std::string mName;
      std::unique_ptr<AudioUnit> mAudioUnit;

   protected:
      void setName(std::string name) { mName = name; }
      void setUnit(std::unique_ptr<AudioUnit> &&unit) { mAudioUnit = std::move(unit); }

   public:
      virtual ~UnitLoader() {} 
      std::string getName() { return mName; }
      std::unique_ptr<AudioUnit>& getUnit() { return mAudioUnit; }
};

/* An envelope class to load and store details for a given compiled SO plugin */
class CppLoader : public UnitLoader
{
   private:
      void *mDlHandle;

   public:
      CppLoader(std::string fileName);
      ~CppLoader();
};

//class ScmLoader : public UnitLoader
//{
//   private:
//      std::shared_ptr<SchemeEngine> mScmEngine;
//
//   public:
//      ScmLoader();
//      ~ScmLoader();
//};

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

      size_t addSynth(std::unique_ptr<UnitLoader> &&synth);
      void delNthSynth(size_t n);
      void replaceNthSynth(size_t n, std::unique_ptr<UnitLoader> &&synth);
      void swapSynths(size_t n1, size_t n2);
      Synthesizers& getSynths();
      std::unique_ptr<UnitLoader>& nthSynth(size_t n);
      size_t getSynthCount();

      friend int jack_process_cb(jack_nframes_t nframes, void *arg);
      friend int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
      friend void jack_shutdown_cb(void *arg);
      friend void* jack_thread_func(void *arg);
};
#endif
