#ifndef DSMCC_BIOP_H
#define DSMCC_BIOP_H

#include "dsmcc-cache.h"

#define	BIOP_OBJ_OFFSET	11
#define BIOP_TAG_OFFSET 17

struct biop_name_comp {
	unsigned char id_len;
	char *id;
	unsigned char kind_len;
	char *kind;
};

struct biop_name {
	unsigned char comp_count;
	struct biop_name_comp *comps;
};

struct biop_body_file {
	unsigned long msgbody_len;
	unsigned long content_len;
};

struct biop_msg_header {
	unsigned char version_major;
	unsigned char version_minor;
	unsigned int message_size;
	unsigned char objkey_len;
	char *objkey;
	unsigned long objkind_len;
	char *objkind;
	unsigned int objinfo_len;
	char *objinfo;
};

struct biop_tap {
	unsigned short id;
	unsigned short use;
	unsigned short assoc_tag;
	unsigned short selector_len;
	char *selector_data;
};

struct descriptor;

struct biop_module_info {
	unsigned long mod_timeout;
	unsigned long block_timeout;
	unsigned long min_blocktime;
	unsigned char taps_count;
	struct biop_tap tap;
	unsigned char userinfo_len;
	struct descriptor *descriptors;
};


struct biop_dsm_connbinder {
	unsigned long component_tag;
	unsigned char component_data_len;
	unsigned char taps_count;
	struct biop_tap tap;
};

struct biop_obj_location {
	unsigned long component_tag;
	char component_data_len;
	unsigned long carousel_id;
	unsigned short module_id;
	char version_major;
	char version_minor;
	unsigned char objkey_len;
	char *objkey;
};

struct biop_profile_body {
	unsigned long data_len;
	char byte_order;
	char lite_components_count;
	struct biop_obj_location obj_loc;
	struct biop_dsm_connbinder dsm_conn;
};

struct biop_profile_lite {
	/* TODO */
};

struct biop_ior {
	unsigned long type_id_len; 
	char *type_id;
	unsigned long tagged_profiles_count;
	unsigned long profile_id_tag;
	union {
		struct biop_profile_body full;
		struct biop_profile_lite lite;
	} body;
};
	
struct biop_binding {
	struct biop_name name;
	char binding_type;
	struct biop_ior ior;
	unsigned int objinfo_len;
	char *objinfo;
};

struct biop_body_gateway {
	unsigned long msgbody_len;
	unsigned int bindings_count;
	struct biop_binding binding;
};

struct biop_body_directory {
	unsigned long msgbody_len;
	unsigned int bindings_count;
	struct biop_binding binding;
};

struct biop_message {
	struct biop_msg_header hdr;
	union {
		struct biop_body_file file;
		struct biop_body_directory dir;
		struct biop_body_gateway srg;
	} body;
};

struct dsmcc_module_info {
        unsigned short module_id;
        unsigned long  module_size;
        unsigned char module_version;
        unsigned char module_info_len;
        struct biop_module_info modinfo;
        unsigned char *data;
        unsigned int curp;
        struct dsmcc_module_info *next;
};

struct dsmcc_dsi {
        unsigned short data_len;
        unsigned short num_groups;
        struct biop_ior profile;
        unsigned short user_data_len;
        unsigned char *user_data;
};

int dsmcc_biop_process_ior(struct biop_ior *, unsigned char *);
int dsmcc_biop_process_module_info(struct biop_module_info *, unsigned char *data); 
void dsmcc_biop_process_data(struct cache *cache, struct cache_module_data *cachep);

#endif
