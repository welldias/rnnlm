CC = x86_64-linux-g++-4.6
WEIGHTTYPE = float
CFLAGS = -D WEIGHTTYPE=$(WEIGHTTYPE) -lm -O2 -Wall -funroll-loops -ffast-math
#CFLAGS = -lm -O2 -Wall

all: rnnlmlib.o options.o rnnlm

rnnlmlib.o : rnnlmlib.cpp
	$(CC) $(CFLAGS) $(OPT_DEF) -c rnnlmlib.cpp

options.o : options.cpp
	$(CC) $(CFLAGS) $(OPT_DEF) -c options.cpp

rnnlm.o : rnnlm.cpp
	$(CC) $(CFLAGS) $(OPT_DEF) -c rnnlm.cpp

rnnlm : rnnlmlib.o options.o rnnlm.o
	$(CC) $(CFLAGS) $(OPT_DEF) rnnlmlib.o options.o rnnlm.o -o rnnlm

clean:
	rm -rf *.o rnnlm
