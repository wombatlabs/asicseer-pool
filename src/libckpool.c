/*
 * Copyright 2011-2014 Con Kolivas
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.  See COPYING for more details.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "ckpool.h"
#include "libckpool.h"

void keep_sockalive(int fd)
{
	const int tcp_one = 1;
	const int tcp_keepidle = 45;
	const int tcp_keepintvl = 30;
	int flags = fcntl(fd, F_GETFL, 0);

	fcntl(fd, F_SETFL, O_NONBLOCK | flags);

	setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (const void *)&tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_NODELAY, (const void *)&tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &tcp_one, sizeof(tcp_one));
	setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &tcp_keepidle, sizeof(tcp_keepidle));
	setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &tcp_keepintvl, sizeof(tcp_keepintvl));
}

/* Align a size_t to 4 byte boundaries for fussy arches */
void align_len(size_t *len)
{
	if (*len % 4)
		*len += 4 - (*len % 4);
}

/* Adequate size s==len*2 + 1 must be alloced to use this variant */
void __bin2hex(uchar *s, const uchar *p, size_t len)
{
	static const char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
	int i;

	for (i = 0; i < (int)len; i++) {
		*s++ = hex[p[i] >> 4];
		*s++ = hex[p[i] & 0xF];
	}
	*s++ = '\0';
}

/* Returns a malloced array string of a binary value of arbitrary length. The
 * array is rounded up to a 4 byte size to appease architectures that need
 * aligned array  sizes */
void *bin2hex(const uchar *p, size_t len)
{
	size_t slen;
	uchar *s;

	slen = len * 2 + 1;
	align_len(&slen);
	s = calloc(slen, 1);
	if (likely(s))
		__bin2hex(s, p, len);

	/* Returns NULL if calloc failed. */
	return s;
}

static const int hex2bin_tbl[256] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
};

/* Does the reverse of bin2hex but does not allocate any ram */
bool hex2bin(uchar *p, const uchar *hexstr, size_t len)
{
	int nibble1, nibble2;
	uchar idx;
	bool ret = false;

	while (*hexstr && len) {
		if (unlikely(!hexstr[1]))
			return ret;

		idx = *hexstr++;
		nibble1 = hex2bin_tbl[idx];
		idx = *hexstr++;
		nibble2 = hex2bin_tbl[idx];

		if (unlikely((nibble1 < 0) || (nibble2 < 0)))
			return ret;

		*p++ = (((uchar)nibble1) << 4) | ((uchar)nibble2);
		--len;
	}

	if (likely(len == 0 && *hexstr == 0))
		ret = true;
	return ret;
}

static const int b58tobin_tbl[] = {
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
	-1,  0,  1,  2,  3,  4,  5,  6,  7,  8, -1, -1, -1, -1, -1, -1,
	-1,  9, 10, 11, 12, 13, 14, 15, 16, -1, 17, 18, 19, 20, 21, -1,
	22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1,
	-1, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, -1, 44, 45, 46,
	47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57
};

/* b58bin should always be at least 25 bytes long and already checked to be
 * valid. */
void b58tobin(uchar *b58bin, const uchar *b58)
{
	uint32_t c, bin32[7];
	int len, i, j;
	uint64_t t;

	memset(bin32, 0, 7 * sizeof(uint32_t));
	len = strlen((const char *)b58);
	for (i = 0; i < len; i++) {
		c = b58[i];
		c = b58tobin_tbl[c];
		for (j = 6; j >= 0; j--) {
			t = ((uint64_t)bin32[j]) * 58 + c;
			c = (t & 0x3f00000000ull) >> 32;
			bin32[j] = t & 0xffffffffull;
		}
	}
	*(b58bin++) = bin32[0] & 0xff;
	for (i = 1; i < 7; i++) {
		*((uint32_t *)b58bin) = htobe32(bin32[i]);
		b58bin += sizeof(uint32_t);
	}
}

void address_to_pubkeyhash(uchar *pkh, const uchar *addr)
{
	uchar b58bin[25];

	memset(b58bin, 0, 25);
	b58tobin(b58bin, addr);
	pkh[0] = 0x76;
	pkh[1] = 0xa9;
	pkh[2] = 0x14;
	memcpy(&pkh[3], &b58bin[1], 20);
	pkh[23] = 0x88;
	pkh[24] = 0xac;
}

/*  For encoding nHeight into coinbase, return how many bytes were used */
int ser_number(uchar *s, int32_t val)
{
	int32_t *i32 = (int32_t *)&s[1];
	int len;

	if (val < 128)
		len = 1;
	else if (val < 16512)
		len = 2;
	else if (val < 2113664)
		len = 3;
	else
		len = 4;
	*i32 = htole32(val);
	s[0] = len++;
	return len;
}
