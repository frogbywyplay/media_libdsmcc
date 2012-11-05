#ifndef DSMCC_BIOP_TAP_H
#define DSMCC_BIOP_TAP_H

enum
{
	BIOP_DELIVERY_PARA_USE = 0x0016,
	BIOP_OBJECT_USE        = 0x0017
};

struct biop_tap
{
	unsigned short id;
	unsigned short use;
	unsigned short assoc_tag;
	unsigned short selector_length;
	unsigned char *selector_data;
};

int dsmcc_biop_parse_tap(struct biop_tap *tap, unsigned char *data, int data_length);
int dsmcc_biop_parse_taps_keep_only_first(struct biop_tap **tap0, unsigned short tap0_use, unsigned char *data, int data_length);
void dsmcc_biop_free_tap(struct biop_tap *tap);

#endif /* DSMCC_BIOP_TAP_H */
