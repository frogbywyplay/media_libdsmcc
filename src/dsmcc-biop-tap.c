/* Taps */
/* see ETSI TR 101 202 Table 4.3 and 4.5 */

#include <stdlib.h>
#include <string.h>

#include "dsmcc-biop-tap.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"

int dsmcc_biop_parse_tap(struct biop_tap *tap, unsigned char *data, int data_length)
{
	int off = 0;

	(void) data_length; /* TODO check data length */

	tap->id = dsmcc_getshort(data);
	off += 2;
	DSMCC_DEBUG("ID = 0x%x",tap->id);

	tap->use = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Use = 0x%x",tap->use);

	tap->assoc_tag = dsmcc_getshort(data + off);
	off+=2;
	DSMCC_DEBUG("Assoc = 0x%x", tap->assoc_tag);

	tap->selector_length = data[off++];
	if (tap->selector_length> 0)
	{
		tap->selector_data = malloc(tap->selector_length);
		memcpy(tap->selector_data, data + off, tap->selector_length);
	}
	else
		tap->selector_data = NULL;
	off += tap->selector_length;
	DSMCC_DEBUG("Selector Length = %d", tap->selector_length);

	return off;
}

int dsmcc_biop_parse_taps_keep_only_first(struct biop_tap **tap0, unsigned short tap0_use, unsigned char *data, int data_length)
{
	int off = 0, ret, i;
	unsigned char taps_count;

	taps_count = data[off];
	off++;
	if (taps_count < 1)
	{
		DSMCC_ERROR("Invalid number of taps (got %d but expected at least %d)", taps_count, 1);
		return -1;
	}
	DSMCC_DEBUG("Taps count = %d", taps_count);

	for (i = 0; i < taps_count; i++)
	{
		struct biop_tap *tap = malloc(sizeof(struct biop_tap));

		ret = dsmcc_biop_parse_tap(tap, data + off, data_length - off);
		if (ret < 0)
		{
			DSMCC_ERROR("dsmcc_biop_parse_tap returned %d", ret);
			dsmcc_biop_free_tap(tap);
			return -1;
		}
		off += ret;

		/* first tap should be tap0_use */
		if (i == 0)
		{
			if (tap->use != tap0_use)
			{
				DSMCC_ERROR("Expected a first tap with BIOP_DELIVERY_PARA_USE, but got Use = %d", tap->use);
				dsmcc_biop_free_tap(tap);
				return -1;
			}

			*tap0 = tap;
		}
		else
		{
			DSMCC_DEBUG("Skipping tap %d (Use = %d)", i, tap->use);
			dsmcc_biop_free_tap(tap);
		}
	}

	return off;
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
