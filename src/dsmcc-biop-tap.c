/* Taps */
/* see ETSI TR 101 202 Table 4.3 and 4.5 */

#include <stdlib.h>
#include <string.h>

#include "dsmcc-biop-tap.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"

static int dsmcc_biop_parse_tap(struct biop_tap *tap, uint8_t *data, int data_length)
{
	int off = 0;
	uint16_t id;

	(void) data_length; /* TODO check data length */

	id = dsmcc_getshort(data);
	off += 2;
	DSMCC_DEBUG("ID = 0x%hx", id);

	tap->use = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Use = 0x%hx (%s)", tap->use, dsmcc_biop_get_tap_use_str(tap->use));

	tap->assoc_tag = dsmcc_getshort(data + off);
	off+=2;
	DSMCC_DEBUG("Assoc = 0x%hx", tap->assoc_tag);

	tap->selector_length = data[off++];
	if (tap->selector_length> 0)
	{
		tap->selector_data = malloc(tap->selector_length);
		memcpy(tap->selector_data, data + off, tap->selector_length);
	}
	else
		tap->selector_data = NULL;
	off += tap->selector_length;
	DSMCC_DEBUG("Selector Length = %hhd", tap->selector_length);

	return off;
}

int dsmcc_biop_parse_taps_keep_only_first(struct biop_tap **tap0, uint16_t tap0_use, uint8_t *data, int data_length)
{
	int off = 0, ret, i;
	uint8_t taps_count;

	taps_count = data[off];
	off++;
	if (taps_count < 1)
	{
		DSMCC_ERROR("Invalid number of taps (got %hhd but expected at least %d)", taps_count, 1);
		return -1;
	}
	DSMCC_DEBUG("Taps count = %hhd", taps_count);

	for (i = 0; i < taps_count; i++)
	{
		struct biop_tap *tap = malloc(sizeof(struct biop_tap));

		ret = dsmcc_biop_parse_tap(tap, data + off, data_length - off);
		if (ret < 0)
		{
			dsmcc_biop_free_tap(tap);
			return -1;
		}
		off += ret;

		/* first tap should be tap0_use */
		if (i == 0)
		{
			if (tap->use != tap0_use)
			{
				DSMCC_ERROR("Expected a first tap with BIOP_DELIVERY_PARA_USE, but got Use 0x%hx (%s)", tap->use, dsmcc_biop_get_tap_use_str(tap->use));
				dsmcc_biop_free_tap(tap);
				return -1;
			}

			*tap0 = tap;
		}
		else
		{
			DSMCC_DEBUG("Skipping tap %d Use 0x%hx (%s)", i, tap->use, dsmcc_biop_get_tap_use_str(tap->use));
			dsmcc_biop_free_tap(tap);
		}
	}

	return off;
}

const char *dsmcc_biop_get_tap_use_str(uint16_t use)
{
	switch (use)
	{
		case BIOP_DELIVERY_PARA_USE:
			return "BIOP_DELIVERY_PARA_USE";
		case BIOP_OBJECT_USE:
			return "BIOP_OBJECT_USE";
		default:
			return "Unknown";
	}
}
void dsmcc_biop_free_tap(struct biop_tap *tap)
{
	if (tap == NULL)
		return;

	if (tap->selector_data != NULL)
	{
		free(tap->selector_data);
		tap->selector_data = NULL;
	}

	free(tap);
}
