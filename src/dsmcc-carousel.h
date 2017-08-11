#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include <stdint.h>
#include <stdio.h>


struct dsmcc_object_carousel
{
	struct dsmcc_state *state;
	uint32_t            cid;
	int                 type;
	int                 status;

	uint16_t requested_pid;
	uint32_t requested_transaction_id;

	uint32_t dsi_transaction_id;
	uint32_t dii_transaction_id;

	uint8_t tid;
	uint8_t section_control_table_id;
	uint8_t section_data_table_id;
	uint8_t skip_leading_bytes;

	struct dsmcc_module     *modules;
	struct dsmcc_file_cache *filecaches;
	struct dsmcc_group_list *group_list;

	struct dsmcc_object_carousel *next;
};

struct dsmcc_object_carousel *find_carousel_by_requested_pid(struct dsmcc_state *state, uint16_t pid);
void dsmcc_object_carousel_queue_add(struct dsmcc_state *state, uint32_t queue_id,
		struct dsmcc_parameters *parameters, struct dsmcc_carousel_callbacks *callbacks);
void dsmcc_object_carousel_queue_remove(struct dsmcc_state *state, uint32_t queue_id);
bool dsmcc_object_carousel_load_all(FILE *file, struct dsmcc_state *state);
bool dsmcc_object_carousel_save_all(FILE *file, struct dsmcc_state *state);
void dsmcc_object_carousel_free(struct dsmcc_object_carousel *carousel, bool keep_cache);
void dsmcc_object_carousel_free_all(struct dsmcc_state *state, bool keep_cache);
void dsmcc_object_carousel_set_status(struct dsmcc_object_carousel *carousel, int newstatus);
uint32_t dsmcc_object_carousel_get_transaction_id(struct dsmcc_state *state, uint32_t queue_id);

#endif
