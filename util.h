
#ifndef HAVE_UTIL_H
#define HAVE_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

uint32_t crc32mpeg(uint32_t crc, const void * data, size_t len);
size_t trimnl(char * line, size_t len);

// ceil(64 / 7) = 10
#define VARINT_MAX_SIZE 10

void varuint_pack(uint8_t ** buf, uint64_t val);
bool varuint_unpack(const uint8_t ** posptr, const uint8_t * limit, uint64_t * valptr);
bool varuint_read(uint8_t ** buf, const uint8_t * limit, FILE * f);

#endif
