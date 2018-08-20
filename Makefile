SRCDIR = src
OBJDIR = obj
SHRDIR = shrobj

#NIXINC = -I/home/tentaclius/.nix-profile/include
#NIXLIB = -L/home/tentaclius/.nix-profile/lib

LIBS = -ljack -lm -ldl -lpthread -lreadline -lunitlib
LIBDIR = -L. $(NIXLIB)
INCDIR = -I$(SRCDIR) $(NIXINC)
CFLAGS = -Wall -g -std=c++11
SFLAGS = -Wall -fPIC -shared -g -std=c++11

TGT = jcplayer
OBJECTS = $(OBJDIR)/main.o \
			 $(OBJDIR)/exception.o \
			 $(OBJDIR)/audiounit.o

SHROBJECTS = $(SHRDIR)/exception.o \
				 $(SHRDIR)/audiounit.o

## build the executable
$(TGT): $(OBJECTS) libunitlib.so
	$(CXX) $(CFLAGS) $(OBJECTS) -o $(TGT) $(LIBDIR) $(LIBS)

## build an object file
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/%.h
	$(CXX) -c $(CFLAGS) -o $@ $< $(INCDIR)

## special rule for sharable objects
$(SHRDIR)/%.o: $(SRCDIR)/%.cpp $(SRCDIR)/%.h
	$(CXX) -c -fPIC $(CFLAGS) -o $@ $< $(INCDIR) $(LIBDIR)

## build the shared library file
libunitlib.so: $(SRCDIR)/unitlib.cpp $(SRCDIR)/unitlib.h
	$(CXX) $(SFLAGS) $(SRCDIR)/unitlib.cpp -o libunitlib.so $(INCDIR) $(LIBDIR)

## build a loadable module
$(JJ_MODULE).so: $(JJ_MODULE_SOURCE) $(JJ_MAIN_SOURCE) $(SRCDIR)/script.cpp $(SRCDIR)/script.h $(SHROBJECTS)
	$(CXX) $(SFLAGS) $(JJ_MAIN_SOURCE) -o $(JJ_MODULE).so $(INCDIR) $(LIBDIR) $(SHROBJECTS)

## build s7
$(OBJDIR)/s7.o: $(SRCDIR)/s7/s7.c $(SRCDIR)/s7/s7.h
	gcc -c -fPIC -I$(SRCDIR)/s7 $(SRCDIR)/s7/s7.c -o $(OBJDIR)/s7.o -g3

s7: $(SRCDIR)/s7/s7.c $(SRCDIR)/s7/s7.h
	gcc -I$(SRCDIR)/s7 $(SRCDIR)/s7/s7.c -o s7 -g3 -DWITH_MAIN -ldl -lm

## build s7 module
scheme.so: $(SRCDIR)/scheme.cpp $(SRCDIR)/unitlib.h $(SHROBJECTS) $(OBJDIR)/s7.o
	$(CXX) $(SFLAGS) $(SRCDIR)/scheme.cpp -o scheme.so $(OBJDIR)/s7.o $(INCDIR) $(LIBDIR) $(SHROBJECTS)

## test
t: libunitlib.so
	$(CXX) $(CFLAGS) $(SRCDIR)/test.cpp -o test $(LIBS) $(LIBDIR) $(INCDIR)

## remove all build files except the run files
clean:
	rm -f $(OBJDIR)/* $(SHRDIR)/*.o

## remove all build files
clear: clean
	rm -f *.so $(TGT) test

## rebuild all
re: clear $(TGT)

