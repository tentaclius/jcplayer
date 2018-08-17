#include "exception.h"
#include "audiounit.h"

//=================================================================================
// Default constructor
AudioUnit::AudioUnit()
{
   t0 = 0;
}

//=================================================================================
// Constructor with initial time value.
AudioUnit::AudioUnit(uint64_t t)
{
   t0 = t;
}

//=================================================================================
// Process a buffer of nframes samples.
// Virtual. Can be overloaded in a program for detailed control.
int AudioUnit::process(jack_nframes_t nframes, sample_t *out, uint64_t t)
{
   for (jack_nframes_t i = 0; i < nframes; i ++)
      out[i] = this->operator()(t + i, out[i]);

   return 0;
}

//=================================================================================
// Modifying controls.
// Virtual. To be overloaded.
void AudioUnit::onControlUpdate()
{
}

//=================================================================================
// Adds a control to the hashmap.
void AudioUnit::addCtl(std::string control, double *ptr)
{
   controls[control] = ptr;   
}

//=================================================================================
// Assign a value to a control.
void AudioUnit::setCtl(std::string control, double value)
{
   controlIter_t el = controls.find(control);
   if (el != controls.end())
   {
      double *ptr = el->second;
      if (ptr != NULL)
      {
         *ptr = value;
         onControlUpdate();
      }
   }
   else
      throw Exception("Unsupported control");
}

//=================================================================================
// Get a value of a control.
double AudioUnit::getCtl(std::string control)
{
   controlIter_t el = controls.find(control);
   if (el != controls.end())
   {
      double *ptr = el->second;
      return *ptr;
   }
   else
   {
      throw Exception("Unsupported control");
      return 0;
   }
}

//=================================================================================
// Returns an iterator over the control list.
controlIter_t AudioUnit::ctlListIter()
{
   return controls.begin();
}

//=================================================================================
// Iterator end for the control list.
controlIter_t AudioUnit::ctlListEnd()
{
   return controls.end();
}
