#ifndef DSMCC_CACHE_MODULE_H
#define DSMCC_CACHE_MODULE_H

/* from dsmcc-biop-message.h */
struct biop_module_info;

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

/* from dsmcc-section.h */
struct dsmcc_dii;
struct dsmcc_module_info;
struct dsmcc_ddb;

struct dsmcc_cached_module
{
	unsigned long  carousel_id;
	unsigned short module_id;
	unsigned short assoc_tag;
	unsigned char  version;

	unsigned long  total_size;
	unsigned long  block_size;
	unsigned long  downloaded_size;
	char          *bstatus;

	char          *data_file;
	char           cached;

	struct dsmcc_descriptor *descriptors;

	struct dsmcc_cached_module *next, *prev;
};


void dsmcc_add_cached_module_info(struct dsmcc_status *status, struct dsmcc_object_carousel *car, struct dsmcc_dii *dii, struct dsmcc_module_info *dmi, struct biop_module_info *bmi);
void dsmcc_save_cached_module_data(struct dsmcc_status *status, int download_id, struct dsmcc_ddb *ddb, unsigned char *data, int data_length);
void dsmcc_free_cached_module(struct dsmcc_object_carousel *car, struct dsmcc_cached_module *cachep);

#endif /* DSMCC_CACHE_MODULE_H */
