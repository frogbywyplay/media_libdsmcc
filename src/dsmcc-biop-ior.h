#ifndef DSMCC_BIOP_IOR_H
#define DSMCC_BIOP_IOR_H

struct biop_dsm_conn_binder
{
	unsigned short assoc_tag;
	unsigned long transactionId;
	unsigned long timeout;
};

struct biop_obj_location
{
	unsigned long  carousel_id;
	unsigned short module_id;
	char           version_major;
	char           version_minor;
	unsigned char  objkey_len;
	unsigned char *objkey;
};

struct biop_profile_body
{
	struct biop_obj_location   obj_loc;
	struct biop_dsm_conn_binder conn_binder;
};

enum
{
	IOR_TYPE_DSM_DIRECTORY = 0,
	IOR_TYPE_DSM_FILE,
	IOR_TYPE_DSM_STREAM,
	IOR_TYPE_DSM_SERVICE_GATEWAY,
	IOR_TYPE_DSM_STREAM_EVENT
};

struct biop_ior
{
	int                      type;
	struct biop_profile_body profile_body;
};

int dsmcc_biop_parse_ior(struct biop_ior *ior, unsigned char *data, int data_length);
const char *dsmcc_biop_get_ior_type_str(int type);
void dsmcc_biop_free_ior(struct biop_ior *ior);

#endif /* DSMCC_BIOP_IOR_H */
