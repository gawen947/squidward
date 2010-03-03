CC=gcc
RM=rm -f
INSTALL=install
OBJS=squidward.o
PREF=/usr/local/
BIN=$(PREF)bin/
SHARE=$(PREF)share/squidward/

squidward : $(OBJ) config.h
.PHONY : clean install

config.h :
	echo "#define SRS_PATH \"$(SHARE)default.srs\"" > $@ 

clean:
	$(RM) config.h
	$(RM) $(OBJ) squidward

install:
	mkdir -p $(SHARE)
	$(INSTALL) default.srs $(SHARE)
	$(INSTALL) squidward $(BIN)
