# defaults commands
CC=gcc
RM=rm -f
INSTALL=install
INSTALL_PROGRAM=install -s -m 555
INSTALL_DATA=$(INSTALL) -m 444
INSTALL_SCRIPT=$(INSTALL_PROGRAM)
MKDIR=mkdir
CP=cp
