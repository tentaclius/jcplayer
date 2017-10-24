#include "exception.h"

Exception::Exception(std::string txt, int err)
{
   text = txt;
   code = err;
}

Exception::Exception(std::string txt)
{
   text = txt;
   code = -1;
}
