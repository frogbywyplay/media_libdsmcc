#include <stdlib.h>

#include "dsmcc-receiver.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-carousel.h"
#include "libdsmcc.h"


void dsmcc_objcar_free(struct obj_carousel *obj) {
	struct stream *str, *strnext;
	struct cache_module_data *cachep, *cachepnext;
	struct descriptor *desc, *last;

	/* Free gateway info */
	if(obj->gate != NULL) {

	      if(obj->gate->user_data_len > 0)
		free(obj->gate->user_data);

	      if(obj->gate->profile.type_id_len > 0)
	        free(obj->gate->profile.type_id);

	      if(obj->gate->profile.body.full.obj_loc.objkey_len>0)
		free(obj->gate->profile.body.full.obj_loc.objkey);

	      if(obj->gate->profile.body.full.dsm_conn.taps_count>0) {
	       if(obj->gate->profile.body.full.dsm_conn.tap.selector_len>0)
		 free(obj->gate->profile.body.full.dsm_conn.tap.selector_data);
	      }
	}

	/* Free stream info */
	str = obj->streams;
	while(str!=NULL) {
	     strnext = str->next;
	     free(str);
	     str = strnext;
	}

	obj->streams = NULL;

	/* Free cache info */
	cachep = obj->cache;
	while(cachep!=NULL) {
	      cachepnext = cachep->next;
	      if(cachep->data != NULL) { /* should be empty */
		      free(cachep->data);
	      }

	      if(cachep->bstatus != NULL) {
		      free(cachep->bstatus);
	      }

	      if(cachep->descriptors != NULL) { /* TODO badness */
                desc = cachep->descriptors;
                while(desc != NULL) {
                  last = desc->next;
                  dsmcc_desc_free(desc);
                  desc = last;
                }
	      }

	      free(cachep);
	      cachep = cachepnext;
	}

	dsmcc_cache_free(obj->filecache);
}
