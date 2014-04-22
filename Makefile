CC=gcc
CFLAGS+=-g -Wall -Wextra -Wno-unused-parameter
INCLUDE+=-Iunix -Ilib -Iext
JSON = ../js0n/js0n.c ../js0n/j0g.c -I../js0n
CS1a = cs1a/aes.c cs1a/sha1.c cs1a/base64_dec.c cs1a/crypt_1a.c cs1a/uECC.c cs1a/sha256.c cs1a/base64_enc.c  cs1a/sha2_small_common.c
CS2a = -ltomcrypt -ltommath -DLTM_DESC -DCS_2a cs2a/crypt_libtom_*.c
CS3a = -Ics1a -lsodium -DCS_3a cs3a/crypt_3a.c

# this is CS1a only
ARCH = unix/platform.c cs1a/crypt_base.c $(JSON) $(CS1a) $(INCLUDE) $(LIBS)

# this is CS3a only
#ARCH = -DNOCS_1a unix/platform.c cs3a/crypt_base.c cs1a/base64*.c $(JSON) $(CS3a) $(INCLUDE) $(LIBS)

# CS1a and CS2a
#ARCH = unix/platform.c $(JSON) $(CS1a) $(CS2a) $(INCLUDE) $(LIBS)

# CS1a and CS3a
#ARCH = unix/platform.c cs3a/crypt_base.c $(JSON) $(CS1a) $(CS3a) $(INCLUDE) $(LIBS)

# all
#ARCH = unix/platform.c $(JSON) $(CS1a) $(CS2a) $(CS3a) $(INCLUDE) $(LIBS)


LIBS+=
all: idgen ping seed tft

test:
	$(CC) $(CFLAGS) -o bin/test util/test.c lib/packet.c lib/crypt.c lib/util.c $(ARCH)

idgen:
	$(CC) $(CFLAGS) -o bin/idgen util/idgen.c lib/packet.c lib/crypt.c lib/util.c $(ARCH)

ping:
	$(CC) $(CFLAGS) -o bin/ping util/ping.c lib/*.c unix/util.c $(ARCH)

seed:
	$(CC) $(CFLAGS) -o bin/seed util/seed.c lib/*.c unix/util.c ext/*.c $(ARCH)

tft:
	$(CC) $(CFLAGS) -o bin/tft util/tft.c lib/*.c unix/util.c ext/*.c $(ARCH)
 
clean:
	rm -f bin/*
	rm -f id.json
