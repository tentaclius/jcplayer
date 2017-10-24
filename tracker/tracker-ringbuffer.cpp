#include <iostream>
#include <vector>
#include <sstream>
#include <list>

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

// Action plan:
// [v] Set up jackd skeleton.
// [v] Try sending few single note_on note_off midi events to the gJack port in process() callback.
// [v] Signal handler to send midi stop all event.
// [v] Write the sequencer: note_on at the note appearing; note_off at the next note in the row.
// [v] Pedaled notes.
// [-] Create event vectors with pre-defined size.
// [ ] Implement bar event and sizes support.
// [ ] Find a proper way to handle delayed and timed notes.
// [ ] Process empty notes, default notes and other templates.
// [ ] Directives: volume, default, tempo etc.
// [ ] Handle loops.
// [ ] Separate threads for different tasks.
// [ ] Proper time management.
// [ ] Pattern file management: load, unload, reload of multiple patterns.

#define MIDI_NOTE_ON                   0x90
#define MIDI_NOTE_OFF                  0x80
#define MIDI_PROGRAM_CHANGE            0xC0
#define MIDI_CONTROLLER                0xB0
#define MIDI_RESET                     0xFF
#define MIDI_HOLD_PEDAL                64
#define MIDI_ALL_SOUND_OFF             0x7B
#define MIDI_ALL_MIDI_CONTROLLERS_OFF  121
#define MIDI_ALL_NOTES_OFF             123
#define MIDI_BANK_SELECT_MSB           0
#define MIDI_BANK_SELECT_LSB           32

static bool gTracingEnabled = true;

typedef jack_default_audio_sample_t sample_t;

int jack_process_cb(jack_nframes_t nframes, void *arg);
int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
void jack_shutdown_cb(void *arg);


void TRACE(const char *fmt, ...)
{
   if (!gTracingEnabled)
      return;

   va_list argP;
   va_start(argP, fmt);

   if (fmt)
   {
      fprintf(stderr, "DBG: ");
      vfprintf(stderr, fmt, argP);
   }

   va_end(argP);
}

struct MidiMessage
{
   jack_nframes_t time;
   int len;
   unsigned char data[3];

   MidiMessage(int b0, int b1, int b2, jack_nframes_t tm)
   {
      if (b0 >= 0x80 && b0 <= 0xEF)
      {
         b0 &= 0xF0;
         b0 += 0;       // channel <?>
      }

      if (b1 == -1)
      {
         len = 1;
         data[0] = b0;
      }
      else if (b2 == -1)
      {
         len = 2;
         data[0] = b0;
         data[1] = b1;
      }
      else
      {
         len = 3;
         data[0] = b0;
         data[1] = b1;
         data[2] = b2;
      }

      time = tm;
   }

   MidiMessage()
   {}
};

class JackEngine
{
   private:
      jack_ringbuffer_t *ringbuffer;
      jack_client_t     *client;

   public:
      jack_port_t   *input_port;
      jack_port_t   *output_port;

      jack_nframes_t sampleRate;

      void init()
      {
         jack_options_t options = JackNullOption;
         jack_status_t status;

         // Create the ringbuffer.
         ringbuffer = jack_ringbuffer_create(1024 * sizeof(MidiMessage));

         if ((client = jack_client_open("TestJackClient", options, &status)) == 0)
            throw "Jack server is not running.";

         jack_set_process_callback(client, jack_process_cb, (void*) this);
         jack_set_buffer_size_callback(client, jack_buffsize_cb, (void*) this);
         jack_on_shutdown(client, jack_shutdown_cb, (void*) this);

         sampleRate = jack_get_sample_rate(client);

         /* create two ports */
         input_port  = jack_port_register(client, "input",  JACK_DEFAULT_MIDI_TYPE, JackPortIsInput,  0);
         output_port = jack_port_register(client, "output", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);

         if (jack_activate(client))
            throw "cannot activate Jack client";
      }

      int writeMidiData(int b0, int b1, int b2)
      {
         MidiMessage theMessage (b0, b1, b2, jack_frame_time(client));
         TRACE("midi(%x,%x,%x) t=%d\n", b0, b1, b2, theMessage.time);

         if (jack_ringbuffer_write_space(ringbuffer) > sizeof(MidiMessage))
         {
            if (jack_ringbuffer_write(ringbuffer, (const char*) &theMessage, sizeof(MidiMessage)) != sizeof(MidiMessage))
            {
               std::cerr << "WARNING! Midi message is not written entirely." << std::endl;
               return -1;
            }
            return 0;
         }
         return 1;
      }

      void shutdown()
      {
         std::cout << "Jack shutdowns." << std::endl;
         jack_client_close(client);
      }

      friend int jack_process_cb(jack_nframes_t nframes, void *arg);
      friend int jack_buffsize_cb(jack_nframes_t nframes, void *arg);
      friend void jack_shutdown_cb(void *arg);
}
gJack;

void signalHandler(int s)
{
   std::cerr << "Signal " << s << " arrived. Shutting down." << std::endl;
   gJack.writeMidiData(MIDI_CONTROLLER, MIDI_ALL_SOUND_OFF, 0);
   usleep(100000);
   exit(1);
}

int jack_process_cb(jack_nframes_t nframes, void *arg)
{
   JackEngine *gJack = (JackEngine*) arg;

   int t = 0;
   jack_nframes_t lastFrameTime = jack_last_frame_time(gJack->client);

   // Initialize output buffer.
   void* portbuffer = jack_port_get_buffer(gJack->output_port, nframes);
   if (portbuffer == NULL)
   {
      std::cerr << "WARNING! Cannot get gJack port buffer." << std::endl;
      return -1;
   }
   jack_midi_clear_buffer(portbuffer);

   // Read the data from the ringbuffer.
   while (jack_ringbuffer_read_space(gJack->ringbuffer) >= sizeof(MidiMessage))
   {
      MidiMessage midiData;
      if (jack_ringbuffer_read(gJack->ringbuffer, (char*)&midiData, sizeof(MidiMessage)) != sizeof(MidiMessage))
      {
         std::cerr << "WARNING: Incomplete MIDI message read." << std::endl;
         continue;
      }

      t = midiData.time + nframes - lastFrameTime;
      if (t >= (int)nframes)
         break;

      if (t < 0)
         t = 0;

      jack_midi_data_t *buffer = jack_midi_event_reserve(portbuffer, t, midiData.len);
      if (buffer == NULL)
      {
         std::cerr << "WARNING! Cannot get buffer for midi content." << std::endl;
         break;
      }
      memcpy(buffer, midiData.data, midiData.len);
   }

   return 0;      
}

int jack_buffsize_cb(jack_nframes_t nframes, void *arg)
{
   return 0;
}

void jack_shutdown_cb(void *arg)
{
   JackEngine *gJack = (JackEngine*) arg;
   gJack->shutdown();
}

class Event
{
   public:
      virtual void print() = 0;     // Virtual display function for debugging.
      virtual ~Event() {}
};

struct NoteEvent : public Event
{
   unsigned pitch;
   unsigned volume;
   double   time;
   double   delay;

   NoteEvent(unsigned n, unsigned v, double tm, double dl) : pitch(n), volume(v), time(tm), delay(dl) {}
   NoteEvent() { NoteEvent(0,0,0,0); }

   NoteEvent(std::string &buf)
   {
      const int octaveLen = 12;

      pitch = 0;
      volume = 0;
      time = 0;
      delay = 0;

      std::istringstream iss (buf);

      if (buf.length() == 0)
         throw 0;

      // The note
      switch (iss.get())
      {
         case 'C':
         case 'c':
            pitch = 0;
            break;

         case 'D':
         case 'd':
            pitch = 2;
            break;

         case 'E':
         case 'e':
            pitch = 4;
            break;

         case 'F':
         case 'f':
            pitch = 5;
            break;

         case 'G':
         case 'g':
            pitch = 7;
            break;

         case 'A':
         case 'a':
            pitch = 9;
            break;

         case 'B':
         case 'b':
            pitch = 11;
            break;

         default:
            throw 0;
      }

      // Check for Flat or Sharp modifer
      if (iss.peek() == '#')
      {
         iss.get();
         pitch ++;
      }
      if (iss.peek() == 'b')
      {
         iss.get();
         pitch --;
      }

      // Read the octave number:
      unsigned octave = 0;
      if (iss >> octave)
         pitch += octave * octaveLen;
      else
         // Default is fourth octave
         pitch += 4 * octaveLen;

      // Read possible modifiers:
      char c = 0;
      iss.clear();
      while ((c = iss.get()) != EOF && c != ' ' && c != '\t')
      {
         switch (c)
         {
            case '@':
               // Playing time modifier
               iss >> time;
               break;

            case '%':
               // Delay time modifier
               iss >> delay;
               break;

            case '!':
               // Volume modifier
               iss >> volume;
               break;
         }
      }
   }

   ~NoteEvent() {}

   void print()
   {
      std::cout << "note(" << pitch << ", " << volume << ", " << time << ", " << delay << ")";
   }

   NoteEvent* clone()
   {
      return new NoteEvent(pitch, volume, time, delay);
   }
};

struct SkipEvent : public Event
{
   SkipEvent() {}
   ~SkipEvent() {}

   void print()
   {
      std::cout << "silent";
   }
};

struct BarEvent : public Event
{
   unsigned nom, div;
   
   BarEvent(unsigned n, unsigned d) : nom(n), div(d) {}
   ~BarEvent() {}

   void print()
   {
      std::cout << "bar(" << nom << "/" << div << ")";
   }
};

struct TempoEvent : public Event
{
   unsigned tempo;

   TempoEvent(unsigned t) : tempo(t) {}
   ~TempoEvent() {}

   void print()
   {
      std::cout << "tempo(" << tempo << ")";
   }
};

struct PedalEvent : public Event
{
   ~PedalEvent() {}

   void print()
   {
      std::cout << "pedal";
   }
};

struct LoopEvent : public Event
{};

struct EndLoopEvent : public Event
{};


std::vector<Event*> parseLine(std::string line)
{
   NoteEvent dfltNote;         // The default notei parameters.
   NoteEvent lastNote;         // The last note.
   std::string chunk;          // A piece of the line to read the command.
   std::istringstream iss (line);
   std::vector<Event*> eventList;

   // Return the empty list if the line is empty.
   if (line.length() == 0)
      return eventList;

   // A bar; find a number to identify the new size
   if (line[0] == '-')
   {
      unsigned i = 1;
      unsigned n = 0, d = 0;        // Nominator and divisor for the new size.
      char c = 0;

      while (i < line.length() && line[i] == '-')
         i ++;

      std::istringstream barIss (line.substr(i));

      if (barIss >> n && barIss >> c && barIss >> d)
         eventList.push_back(new BarEvent(n, d));
      else
         eventList.push_back(new BarEvent(0, 0));     // 0/0 means no changes to the size from the previous one.

      return eventList;
   }

   // Followup processing by chunks
   iss >> chunk;
   
   // Set the default note.
   if (chunk == "default")
   {
      iss >> chunk;
      try 
      {
         NoteEvent *n = new NoteEvent(chunk);
         dfltNote = *n;
         delete n;
      }
      catch (int e)
      {
         throw iss.tellg();
      }

      return eventList;
   }

   // Set the default volume.
   if (chunk == "volume")
   {
      unsigned volume;
      iss >> volume;
      dfltNote.volume = volume;
      return eventList;
   }

   // Set the tempo.
   if (chunk == "tempo")
   {
      unsigned tempo;
      iss >> tempo;
      eventList.push_back(new TempoEvent(tempo));
      return eventList;
   }

   // If nothing else, try to parse as a note
   iss.seekg(0);
   iss.clear();
   while (iss >> chunk)
   {
      try
      {
         // Comment line.
         if (chunk.length() == 0 || chunk[0] == ';')
            return eventList;
         
         // Silent note.
         if (chunk == ".")
         {
            eventList.push_back(new SkipEvent());
            continue;
         }

         // Continuing the previous note.
         if (chunk == "|")
         {
            eventList.push_back(new PedalEvent());
            continue;
         }

         // Default note
         if (chunk == "*")
         {
            eventList.push_back(dfltNote.clone());
            continue;
         }

         // And finally this must be a real note:
         eventList.push_back(new NoteEvent(chunk));
      }
      catch (int e)
      {
         throw (int) iss.tellg();
      }
   }

   return eventList;
}

int main(int argc, char **argv)
{
   std::vector<std::vector<Event*> > song;
   std::string line;
   
   // Setup signal handler.
   struct sigaction action;
   sigemptyset(&action.sa_mask);
   action.sa_flags = 0;
   action.sa_handler = signalHandler;
   action.sa_flags = 0;
   for (int signal = 1; signal < NSIG; signal ++)
      sigaction(signal, &action, 0L);

   // Init Jackd connection.
   try
   {
      gJack.init();
   }
   catch (std::string &s)
   {
      std::cout << "Error during Jack initialization: " << s << std::endl;
   }

   // Read the pattern.
   while (std::getline(std::cin, line))
   {
      try
      {
         std::vector<Event*> lst = parseLine(line);
         if (!lst.empty())
            song.push_back(lst);
      }
      catch (int e)
      {
         fprintf(stderr, "Cannot parse line: %s\n", line.c_str());
         song.push_back(std::vector<Event*> ());
      }
   }

   // Continuously play the sequence.
   std::list<std::pair<unsigned, unsigned> > activeNotes;
   while (true)
   {
      for (std::vector<std::vector<Event*> >::iterator it = song.begin(); it != song.end(); it ++)
      {
         std::list<std::pair<unsigned, unsigned> > nextActive;

         // Silence the previous notes.
         while (!activeNotes.empty())
         {
            std::pair<unsigned, unsigned> n = activeNotes.front();
            activeNotes.pop_front();

            // Check if we have a pedal event in this iteration for the previous note.
            PedalEvent *pedal = dynamic_cast<PedalEvent*>((*it)[n.second]);
            if ((*it).size() > n.second && pedal != NULL)
               nextActive.push_back(n);
            else
               gJack.writeMidiData(MIDI_NOTE_OFF, n.first, 0);
         }
         activeNotes = nextActive;

         // Start the new notes.
         unsigned ch = 0;
         for (std::vector<Event*>::iterator jt = (*it).begin(); jt != (*it).end(); jt ++)
         {
            NoteEvent *e;
            if ((e = dynamic_cast<NoteEvent*>(*jt)) != NULL)
            {
               std::pair<unsigned, unsigned> n;
               n.first = e->pitch; // pitch;
               n.second = ch;      // channel
               activeNotes.push_back(n);

               gJack.writeMidiData(MIDI_NOTE_ON, e->pitch, 0x40);
            }

            ch ++;
         }

         usleep(500000);
      }
   }

   return 0;
}
