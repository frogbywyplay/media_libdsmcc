#ifndef DSMCC_DESCRIPTOR_H
#define DSMCC_DESCRIPTOR_H

#include <stdio.h>

struct descriptor_type {
	char *text;
};

struct descriptor_name {
	char *text;
};

struct descriptor_info {
	char lang_code[3];
	char *text;
};

struct descriptor_modlink {
	char position;
	unsigned short module_id;
};

struct descriptor_crc32 {
	unsigned long crc;
};

struct descriptor_location {
	char location_tag;
};

struct descriptor_dltime {
	unsigned long download_time;
};

struct descriptor_grouplink {
	char position;
	unsigned long group_id;
};

struct descriptor_private {
	char *descriptor;
};
	
struct descriptor_compressed { 
	char method;
	unsigned long original_size;
};

struct descriptor {
	unsigned char tag;
	unsigned char len;
	union {
		struct descriptor_type type;
		struct descriptor_name name;
		struct descriptor_info info;
		struct descriptor_modlink modlink;
		struct descriptor_crc32 crc32;
		struct descriptor_location location;
		struct descriptor_dltime dltime;
		struct descriptor_grouplink grouplink;
		struct descriptor_compressed compressed;
/*		struct descriptor_private private;
		struct descriptor_subgroup subgroup  #ref. DVB SSU */
	} data;

	struct descriptor *next;
};

void dsmcc_desc_free(struct descriptor *desc);

void dsmcc_desc_process_type(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_name(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_info(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_modlink(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_crc32(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_location(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_dltime(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_grouplink(unsigned char *Data, struct descriptor *);
void dsmcc_desc_process_compressed(unsigned char *Data, struct descriptor *);

struct descriptor *
dsmcc_desc_process_one(unsigned char *Data, int *offset);

struct descriptor *
	dsmcc_desc_process(unsigned char *Data, int data_len, int *offset);
 
#endif
