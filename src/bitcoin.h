/* -*- mode: c; c-file-style: "linux"; compile-command: "scons -C .." -*-
 *  vi: set shiftwidth=8 tabstop=8 noexpandtab:
 */

#ifndef BITCOIN_H_
#define BITCOIN_H_

#include <glib.h>
#include <stdbool.h>

enum msg_type {
	UNDEFINED,
	OTHER,
	INV,
	TX,
	BLOCK,
	ADDR,
	VERSION,
	VERACK,
};

/**
 * Data type for message structure on wire.
 * https://en.bitcoin.it/wiki/Protocol_specification#Message_structure
 */
struct __attribute__ ((__packed__)) msg_wire {
	guint32 magic;
	char command[12];
	guint32 length_le; // Little-endian!
	guint32 checksum;
	guint8 payload[];
};

/**
 * Data type for block which is a contained in payload of msg.
 * https://en.bitcoin.it/wiki/Protocol_specification#block
 */
struct __attribute__ ((__packed__)) block {
	guint32 version;
	guint8 prev_block[32];
	guint8 merkle_root[32];
	guint32 timestamp_le; // Little-endian!
	guint32 bits_le; // Calculated difficulty, little-endian!
	guint32 nonche;
	guint8 txs[]; // Transaction count and list of transactions
};

struct __attribute__ ((__packed__)) msg {
	guint length;
	guint height;
	enum msg_type type;
	union {
		struct block block;
		guint8 payload[1];
	};
};

// TODO function comments

guint64 var_int(const guint8 *const buf);
gint var_int_len(const guint8 *const buf);

/**
 * Calculates checksum for given wire message.
 */
guint32 checksum(const struct msg_wire *const m);

/**
 * Calculates double SHA256 of given data. This is described in
 * Bitcoin Protocol Specification.
 */
guchar *dhash(const guchar *const d, const gulong n, guchar *const md);

/**
 * Returns number of "identifying bytes" in an inventory item. Rest may
 * ignored when calculating the hash.
 */
int bitcoin_hashable_length(const struct msg *const m);

/**
 * Calculates inventory hash from given message using static buffer.
 */
const guchar *const bitcoin_inv_hash(const struct msg *const m);

/**
 * Calculates inventory hash from given message using buffer allocated
 * by the caller. If md is NULL the behaviour is identical to
 * bitcoin_inv_hash().
 */
guchar *bitcoin_inv_hash_buf(const struct msg *const m, guchar *const md);

char *hex256(const guchar *const buf);
bool bitcoin_join(int fd);

/**
 * Returns a new Bitcoin message inventory which uses bitcoin specific
 * hash calculation and equality test. Only data of type `struct msg`
 * should be stored here.
 */
GHashTable *bitcoin_new_inventory();

/**
 * Insert given message to inventory of objects. This function doesn't
 * modify m, but it is not declared as const because glib doesn't have
 * constant pointers to data.
 */
bool bitcoin_inv_insert(GHashTable *inv, struct msg *const m);

/**
 * Returns message type of given wire message.
 */
enum msg_type bitcoin_find_type(const struct msg_wire *m);

/**
 * Convert msg_type inside msg to string constant. Returns static
 * buffer.
 */
const char* bitcoin_type_str(const struct msg *m);

#endif /* BITCOIN_H_ */
