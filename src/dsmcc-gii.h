#ifndef DSMCC_GII
#define DSMCC_GII

struct dsmcc_group_list
{
	uint32_t id;
	uint32_t size;

	struct dsmcc_group_list *next;
};

int dsmcc_group_info_indication_parse(struct dsmcc_group_list **groups, uint8_t *data, int data_length);
void dmscc_group_list_free(struct dsmcc_group_list *groups);

#endif

