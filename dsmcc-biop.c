#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <syslog.h>

#include "dsmcc-biop.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-receiver.h"
#include "dsmcc-cache.h"

// FILE *biop_fd;
// FILE *bd_fd;

/*
struct biop_stream *
	dsmcc_biop_process_strream() { ; }

struct biop_streamevent *
	dsmcc_biop_process_event() { ; }

*/

int
dsmcc_biop_process_msg_hdr(struct biop_message *bm, struct cache_module_data *cachep) {
	struct biop_msg_header *hdr = &bm->hdr;
	unsigned char *Data = cachep->data + cachep->curp;
	int off = 0;

	if(Data == NULL) { 
		return -1; 
	}

//	fprintf(bd_fd, "Hdr -> Checking magic\n");


	if(Data[0] !='B' || Data[1] !='I' || Data[2] !='O' || Data[3] !='P') {
		return -2;
	}

//	fprintf(bd_fd, "Hdr -> Magic OK!\n");
//	fflush(bd_fd);

	off+=4;/* skip magic */

	hdr->version_major = Data[off++];
//	fprintf(bd_fd, "Hdr -> Version Major = %d\n", hdr->version_major);
	hdr->version_minor = Data[off++];

	off+=2; /* skip byte order & message type */

//	fprintf(bd_fd, "Hdr -> Version Minor = %d\n", hdr->version_minor);
	hdr->message_size  = (Data[off] << 24) | (Data[off+1] << 16) |
		     (Data[off+2] << 8)  | Data[off+3];

	off+=4;
//	fprintf(bd_fd, "Hdr -> Message Size = %d\n", hdr->message_size);
	hdr->objkey_len = Data[off++];
//	fprintf(bd_fd, "Hdr -> ObjKey Len = %d\n", hdr->objkey_len);
	hdr->objkey = (char *)malloc(hdr->objkey_len);

	memcpy(hdr->objkey, Data+off, hdr->objkey_len);

//	fprintf(bd_fd, "Hdr -> ObjKey = %c%c%c%c\n", hdr->objkey[0], hdr->objkey[1], hdr->objkey[2], hdr->objkey[3]);

	off+= hdr->objkey_len;

	hdr->objkind_len = (Data[off] << 24) | (Data[off+1] << 16) |
			   (Data[off+2] << 8) | Data[off+3];

	off+=4;

//	fprintf(bd_fd, "Hdr -> ObjKind Len = %ld\n", hdr->objkind_len);
	hdr->objkind = (char *)malloc(hdr->objkind_len);

	memcpy(hdr->objkind, Data+off, hdr->objkind_len);

//	fprintf(bd_fd, "Hdr -> ObjKind Data = %s\n", hdr->objkind);

	off+= hdr->objkind_len;

	hdr->objinfo_len = Data[off] << 8 | Data[off+1];

	off+=2;
//	fprintf(bd_fd, "Hdr -> ObjInfo Len = %d\n", hdr->objkey_len);

	hdr->objinfo = (char *)malloc(hdr->objinfo_len);

	memcpy(hdr->objinfo, Data+off, hdr->objinfo_len);

//	fprintf(bd_fd, "Hdr -> ObjInfo = %c%c%c\n", hdr->objinfo[0], hdr->objinfo[1], hdr->objinfo[2]);

	off+= hdr->objinfo_len;

	cachep->curp += off;

	return 0;
}

int
dsmcc_biop_process_name_comp(struct biop_name_comp *comp, unsigned char *Data) {
	int off = 0;

	comp->id_len = Data[off++];

//	fprintf(bd_fd, "Dir -> Binding -> Name -> Comp -> Id Len = %d\n", comp->id_len);

	comp->id = (char *)malloc(comp->id_len);

	memcpy(comp->id, Data+off, comp->id_len);

	off+=comp->id_len;

	comp->kind_len = Data[off++];

//	fprintf(bd_fd, "Dir -> Binding -> Name -> Comp -> Kind Len = %d\n", comp->kind_len);

	comp->kind = (char *)malloc(comp->kind_len);

	memcpy(comp->kind, Data+off, comp->kind_len);

	off+= comp->kind_len;

//	fprintf(bd_fd, "Dir -> Binding -> Name -> Comp -> Kind = %s\n", comp->kind);

	return off;
}

int
dsmcc_biop_process_name(struct biop_name *name, unsigned char *Data) {
	int i, off = 0, ret;

	name->comp_count = Data[0];

//	fprintf(bd_fd, "Dir -> Binding -> Name -> Comp Count = %d\n", name->comp_count);

	off++;

	name->comps = (struct biop_name_comp *)
		malloc(sizeof(struct biop_name_comp) * name->comp_count);

	for(i = 0; i < name->comp_count; i++) {
		ret = dsmcc_biop_process_name_comp(&name->comps[i], Data+off);
		if(ret > 0) { off += ret; } else { /* TODO error */ }
	}

	return off;
}

int
dsmcc_biop_process_binding(struct biop_binding *bind, unsigned char *Data) {
	int off = 0, ret;

//	fprintf(bd_fd, "Dir -> Binding ->  Processing Name \n");
	ret = dsmcc_biop_process_name(&bind->name, Data);
	if(ret > 0) { off += ret; } else { /* TODO error */ }

	bind->binding_type = Data[off++];
//	fprintf(bd_fd, "Dir -> Binding ->  Type = %d\n", bind->binding_type);

	ret = dsmcc_biop_process_ior(&bind->ior, Data+off);
	if(ret > 0) { off += ret; } else { /*TODO error */ }

	bind->objinfo_len = (Data[off] << 8) | Data[off+1];

//	fprintf(bd_fd, "Dir -> Binding ->  ObjInfo Len = %d\n", bind->objinfo_len);
	off+=2;

	bind->objinfo = (char *)malloc(bind->objinfo_len);
	memcpy(bind->objinfo, Data+off, bind->objinfo_len);
//	fprintf(bd_fd, "Dir -> Binding ->  ObjInfo = %s\n", bind->objinfo);

	off+= bind->objinfo_len;

	return off;
}

int dsmcc_biop_process_srg(struct biop_message *bm,struct cache_module_data *cachep, struct cache *filecache) {
	unsigned int i;
	int off = 0, ret;
	unsigned char *Data = cachep->data + cachep->curp;

	off++; /* skip service context count */

	bm->body.srg.msgbody_len = (Data[off] << 24) | (Data[off+1] << 16) |
				    (Data[off+2] << 8) | Data[off+3];

//	fprintf(bd_fd, "Gateway -> MsgBody Len = %ld\n", bm->body.srg.msgbody_len);

	off+=4;

	bm->body.srg.bindings_count = Data[off] << 8 | Data[off+1];

//	fprintf(bd_fd, "Gateway -> Bindings Count = %d\n", bm->body.srg.bindings_count);
	off+=2;

	for(i = 0; i < bm->body.srg.bindings_count; i++) {
	  ret = dsmcc_biop_process_binding(&bm->body.srg.binding, Data+off);
	  if(ret > 0) { off += ret; } else { /* TODO error */ }
	  if(!strcmp("dir", bm->body.srg.binding.name.comps[0].kind)) {
		dsmcc_cache_dir_info(filecache, 0,0,NULL,&bm->body.srg.binding);
	  } else if(!strcmp("fil",bm->body.srg.binding.name.comps[0].kind)) {
		dsmcc_cache_file_info(filecache, 0,0,NULL,&bm->body.srg.binding);
	  }
	  dsmcc_biop_free_binding(&bm->body.srg.binding);
	}

	cachep->curp += off;

	return 0;
}

void
dsmcc_biop_free_binding(struct biop_binding *binding) {
	int i;

	for(i = 0; i < binding->name.comp_count; i++) {
		if(binding->name.comps[i].id_len > 0)
			free(binding->name.comps[i].id);
		if(binding->name.comps[i].kind_len > 0)
			free(binding->name.comps[i].kind);
	}

	free(binding->name.comps);

	if(binding->ior.type_id_len > 0)
		free(binding->ior.type_id);

	if(binding->ior.body.full.obj_loc.objkey_len > 0)
		free(binding->ior.body.full.obj_loc.objkey);

	if(binding->ior.body.full.dsm_conn.tap.selector_len > 0)
		free(binding->ior.body.full.dsm_conn.tap.selector_data);

	if(binding->objinfo_len > 0)
		free(binding->objinfo);
}

void dsmcc_biop_process_dir(struct biop_message *bm, struct cache_module_data *cachep, struct cache *filecache){
	unsigned int i;
	int off = 0, ret;
	unsigned char *Data = cachep->data + cachep->curp;

	off++; /* skip service context count */

	bm->body.dir.msgbody_len = (Data[off] << 24) | (Data[off+1] << 16) |
				    (Data[off+2] << 8) | Data[off+3];

//	fprintf(bd_fd, "Dir -> MsgBody Len = %ld\n", bm->body.dir.msgbody_len);
	off+=4;

	bm->body.dir.bindings_count = Data[off] << 8 | Data[off+1];

//	fprintf(bd_fd, "Dir -> Bindings Count = %d\n", bm->body.dir.bindings_count);
	off+=2;

	for(i = 0; i < bm->body.dir.bindings_count; i++) {
	  ret = dsmcc_biop_process_binding(&bm->body.dir.binding, Data+off);
	  if(ret > 0) { off+= ret; } else { /* TODO error */ }
	  if(!strcmp("dir",bm->body.dir.binding.name.comps[0].kind)) {
	    dsmcc_cache_dir_info(filecache, cachep->module_id, bm->hdr.objkey_len,
				  bm->hdr.objkey, &bm->body.dir.binding);
	  } else if(!strcmp("fil", bm->body.dir.binding.name.comps[0].kind)){
	    dsmcc_cache_file_info(filecache, cachep->module_id, bm->hdr.objkey_len,
				  bm->hdr.objkey, &bm->body.dir.binding);
	  }
	  dsmcc_biop_free_binding(&bm->body.dir.binding);
	}

	cachep->curp += off;

	filecache->num_dirs--;
}

void
dsmcc_biop_process_file(struct biop_message *bm,struct cache_module_data *cachep, struct cache *filecache){
	int off = 0;
	unsigned char *Data = cachep->data+cachep->curp;

	/* skip service contect count */

	off++;

	bm->body.file.msgbody_len = (Data[off] << 24) | (Data[off+1] << 16) |
        			     (Data[off+2] << 8) | Data[off+3];

	off+=4;

//	fprintf(bd_fd, "File -> MsgBody Len = %ld\n",bm->body.file.msgbody_len);

	bm->body.file.content_len = (Data[off] << 24) | (Data[off+1] << 16) |
      				     (Data[off+2] << 8)  | Data[off+3];

	off+=4;

//	fprintf(bd_fd, "File -> Content Len = %ld\n", bm->body.file.content_len);

	cachep->curp += off;


	dsmcc_cache_file(filecache, bm, cachep);

	cachep->curp += bm->body.file.content_len;
}

void
dsmcc_biop_process_data(struct cache *filecache, struct cache_module_data *cachep) {
	struct biop_message bm;
	struct descriptor *desc;
	int ret;
	unsigned int len;
	static int i = 0;

//	bd_fd = fopen("/tmp/biop_data", "a");

	for(desc = cachep->descriptors; desc != NULL; desc=desc->next) {
		if(desc->tag == 0x09) { break; }
	}

	if(desc != NULL) {
		len = desc->data.compressed.original_size;
	} else {
		len = cachep->size;
	}

	cachep->curp = 0;

	if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd, "[libbiop] Module size (uncompressed) = %d\n", len);
	}
//	fprintf(bd_fd, "Full Length - %d\n", len);

	/* Replace off with cachep->curp.... */
	while(cachep->curp < len) {
//		fprintf(bd_fd, "Current %ld / Full %d\n", cachep->curp, len);

//		fprintf(bd_fd, "Processing header\n");
		/* Parse header */
		ret = dsmcc_biop_process_msg_hdr(&bm, cachep);
		if(ret < 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Invalid biop header, dropping rest of module\n");
			/* not valid, skip rest of data */
			}
			break;
		}

		/* Handle each message type */

		if(strcmp(bm.hdr.objkind, "fil") == 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Processing file\n");
			}
			dsmcc_biop_process_file(&bm, cachep, filecache);
		} else if(strcmp(bm.hdr.objkind, "dir") == 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Processing directory\n");
			}
			dsmcc_biop_process_dir(&bm, cachep, filecache);
		} else if(strcmp(bm.hdr.objkind, "srg") == 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Processing gateway\n");
			}
			dsmcc_biop_process_srg(&bm, cachep, filecache);
		} else if(strcmp(bm.hdr.objkind, "str") == 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Processing stream (todo)\n");
			}
			/*dsmcc_biop_process_stream(&bm, cachep); */
		} else if(strcmp(bm.hdr.objkind, "ste") == 0) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd, "[libbiop] Processing events (todo)\n");
			}
			/*dsmcc_biop_process_event(&bm, cachep); */
		} else {
			/* Error */
		}

		free(bm.hdr.objkey);
		free(bm.hdr.objkind);
		free(bm.hdr.objinfo);
	}

//	fclose(bd_fd);

}

int
dsmcc_biop_process_module_info(struct biop_module_info *modinfo, unsigned char *Data) {
	int off, ret;

//	biop_fd = fopen("/tmp/biop2.debug", "a");

	modinfo->mod_timeout = (Data[0] << 24 ) | (Data[1] << 16) |
       			       (Data[2] << 8 )  | Data[3];

//	fprintf(biop_fd, "Module Info -> Mod Timeout = %ld\n", modinfo->mod_timeout);

	modinfo->block_timeout = (Data[4] << 24) | (Data[5] << 16) |
				 (Data[6] << 8) | Data[7];

//	fprintf(biop_fd, "Module Info -> BLock Timeout = %ld\n", modinfo->block_timeout);

	modinfo->min_blocktime = (Data[8] << 24) | (Data[9] << 16) |
				 (Data[10] << 8) | Data[11];

//	fprintf(biop_fd, "Module Info -> Min Block Timeout = %ld\n", modinfo->min_blocktime);

	modinfo->taps_count = Data[12];

//	fprintf(biop_fd,"Module Info -> Taps Count = %d\n",modinfo->taps_count);

	off = 13;

	/* only 1 allowed TODO - may not be first though ? */
	ret = dsmcc_biop_process_tap(&modinfo->tap, Data+off);
	if(ret > 0) { off += ret; } else { /* TODO error */ }

	modinfo->userinfo_len = Data[off++];

//	fprintf(biop_fd, "Module Info -> UserInfo Len = %d\n", modinfo->userinfo_len);

	if(modinfo->userinfo_len > 0) {
		modinfo->descriptors = 
			dsmcc_desc_process(Data+off,modinfo->userinfo_len,&off);
	} else {
		modinfo->descriptors = NULL;
	}

//	fclose(biop_fd);

	return off;

}

int
dsmcc_biop_process_tap(struct biop_tap *tap, unsigned char *Data) {
	int off=0;

	tap->id = (Data[0] << 8) | Data[1];
//	fprintf(biop_fd, "Tap -> ID = %X\n",tap->id);
	off+=2;
	tap->use = (Data[off] << 8) | Data[off+1];
//	fprintf(biop_fd, "Tap -> Use = %X\n",tap->use);
	off+=2;
	tap->assoc_tag = (Data[off] << 8) | Data[off+1];
//	syslog(LOG_ERR, ("Tap for stream %X", tap->assoc_tag);

//	fprintf(biop_fd, "Tap -> Assoc = %X\n",tap->assoc_tag);
	off+=2;
	tap->selector_len = Data[off++];
//	fprintf(biop_fd, "Tap -> Selector Length= %d\n",tap->selector_len);

	tap->selector_data = (char *)malloc(tap->selector_len);

	memcpy(tap->selector_data, Data+off, tap->selector_len);

	off+=tap->selector_len;

	return off;
}

int
dsmcc_biop_process_binder(struct biop_dsm_connbinder *binder, unsigned char *Data) {
	int off = 0, ret;

	binder->component_tag = (Data[0] << 24) | (Data[1] << 16) |
				(Data[2] << 8)  | Data[3];

	off+=4;

//	fprintf(biop_fd, "Binder -> Component_tag = %lX\n", binder->component_tag);

	binder->component_data_len = Data[off++];

//	fprintf(biop_fd, "Binder -> Component data len = %d\n", binder->component_data_len);

	binder->taps_count = Data[off++];

//	fprintf(biop_fd, "Binder -> Taps count = %d\n",binder->taps_count);

	/* UKProfile - only first tap read */

	ret = dsmcc_biop_process_tap(&binder->tap, Data+off);
	if(ret > 0) { off += ret; } else { /* TODO error */ }

	return off;
}

int
dsmcc_biop_process_object(struct biop_obj_location *loc, unsigned char *Data) {
	int off = 0;

	loc->component_tag = (Data[0] << 24) | (Data[1] << 16) |
			     (Data[2] << 8)  | Data[3];

//	fprintf(biop_fd, "Object -> Component_tag = %lX\n",loc->component_tag);

	off+=4;

	loc->component_data_len = Data[off++];

//	fprintf(biop_fd, "Object -> Component data len = %d\n", loc->component_data_len);

	loc->carousel_id = (Data[off] << 24) | (Data[off+1] << 16) |
			   (Data[off+2] << 8)  | Data[off+3];

//	fprintf(biop_fd, "Object -> Carousel id = %ld\n",loc->carousel_id);

	off+=4;

	loc->module_id = (Data[off] << 8) | Data[off+1];

//	fprintf(biop_fd, "Object -> Module id = %d\n",loc->module_id);

	off+=2;

	loc->version_major = Data[off++];
	loc->version_minor = Data[off++];

//	fprintf(biop_fd, "Object -> Version = (%d/%d)\n", loc->version_major, loc->version_minor);

	loc->objkey_len = Data[off++]; /* <= 4 */

//	fprintf(biop_fd, "Object -> Key Length = %d\n",loc->objkey_len );

	loc->objkey = (char *)malloc(loc->objkey_len);

	memcpy(loc->objkey, Data+off, loc->objkey_len);

	off+=loc->objkey_len;

	return off;
}


int
dsmcc_biop_process_lite(struct biop_profile_lite *lite, unsigned char *Data) { 
	syslog(LOG_ERR, "BiopLite - Not Implemented Yet");
	return 0; }

int
dsmcc_biop_process_body(struct biop_profile_body *body, unsigned char *Data) {
	int off = 0, ret;

	body->data_len = (Data[off] << 24) | (Data[off+1] << 16) |
	 		 (Data[off+2] << 8) | Data[off+3];

//	fprintf(biop_fd, "Body -> Data Length = %ld\n", body->data_len);

	off+=4;

	/* skip bit order */

	off+=1;

	body->lite_components_count = Data[off++];

//	fprintf(biop_fd, "Body -> Lite Components Count= %x\n", body->lite_components_count);

	ret = dsmcc_biop_process_object(&body->obj_loc, Data+off);
	if(ret > 0) { off += ret; } else { /* TODO error */ }

	ret = dsmcc_biop_process_binder(&body->dsm_conn, Data+off);
	if(ret > 0) { off += ret; } else { /* TODO error */ }

	/* UKProfile - ignore anything else */

	return off;
}

int
dsmcc_biop_process_ior(struct biop_ior *ior, unsigned char *Data) {
	int off = 0, ret;

//	biop_fd = fopen("/tmp/biop.debug", "a");
//	fprintf(biop_fd, "New BIOP IOR\n");

	ior->type_id_len = (Data[0] << 24) | (Data[1] << 16) |
			   (Data[2] << 8)  | (Data[3]);

//	fprintf(biop_fd, "Type id length = %ld\n", ior->type_id_len);

	ior->type_id = (char *)malloc(ior->type_id_len);

	off+=4;

	memcpy(ior->type_id, Data+off, ior->type_id_len);

	off+=ior->type_id_len;

	ior->tagged_profiles_count = (Data[off] << 24) | (Data[off+1] << 16) |
       				     (Data[off+2] << 8) | (Data[off+3]);

//	fprintf(biop_fd,"Tagged Profiles Count= %ld\n", ior->tagged_profiles_count);

	off+=4;

	ior->profile_id_tag = (Data[off] << 24) | (Data[off+1] << 16) |
      			      (Data[off+2] << 8)  | Data[off+3];

//	fprintf(biop_fd, "Profile Id Tag= %lX\n", ior->profile_id_tag);
	off+=4;

//	fprintf(biop_fd, "Profile Id Tag last= %lX\n", (ior->profile_id_tag & 0xFF));

	if((ior->profile_id_tag & 0xFF) == 0x06) {
		ret = dsmcc_biop_process_body(&ior->body.full, Data+off);
		if(ret > 0) { off += ret; } else { /* TODO error */ }
	} else if((ior->profile_id_tag & 0xFF) == 0x05) {
		ret = dsmcc_biop_process_lite(&ior->body.lite, Data+off);
		if(ret > 0) { off += ret; } else { /* TODO error */ }
	}

	/* UKProfile - receiver may ignore other profiles */

//	fclose(biop_fd);

	return off;
}

