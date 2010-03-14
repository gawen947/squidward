CC=gcc
RM=rm -f
INSTALL=install
SRC=squidward.c config.h
COMMIT=$(shell ./hash.sh)
CFLAGS=-std=c99 -O2
PREF=/usr/local/
BIN=$(PREF)bin/
SHARE=$(PREF)share/squidward/

all: squidward

squidward: $(SRC)
	@echo COMPILING 
	@$(CC) -DHAVE_CONFIG="1" -DCOMMIT="\"$(COMMIT)\"" $(CFLAGS) $^ -o $@
	@echo ... done.
.PHONY : clean install

clean:
	$(RM) $(OBJ) squidward

install:
	mkdir -p $(SHARE)
	$(INSTALL) default.srs $(SHARE)
	$(INSTALL) squidward $(BIN)
