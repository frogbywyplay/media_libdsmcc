#ifndef DSMCC_RECEIVER_H
#define DSMCC_RECEIVER_H

#include "dsmcc-carousel.h"
#include "dsmcc-biop.h"

#define MAXCAROUSELS 16

struct dsmcc_status;

#define DSMCC_SYNC_BYTE		0x47
#define DSMCC_TRANSPORT_ERROR	0x80
#define DSMCC_START_INDICATOR	0x40

#define DSMCC_MESSAGE_DSI	0x1006
#define DSMCC_MESSAGE_DII	0x1002
#define DSMCC_MESSAGE_DDB	0x1003

#define DSMCC_SECTION_INDICATION	0x3B
#define DSMCC_SECTION_DATA		0x3C
#define DSMCC_SECTION_DESCR		0x3D

#define DSMCC_SECTION_OFFSET	0
#define DSMCC_MSGHDR_OFFSET	8
#define DSMCC_DATAHDR_OFFSET	8
#define DSMCC_DSI_OFFSET	20
#define DSMCC_DII_OFFSET	20
#define DSMCC_DDB_OFFSET	20
#define DSMCC_BIOP_OFFSET	24

#define BLOCK_GOT(s,i)	(s[i/8]& (1<<(i%8)))
#define BLOCK_SET(s,i)  (s[i/8]|=(1<<(i%8)))

	/* hack forward decls */

struct cache_module_data {
	unsigned long carousel_id;
	unsigned short module_id;

	unsigned char version;
	unsigned long size;
	unsigned long curp;

	unsigned short block_num;
	char *bstatus;	/* Block status bit field */
	struct dsmcc_ddb *blocks;
	char cached;

	unsigned char *data;
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

	unsigned char flags[2];

	unsigned short table_id_extension;

	/*
	*  unsigned int section_syntax_indicator : 1; UKProfile - always 1
	*  unsigned int private_indicator : 1;  UKProfile - hence always 0
	*  unsigned int reserved : 2;  always 11b
	*  unsigned int dsmcc_section_length : 12;
	**/

	unsigned char flags2;

	/*
	*  unsigned int reserved : 2;  always 11b
	*  unsigned int version_number : 5;  00000b
	*  unsigned int current_next_indicator : 1  1b
	* */

	unsigned long crc;    /* UKProfile */
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
	unsigned char *blockdata;
	unsigned int len;
	struct dsmcc_ddb *next;	/* Needed for caching */
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
void dsmcc_free(struct dsmcc_status *);
void dsmcc_add_stream(struct dsmcc_status *, struct stream *);

void dsmcc_add_module_info(struct dsmcc_status *, struct dsmcc_section *, struct obj_carousel *);
void dsmcc_add_module_data(struct dsmcc_status *, struct dsmcc_section *, unsigned char *);

int dsmcc_process_section_gateway(struct dsmcc_status *, unsigned char *, int, int);
int dsmcc_process_section_info(struct dsmcc_status *, struct dsmcc_section *,unsigned char *, int);
int dsmcc_process_section_block(struct dsmcc_status *, struct dsmcc_section *, unsigned char *, int);

int dsmcc_process_section_header(struct dsmcc_section *, unsigned char *, int);
int dsmcc_process_msg_header(struct dsmcc_section *, unsigned char *);
int dsmcc_process_data_header(struct dsmcc_section *, unsigned char *, int);


void dsmcc_process_section_desc(unsigned char *Data, int Length);
void dsmcc_process_section_data(struct dsmcc_status *, unsigned char *Data, int Length);
void dsmcc_process_section_indication(struct dsmcc_status *, unsigned char *Data, int pid, int Length);
void dsmcc_process_section(struct dsmcc_status *, unsigned char *Data, int pid, int Length);


#endif

