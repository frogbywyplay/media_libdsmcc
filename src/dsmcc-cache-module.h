#ifndef DSMCC_CACHE_MODULE_H
#define DSMCC_CACHE_MODULE_H

#include <stdint.h>

/* from dsmcc-biop-message.h */
struct biop_module_info;

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

/* from dsmcc-section.h */
struct dsmcc_dii;
struct dsmcc_module_info;
struct dsmcc_ddb;

struct dsmcc_cached_module;

void dsmcc_add_cached_module_info(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_dii *dii, struct dsmcc_module_info *dmi, struct biop_module_info *bmi);
void dsmcc_save_cached_module_data(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_ddb *ddb, uint8_t *data, int data_length);
void dsmcc_free_cached_module(struct dsmcc_object_carousel *car, struct dsmcc_cached_module *module);

#endif /* DSMCC_CACHE_MODULE_H */
