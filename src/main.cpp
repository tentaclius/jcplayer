#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pthread.h>

#include <readline/readline.h>
#include <readline/history.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>

#include <jack/jack.h>

#include "exception.h"

#define _MASTER_
#include "audiounit.h"

#include "main.h"
#include "unitlib.h"

using namespace std;

// Callback function for jack buffer processing.
int jack_process_cb(jack_nframes_t nframes, void *arg);

// Callback for jack buffer size change event.
int jack_buffsize_cb(jack_nframes_t nframes, void *arg);

// Callback for jack shutdown event.
void jack_shutdown_cb(void *arg);


//=================================================================================
// < UnitLoader >
// Constructor for UnitLoader, the wrapper for sound unit files.
UnitLoader::UnitLoader(string fileName, JackEngine *jEngine)
{
   mFileName = fileName;
   mJackEngine = jEngine;

   mDlHandle = dlopen(("./" + fileName + ".so").c_str(), RTLD_NOW);
   if (mDlHandle == NULL)
      throw Exception("cannot load " + fileName + ": " + dlerror(), errno);

   jack_nframes_t *sr = (jack_nframes_t*) dlsym(mDlHandle, "SampleRate");
   (*sr) = mJackEngine->sampleRate;

   externalInit_t init = (externalInit_t) dlsym(mDlHandle, "init");
   if (init != NULL)
   {
      unit = init();
      if (unit == NULL)
         throw Exception("null pointer returned by init");
   }
   else
      throw Exception("cannot find symbol 'init'");
}

//=================================================================================
// < UnitLoader >
// Destructor
UnitLoader::~UnitLoader()
{
   delete unit;
   dlclose(mDlHandle);
}

//=================================================================================
// < UnitLoader >
// Get the name of a current file.
string UnitLoader::getName()
{
   return mFileName;
}


//=================================================================================
// < JackEngine >
// Initialize JackEngine, the interface with Jack.
void JackEngine::init()
{
   jack_options_t options = JackNullOption;
   jack_status_t status;

   if ((client = jack_client_open("TestJackClient", options, &status)) == 0)
      throw Exception("Jack server not running?");

   jack_set_process_callback(client, jack_process_cb, (void*) this);
   jack_set_buffer_size_callback(client, jack_buffsize_cb, (void*) this);
   jack_on_shutdown(client, jack_shutdown_cb, (void*) this);

   sampleRate = jack_get_sample_rate(client);
   SampleRate = sampleRate;

   // Create a ringbuffer.

   ringbuffer = jack_ringbuffer_create(2048);

   // Create two ports.
   input_port  = jack_port_register(client, "input",  JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput,  0);
   output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

   if (jack_activate(client))
      throw Exception("cannot activate Jack client");
}

//=================================================================================
// < JackEngine >
// Gracefully shut down the engine.
void JackEngine::shutdown()
{
   jack_ringbuffer_free(ringbuffer);
   
   cout << "Jack shutdown" << endl;
   jack_client_close(client);
}

//=================================================================================
// < JackEngine >
// Add a synthesizer.
size_t JackEngine::addSynth(UnitLoader *s)
{
   mUnitLoaders.push_back(s);
   return mUnitLoaders.size(); // synth's id;
}

//=================================================================================
// < JackEngine >
// Del a synthesizer by number.
void JackEngine::delNthSynth(size_t n)
{
   delete mUnitLoaders[n];
   mUnitLoaders.erase(mUnitLoaders.begin() + n);
}

//=================================================================================
// < JackEngine >
// Replace a synthesizer with a new one.
void JackEngine::replaceNthSynth(size_t n, UnitLoader *s)
{
   delete mUnitLoaders[n];
   mUnitLoaders[n] = s;
}

//=================================================================================
// < JackEngine >
// Get the synthesizer list.
Synthesizers* JackEngine::getSynths()
{
   return &mUnitLoaders;
}

//=================================================================================
// < JackEngine >
// Get n-th synth.
UnitLoader* JackEngine::nthSynth(size_t n)
{
   return mUnitLoaders[n];
}

//=================================================================================
// < JackEngine >
// Get the number of registered synthesizers.
size_t JackEngine::getSynthCount()
{
   return mUnitLoaders.size();
}

//=================================================================================
// Callback for Jack.
int jack_process_cb(jack_nframes_t nframes, void *arg)
{
   static uint64_t t = 0;
   JackEngine *jack = (JackEngine*) arg;

   jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer(jack->output_port, nframes);

   memset(out, 0, nframes * sizeof(jack_default_audio_sample_t));

   size_t readLen = nframes * sizeof(jack_default_audio_sample_t);
   char *outP = (char*)out;
   while (readLen > 0)
   {
      size_t l = jack_ringbuffer_read(jack->ringbuffer, outP, readLen);
      outP += l;
      readLen -= l;
   }

   t += nframes;

   return 0;      
}

//=================================================================================
// Callback for the buffer size change event.
int jack_buffsize_cb(jack_nframes_t nframes, void *arg)
{
   return 0;
}

//=================================================================================
// Callback for Jack shutdown.
void jack_shutdown_cb(void *arg)
{
   JackEngine *jack = (JackEngine*) arg;
   jack->shutdown();
}

//=================================================================================
// Parse and execute a UI command.
int processCommand(JackEngine *jack, char *s, bool quiet = false)
{
   istringstream iss (s);

   string cmd;
   iss >> cmd;

   /* command: load; load a shared object */
   if (cmd == "+" || cmd == "l" || cmd == "load")
   {
      string arg;

      try
      {
         iss >> arg;

         jack->addSynth(new UnitLoader(arg, jack));
         cout << jack->getSynthCount() << ": " << arg << endl;
      }
      catch (Exception &err)
      {
         if (!quiet)
            cout << err.text << endl;
      }
   }

   /* command: unload */
   else if (cmd == "-" || cmd == "u" || cmd == "unload")
   {
      unsigned n;
      iss >> n;

      if (iss.fail())
      {
         if (!quiet)
            cout << "unload: wrong input" << endl;
         return 2;
      }

      if (n > 0 && n <= jack->getSynthCount())
         jack->delNthSynth(n - 1);
      else
      {
         if (!quiet)
            cout << "Wrong id" << endl;
         return 2;
      }
   }

   /* command: replace */
   else if (cmd == "=" || cmd == "replace")
   {
      unsigned n;
      string fileName;

      iss >> n >> fileName;

      if (iss.fail())
      {
         if (!quiet)
            cout << "replace: wrong input" << endl;
         return 2;
      }

      if (n > 0 && n <= jack->getSynthCount())
      {
         UnitLoader *s;
         try
         {
            s = new UnitLoader(fileName, jack);
            jack->replaceNthSynth(n - 1, s);
            cout << n << ". " << fileName << endl;
         }
         catch (Exception &err)
         {
            if (!quiet)
               cout << "Cannot load the module: " << err.text << endl
                  << "errno: " << err.code << " (" << strerror(err.code) << ")" << endl;
            return 2;
         }

      }
   }

   /* command: control */
   else if (cmd == "c" || cmd == "ctl" || cmd == "control")
   {
      unsigned n;
      string c;
      double v;

      iss >> n;
      if (iss.fail())
      {
         if (!quiet)
            cout << "control: wrong data" << endl;
         return 2;
      }

      if (n <= 0 || n > jack->getSynthCount())
      {
         cout << "control: wrong data" << endl;
         return 2;
      }

      try
      {
         iss >> c;
         if (iss.fail())
         {
            // list all controls for the given unit
            for (controlIter_t it = jack->nthSynth(n - 1)->unit->ctlListIter();
                 it != jack->nthSynth(n - 1)->unit->ctlListEnd();
                 it ++)
            {
               cout << it->first << " = ";
               if (it->second == NULL)
                  cout << "NULL";
               else
                  cout << *it->second;
               cout << endl;
            }
            return 0;
         }

         iss >> v;
         if (iss.fail())
            // display the control's value
            cout << jack->nthSynth(n - 1)->unit->getCtl(c) << endl;
         else
            // set the new value
            jack->nthSynth(n - 1)->unit->setCtl(c, v);
      }
      catch (Exception &e)
      {
         cout << e.text << endl;
      }
   }

   /* command: list */
   else if (cmd == "." || cmd == "ls" || cmd == "list")
   {
      Synthesizers *ss = jack->getSynths();
      unsigned i = 0;
      for (Synthesizers::iterator it = ss->begin(); it != ss->end(); it ++, i ++)
         cout << (i + 1) << ": " << (*it)->getName() << endl;
   }

   /* command: quit */
   else if (cmd == "q" || cmd == "quit")
   {
      jack->shutdown();
      exit(0);
   }

   else if (cmd == "?" || cmd == "help")
   {
      if (!quiet)
         cout << "Available commands:" << endl
            << "(+ | l | load) <fileName>     -- load the module (file name without .so extension)" << endl
            << "(- | u | unload) <id>         -- unload the module by index (see list for index)" << endl
            << "(= | replace) <id> <fileName> -- replace the module with another one" << endl
            << "(c | ctl | control) <id> [<control> [<value>]]" << endl
            << "                              -- list, display or update control value for the unit id" << endl
            << "(. | ls | list)               -- list loaded modules" << endl
            << "(? | help)                    -- this help message" << endl
            << "(q | quit)                    -- exit the programm" << endl;
   }

   else if (cmd != "")
   {
      if (!quiet)
         cout << "Unrecognized input: " << cmd << endl;
      return 2;
   }

   return 0;
}

//=================================================================================
// Thread function for UI processing.
void* commandPipeThread(void *arg)
{
   FILE *fd = NULL;
   char buf[1024 + 1] = {0};

   if ((mkfifo("./jackclient.cmd", S_IFIFO|0666) != 0 && errno != EEXIST))
   {
      perror("error creating command FIFO");
      return NULL;
   }

   while ((fd = fopen("./jackclient.cmd", "r")) != NULL)
   {
      while (fgets(buf, 1024, fd))
      {
         cout << endl << "pipe command: " << buf;
         processCommand((JackEngine*) arg, buf);
      }
      fclose (fd);
   }

   cout << "FIFO error " << strerror(errno) << endl;

   return NULL;
}

//=================================================================================
// Processing function. Writes the data into the jack's ringbuffer.
void* jack_thread_func(void *arg)
{
   static uint64_t t = 0;     // Frame count.
   static const size_t writeBufSize = 1024768;
   static jack_default_audio_sample_t writeBuf[writeBufSize];

   JackEngine *jack = (JackEngine*) arg;

   while (true)
   {
      // The number of samples to write.
      size_t nframes = jack_ringbuffer_write_space(jack->ringbuffer)
         / sizeof(jack_default_audio_sample_t);

      if (nframes == 0)
      {
         // Wait and skip the rest of the loop if the buffer is full.
         usleep(100);
         continue;
      }

      for (Synthesizers::iterator it = jack->mUnitLoaders.begin(); it != jack->mUnitLoaders.end(); it ++)
         (*it)->unit->process(nframes, writeBuf, t);

      t += nframes;

      jack_ringbuffer_write(jack->ringbuffer, (const char*) writeBuf, nframes * sizeof(jack_default_audio_sample_t));
   }

   return NULL;
}

//=================================================================================
// MAIN
int main(int argc, char *argv[])
{
   pthread_t cmdThread, procThread;

   // Initialize Jack
   JackEngine jack;
   try
   {
      jack.init();
   }
   catch (Exception &e)
   {
      cout << "Error in Jack initialization: " << e.text << endl;
      exit(1);
   }

   // Start parallel processing for user input and for the ringbuffer.
   pthread_create(&cmdThread, NULL, commandPipeThread, &jack);
   pthread_create(&procThread, NULL, jack_thread_func, &jack);

   // Initialize readline library
   using_history();

   // Read command input
   while (1)
   {
      char *s = readline("> ");
      if (s == NULL)
      {
         cout << "Error reading the command line input" << endl;
         return -1;
      }

      add_history(s);
      processCommand(&jack, s);
      delete s;
   }

   jack.shutdown();
   return 0;
}
