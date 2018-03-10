# Variables to override
#
# CC            C compiler
# CROSSCOMPILE	crosscompiler prefix, if any
# CFLAGS	compiler flags for compiling all C files
# ERL_CFLAGS	additional compiler flags for files using Erlang header files
# ERL_EI_LIBDIR path to libei.a
# LDFLAGS	linker flags for linking all binaries
# ERL_LDFLAGS	additional linker flags for projects referencing Erlang libraries

PROGRAMS = main
SOURCES = $(PROGRAMS:=.cpp)

# Objects to compile
OBJECTS=RF24.o spi.o bcm2835.o interrupt.o

# Check that we're on a supported build platform
ifeq ($(CROSSCOMPILE),)
    # Not crosscompiling, so check that we're on Linux.
    ifneq ($(shell uname -s),Linux)
        $(error RF24L01 driver only works on Linux. Crosscompiling is possible if $$CROSSCOMPILE is set.)
    endif
endif

# Look for the EI library and header files
ERL_BASE_DIR ?= $(shell dirname $(shell which erl|sed -e 's~/erts-[^/]*~~'))/..
ERL_EI_INCLUDE_DIR ?= $(shell find $(ERL_BASE_DIR) -name ei.h -printf '%h\n' 2> /dev/null | head -1)
ERL_EI_LIBDIR ?= $(shell find $(ERL_BASE_DIR) -name libei.a -printf '%h\n' 2> /dev/null | head -1)

ifeq ($(ERL_EI_INCLUDE_DIR),)
   $(error Could not find include directory for ei.h. Check that Erlang header files are available)
endif
ifeq ($(ERL_EI_LIBDIR),)
   $(error Could not find libei.a. Check your Erlang installation)
endif

# Set Erlang-specific compile and linker flags
ERL_CFLAGS ?= -I$(ERL_EI_INCLUDE_DIR)
ERL_LDFLAGS ?= -L$(ERL_EI_LIBDIR) -lei

$(info $$ERL_CFLAGS is [${ERL_CFLAGS}])
$(info $$ERL_LDFLAGS is [${ERL_LDFLAGS}])

LDFLAGS +=
#CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter
#CC ?= $(CROSSCOMPILER)gcc

CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++
CFLAGS=-march=armv6zk -mtune=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard -O2 -Wall -Wextra -Wno-unused-parameter -pthread 

#$ERL_CFLAGS is [-I/usr/local/bin/../lib/erlang/usr/include]
#$ERL_LDFLAGS is [-L/usr/local/bin/../lib/erlang/usr/lib -lei]

all: priv/rf24

%.o: %.c
	$(CC) -c  $(ERL_CFLAGS) $(CFLAGS) -o $@ $<

%.o: %.cpp
	$(CXX) -c $(ERL_CFLAGS) $(CFLAGS) -o $@ $<

priv/rf24: src/main.o src/bcm2835.o src/RF24.o src/interrupt.o src/spi.o
	@mkdir -p priv
	$(CXX) $^ $(ERL_LDFLAGS) $(LDFLAGS) -lpthread -o $@

clean:
	rm -f priv/rf24 src/*.o





