#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include "dsmcc-cache.h"
#include "dsmcc-biop.h"
#include "dsmcc-receiver.h"
#include "dsmcc-descriptor.h"
// #include <mpatrol.h>

FILE *cache_fd = NULL;
/* TODO This should be stored in obj_carousel structure  */

void
dsmcc_cache_init(struct cache *filecache, const char *channel_name, FILE *debug_fd) {

	/* TODO - load cache from disk into obj_carousel */

	filecache->gateway = filecache->dir_cache = NULL;
	filecache->file_cache = NULL; filecache->data_cache = NULL;

	/* Write contents under channel name */
	if(channel_name) {
		filecache->name = (char*)malloc(strlen(channel_name)+1);
		strcpy(filecache->name, channel_name);
	} else {
		filecache->name = (char*)malloc(1);
		filecache->name = '\0';
	}

	mkdir("/tmp/cache", 0755);	/* By popular demand... */

	filecache->num_files = filecache->num_dirs = filecache->total_files 
		= filecache->total_dirs = 0;

	filecache->files = NULL;

	filecache->debug_fd = debug_fd;
}

void
dsmcc_cache_free(struct cache *filecache) {
	struct cache_file *f, *fn;
	struct cache_dir *d, *dn;

	/* Free unconnected files */
	f = filecache->file_cache;
	while(f!=NULL) {
	  fn = f->next;
	  if(f->key_len>0) { free(f->key); }
	  if(f->filename!=NULL) { free(f->filename); }
	  if(f->data!=NULL) { free(f->data); }
	  if(f->p_key_len>0) { free(f->p_key); }
	  free(f);
	  f = fn;
	}
	/* Free cached data */
	f = filecache->data_cache;
	while(f!=NULL) {
	  fn = f->next;
	  if(f->key_len>0) { free(f->key); }
	  if(f->data!=NULL) { free(f->data); }
	  free(f);
	  f = fn;
	}
	/* Free unconnected dirs */
	d = filecache->dir_cache;
	while(d!=NULL) {
	  dn = d->next;
	  if(d->name!=NULL) { free(d->name); }
	  if(d->dirpath!=NULL) { free(d->dirpath); }
	  if(d->key_len>0) { free(d->key); }
	  if(d->p_key_len>0) { free(d->p_key); }
	  f = d->files;
	  while(f!=NULL) {
	    fn = f->next;
	    if(f->key_len>0) { free(f->key); }
	    if(f->filename!=NULL) { free(f->filename); }
	    if(f->data!=NULL) { free(f->data); }
	    if(f->p_key_len>0) { free(f->p_key); }
	    free(f);
	    f = fn;
	  }
	  free(d);
	  d = dn;
	}

	/* Free cache - TODO improve this */

	if(filecache->gateway != NULL)
		dsmcc_cache_free_dir(filecache->gateway);

	filecache->file_cache = filecache->data_cache = NULL;
	filecache->gateway = filecache->dir_cache = NULL;

	if(filecache->name)
		free(filecache->name);
}

void
dsmcc_cache_free_dir(struct cache_dir *d) {
	struct cache_dir *dn, *dnn;
	struct cache_file *f, *fn;

	if(d->sub!=NULL) {
		dn = d->sub;
		while(dn!=NULL) {
		  dnn = dn->next;
		  dsmcc_cache_free_dir(dn);
		  dn = dnn;
		}
	}

	if(d->name!=NULL) { free(d->name); }
	if(d->dirpath!=NULL) { free(d->dirpath); }
	if(d->key_len>0) { free(d->key); }
	if(d->p_key_len>0) { free(d->p_key); }
	f = d->files;
	while(f!=NULL) {
	  fn = f->next;
	  if(f->key_len>0) { free(f->key); }
	  if(f->filename!=NULL) { free(f->filename); }
	  if(f->data!=NULL) { free(f->data); }
	  if(f->p_key_len>0) { free(f->p_key); }
	  free(f);
	  f = fn;
	}
	free(d);
}

unsigned int
dsmcc_cache_key_cmp(char *str1, char *str2, unsigned int len1, unsigned int len2) {
        unsigned int i;

        /* Key Len must be equal */

        if(len1 != len2) { return 0; }

        for(i = 0; i < len1; i++) {
                if(str1[i] != str2[i]) {
                        return 0;
                }
        }

        return 1;
}

struct cache_dir *
dsmcc_cache_scan_dir(struct cache_dir *dir, unsigned long car_id, unsigned short module_id, unsigned int key_len, char *key) {
        struct cache_dir *founddir, *subdir;

        if(dir == NULL) { return NULL; }

        if((dir->carousel_id == car_id) && (dir->module_id == module_id) &&
                        dsmcc_cache_key_cmp(dir->key, key, dir->key_len, key_len)) {
                        return dir;
        }

        /* Search sub dirs */

        for(subdir = dir->sub; subdir != NULL; subdir=subdir->next) {
                founddir = dsmcc_cache_scan_dir(subdir, car_id, module_id, key_len,key);
                if(founddir != NULL) 
                        return founddir;
        }

        return NULL;
}

struct cache_dir *
dsmcc_cache_dir_find(struct cache *filecache, unsigned long car_id, unsigned short module_id, unsigned int key_len, char *key) {        struct cache_dir *dir, *fdir;
	struct cache_file *file, *nf;
	
//      fprintf(cache_fd,"Searching for dir %d/%d/(key)\n", module_id, key_len);

        /* Scan through known dirs and return details if known else NULL */

        if(module_id == 0 && key_len == 0) {
                /* Return gateway object. Create if not already */
                if(filecache->gateway == NULL) {
                        filecache->gateway = (struct cache_dir *)malloc(sizeof(struct cache_dir));
                        filecache->gateway->name = (char *)malloc(2);
			filecache->gateway->carousel_id = car_id;
			filecache->gateway->module_id = filecache->gateway->key_len = 
					filecache->gateway->p_key_len = 0;
	/*TODO argg! a hack to fix a bug caused by a hack.Need better linking */
                        strcpy(filecache->gateway->name, "/");
			filecache->gateway->dirpath = (char *)malloc(2);
                        strcpy(filecache->gateway->dirpath, "/");
                        filecache->gateway->sub = filecache->gateway->parent = 
				filecache->gateway->prev = filecache->gateway->next = NULL;
                        filecache->gateway->files = NULL;

                        /* Attach any subdirs or files that arrived prev. */
			for(file=filecache->file_cache;
			    file!=NULL;
			    file=nf) {
				nf=file->next;
		if((file->carousel_id == filecache->gateway->carousel_id) &&
 		(file->p_module_id == filecache->gateway->module_id) &&
 		dsmcc_cache_key_cmp(file->p_key, filecache->gateway->key,
				file->p_key_len,filecache->gateway->key_len)) {
		    		dsmcc_cache_attach_file(filecache,filecache->gateway, file);
		}
			}

			for(fdir=filecache->dir_cache;fdir!=NULL;fdir=fdir->next) 
				dsmcc_cache_attach_dir(filecache, filecache->gateway, fdir);
		
			dsmcc_cache_write_dir(filecache, filecache->gateway);	/* Write files to filesystem */

                        return filecache->gateway;
                } else {
                        return filecache->gateway;
                }
        }


        /* Find dir magic */
        dir = dsmcc_cache_scan_dir(filecache->gateway, car_id, module_id, key_len, key);


	if(dir == NULL) {	/* Try looking in unlinked dirs list */
          for(fdir=filecache->dir_cache;(dir==NULL)&&(fdir!=NULL); fdir = fdir->next) {
		dir = dsmcc_cache_scan_dir(fdir, car_id, module_id, key_len, key);
	  }
        }

        /* TODO - Directory not known yet, cache it ? */

        return dir;
}

void
dsmcc_cache_attach_file(struct cache *filecache, struct cache_dir *root, struct cache_file *file) {
	struct cache_file *cfile;

	/* Search for any files that arrived previously in unknown files list*/
	if(root->files == NULL) {
	    if(file->prev!=NULL) {
		file->prev->next = file->next;
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Set filecache prev to next file\n");
		}
	     } else {
		filecache->file_cache=file->next;
		if(filecache->debug_fd != NULL) {
	    	    fprintf(filecache->debug_fd,"[libcache] Set filecache to next file\n");
		}
	     }

	     if(file->next!=NULL)
	        file->next->prev = file->prev; 

	        root->files = file;
		root->files->next = root->files->prev = NULL;
		file->parent = root;
	 } else {

	     if(file->prev!=NULL) {
	         file->prev->next = file->next;
		 if(filecache->debug_fd != NULL) {
		     fprintf(filecache->debug_fd,"[libcache] Set filecache (not start) prev to next file\n");
		 }
	     } else {
	        filecache->file_cache=file->next;
	        if(filecache->debug_fd != NULL) {
	            fprintf(filecache->debug_fd,"[libcache] Set filecache (not start) to next file\n");
	        }
	     }

	     if(file->next!=NULL)
		file->next->prev = file->prev;

	     for(cfile=root->files;cfile->next!=NULL;cfile=cfile->next){;}
	     cfile->next = file;
	     file->prev = cfile;
	     file->next = NULL;	/* TODO uurrgh */
	     file->parent = root;
	 }

}

void
dsmcc_cache_attach_dir(struct cache *filecache, struct cache_dir *root, struct cache_dir *dir) {
	struct cache_dir *last;
	
   if((dir->carousel_id == root->carousel_id) &&
      (dir->p_module_id == root->module_id) &&
      dsmcc_cache_key_cmp(dir->p_key,root->key,dir->p_key_len,root->key_len)) {

	if(root->sub == NULL) {
	  if(dir->prev != NULL) { 
		dir->prev->next = dir->next; 
	  } else {
		filecache->dir_cache = dir->next;
	  }

	  if(dir->next!=NULL) 
		dir->next->prev = dir->prev;

	  root->sub = dir;
	  root->sub->next = root->sub->prev = NULL;
	  dir->parent = root;
	} else {
	  if(dir->prev!=NULL) {
		dir->prev->next = dir->next;
	  } else {
		filecache->dir_cache = dir->next;
	  }

	  if(dir->next!=NULL)
		dir->next->prev = dir->prev;

	  for(last=root->sub;last->next!=NULL;last=last->next) { ; }
	  last->next = dir;
	  dir->prev = last;
	  dir->next = NULL;
	  dir->parent = root;
	}
   }
}

struct cache_file *
dsmcc_cache_scan_file(struct cache_dir *dir, unsigned long car_id, unsigned int mod_id, unsigned int key_len, char *key) {
        struct cache_file *file;
        struct cache_dir *subdir;

        if(dir == NULL) { return NULL; }

/*      fprintf(cache_fd,"Searching for file %d - \n", mod_id);
        for(i = 0; i < key_len; i++) {
                fprintf(cache_fd, "%c", key[i]);
        }
        fprintf(cache_fd, "\n");
*/
        /* Search files in this dir */

        for(file = dir->files; file != NULL; file = file->next) {
                if((file->carousel_id == car_id) &&
		   (file->module_id == mod_id) &&
                   dsmcc_cache_key_cmp(file->key, key, file->key_len, key_len)) {
                        return file;
                }
        }

        /* Search sub dirs */

        for(subdir = dir->sub; subdir != NULL; subdir=subdir->next) {
                file = dsmcc_cache_scan_file(subdir, car_id, mod_id, key_len, key);
                if(file != NULL) {
                        return file;
                }
        }

        return NULL;
}

struct cache_file *
dsmcc_cache_file_find(struct cache *filecache, unsigned long car_id, unsigned short module_id, unsigned int key_len, char *key)
{
        struct cache_file *file;

        /* Try looking in parent-less list */

        for(file = filecache->file_cache; file != NULL; file = file->next) {
                if((file->carousel_id == car_id) &&
		   (file->module_id == module_id) &&
                   dsmcc_cache_key_cmp(file->key, key, file->key_len, key_len)) {
                       return file;
                }
        }

        /* Scan through known files and return details if known else NULL */

        file = dsmcc_cache_scan_file(filecache->gateway, car_id, module_id, key_len, key);

        return file;
}

void
dsmcc_cache_dir_info(struct cache *filecache, unsigned short module_id, unsigned int objkey_len, char *objkey, struct biop_binding *bind) {
        struct cache_dir *dir, *last, *subdir;
	struct cache_file *file, *nf;

	dir = dsmcc_cache_dir_find(filecache,
			   bind->ior.body.full.obj_loc.carousel_id,
			   bind->ior.body.full.obj_loc.module_id,
			   bind->ior.body.full.obj_loc.objkey_len,
			   bind->ior.body.full.obj_loc.objkey);

	if(dir != NULL) { return; }	/* Already got (check version TODO) */

        dir = (struct cache_dir *)malloc(sizeof(struct cache_dir));
        dir->name = (char *)malloc(bind->name.comps[0].id_len);
        memcpy(dir->name, bind->name.comps[0].id, bind->name.comps[0].id_len);
	dir->dirpath = NULL;
	dir->next = dir->prev = dir->sub = NULL;
	dir->files = NULL;
	dir->carousel_id = bind->ior.body.full.obj_loc.carousel_id;
	dir->module_id = bind->ior.body.full.obj_loc.module_id;
	dir->key_len = bind->ior.body.full.obj_loc.objkey_len;
	dir->key = (char *)malloc(dir->key_len);
	memcpy(dir->key, bind->ior.body.full.obj_loc.objkey, dir->key_len);

//	dir->p_carousel_id = carousel_id; Must be the same ?

	dir->p_module_id = module_id;
	dir->p_key_len = objkey_len;
	dir->p_key = (char *)malloc(dir->p_key_len);
	memcpy(dir->p_key, objkey, objkey_len);

	dir->parent = dsmcc_cache_dir_find(filecache, dir->carousel_id, module_id, objkey_len, objkey);

	if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Caching dir %s (with parent %d/%d/%c%c%c%c\n", dir->name, dir->p_module_id, dir->p_key_len, dir->p_key[0], dir->p_key[1], dir->p_key[2]);
	}

	if(dir->parent == NULL) {

		if(filecache->dir_cache == NULL) {
			filecache->dir_cache = dir;
		} else {
			/* Directory not yet known. Add this to dirs list */
			for(last=filecache->dir_cache;last->next!=NULL;last=last->next){;}
//			fprintf(cache_fd,"Added to Unknown list not empty\n");
			last->next = dir;
			dir->prev = last;
		}
	} else {
	  if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Caching dir %s under parent %s\n", dir->name, dir->parent->name);
	  }
		/* Create under parent directory */
		if(dir->parent->sub == NULL) {
//			fprintf(cache_fd,"Parent has no subdirs\n");
			dir->parent->sub = dir;
		} else {
//			fprintf(cache_fd,"Parent has other subdirs\n");
			for(last=dir->parent->sub;last->next!=NULL;last=last->next) { ; }
			last->next = dir;
			dir->prev = last;
//			fprintf(cache_fd,"Added to Parent has other subdirs\n");
		}
	}

	/* Attach any files that arrived previously */

	for(file = filecache->file_cache;
	    file != NULL;
            file = nf) {

		    nf = file->next;
		if((file->carousel_id == dir->carousel_id) &&
 		(file->p_module_id == dir->module_id) &&
 		dsmcc_cache_key_cmp(file->p_key, dir->key, file->p_key_len, dir->key_len)) {
	  if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Attaching previously arrived file %s to newly created directory %s\n", file->filename, dir->name);
	  }
		    dsmcc_cache_attach_file(filecache, dir, file);
	        }
	}

	/* Attach any subdirs that arrived beforehand */

	for(subdir = filecache->dir_cache; subdir != NULL; subdir = subdir->next) {
	  dsmcc_cache_attach_dir(filecache, dir, subdir);
	}
		
	if((dir->parent!=NULL)&&(dir->parent->dirpath!=NULL))
		dsmcc_cache_write_dir(filecache, dir);	/* Write dir/files to filesystem */

	filecache->num_dirs++; filecache->total_dirs++;
}

void
dsmcc_cache_write_dir(struct cache *filecache, struct cache_dir *dir) {
	struct cache_dir *subdir;
	struct cache_file *file;
	char dirbuf[256];

	if(dir->dirpath == NULL)  {
		dir->dirpath= (char*)
		       malloc(strlen(dir->parent->dirpath)+strlen(dir->name)+2);

		strcpy(dir->dirpath, dir->parent->dirpath);
		strcat(dir->dirpath, "/");
		strcat(dir->dirpath, dir->name);
	}

	sprintf(dirbuf, "%s/%s/%s", "/tmp/cache/", filecache->name, dir->dirpath);

	if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Writing directory %s to filesystem\n", dir->dirpath);
	  }

	mkdir(dirbuf, 0755); 

	/* Write out files that had arrived before directory */

	for(file=dir->files;file!=NULL;file=file->next) {
		if(file->data != NULL) {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd,"[libcache] Writing out file %s under new dir %s\n", file->filename, dir->dirpath);
	  		}
			dsmcc_cache_write_file(filecache, file);
		}
	}

	/* Recurse thorugh child directories */

	for(subdir=dir->sub;subdir!=NULL;subdir=subdir->next) {
		dsmcc_cache_write_dir(filecache, subdir);
	}

}
	
void
dsmcc_cache_file(struct cache *filecache, struct biop_message *bm, struct cache_module_data *cachep) {
	struct cache_file *file;


	/* search for file info */

	file = dsmcc_cache_file_find(filecache, cachep->carousel_id, cachep->module_id, bm->hdr.objkey_len, bm->hdr.objkey);

	if(file == NULL) {
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Unknown file %ld/%d/%d/%c%c%c, caching data\n", cachep->carousel_id, cachep->module_id, bm->hdr.objkey_len, bm->hdr.objkey[0], bm->hdr.objkey[1], bm->hdr.objkey[2]);
	  	}
		/* Not known yet. Save data */

		file = (struct cache_file *)malloc(sizeof(struct cache_file));
		file->data_len = bm->body.file.content_len;
		file->data = (char*)malloc(file->data_len);
		memcpy(file->data, cachep->data+cachep->curp,
						file->data_len);
		file->carousel_id= cachep->carousel_id;
		file->module_id= cachep->module_id;
		file->key_len= bm->hdr.objkey_len;
		file->key= (char*)malloc(file->key_len);
		memcpy(file->key, bm->hdr.objkey, file->key_len);
		file->next = file->prev = NULL;
		// Add to unknown data cache

		if(filecache->data_cache == NULL) {
			filecache->data_cache = file;
		} else {
			struct cache_file *last;
			for(last=filecache->data_cache;last->next!=NULL;last=last->next){;}
			last->next = file;
			file->prev = last;
		}

		filecache->num_files++; filecache->total_files++;

	} else {
		/* Save data. Save file if wanted  (TODO check versions ) */
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Data for file %s\n", file->filename);
	  	}
		if(file->data == NULL) {
			file->data_len = bm->body.file.content_len;
			file->data = (char *)malloc(file->data_len);
			memcpy(file->data,cachep->data+cachep->curp,
							file->data_len);
			/* TODO this should be a config option */
			dsmcc_cache_write_file(filecache, file);
		} else {
			if(filecache->debug_fd != NULL) {
				fprintf(filecache->debug_fd,"[libcache] Data for file %s had already arrived\n", file->filename);
		  	}
		}
	}
}

void
dsmcc_cache_write_file(struct cache *filecache, struct cache_file *file) {
	FILE *data_fd;
	char buf[128];
        struct file_info *filei, *files;

	/* TODO create directory structure rather than one big mess! */


	if((file->parent!=NULL) && (file->parent->dirpath != NULL)) {
	  if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Writing file %s/%s (%d bytes)\n", file->parent->dirpath, file->filename, file->data_len);
	  }
	  sprintf(buf,"/tmp/cache/%s/%s/%s", filecache->name, file->parent->dirpath, file->filename);
	  data_fd = fopen(buf, "wb");
	  fwrite(file->data, 1, file->data_len, data_fd);
	  fclose(data_fd);
	  /* Free data as no longer needed */
	  free(file->data);
	  file->data = NULL; file->data_len = 0;

	  filecache->num_files--;

          /* Update information in file info */
          filei = malloc(sizeof(struct file_info));
          filei->filename = malloc(strlen(file->filename)+1);
          strcpy(filei->filename, file->filename);
          filei->path = malloc(strlen(buf)+1);
          strcpy(filei->path, buf);
          filei->arrived = filei->written = 1;
          if(filecache->files == NULL) {
                filecache->files = filei;
          } else {
                for(files=filecache->files;files->next!=NULL;files=files->next)
{ ; }
                files->next = filei;
          }
          filei->next = NULL;
	} else {
	 if(filecache->debug_fd != NULL) {
/*	   fprintf(filecache->debug_fd,"[libcache] File %s Parent == %p Dirpath == %s\n", file->filename, file->parent, file->parent->dirpath);
*/
	 }
	}
}

void
dsmcc_cache_unknown_dir_info(struct cache *filecache, struct cache_dir *newdir) {
	struct cache_dir *last;

	if(filecache->dir_cache == NULL) {
		filecache->dir_cache = newdir;
		newdir->next = newdir->prev = NULL;
	} else {
		for(last=filecache->dir_cache;last->next!=NULL;last=last->next) { ; }
		last->next = newdir;
		newdir->prev = last;
		newdir->next = NULL;
	}
}

void
dsmcc_cache_unknown_file_info(struct cache *filecache, struct cache_file *newfile) {
	struct cache_file *last;

/* TODO Check if already unknown file (i.e. data arrived twice before
 * dir/srg or missed dir/srg message, if so skip.
 */

	if(filecache->file_cache == NULL) {
		filecache->file_cache = newfile;
		filecache->file_cache->next = filecache->file_cache->prev = NULL;
	} else {
		for(last=filecache->file_cache;last->next!=NULL; last = last->next) { ; }
		last->next = newfile;
		newfile->prev = last;
		newfile->next = NULL;
	}
}

struct cache_file *
dsmcc_cache_file_find_data(struct cache *filecache, unsigned long car_id, unsigned short mod_id, unsigned int key_len, char *key) {

	struct cache_file *last;

	for(last=filecache->data_cache; last!=NULL; last = last->next) {
		
		if((last->carousel_id==car_id) && (last->module_id==mod_id)
		   && dsmcc_cache_key_cmp(key, last->key, key_len, last->key_len)) {

			if(last->prev != NULL) {
				last->prev->next = last->next;
			} else {
				filecache->data_cache = last->next;
			}

			if(last->next != NULL)
				last->next->prev = last->prev;

			break;
		}
	}

	return last;
}

void
dsmcc_cache_file_info(struct cache *filecache, unsigned short mod_id, unsigned int key_len, char *key, struct biop_binding *bind) {
	struct cache_file *newfile, *last;
	struct cache_dir *dir;

	if(filecache->debug_fd != NULL) {
		fprintf(filecache->debug_fd,"[libcache] Caching file info\n");
	}

	// Check we do not already have file (or file info) cached 
	if(dsmcc_cache_file_find(filecache,
			bind->ior.body.full.obj_loc.carousel_id,
			bind->ior.body.full.obj_loc.module_id,
			bind->ior.body.full.obj_loc.objkey_len,
			bind->ior.body.full.obj_loc.objkey) != NULL) {
		return;
	}

	// See if the data had already arrived for the file 

	newfile = dsmcc_cache_file_find_data(filecache,
	bind->ior.body.full.obj_loc.carousel_id,
	bind->ior.body.full.obj_loc.module_id,
	bind->ior.body.full.obj_loc.objkey_len,
	bind->ior.body.full.obj_loc.objkey);
	
	if(newfile == NULL) {
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Data not arrived for file %s, caching\n", bind->name.comps[0].id);
		}
		// Create the file from scratch
		newfile = (struct cache_file*)malloc(sizeof(struct cache_file));
		newfile->carousel_id = bind->ior.body.full.obj_loc.carousel_id;
		newfile->module_id = bind->ior.body.full.obj_loc.module_id;
		newfile->key_len = bind->ior.body.full.obj_loc.objkey_len;
		newfile->key= (char *)malloc(newfile->key_len);
		memcpy(newfile->key, bind->ior.body.full.obj_loc.objkey,
							newfile->key_len);
		newfile->data = NULL;
	} else {
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Data already arrived for file %s\n", bind->name.comps[0].id);
		}
	}

	newfile->filename = (char*)malloc(bind->name.comps[0].id_len);
	memcpy(newfile->filename, bind->name.comps[0].id, 
					bind->name.comps[0].id_len);
	newfile->next = NULL;

	dir = dsmcc_cache_dir_find(filecache, newfile->carousel_id, mod_id, key_len, key);

	 filecache->num_files++; filecache->total_files++;

	if(dir == NULL) {
		/* Parent directory not yet known */
		newfile->p_module_id = mod_id;
		newfile->p_key_len = key_len;
		newfile->p_key = (char *)malloc(newfile->p_key_len);
		memcpy(newfile->p_key, key, key_len);
		newfile->parent = NULL;
		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Caching info for file %s with unknown parent dir (file info - %ld/%d/%d/%c%c%c%c)\n", newfile->filename, newfile->carousel_id, newfile->module_id, newfile->key_len, newfile->key[0], newfile->key[1], newfile->key[2], newfile->key[3]);
		}
		dsmcc_cache_unknown_file_info(filecache, newfile);
	} else {
		/* TODO Check if already stored under directory (new version?)
		 *      Checking version info for a file is difficult,
		 *      the data should not be passed to us by dsmcc layer 
		 *      unless the version has changed. Need to remove old
		 *      and store new.
		*/
		/* If not append to list */

		newfile->p_key_len = dir->key_len;
		newfile->p_key = malloc(dir->key_len);
		memcpy(newfile->p_key, dir->key, dir->key_len);
		newfile->parent = dir;
		if(dir->files == NULL) {
			dir->files = newfile;
			newfile->prev = NULL;
		} else {
			for(last=dir->files;last->next!=NULL;last=last->next){;}
			last->next = newfile;
			newfile->prev = last;
		}

		if(filecache->debug_fd != NULL) {
			fprintf(filecache->debug_fd,"[libcache] Caching info for file %s with known parent dir (file info - %ld/%d/%d/%c%c%c)\n", newfile->filename, newfile->carousel_id, newfile->module_id, newfile->key_len, newfile->key[0], newfile->key[1], newfile->key[2]);
		}

		if(newfile->data != NULL)
			dsmcc_cache_write_file(filecache, newfile);
	}
}
