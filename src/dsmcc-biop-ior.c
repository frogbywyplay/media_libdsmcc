/* IOR and Taps */
/* see ETSI TR 101 202 Table 4.3 and 4.5 */

#include <stdlib.h>
#include <string.h>

#include "dsmcc-biop-ior.h"
#include "dsmcc-biop-tap.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"

#define TAG_BIOP           0x49534F06
#define TAG_ObjectLocation 0x49534F50
#define TAG_ConnBinder     0x49534F40

static int dsmcc_biop_parse_dsm_conn_binder(struct biop_dsm_conn_binder *binder, uint8_t *data, int data_length)
{
	int off = 0, ret;
	struct biop_tap *tap = NULL;
	uint16_t selector_type;

	ret = dsmcc_biop_parse_taps_keep_only_first(&tap, BIOP_DELIVERY_PARA_USE, data, data_length);
	if (ret < 0)
		goto error;
	off += ret;

	if (tap->selector_length != 0x0a)
	{
		DSMCC_ERROR("Invalid selector length while parsing BIOP_DELIVERY_PARA_USE tap (got 0x%hhx but expected 0x0a)", tap->selector_length);
		goto error;
	}

	dsmcc_getshort(&selector_type, tap->selector_data, 0, tap->selector_length);
	if (selector_type != 0x01)
	{
		DSMCC_ERROR("Invalid selector type while parsing BIOP_DELIVERY_PARA_USE tap (got 0x%hx but expected 0x01)", selector_type);
		goto error;
	}

	if (!dsmcc_getlong(&binder->transaction_id, tap->selector_data, 2, tap->selector_length))
		goto error;
	if (!dsmcc_getlong(&binder->timeout, tap->selector_data, 6, tap->selector_length))
		goto error;
	binder->assoc_tag = tap->assoc_tag;

	dsmcc_biop_free_tap(tap);
	return off;

error:
	dsmcc_biop_free_tap(tap);
	return -1;
}

static int dsmcc_biop_parse_obj_location(struct biop_obj_location *loc, uint8_t *data, int data_length)
{
	int off = 0;
	uint16_t version;

	if (!dsmcc_getlong(&loc->carousel_id, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Carousel id = %ld", loc->carousel_id);

	if (!dsmcc_getshort(&loc->module_id, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("Module id = %d", loc->module_id);

	if (!dsmcc_getshort(&version, data, off, data_length))
		return -1;
	off += 2;
	if (version != 0x100)
	{
		DSMCC_ERROR("Invalid version in BIOP::ObjectLocation: got 0x%hx but expected 0x100", version);
		return -1;
	}

	if (!dsmcc_getbyte(&loc->objkey_len, data, off, data_length))
		return -1;
	off++;
	if (!dsmcc_memdup(&loc->objkey, loc->objkey_len, data, off, data_length))
		return -1;
	off += loc->objkey_len;
	DSMCC_DEBUG("Key Length = %hhd", loc->objkey_len);

	return off;
}

static int dsmcc_biop_parse_body(struct biop_profile_body *body, uint8_t *data, int data_length)
{
	int off = 0, ret, i;
	uint8_t lite_components_count;
	uint32_t component_tag;
	uint8_t component_data_len;

	/* skip byte order */
	off++;

	if (!dsmcc_getbyte(&lite_components_count, data, off, data_length))
		return -1;
	off++;
	if (lite_components_count < 2)
	{
		DSMCC_ERROR("Invalid number of components in BIOPProfileBody: got %hhd but expected at least 2", lite_components_count);
		return -1;
	}
	DSMCC_DEBUG("Lite Components Count %d", lite_components_count);

	for (i = 0; i < lite_components_count; i++)
	{
		if (!dsmcc_getlong(&component_tag, data, off, data_length))
			goto error;
		off += 4;

		if (!dsmcc_getbyte(&component_data_len, data, off, data_length))
			goto error;
		off++;

		if (i == 0)
		{
			/* First component should be a BIOP::ObjectLocation */
			if (component_tag != TAG_ObjectLocation)
			{
				DSMCC_ERROR("Invalid component ID tag while parsing BIOP::ObjectLocation: got 0x%08x but expected 0x%08x", component_tag, TAG_ObjectLocation);
				goto error;
			}

			ret = dsmcc_biop_parse_obj_location(&body->obj_loc, data + off, data_length - off);
			if (ret < 0)
				goto error;
		}
		else if (i == 1)
		{
			/* Second component should be a DSM::ConnBinder */
			if (component_tag != TAG_ConnBinder)
			{
				DSMCC_ERROR("Invalid component ID tag while parsing DSM::ConnBinder: got 0x%08x but expected 0x%08x", component_tag, TAG_ConnBinder);
				goto error;
			}

			ret = dsmcc_biop_parse_dsm_conn_binder(&body->conn_binder, data + off, data_length - off);
			if (ret < 0)
				goto error;
		}
		else
		{
			/* ignore remaining components */
			DSMCC_WARN("Ignoring unknown component %d with ID tag 0x%08x", i, component_tag);
		}

		off += component_data_len;
	}

	return off;

error:
	if (body->obj_loc.objkey != NULL)
		free(body->obj_loc.objkey);
	memset(body, 0, sizeof(*body));
	return -1;
}

const char *dsmcc_biop_get_ior_type_str(int type)
{
	switch (type)
	{
		case IOR_TYPE_DSM_DIRECTORY:
			return "DSM::Directory";
		case IOR_TYPE_DSM_FILE:
			return "DSM::File";
		case IOR_TYPE_DSM_STREAM:
			return "DSM::Stream";
		case IOR_TYPE_DSM_SERVICE_GATEWAY:
			return "DSM::ServiceGateway";
		case IOR_TYPE_DSM_STREAM_EVENT:
			return "DSM::StreamEvent";
		default:
			return "Unknown";
	}
}

int dsmcc_biop_parse_ior(struct biop_ior *ior, uint8_t *data, int data_length)
{
	int off = 0, ret, found;
	unsigned int i;
	uint32_t type_id_len, tagged_profiles_count;
	char *type_id;

	if (!dsmcc_getlong(&type_id_len, data, off, data_length))
		return -1;
	off += 4;
	if (type_id_len <= 0)
	{
		DSMCC_ERROR("type_id_len = %d\n", type_id_len);
		return -1;
	}
	type_id = (char *)(data + off);
	off += type_id_len;
	if ((type_id_len & 3) != 0)
		off += (4 - (type_id_len & 3)); // alignement gap

	if (!strcmp("DSM::Directory", type_id) || !strcmp("dir", type_id))
		ior->type = IOR_TYPE_DSM_DIRECTORY;
	else if (!strcmp("DSM::File", type_id) || !strcmp("fil", type_id))
		ior->type = IOR_TYPE_DSM_FILE;
	else if (!strcmp("DSM::Stream", type_id) || !strcmp("str", type_id))
		ior->type = IOR_TYPE_DSM_STREAM;
	else if (!strcmp("DSM::ServiceGateway", type_id) || !strcmp("srg", type_id))
		ior->type = IOR_TYPE_DSM_SERVICE_GATEWAY;
	else if (!strcmp("DSM::StreamEvent", type_id) || !strcmp("ste", type_id))
		ior->type = IOR_TYPE_DSM_STREAM_EVENT;
	else
	{
		DSMCC_ERROR("IOR with unknown type_id of length %d", type_id_len);
		return -1;
	}

	if (!dsmcc_getlong(&tagged_profiles_count, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Tagged Profiles Count = %d", tagged_profiles_count);

	found = 0;
	for (i = 0; i < tagged_profiles_count; i++)
	{
		uint32_t profile_id_tag, profile_data_length;

		if (!dsmcc_getlong(&profile_id_tag, data, off, data_length))
			return -1;
		off += 4;
		DSMCC_DEBUG("Profile Id Tag = %08x", profile_id_tag);

		if (!dsmcc_getlong(&profile_data_length, data, off, data_length))
			return -1;
		off += 4;
		DSMCC_DEBUG("Profile Data Length = %d", profile_data_length);

		if (profile_id_tag == TAG_BIOP)
		{
			if (!found)
			{
				ret = dsmcc_biop_parse_body(&ior->profile_body, data + off, data_length - off);
				if (ret < 0)
					return -1;
				found = 1;
			}
			else
				DSMCC_WARN("Already got a BIOPProfileBody, skipping profile %d", i);
		}
		else
			DSMCC_WARN("Skipping Unknown Profile %d Id Tag %08x Size %d", i, profile_id_tag, profile_data_length);

		off += profile_data_length;
	}
	if (!found)
	{
		DSMCC_ERROR("IOR does not contain a BIOPProfileBody");
		return -1;
	}

	return off;
}

void dsmcc_biop_free_ior(struct biop_ior *ior)
{
	if (ior == NULL)
		return;

	if (ior->profile_body.obj_loc.objkey != NULL)
	{
		free(ior->profile_body.obj_loc.objkey);
		ior->profile_body.obj_loc.objkey = NULL;
	}

	free(ior);
}
