EXT	= os3
PROG	= csomaker.$(EXT)
ARCH	= m68k-amigaos
CPU	= -m68020-60
OPTS	= $(CPU) -noixemul -msmall-code #-fbaserel -resident
CC	= $(ARCH)-gcc
LIBS	= -lz -Wl,-Map,$@.map,--cref
WARNS	= -W -Wall -Winline
CFLAGS	= -O3 -funroll-loops -fomit-frame-pointer $(OPTS) $(WARNS) $(DEFINES)
LDFLAGS	= $(CFLAGS)
OBJDIR	= .objs_$(ARCH)
RM	= rm -frv

ifdef DEBUG
	CFLAGS += -g -DDEBUG
endif

OBJS =	\
	$(OBJDIR)/csomaker.o

all: $(PROG)

$(PROG): $(OBJDIR) $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $(OBJS) $(LIBS)

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/%.o: %.c
	@echo Compiling $@
	$(CC) $(CFLAGS) -c $< -o $@

aros:
	$(MAKE) ARCH=i686-aros OPTS="-DAROS" EXT=aros

os4:
	$(MAKE) ARCH=ppc-amigaos OPTS="" EXT=os4

clean:
	rm -f .objs*/*.o
