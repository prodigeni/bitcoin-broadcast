/* -*- mode: c; c-file-style: "linux"; compile-command: "scons -C .." -*-
 *  vi: set shiftwidth=8 tabstop=8 noexpandtab:
 */

#include <err.h>
#include <glib.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "bitcoin.h"

// Local prototypes
static guint dhash_hash(gconstpointer key);
static gboolean dhash_eq(gconstpointer a, gconstpointer b);
static gint comparator(gconstpointer a, gconstpointer b, gpointer user_data);

// Local constants
static const guint8 join_message[] = {
	0xF9, 0xBE, 0xB4, 0xD9, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6F,
	0x6E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x00, 0x00,
	0x35, 0x8D, 0x49, 0x32, 0x62, 0xEA, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0xB2, 0xD0, 0x50,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3B, 0x2E, 0xB3, 0x5D,
	0x8C, 0xE6, 0x17, 0x65, 0x0F, 0x2F, 0x53, 0x61, 0x74, 0x6F,
	0x73, 0x68, 0x69, 0x3A, 0x30, 0x2E, 0x37, 0x2E, 0x32, 0x2F,
	0xC0, 0x3E, 0x03, 0x00
};

guint64 var_int(const guint8 *const buf)
{
	if (*buf == 0xfd) return GUINT16_FROM_LE(*(guint16*)(buf+1));
	if (*buf == 0xfe) return GUINT32_FROM_LE(*(guint32*)(buf+1));
	if (*buf == 0xff) return GUINT64_FROM_LE(*(guint64*)(buf+1));
	return *buf;
}

int var_int_len(const guint8 *const buf)
{
	if (*buf == 0xfd) return 3;
	if (*buf == 0xfe) return 5;
	if (*buf == 0xff) return 9;
	return 1;
}

guint64 get_var_int(const guint8 **p) {
	guint64 n = var_int(*p);
	*p += var_int_len(*p);
	return n;
}

guint32 checksum(const struct msg_wire *const m)
{
	return *(guint32*)dhash(m->payload,GUINT32_FROM_LE(m->length_le),NULL);
}

guchar *dhash(const guchar *const d, const gulong n, guchar *const md)
{
	return SHA256(SHA256(d,n,NULL),SHA256_DIGEST_LENGTH,md);
}

int bitcoin_hashable_length(const struct msg *const m)
{
	if (m->type == BLOCK) {
		// Block hash is calculated only from 6 first fields
		return sizeof(m->block);
	} else {
		// Use all bytes to calculate the hash
		return m->length;
	}
}

const guchar *const bitcoin_inv_hash(const struct msg *const m)
{
	return bitcoin_inv_hash_buf(m,NULL);
}

guchar *bitcoin_inv_hash_buf(const struct msg *const m, guchar *const md)
{
	int hash_end = bitcoin_hashable_length(m);
	return dhash(m->payload,hash_end,md);
}

gchar *hex256(const guchar *const in)
{
	static char out[2*SHA256_DIGEST_LENGTH+1];

	for (int i=0; i<SHA256_DIGEST_LENGTH; i++) {
		snprintf(out+2*i,3,"%02hhx",in[SHA256_DIGEST_LENGTH-1-i]);
	}

	return out;
}

bool bitcoin_join(int fd)
{
	return write(fd,join_message,sizeof(join_message)) !=
		sizeof(join_message);
}

struct bitcoin_storage bitcoin_new_storage()
{
	// st.inv owns the key. Items in inventory must NOT be removed
	// before they are dequeued from send queue!
	struct bitcoin_storage st;
	st.inv = g_hash_table_new_full(&dhash_hash,dhash_eq,g_free,g_free);
	st.send_queue = g_sequence_new(NULL);
	return st;
}

bool bitcoin_inv_insert(struct bitcoin_storage const *st, struct msg *const m)
{
	// Allocate buffer for key and calculate hash of a message
	guchar* key = g_malloc(SHA256_DIGEST_LENGTH);
	bitcoin_inv_hash_buf(m, key);

	// If key is already stored, do not replace old one
	if (g_hash_table_contains(st->inv,key)) {
		g_free(key);
		return false;
	}

	// Store message
	g_hash_table_insert(st->inv,key,m);

	// Put hash key to the send queue
	bitcoin_enqueue(st,key);

	return true;
}

void bitcoin_enqueue(struct bitcoin_storage const *st, guchar *key)
{
	g_sequence_insert_sorted(st->send_queue,key,comparator,st->inv);
}

struct msg *bitcoin_dequeue(struct bitcoin_storage const *st)
{
	// Fetch and dequeue key
	GSequenceIter *it = g_sequence_get_begin_iter(st->send_queue);
	guchar *key = g_sequence_get(it);
	g_sequence_remove(it);
	
	// Fetch value (or NULL in case of failure)
	return g_hash_table_lookup(st->inv,key);
}

/**
 * Calculates a hash for use internally in Hash Table storage. This
 * hash should not be used outside g_hash_table.
 */
static guint dhash_hash(gconstpointer key)
{
	// Truncate first bytes of the key because the key is already
	// a hash.
	return *(guint*)key;
}

/**
 * Compares two keys for equality. It is used internally in Hash Table
 * storage.
 */
static gboolean dhash_eq(gconstpointer a, gconstpointer b)
{	
	// The keys are already hashes so comparing them byte-by-byte.
	return memcmp(a,b,SHA256_DIGEST_LENGTH) == 0;
}

enum msg_type bitcoin_find_type(const struct msg_wire *m)
{
	if (strcmp(m->command,"inv"    ) == 0) return INV;
	if (strcmp(m->command,"tx"     ) == 0) return TX;
	if (strcmp(m->command,"block"  ) == 0) return BLOCK;
	if (strcmp(m->command,"addr"   ) == 0) return ADDR;
	if (strcmp(m->command,"version") == 0) return VERSION;
	if (strcmp(m->command,"verack" ) == 0) return VERACK;
	return OTHER;
}

const char* bitcoin_type_str(const struct msg *m)
{
	switch (m->type) {
	case INV     : return "inv";
	case TX      : return "tx";
	case BLOCK   : return "block";
	case ADDR    : return "addr";
	case VERSION : return "version";
	case VERACK  : return "verack";
	default      : return "other";
	}
}

/**
 * Comparator of given inventory hashes. Hash table should be passed
 * as the user_data. Higher priority message has lower return value.
 */
static gint comparator(gconstpointer a, gconstpointer b, gpointer inv)
{
	const struct msg *msg_a = g_hash_table_lookup(inv,a);
	const struct msg *msg_b = g_hash_table_lookup(inv,b);

	if (msg_a == NULL || msg_b == NULL) {
		errx(9,"Inventory hash lookup failed. a=%s b=%s",
		     (char *)a,(char *)b);
	}

	// Already sent items are pushed towards the small end to aid
	// them getting out of the queue
	if (msg_a->sent) return -1;
	if (msg_b->sent) return 1;

	// Send lowest height first
	if (msg_a->height != msg_b->height) return msg_a->height - msg_b->height; 

	// Send transactions first, then block. The priority is defined
	// by the enum in bitcoin.h
	if (msg_a->type != msg_b->type) return msg_a->type - msg_b->type;

	// Now we may have two unconfirmed transactions, two blocks of
	// same height (fork) or two transactions belonging to the
	// same block. More elegant sorting (Satoshi's algorithm) may
	// be done especially for the unconfirmed transactions, but
	// currently we are considering it as a tie.
	return 0;
}

int bitcoin_tx_len(const guint8 *const buf)
{
	const guint8 *p = buf;

	p += 4; // Skip version
	const guint64 tx_in_count = get_var_int(&p);
	for (guint64 tx_in=0; tx_in < tx_in_count; tx_in++) {
		p += 36; // Skip previous_output
		const guint64 script_length = get_var_int(&p);
		p += script_length;
		p += 4; // Skip sequence
	}
	const guint64 tx_out_count = get_var_int(&p);
	for (guint64 tx_out=0; tx_out < tx_out_count; tx_out++) {
		p += 8; // Skip transaction value
		const guint64 script_length = get_var_int(&p);
		p += script_length;
	}
	p += 4; // Skip lock time

	return p-buf;
}