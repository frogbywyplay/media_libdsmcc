#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

#include "libdsmcc.h"
#include "dsmcc-util.h"
#include "dsmcc-biop.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-receiver.h"
#include "dsmcc-cache.h"

static int dsmcc_biop_process_name(struct biop_name *,unsigned char *);
static int dsmcc_biop_process_name_comp(struct biop_name_comp*,unsigned char *);
static int dsmcc_biop_process_binding(struct biop_binding*,unsigned char *Data);
static int dsmcc_biop_process_srg(struct biop_message *, struct cache_module_data *cachep, struct cache *cache);
static void dsmcc_biop_process_dir(struct biop_message *, struct cache_module_data *cachep, struct cache *cache);
static void dsmcc_biop_process_file(struct biop_message *, struct cache_module_data *cachep, struct cache *cache);
static int dsmcc_biop_process_msg_hdr(struct biop_message *, struct cache_module_data *cachep);
static int dsmcc_biop_process_tap(struct biop_tap *, unsigned char *);
static int dsmcc_biop_process_binder(struct biop_dsm_connbinder*, unsigned char*);
static int dsmcc_biop_process_object(struct biop_obj_location*, unsigned char*);
static int dsmcc_biop_process_body(struct biop_profile_body *,unsigned char *);
static int dsmcc_biop_process_lite(struct biop_profile_lite *, unsigned char *);
static void dsmcc_biop_free_binding(struct biop_binding *);
static int dsmcc_biop_nmap_data(struct cache *filecache, struct cache_module_data *cachep);

int dsmcc_biop_process_msg_hdr(struct biop_message *bm, struct cache_module_data *cachep)
{
	struct biop_msg_header *hdr = &bm->hdr;
	unsigned char *data;
	int off = 0;

	if (cachep->data_ptr == NULL)
	{
		DSMCC_ERROR("[biop] data_ptr is NULL\n");
		return -1;
	}

	data = cachep->data_ptr + cachep->curp;

	DSMCC_DEBUG("[biop] MsgHdr -> Checking magic\n");
	if (data[0] !='B' || data[1] !='I' || data[2] !='O' || data[3] !='P')
	{
		DSMCC_ERROR("[biop] MsgHdr -> Invalid magic: expected 'BIOP' but got '%c%c%c%c'\n", data[0], data[1], data[2], data[3]);
		return -2;
	}
	off += 4; /* skip magic */
	DSMCC_DEBUG("[biop] MsgHdr -> Magic OK!\n");

	hdr->version_major = data[off++];
	hdr->version_minor = data[off++];
	DSMCC_DEBUG("[biop] MsgHdr -> Version Major = %d\n", hdr->version_major);
	DSMCC_DEBUG("[biop] MsgHdr -> Version Minor = %d\n", hdr->version_minor);

	/* skip byte order & message type */
	off += 2;

	hdr->message_size  = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] MsgHdr -> Message Size = %d\n", hdr->message_size);

	hdr->objkey_len = data[off++];
	if (hdr->objkey_len > 0)
	{
		hdr->objkey = (char *) malloc(hdr->objkey_len);
		memcpy(hdr->objkey, data + off, hdr->objkey_len);
	}
	else
		hdr->objkey = NULL;
	off += hdr->objkey_len;
	DSMCC_DEBUG("[biop] MsgHdr -> ObjKey Len = %d\n", hdr->objkey_len);
	DSMCC_DEBUG("[biop] MsgHdr -> ObjKey = %02x%02x%02x%02x\n", hdr->objkey[0], hdr->objkey[1], hdr->objkey[2], hdr->objkey[3]);

	hdr->objkind_len = dsmcc_getlong(data + off);
	off += 4;
	if (hdr->objkind_len > 0)
	{
		hdr->objkind = (char *) malloc(hdr->objkind_len);
		memcpy(hdr->objkind, data + off, hdr->objkind_len);
	}
	else
		hdr->objkind = NULL;
	off += hdr->objkind_len;
	DSMCC_DEBUG("[biop] MsgHdr -> ObjKind Len = %ld\n", hdr->objkind_len);
	DSMCC_DEBUG("[biop] MsgHdr -> ObjKind Data = %s\n", hdr->objkind);

	hdr->objinfo_len = dsmcc_getshort(data + off);
	off += 2;
	if (hdr->objinfo_len > 0)
	{
		hdr->objinfo = (char *) malloc(hdr->objinfo_len);
		memcpy(hdr->objinfo, data + off, hdr->objinfo_len);
	}
	else
		hdr->objinfo = NULL;
	off += hdr->objinfo_len;
	DSMCC_DEBUG("[biop] MsgHdr -> ObjInfo Len = %d\n", hdr->objkey_len);
	switch (hdr->objinfo_len)
	{
		case 1:
			DSMCC_DEBUG("[biop] MsgHdr -> ObjInfo = %02x%02x%02x\n", hdr->objinfo[0]);
			break;
		case 2:
			DSMCC_DEBUG("[biop] MsgHdr -> ObjInfo = %02x%02x%02x\n", hdr->objinfo[0], hdr->objinfo[1]);
			break;
		case 3:
			DSMCC_DEBUG("[biop] MsgHdr -> ObjInfo = %02x%02x%02x\n", hdr->objinfo[0], hdr->objinfo[1], hdr->objinfo[2]);
			break;
	}

	cachep->curp += off;

	return 0;
}

static int dsmcc_biop_process_name_comp(struct biop_name_comp *comp, unsigned char *data)
{
	int off = 0;

	comp->id_len = data[off++];
	if (comp->id_len > 0)
	{
		comp->id = (char *) malloc(comp->id_len);
		memcpy(comp->id, data + off, comp->id_len);
	}
	else
		comp->id = NULL;
	off += comp->id_len;
	DSMCC_DEBUG("[biop] Dir -> Binding -> Name -> Comp -> Id Len = %d\n", comp->id_len);
	DSMCC_DEBUG("[biop] Dir -> Binding -> Name -> Comp -> Id = %s\n", comp->id);

	comp->kind_len = data[off++];
	if (comp->kind_len > 0)
	{
		comp->kind = (char *) malloc(comp->kind_len);
		memcpy(comp->kind, data + off, comp->kind_len);
	}
	else
		comp->kind = NULL;
	off += comp->kind_len;
	DSMCC_DEBUG("[biop] Dir -> Binding -> Name -> Comp -> Kind Len = %d\n", comp->kind_len);
	DSMCC_DEBUG("[biop] Dir -> Binding -> Name -> Comp -> Kind = %s\n", comp->kind);

	return off;
}

static int dsmcc_biop_process_name(struct biop_name *name, unsigned char *data)
{
	int i, off = 0, ret;

	name->comp_count = data[off++];

	DSMCC_DEBUG("[biop] Dir -> Binding -> Name -> Comp Count = %d\n", name->comp_count);

	name->comps = (struct biop_name_comp *) malloc(sizeof(struct biop_name_comp) * name->comp_count);
	for (i = 0; i < name->comp_count; i++)
	{
		ret = dsmcc_biop_process_name_comp(&name->comps[i], data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			DSMCC_ERROR("[biop] dsmcc_biop_process_name_comp returned %d\n", ret);
			return -1;
		}
	}

	return off;
}

int dsmcc_biop_process_binding(struct biop_binding *bind, unsigned char *data)
{
	int off = 0, ret;

	DSMCC_DEBUG("[biop] Dir -> Binding -> Processing Name\n");
	ret = dsmcc_biop_process_name(&bind->name, data);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_name returned %d\n", ret);
		return -1;
	}

	bind->binding_type = data[off++];
	DSMCC_DEBUG("[biop] Dir -> Binding -> Type = %d\n", bind->binding_type);

	ret = dsmcc_biop_process_ior(&bind->ior, data + off);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_ior returned %d\n", ret);
		return -1;
	}

	bind->objinfo_len = dsmcc_getshort(data + off);
	off += 2;
	if (bind->objinfo_len > 0)
	{
		bind->objinfo = (char *) malloc(bind->objinfo_len);
		memcpy(bind->objinfo, data + off, bind->objinfo_len);
	}
	else
		bind->objinfo = NULL;
	off += bind->objinfo_len;
	DSMCC_DEBUG("[biop] Dir -> Binding -> ObjInfo Len = %d\n", bind->objinfo_len);
	DSMCC_DEBUG("[biop] Dir -> Binding -> ObjInfo = %s\n", bind->objinfo);

	return off;
}

int dsmcc_biop_process_srg(struct biop_message *bm, struct cache_module_data *cachep, struct cache *filecache)
{
	unsigned int i;
	int off = 0, ret;
	unsigned char *data = cachep->data_ptr + cachep->curp;

	/* skip service context count */
	off++;

	bm->body.srg.msgbody_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Gateway -> MsgBody Len = %ld\n", bm->body.srg.msgbody_len);

	bm->body.srg.bindings_count = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[biop] Gateway -> Bindings Count = %d\n", bm->body.srg.bindings_count);

	for(i = 0; i < bm->body.srg.bindings_count; i++)
	{
		ret = dsmcc_biop_process_binding(&bm->body.srg.binding, data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			DSMCC_ERROR("[biop] dsmcc_biop_process_binding returned %d\n", ret);
			return -1;
		}
		
		if(!strcmp("dir", bm->body.srg.binding.name.comps[0].kind))
			dsmcc_cache_dir_info(filecache, 0, 0, NULL, &bm->body.srg.binding);
		else if(!strcmp("fil", bm->body.srg.binding.name.comps[0].kind))
			dsmcc_cache_file_info(filecache, 0, 0, NULL, &bm->body.srg.binding);
		else
		{
			DSMCC_ERROR("[biop] Skipping unknown kind \"%s\"\n", bm->body.srg.binding.name.comps[0].kind);
		}
		dsmcc_biop_free_binding(&bm->body.srg.binding);
	}

	cachep->curp += off;

	return 0;
}

void dsmcc_biop_free_binding(struct biop_binding *binding)
{
	int i;

	for (i = 0; i < binding->name.comp_count; i++)
	{
		if (binding->name.comps[i].id != NULL)
			free(binding->name.comps[i].id);
		if (binding->name.comps[i].kind != NULL)
			free(binding->name.comps[i].kind);
	}
	free(binding->name.comps);

	if (binding->ior.type_id != NULL)
		free(binding->ior.type_id);

	if (binding->ior.body.full.obj_loc.objkey != NULL)
		free(binding->ior.body.full.obj_loc.objkey);

	if (binding->ior.body.full.dsm_conn.tap.selector_data != NULL)
		free(binding->ior.body.full.dsm_conn.tap.selector_data);

	if (binding->objinfo != NULL)
		free(binding->objinfo);
}

void dsmcc_biop_process_dir(struct biop_message *bm, struct cache_module_data *cachep, struct cache *filecache)
{
	unsigned int i;
	int off = 0, ret;
	unsigned char *data = cachep->data_ptr + cachep->curp;

	/* skip service context count */
	off++;

	bm->body.dir.msgbody_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Dir -> MsgBody Len = %ld\n", bm->body.dir.msgbody_len);

	bm->body.dir.bindings_count = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[biop] Dir -> Bindings Count = %d\n", bm->body.dir.bindings_count);

	for (i = 0; i < bm->body.dir.bindings_count; i++)
	{
		ret = dsmcc_biop_process_binding(&bm->body.dir.binding, data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			/* TODO handle error */
			DSMCC_ERROR("[biop] dsmcc_biop_process_binding returned %d\n", ret);
			return;
		}
	
		if (!strcmp("dir",bm->body.dir.binding.name.comps[0].kind))
			dsmcc_cache_dir_info(filecache, cachep->module_id, bm->hdr.objkey_len,
				bm->hdr.objkey, &bm->body.dir.binding);
		else if(!strcmp("fil", bm->body.dir.binding.name.comps[0].kind))
			dsmcc_cache_file_info(filecache, cachep->module_id, bm->hdr.objkey_len,
				bm->hdr.objkey, &bm->body.dir.binding);
		else
		{
			DSMCC_ERROR("[biop] Skipping unknown object kind \"%s\"\n", bm->body.srg.binding.name.comps[0].kind);
		}

		dsmcc_biop_free_binding(&bm->body.dir.binding);
	}

	cachep->curp += off;

	filecache->num_dirs--;
}

void dsmcc_biop_process_file(struct biop_message *bm,struct cache_module_data *cachep, struct cache *filecache)
{
	int off = 0;
	unsigned char *data = cachep->data_ptr + cachep->curp;

	/* skip service context count */
	off++;

	bm->body.file.msgbody_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] File -> MsgBody Len = %ld\n", bm->body.file.msgbody_len);

	bm->body.file.content_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] File -> Content Len = %ld\n", bm->body.file.content_len);

	cachep->curp += off;

	dsmcc_cache_file(filecache, bm, cachep);

	cachep->curp += bm->body.file.content_len;
}

int dsmcc_biop_nmap_data(struct cache *filecache, struct cache_module_data *cachep)
{
	int fd;
	int err;

	fd = open(cachep->data_file, O_RDONLY);
	if (fd < 0)
	{
		DSMCC_ERROR("[biop] Can't open temporary file '%s' : %s\n", cachep->data_file, strerror(errno));
		return -1;
	}

	cachep->data_ptr = mmap(NULL, cachep->size, PROT_READ, MAP_PRIVATE, fd, 0);
	err = errno;
	close(fd);
	if (cachep->data_ptr == MAP_FAILED)
	{
		DSMCC_ERROR("[biop] Mmap failed on '%s' : %s\n", cachep->data_file, strerror(err));
		return -1;
	}

	return 0;
}

void dsmcc_biop_process_data(struct cache *filecache, struct cache_module_data *cachep)
{
	struct biop_message bm;
	struct descriptor *desc;
	int ret;
	unsigned int len;
	static int i = 0;

	for (desc = cachep->descriptors; desc; desc=desc->next)
	{
		if (desc->tag == 0x09)
			break;
	}

	if (desc)
		len = desc->data.compressed.original_size;
	else
		len = cachep->size;

	cachep->curp = 0;

	DSMCC_DEBUG("[biop] Module size (uncompressed) = %d\n", len);

	if (dsmcc_biop_nmap_data(filecache, cachep) < 0)
	{
		/* TODO handle error */
		return;
	}

	/* Replace off with cachep->curp.... */
	while (cachep->curp < len)
	{
		DSMCC_DEBUG("[biop] Current %ld / Full %d\n", cachep->curp, len);

		/* Parse header */
		DSMCC_DEBUG("[biop] Processing header\n");
		ret = dsmcc_biop_process_msg_hdr(&bm, cachep);
		if (ret < 0)
		{
			DSMCC_ERROR("[biop] Invalid biop header, dropping rest of module\n");
			/* not valid, skip rest of data */
			break;
		}

		/* Handle each message type */
		if (strcmp(bm.hdr.objkind, "fil") == 0)
		{
			DSMCC_DEBUG("[biop] Processing file\n");
			dsmcc_biop_process_file(&bm, cachep, filecache);
		}
		else if(strcmp(bm.hdr.objkind, "dir") == 0)
		{
			DSMCC_DEBUG("[biop] Processing directory\n");
			dsmcc_biop_process_dir(&bm, cachep, filecache);
		}
		else if(strcmp(bm.hdr.objkind, "srg") == 0)
		{
			DSMCC_DEBUG("[biop] Processing gateway\n");
			dsmcc_biop_process_srg(&bm, cachep, filecache);
		}
		else if(strcmp(bm.hdr.objkind, "str") == 0)
		{
			DSMCC_ERROR("[biop] Don't known of to handle stream objects, dropping rest of module\n");
			break;
		}
		else if(strcmp(bm.hdr.objkind, "ste") == 0)
		{
			DSMCC_ERROR("[biop] Don't known of to handle stream event objects, dropping rest of module\n");
			break;
		}
		else
		{
			/* Error */
			DSMCC_ERROR("[biop] Don't known of to handle unknown object (kind \"%s\"), dropping rest of module\n", bm.hdr.objkind);
			break;
		}

		free(bm.hdr.objkey);
		free(bm.hdr.objkind);
		free(bm.hdr.objinfo);
	}

	if (munmap(cachep->data_ptr, cachep->size) < 0)
	{
		DSMCC_ERROR("[biop] munmap error : %s\n", strerror(errno));
	}
}

int dsmcc_biop_process_module_info(struct biop_module_info *modinfo, unsigned char *data)
{
	int off = 0, ret;

	modinfo->mod_timeout = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("[biop] Module Info -> Mod Timeout = %ld\n", modinfo->mod_timeout);

	modinfo->block_timeout = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Module Info -> Block Timeout = %ld\n", modinfo->block_timeout);

	modinfo->min_blocktime = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Module Info -> Min Block Timeout = %ld\n", modinfo->min_blocktime);

	modinfo->taps_count = data[off++];
	DSMCC_DEBUG("[biop] Module Info -> Taps Count = %d\n", modinfo->taps_count);

	/* only 1 allowed TODO - may not be first though ? */
	ret = dsmcc_biop_process_tap(&modinfo->tap, data + off);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_tap returned %d\n", ret);
		return -1;
	}

	modinfo->userinfo_len = data[off++];
	DSMCC_DEBUG("[biop] Module Info -> UserInfo Len = %d\n", modinfo->userinfo_len);

	if (modinfo->userinfo_len > 0)
	{
		int read = 0;
		modinfo->descriptors = dsmcc_desc_process(data + off, modinfo->userinfo_len, &read);
		if (read != modinfo->userinfo_len)
			DSMCC_DEBUG("[biop] Descriptor processing has not used the correct amount of data (%d instead of %d)\n", read, modinfo->userinfo_len);

		off += modinfo->userinfo_len;
	}
	else
	{
		modinfo->descriptors = NULL;
	}

	return off;
}

int dsmcc_biop_process_tap(struct biop_tap *tap, unsigned char *data)
{
	int off = 0;

	tap->id = dsmcc_getshort(data);
	off += 2;
	DSMCC_DEBUG("[biop] Tap -> ID = %X\n",tap->id);

	tap->use = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[biop] Tap -> Use = %X\n",tap->use);

	tap->assoc_tag = dsmcc_getshort(data + off);
	off+=2;
	DSMCC_DEBUG("[biop] Tap -> Assoc = %X\n", tap->assoc_tag);

	tap->selector_len = data[off++];
	if (tap->selector_len > 0)
	{
		tap->selector_data = (char *) malloc(tap->selector_len);
		memcpy(tap->selector_data, data + off, tap->selector_len);
	}
	else
		tap->selector_data = NULL;
	off += tap->selector_len;
	DSMCC_DEBUG("[biop] Tap -> Selector Length = %d\n", tap->selector_len);

	return off;
}

int dsmcc_biop_process_binder(struct biop_dsm_connbinder *binder, unsigned char *data)
{
	int off = 0, ret;

	binder->component_tag = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("[biop] Binder -> Component_tag = %lX\n", binder->component_tag);

	binder->component_data_len = data[off++];
	DSMCC_DEBUG("[biop] Binder -> Component data len = %d\n", binder->component_data_len);

	binder->taps_count = data[off++];
	DSMCC_DEBUG("[biop] Binder -> Taps count = %d\n", binder->taps_count);

	/* UKProfile - only first tap read */
	ret = dsmcc_biop_process_tap(&binder->tap, data + off);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_tap returned %d\n", ret);
		return -1;
	}

	return off;
}

int dsmcc_biop_process_object(struct biop_obj_location *loc, unsigned char *data)
{
	int off = 0;

	loc->component_tag = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("[biop] Object -> Component_tag = %lX\n", loc->component_tag);

	loc->component_data_len = data[off++];
	DSMCC_DEBUG("[biop] Object -> Component data len = %d\n", loc->component_data_len);

	loc->carousel_id = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Object -> Carousel id = %ld\n", loc->carousel_id);

	loc->module_id = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[biop] Object -> Module id = %d\n", loc->module_id);

	loc->version_major = data[off++];
	loc->version_minor = data[off++];
	DSMCC_DEBUG("[biop] Object -> Version = (%d/%d)\n", loc->version_major, loc->version_minor);

	loc->objkey_len = data[off++]; /* <= 4 */
	if (loc->objkey_len > 0)
	{
		loc->objkey = (char *) malloc(loc->objkey_len);
		memcpy(loc->objkey, data + off, loc->objkey_len);
	}
	else
		loc->objkey = NULL;
	off += loc->objkey_len;
	DSMCC_DEBUG("[biop] Object -> Key Length = %d\n", loc->objkey_len);

	return off;
}


int dsmcc_biop_process_lite(struct biop_profile_lite *lite, unsigned char *data)
{
	DSMCC_ERROR("[biop] BiopLite not implemented yet\n");
	return -1;
}


int dsmcc_biop_process_body(struct biop_profile_body *body, unsigned char *data)
{
	int off = 0, ret;

	body->data_len = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("[biop] Body -> Data Length = %ld\n", body->data_len);

	/* skip byte order */
	off++;

	body->lite_components_count = data[off++];
	DSMCC_DEBUG("[biop] Body -> Lite Components Count = %x\n", body->lite_components_count);

	ret = dsmcc_biop_process_object(&body->obj_loc, data + off);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_object returned %d\n", ret);
		return -1;
	}

	ret = dsmcc_biop_process_binder(&body->dsm_conn, data + off);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		DSMCC_ERROR("[biop] dsmcc_biop_process_binder returned %d\n", ret);
		return -1;
	}

	/* UKProfile - ignore anything else */
	return off;
}

int dsmcc_biop_process_ior(struct biop_ior *ior, unsigned char *data)
{
	int off = 0, ret;

	DSMCC_DEBUG("[biop] New BIOP IOR\n");

	ior->type_id_len = dsmcc_getlong(data);
	off += 4;
	if (ior->type_id_len > 0)
	{
		ior->type_id = (char *)malloc(ior->type_id_len);
		memcpy(ior->type_id, data + off, ior->type_id_len);
	}
	else
		ior->type_id = NULL;
	off += ior->type_id_len;
	DSMCC_DEBUG("[biop] Type id length = %ld\n", ior->type_id_len);

	ior->tagged_profiles_count = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Tagged Profiles Count = %ld\n", ior->tagged_profiles_count);

	ior->profile_id_tag = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[biop] Profile Id Tag = %lX\n", ior->profile_id_tag);

	if ((ior->profile_id_tag & 0xFF) == 0x06)
	{
		ret = dsmcc_biop_process_body(&ior->body.full, data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			DSMCC_ERROR("[biop] dsmcc_biop_process_body returned %d\n", ret);
			return -1;
		}
	}
	else if((ior->profile_id_tag & 0xFF) == 0x05)
	{
		ret = dsmcc_biop_process_lite(&ior->body.lite, data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			DSMCC_ERROR("[biop] dsmcc_biop_process_lite returned %d\n", ret);
			return -1;
		}
	}

	/* UKProfile - receiver may ignore other profiles */

	return off;
}

