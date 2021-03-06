/*
 * nessDB storage engine
 * Copyright (c) 2011-2012, BohuTANG <overred.shuttler at gmail dot com>
 * All rights reserved.
 * Code is licensed with BSD. See COPYING.BSD file.
 *
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <stdint.h>
#ifdef _WIN32
#include <winsock.h>
#else
/* Unix */
#include <arpa/inet.h> /* htonl/ntohl */
	#ifndef O_BINARY 
		#define O_BINARY 0
	#endif
#endif

#define FILE_ERR(a) (a == -1)

#ifdef __CHECKER__
#define FORCE __attribute__((force))
#else
#define FORCE
#endif


/* Bloom filter */
#if CHAR_BIT != 8
#define CHAR_BIT (8)
#endif
#define SETBIT_1(bitset,i) (bitset[i / CHAR_BIT] |=  (1<<(i % CHAR_BIT)))
#define SETBIT_0(bitset,i) (bitset[i / CHAR_BIT] &=  (~(1<<(i % CHAR_BIT))))
#define GETBIT(bitset,i) (bitset[i / CHAR_BIT] &   (1<<(i % CHAR_BIT)))

/* Get H bit */
static inline int GET64_H(uint64_t x)
{
	if(((x>>63)&0x01)!=0x01)
		return 0;
	else
		return 1;
}

/* Set H bit to 0 */
static inline uint64_t SET64_H_0(uint64_t x)
{
	return	x&=0x3FFFFFFFFFFFFFFF;
}

/* Set H bit to 1 */
static inline uint64_t SET64_H_1(uint64_t x)
{
	return  x|=0x8000000000000000;	
}

struct slice{
	char *data;
	int len;
};

void ensure_dir_exists(const char *path);

static inline unsigned int sax_hash(const char *key)
{
	unsigned int h = 0;

	while (*key) {
		h ^= (h << 5) + (h >> 2) + (unsigned char) *key;
		++key;
	}

	return h;
}

static inline unsigned int sdbm_hash(const char *key)
{
	unsigned int h = 0;

	while (*key) {
		h = (unsigned char) *key + (h << 6) + (h << 16) - h;
		++key;
	}

	return h;
}

static inline unsigned int djb_hash(const char *key)
{
	unsigned int h = 5381;

	while (*key) {
		h = ((h<< 5) + h) + (unsigned int) *key;  /* hash * 33 + c */
		++key;
	}

	return h;
}

uint16_t crc16(const char *buf, int len);

long long get_ustime_sec(void);
#endif
