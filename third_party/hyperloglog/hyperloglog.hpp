//===----------------------------------------------------------------------===//
//                         DuckDB
//
// third_party/hyperloglog/hyperloglog.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include <string.h>
#include <stdint.h>

namespace duckdb_hll {

/* Error codes */
#define HLL_C_OK                    0
#define HLL_C_ERR                   -1

typedef struct {
    void *ptr;
} robj;

struct hllhdr {
	char magic[4];      /* "HYLL" */
	uint8_t encoding;   /* HLL_DENSE or HLL_SPARSE. */
	uint8_t notused[3]; /* Reserved for future use, must be zero. */
	uint8_t card[8];    /* Cached cardinality, little endian. */
	uint8_t registers[1]; /* Data bytes. */
};

//! Create a new empty HyperLogLog object
robj *hll_create(void);
//! Destroy the specified HyperLogLog object
void hll_destroy(robj *obj);
//! Add an element with the specified amount of bytes to the HyperLogLog. Returns C_ERR on failure, otherwise returns 0 if the cardinality did not change, and 1 otherwise.
int hll_add(robj *o, unsigned char *ele, size_t elesize);
//! Returns the estimated amount of unique elements seen by the HyperLogLog. Returns C_OK on success, or C_ERR on failure.
int hll_count(robj *o, size_t *result);
//! Merge hll_count HyperLogLog objects into a single one. Returns NULL on failure, or the new HLL object on success.
robj *hll_merge(robj **hlls, size_t hll_count);

//! Update HLL register
int hllDenseSet(uint8_t *registers, long index, uint8_t count);
int hllSparseSet(robj *o, long index, uint8_t count);

uint64_t MurmurHash64A (const void * key, int len, unsigned int seed);

}