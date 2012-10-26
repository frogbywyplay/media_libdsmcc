#ifndef DSMCC_RECEIVER_H
#define DSMCC_RECEIVER_H

#include "dsmcc-carousel.h"
#include "dsmcc-biop.h"

#define MAXCAROUSELS 16

struct dsmcc_status;

#define BLOCK_GOT(s,i)	(s[i/8]& (1<<(i%8)))
#define BLOCK_SET(s,i)  (s[i/8]|=(1<<(i%8)))

/* hack forward decls */

struct cache_module_data {
	unsigned long carousel_id;
	unsigned short module_id;

	unsigned char version;
	unsigned long size;
	unsigned long block_size;
	unsigned long curp;

	unsigned short block_num;
	char *bstatus;	/* Block status bit field */
	struct dsmcc_ddb *blocks;
	char *data_file;
	unsigned char *data_ptr;
	char cached;

	unsigned short tag;

	struct cache_module_data *next, *prev;
	struct descriptor *descriptors;

};

struct dsmcc_dii {
	unsigned long download_id;
	unsigned short block_size;
	unsigned long tc_download_scenario;
	unsigned short number_modules;
	struct dsmcc_module_info *modules;
	unsigned short private_data_len;
	unsigned char *private_data;
};

struct dsmcc_section_header {
        char table_id;  /* always 0x3B */
	unsigned short table_id_extension;
	unsigned long crc;
};


struct dsmcc_message_header {
        unsigned char protocol;    /* 0x11 */
        unsigned char type;                /* 0x03 U-N */
        unsigned short message_id;      /* 0x1002 -DDI */

	unsigned long transaction_id;

        /* transactionID
	 * unsigned int orig_subfield : 2;  10b
	 * unsigned int version_subfield : 14;
	 * unsigned int id_subfield : 15;  000000000000000
	 * unsigned int update_subfield : 1;
	 */

	unsigned short message_len;
};

struct dsmcc_data_header {
	char protocol;/* 0x11 */
	char type;/* 0x03 */
	unsigned short message_id;/* 0x1003 */
	unsigned long download_id;
	char adaptation_len;/* 0x00 or 0x08 */
	unsigned short message_len;
	/* struct dsmcc_adaption_hdr ??? */
};

struct dsmcc_ddb {
	unsigned short module_id;
	unsigned char module_version;
	unsigned short block_number;

	unsigned int len;
	struct dsmcc_ddb *next; /* Needed for caching */
};

struct dsmcc_section {
	struct dsmcc_section_header sec;

	union {
		struct dsmcc_message_header info;
		struct dsmcc_data_header data;
	} hdr;

	union {
		struct dsmcc_dsi dsi;
		struct dsmcc_dii dii;
		struct dsmcc_ddb ddb;
	} msg;
};

void dsmcc_init(struct dsmcc_status *, const char *channel);
void dsmcc_add_stream(struct dsmcc_status *, int pid);
void dsmcc_process_section(struct dsmcc_status *, unsigned char *data, int pid, int Length);
void dsmcc_free(struct dsmcc_status *);
void dsmcc_free_cache_module_data(struct obj_carousel *car, struct cache_module_data *cachep);

#endif
