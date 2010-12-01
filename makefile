include commands.mk

CFLAGS  := -std=c99 -O2 -fPIC -Wall
LDFLAGS := 

SRC  = $(wildcard *.c)
OBJ  = $(foreach obj, $(SRC:.c=.o), $(notdir $(obj)))
DEP  = $(SRC:.c=.d)

PREFIX  ?= /usr/local
DATADIR ?= $(PREFIX)/share/squidward
BINDIR  ?= $(PREFIX)/bin

ifdef DEBUG
CFLAGS += -ggdb
endif

commit = $(shell ./hash.sh)
ifneq ($(commit), UNKNOWN)
CFLAGS += -DCOMMIT="\"$(commit)\""
endif

.PHONY: all clean

all: squidward

squidward: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) -Wp,-MMD,$*.d -c $(CFLAGS) -o $@ $< 

clean:
	$(RM) $(DEP)
	$(RM) $(OBJ)
	$(RM) squidward

install:
	$(MKDIR) $(DATADIR)
	$(INSTALL_DATA) default.srs $(DATADIR)
	$(INSTALL_PROGRAM) squidward $(BINDIR)

uninstall:
	$(RM) $(BINDIR)/squidward
	$(RM) $(DATADIR)/default.srs

-include $(DEP)

