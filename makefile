include commands.mk

CFLAGS  := -std=c99 -O2 -fPIC -Wall
LDFLAGS := 

SRC  = $(wildcard *.c)
OBJ  = $(foreach obj, $(SRC:.c=.o), $(notdir $(obj)))
DEP  = $(SRC:.c=.d)

PREFIX    ?= /usr/local
LOCALEDIR ?= $(PREFIX)/share/locale
BINDIR    ?= $(PREFIX)/bin

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

install: $(install-locales)
	$(MKDIR) -p $(BINDIR)
	$(INSTALL_PROGRAM) squidward $(BINDIR)

uninstall: $(uninstall-locales)
	$(RM) $(BINDIR)/squidward.so

-include $(DEP)

