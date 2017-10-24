#include <iostream>

#include "unitlib.h"

using namespace std;

void test1() { cout << "Hello" << endl; }
void test2() { cout << "World" << endl; }

int main(int argc, char **argv)
{
   Scheduler s;
   s.schedule(1, test2);
   s.schedule(0, test1);
   s.run(0);
   s.run(1);

   return 0;
}
