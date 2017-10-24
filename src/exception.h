#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <iostream>

class Exception
{
   public:
      std::string text;
      int code;

      Exception(std::string txt, int err);
      Exception(std::string txt);
};

#endif
