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

static int dsmcc_biop_parse_dsm_conn_binder(struct biop_dsm_conn_binder *binder, unsigned char *data, int data_length)
{
	int off = 0, ret;
	struct biop_tap *tap;
	unsigned short selector_type;

	ret = dsmcc_biop_parse_taps_keep_only_first(&tap, BIOP_DELIVERY_PARA_USE, data, data_length);
	if (ret < 0)
	{
		DSMCC_ERROR("dsmcc_biop_parse_taps_return_first returned %d", ret);
		dsmcc_biop_free_tap(tap);
		return -1;
	}
	off += ret;

	if (tap->selector_length != 0x0a)
	{
		DSMCC_ERROR("Invalid selector length while parsing BIOP_DELIVERY_PARA_USE tap (got %d but expected %d)", tap->selector_length, 0x0a);
		dsmcc_biop_free_tap(tap);
		return -1;
	}

	selector_type = dsmcc_getshort(tap->selector_data);
	if (selector_type != 0x01)
	{
		DSMCC_ERROR("Invalid selector type while parsing BIOP_DELIVERY_PARA_USE tap (got %d but expected %d)", selector_type, 0x01);
		dsmcc_biop_free_tap(tap);
		return -1;
	}

	binder->transactionId = dsmcc_getlong(tap->selector_data + 2);
	binder->timeout = dsmcc_getlong(tap->selector_data + 6);
	binder->assoc_tag = tap->assoc_tag;

	dsmcc_biop_free_tap(tap);

	return off;
}

static int dsmcc_biop_parse_obj_location(struct biop_obj_location *loc, unsigned char *data, int data_length)
{
	int off = 0;

	loc->carousel_id = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("Carousel id = %ld", loc->carousel_id);

	loc->module_id = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Module id = %d", loc->module_id);

	loc->version_major = data[off++];
	loc->version_minor = data[off++];
	DSMCC_DEBUG("Version = 0x%x%02x", loc->version_major, loc->version_minor);

	loc->objkey_len = data[off++]; /* <= 4 */
	if (loc->objkey_len > 0)
	{
		loc->objkey = malloc(loc->objkey_len);
		memcpy(loc->objkey, data + off, loc->objkey_len);
	}
	else
		loc->objkey = NULL;
	off += loc->objkey_len;
	DSMCC_DEBUG("Key Length = %d", loc->objkey_len);

	return off;
}

static int dsmcc_biop_parse_body(struct biop_profile_body *body, unsigned char *data, int data_length)
{
	int off = 0, ret, i;
	unsigned char lite_components_count;
	unsigned long component_tag;
	unsigned char component_data_len;

	/* skip byte order */
	off++;

	lite_components_count = data[off];
	off++;
	if (lite_components_count < 2)
	{
		DSMCC_ERROR("Invalid number of components in BIOPProfileBody (got %d but expected at least %d)", lite_components_count, 2);
		return -1;
	}
	DSMCC_DEBUG("Lite Components Count %d", lite_components_count);

	for (i = 0; i < lite_components_count; i++)
	{
		component_tag = dsmcc_getlong(data + off);
		off += 4;

		component_data_len = data[off];
		off++;

		if (i == 0)
		{
			/* First component should be a BIOP::ObjectLocation */
			if (component_tag != TAG_ObjectLocation)
			{
				DSMCC_ERROR("Invalid component ID tag while parsing BIOP::ObjectLocation (got 0x%lx but expected 0x%lx)", component_tag, TAG_ObjectLocation);
				return -1;
			}

			ret = dsmcc_biop_parse_obj_location(&body->obj_loc, data + off, data_length - off);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_biop_parse_object returned %d", ret);
				return -1;
			}
		}
		else if (i == 1)
		{
			/* Second component should be a DSM::ConnBinder */
			if (component_tag != TAG_ConnBinder)
			{
				DSMCC_ERROR("Invalid component ID tag while parsing DSM::ConnBinder (got 0x%lx but expected 0x%lx)", component_tag, TAG_ConnBinder);
				return -1;
			}

			ret = dsmcc_biop_parse_dsm_conn_binder(&body->conn_binder, data + off, data_length - off);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_biop_parse_binder returned %d", ret);
				return -1;
			}
		}
		else
		{
			/* ignore remaining components */
			DSMCC_WARN("Ignoring unknown component %d with ID tag 0x%lx", i, component_tag);
		}

		off += component_data_len;
	}

	return off;
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

/* TODO check data_length */
int dsmcc_biop_parse_ior(struct biop_ior *ior, unsigned char *data, int data_length)
{
	int off = 0, ret, found;
	unsigned int i;
	unsigned long type_id_len, tagged_profiles_count;
	char *type_id;

	type_id_len = dsmcc_getlong(data);
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
		DSMCC_ERROR("IOR with unknown type_id of length %ld", type_id_len);
		return -1;
	}

	tagged_profiles_count = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("Tagged Profiles Count = %ld", tagged_profiles_count);

	found = 0;
	for (i = 0; i < tagged_profiles_count; i++)
	{
		unsigned long profile_id_tag, profile_data_length;

		profile_id_tag = dsmcc_getlong(data + off);
		off += 4;
		DSMCC_DEBUG("Profile Id Tag = %lx", profile_id_tag);

		profile_data_length = dsmcc_getlong(data + off);
		off += 4;
		DSMCC_DEBUG("Profile Data Length = %ld", profile_data_length);

		if (profile_id_tag == TAG_BIOP)
		{
			if (!found)
			{
				ret = dsmcc_biop_parse_body(&ior->profile_body, data + off, data_length - off);
				if (ret < 0)
				{
					DSMCC_ERROR("dsmcc_biop_parse_body returned %d", ret);
					return -1;
				}
				found = 1;
			}
			else
				DSMCC_WARN("Already got a BIOPProfileBody, skipping profile %d", i);
		}
		else
			DSMCC_WARN("Skipping Unknown Profile %d Id Tag %lx Size %d", i, profile_id_tag, profile_data_length);

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
