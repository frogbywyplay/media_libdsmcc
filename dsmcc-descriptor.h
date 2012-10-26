#ifndef DSMCC_DESCRIPTOR_H
#define DSMCC_DESCRIPTOR_H

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
	} data;

	struct descriptor *next;
};

void dsmcc_desc_free_all(struct descriptor *desc);
struct descriptor *dsmcc_desc_process(unsigned char *data, int data_len, int *bytes_read);

#endif
