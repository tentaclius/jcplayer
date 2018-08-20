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

bool globalExit = false;
pthread_mutex_t jackRingbufMtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t jackRingbufCanWrite;

// Callback function for jack buffer processing.
int jack_process_cb(jack_nframes_t nframes, void *arg);

// Callback for jack buffer size change event.
int jack_buffsize_cb(jack_nframes_t nframes, void *arg);

// Callback for jack shutdown event.
void jack_shutdown_cb(void *arg);


//=================================================================================
// < CppLoader >
// Constructor for CppLoader, the wrapper for sound unit files.
CppLoader::CppLoader(string fileName)
{
   setName(fileName);

   mDlHandle = dlopen(("./" + fileName + ".so").c_str(), RTLD_NOW);
   if (mDlHandle == NULL)
      throw Exception("cannot load " + fileName + ": " + dlerror(), errno);

   externalInit_t init = (externalInit_t) dlsym(mDlHandle, "init");
   if (init != NULL)
   {
      setUnit(unique_ptr<AudioUnit>(init()));

      if (getUnit() == nullptr)
         throw Exception("null pointer returned by init");
   }
   else
      throw Exception("cannot find symbol 'init'");
}

//=================================================================================
// < CppLoader >
// Destructor
CppLoader::~CppLoader()
{
   cout << "~CppLoader" << endl;
   dlclose(mDlHandle);
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
   jack_client_close(client);
   jack_ringbuffer_free(ringbuffer);
}

//=================================================================================
// < JackEngine >
// Add a synthesizer.
size_t JackEngine::addSynth(unique_ptr<UnitLoader> &&s)
{
   mUnitLoaders.push_back(std::move(s));
   return mUnitLoaders.size(); // synth's id;
}

//=================================================================================
// < JackEngine >
// Del a synthesizer by number.
void JackEngine::delNthSynth(size_t n)
{
   mUnitLoaders.erase(mUnitLoaders.begin() + n);
}

//=================================================================================
// < JackEngine >
// Replace a synthesizer with a new one.
void JackEngine::replaceNthSynth(size_t n, unique_ptr<UnitLoader> &&s)
{
   mUnitLoaders[n] = std::move(s);
}

//=================================================================================
void JackEngine::swapSynths(size_t n1, size_t n2)
{
   swap(mUnitLoaders[n1], mUnitLoaders[n2]);
}

//=================================================================================
// < JackEngine >
// Get the synthesizer list.
Synthesizers& JackEngine::getSynths()
{
   return mUnitLoaders;
}

//=================================================================================
// < JackEngine >
// Get n-th synth.
unique_ptr<UnitLoader>& JackEngine::nthSynth(size_t n)
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

   // Read the ringbuffer until the output buffer is filled.
   while (readLen > 0)
   {
      size_t l = jack_ringbuffer_read(jack->ringbuffer, outP, readLen);
      outP += l;        // Increment the buffer pointer.
      readLen -= l;     // Decrement the number of bytes to be read.

      // Notify that the buffer can be written
      if (l > 0)
         pthread_cond_signal(&jackRingbufCanWrite);
   }

   t += nframes;

   return 0;      
}

//=================================================================================
// Processing function. Writes the data into the jack's ringbuffer.
void* jack_thread_func(void *arg)
{
   //TODO! corrently there's no guarantee nframe is less than the buffer size!

   static uint64_t t = 0;     // Frame count.
   static const size_t writeBufSize = 1024768;
   static jack_default_audio_sample_t writeBuf[writeBufSize];

   JackEngine *jack = (JackEngine*) arg;

   while (!globalExit)
   {
      // The number of samples to write.
      size_t nframes = jack_ringbuffer_write_space(jack->ringbuffer) / sizeof(jack_default_audio_sample_t);

      if (nframes == 0)
      {
         // Wait for the buffer to be read
         pthread_mutex_lock(&jackRingbufMtx);
         pthread_cond_wait(&jackRingbufCanWrite, &jackRingbufMtx);
         pthread_mutex_unlock(&jackRingbufMtx);
         continue;
      }

      // fill the buffer with zeros
      memset((void*) writeBuf, 0, nframes * sizeof(jack_default_audio_sample_t));

      for (unique_ptr<UnitLoader> &u : jack->getSynths())
         u->getUnit()->process(nframes, writeBuf, t);

      t += nframes;

      jack_ringbuffer_write(jack->ringbuffer, (const char*) writeBuf, nframes * sizeof(jack_default_audio_sample_t));
   }

   return NULL;
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

         jack->addSynth(make_unique<CppLoader>(arg));
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
         return true;
      }

      if (n > 0 && n <= jack->getSynthCount())
         jack->delNthSynth(n - 1);
      else
      {
         if (!quiet)
            cout << "Wrong id" << endl;
         return true;
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
         return true;
      }

      if (n > 0 && n <= jack->getSynthCount())
      {
         try
         {
            jack->replaceNthSynth(n - 1, make_unique<CppLoader>(fileName));
            cout << n << ". " << fileName << endl;
         }
         catch (Exception &err)
         {
            if (!quiet)
               cout << "Cannot load the module: " << err.text << endl
                  << "errno: " << err.code << " (" << strerror(err.code) << ")" << endl;
            return true;
         }

      }
   }

   /* command: swap */
   else if (cmd == "%" || cmd == "swp" || cmd == "swap")
   {
      unsigned n1, n2;
      iss >> n1 >> n2;

      if (iss.fail())
      {
         if (!quiet)
            cout << "swap: wrong input" << endl;
         return true;
      }

      if (n1 > 0 && n1 <= jack->getSynthCount() && n2 > 0 && n2 <= jack->getSynthCount() && n1 != n2)
         jack->swapSynths(n1 - 1, n2 - 1);
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
         return true;
      }

      if (n <= 0 || n > jack->getSynthCount())
      {
         cout << "control: wrong data" << endl;
         return true;
      }

      try
      {
         iss >> c;
         if (iss.fail())
         {
            // list all controls for the given unit
            for (controlIter_t it = jack->nthSynth(n - 1)->getUnit()->ctlListIter();
                 it != jack->nthSynth(n - 1)->getUnit()->ctlListEnd();
                 it ++)
            {
               cout << it->first << " = ";
               if (it->second == NULL)
                  cout << "NULL";
               else
                  cout << *it->second;
               cout << endl;
            }
            return true;
         }

         iss >> v;
         if (iss.fail())
            // display the control's value
            cout << jack->nthSynth(n - 1)->getUnit()->getCtl(c) << endl;
         else
            // set the new value
            jack->nthSynth(n - 1)->getUnit()->setCtl(c, v);
      }
      catch (Exception &e)
      {
         cout << e.text << endl;
      }
   }

   /* command: list */
   else if (cmd == "." || cmd == "ls" || cmd == "list")
   {
      Synthesizers &ss = jack->getSynths();
      unsigned i = 0;
      for (unique_ptr<UnitLoader> &u : ss)
         cout << ++i << ": " << u->getName() << endl;
   }

   /* command: quit */
   else if (cmd == "q" || cmd == "quit")
   {
      return false;
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
      return true;
   }

   return true;
}

//=================================================================================
// Thread function for UI processing.
void* commandPipeThread(void *arg)
{
   FILE *fd = NULL;
   char buf[1024 + 1] = {0};

   if (mkfifo("./jackclient.cmd", S_IFIFO|0666) != 0 && errno != EEXIST)
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
      bool ifContinue = processCommand(&jack, s);
      delete s;

      // Exit if got the exit command
      if (!ifContinue) break;
   }

   // Try to gently stop the thread
   globalExit = true;
   pthread_cond_signal(&jackRingbufCanWrite);

   pthread_cancel(cmdThread);
   pthread_join(cmdThread, NULL);
   //pthread_cancel(procThread);
   pthread_join(procThread, NULL);

   jack.shutdown();

   return 0;
}
