#ifndef _JACKCLIENT_H_
#define _JACKCLIENT_H_

#include <iostream>
#include <memory>
#include <sys/types.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "s7/s7.h"

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

/* Class to represent a scheme engine */
class SchemeEngine
{
   private:
      SchemeEngine() { s7 = s7_init(); }
      s7_scheme *s7;

   public:
      ~SchemeEngine() { s7_quit(s7); s7 = NULL; }
      s7_scheme* get() { return s7; }

      static SchemeEngine& getInstance()
      {
         static SchemeEngine *engine = NULL;

         if (engine == NULL)
            engine = new SchemeEngine();

         return *engine;
      }

      s7_pointer loadFile(std::string fname)
      {
         return s7_load(s7, fname.c_str());
      }
};

/* AudioUnit for a scheme file */
class ScmAudioUnit : public AudioUnit
{
   private:
      SchemeEngine &mEngine;
      s7_pointer mFunction;

   public:
      ScmAudioUnit(SchemeEngine &eng, std::string name)
         : mEngine(eng)
      {
         mFunction = mEngine.loadFile((name + ".scm").c_str());
      }

      virtual double operator() (uint64_t smp, double in = 0)
      {
         s7_pointer t = s7_make_real(mEngine.get(), T(smp));
         s7_pointer i = s7_make_real(mEngine.get(), in);
         s7_pointer args = s7_list(mEngine.get(), 2, t, i);

         s7_pointer r = s7_call(mEngine.get(), mFunction, args);
         return s7_number_to_real(mEngine.get(), r);
      }
};

/* UnitLoader for a scheme file */
class ScmLoader : public UnitLoader
{
   private:
      SchemeEngine &mScmEngine;

   public:
      ScmLoader(SchemeEngine &eng, std::string name)
         : mScmEngine(eng)
      {
         setName(name);
         setUnit(std::make_unique<ScmAudioUnit>(eng, name));
      };

      ~ScmLoader() {};
};

/* Unit dispatcher function */
std::unique_ptr<UnitLoader> loadUnit(std::string name)
{
   if (access((name + ".scm").c_str(), F_OK) == 0)
      return std::make_unique<ScmLoader>(SchemeEngine::getInstance(), name);
   if (access((name + ".so").c_str(), F_OK) == 0)
      return std::make_unique<CppLoader>(name);

   throw Exception("File not found");
}

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
