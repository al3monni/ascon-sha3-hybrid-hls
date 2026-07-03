# Makefile
# 19-Nov-11 Markku-Juhani O. Saarinen <mjos@iki.fi>

BINARY          = hashtest
OBJS     	= hash.o main.o
DIST            = hash_asc_keccak

CC              = gcc
CFLAGS		= -Wall -O3
LIBS            =
LDFLAGS         =
INCLUDES	=

$(BINARY):      $(OBJS)
		$(CC) $(LDFLAGS) -o $(BINARY) $(OBJS) $(LIBS)

.c.o:
		$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
		rm -rf $(DIST)-*.txz $(OBJS) $(BINARY) *~ 

dist:		clean
		cd ..; \
		tar cfvJ $(DIST)/$(DIST)-`date -u "+%Y%m%d%H%M00"`.txz \
			$(DIST)/*
