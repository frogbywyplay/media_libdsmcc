#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "dsmcc.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-section.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-module.h"

struct dsmcc_cached_dir
{
	unsigned long  carousel_id;
	unsigned short module_id;
	unsigned int   key_len;
	unsigned char *key;

	char *name;
	char *dirpath;

	unsigned short p_module_id;
	unsigned int   p_key_len;
	unsigned char *p_key;

	struct dsmcc_cached_file *files;

	struct dsmcc_cached_dir *parent, *sub;
	struct dsmcc_cached_dir *next, *prev;
};

struct dsmcc_cached_file
{
	unsigned long  carousel_id;
	unsigned short module_id;
	unsigned int   key_len;
	unsigned char *key;

	char        *filename;
	unsigned int data_len;

	struct dsmcc_cached_dir *parent;
	unsigned short          p_module_id;
	unsigned int            p_key_len;
	unsigned char          *p_key;

	char         *module_file;
	unsigned long module_offset;

	struct dsmcc_cached_file *next, *prev;
};

struct dsmcc_file_cache
{
	char *downloadpath;

	dsmcc_cache_callback_t *callback;
	void                   *callback_arg;

	struct dsmcc_cached_dir  *gateway;
	struct dsmcc_cached_dir  *orphan_dirs;
	struct dsmcc_cached_file *orphan_files;
	struct dsmcc_cached_file *nameless_files;
};

void dsmcc_filecache_init(struct dsmcc_object_carousel *car, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	car->filecache = malloc(sizeof(struct dsmcc_file_cache));
	memset(car->filecache, 0, sizeof(struct dsmcc_file_cache));

	car->filecache->downloadpath = strdup(downloadpath);
	if (downloadpath[strlen(downloadpath) - 1] == '/')
		car->filecache->downloadpath[strlen(downloadpath) - 1] = '\0';
	dsmcc_mkdir(car->filecache->downloadpath, 0755);

	car->filecache->callback = cache_callback;
	car->filecache->callback_arg = cache_callback_arg;
}

static void dsmcc_filecache_free_dirs(struct dsmcc_cached_dir *d)
{
	struct dsmcc_cached_dir *dn, *dnn;
	struct dsmcc_cached_file *f, *fn;

	if (d->sub)
	{
		dn = d->sub;
		while (dn)
		{
			dnn = dn->next;
			dsmcc_filecache_free_dirs(dn);
			dn = dnn;
		}
	}

	if (d->name)
		free(d->name);
	if (d->dirpath)
		free(d->dirpath);
	if (d->key)
		free(d->key);
	if (d->p_key)
		free(d->p_key);

	f = d->files;
	while (f)
	{
		fn = f->next;
		if (f->key)
			free(f->key);
		if (f->filename)
			free(f->filename);
		if (f->module_file)
			free(f->module_file);
		if (f->p_key)
			free(f->p_key);
		free(f);
		f = fn;
	}
	free(d);
}

void dsmcc_filecache_free(struct dsmcc_object_carousel *car)
{
	struct dsmcc_cached_file *f, *fn;
	struct dsmcc_cached_dir *d, *dn;

	/* Free unconnected files */
	f = car->filecache->orphan_files;
	while (f)
	{
		fn = f->next;
		if (f->key)
			free(f->key);
		if (f->filename)
			free(f->filename);
		if (f->module_file)
			free(f->module_file);
		if (f->p_key)
			free(f->p_key);
		free(f);
		f = fn;
	}

	/* Free cached data */
	f = car->filecache->nameless_files;
	while (f) {
		fn = f->next;
		if (f->key)
			free(f->key);
		if (f->module_file)
			free(f->module_file);
		free(f);
		f = fn;
	}

	/* Free unconnected dirs */
	d = car->filecache->orphan_dirs;
	while (d)
	{
		dn = d->next;
		if (d->name)
			free(d->name);
		if (d->dirpath)
			free(d->dirpath);
		if (d->key)
			free(d->key);
		if (d->p_key)
			free(d->p_key);
		f = d->files;
		while (f)
		{
			fn = f->next;
			if (f->key)
				free(f->key);
			if (f->filename)
				free(f->filename);
			if (f->module_file)
				free(f->module_file);
			if (f->p_key)
				free(f->p_key);
			free(f);
			f = fn;
		}
		free(d);
		d = dn;
	}

	/* Free cache */
	if (car->filecache->gateway)
		dsmcc_filecache_free_dirs(car->filecache->gateway);

	free(car->filecache->downloadpath);
	free(car->filecache);
	car->filecache = NULL;
}

static unsigned int dsmcc_filecache_key_cmp(unsigned char *str1, unsigned int len1, unsigned char *str2, unsigned int len2)
{
	unsigned int i;

	/* Key Len must be equal */
	if (len1 != len2)
		return 0;

	for (i = 0; i < len1; i++)
	{
		if (str1[i] != str2[i])
			return 0;
	}

	return 1;
}

static struct dsmcc_cached_dir *dsmcc_filecache_find_dir_in_subdirs(struct dsmcc_cached_dir *parent, unsigned long car_id, unsigned short module_id, unsigned int key_len, unsigned char *key)
{
	struct dsmcc_cached_dir *dir, *subdir;

	if (!parent)
		return NULL;

	if ((parent->carousel_id == car_id) && (parent->module_id == module_id) &&
			dsmcc_filecache_key_cmp(parent->key, parent->key_len, key, key_len))
		return parent;

	/* Search sub dirs */
	for (subdir = parent->sub; subdir != NULL; subdir = subdir->next)
	{
		dir = dsmcc_filecache_find_dir_in_subdirs(subdir, car_id, module_id, key_len, key);
		if (dir)
			return dir;
	}

	return NULL;
}

static void dsmcc_filecache_attach_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir, struct dsmcc_cached_file *file)
{
	/* detach from orphan files */
	if (file->prev)
		file->prev->next = file->next;
	else
		filecache->orphan_files = file->next;
	if (file->next)
		file->next->prev = file->prev;
	file->prev = NULL;
	file->next = NULL;

	/* attach to dir */
	file->parent = dir;
	file->next = dir->files;
	if (file->next)
		file->next->prev = file;
	dir->files = file;
}

static void dsmcc_filecache_attach_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *parent, struct dsmcc_cached_dir *dir)
{
	/* detach from orphan dirs */
	if (dir->prev)
		dir->prev->next = dir->next;
	else
		filecache->orphan_dirs = dir->next;
	if (dir->next)
		dir->next->prev = dir->prev;
	dir->prev = NULL;
	dir->next = NULL;

	/* attach to parent */
	dir->parent = parent;
	dir->next = parent->sub;
	if (dir->next)
		dir->next->prev = dir;
	parent->sub = dir;
}

static void dsmcc_filecache_write_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_file *file)
{
	FILE *data_fd, *module_fd;
	char fn[1024], dirpath[1024];
	char data_buf[4096];
	unsigned long copied, cp_size, rret, wret;

	/* TODO create directory structure rather than one big mess! */

	if (file->parent && file->parent->dirpath)
	{
		snprintf(dirpath, 1024, "%s/%s", file->parent->dirpath, file->filename);
		snprintf(fn, 1024, "%s%s%s", filecache->downloadpath, dirpath[0] == '/' ? "" : "/", dirpath);

		if (filecache->callback)
		{
			int ok = (*filecache->callback)(filecache->callback_arg, file->carousel_id, DSMCC_CACHE_FILE_CHECK, dirpath, fn);
			if (!ok)
			{
				DSMCC_DEBUG("Not writing file %s as requested by callback", dirpath);
				return;
			}
		}

		DSMCC_DEBUG("Writing file %s to %s (%d bytes)", dirpath, fn, file->data_len);

		module_fd = fopen(file->module_file, "r");
		if (!module_fd)
		{
			DSMCC_DEBUG("Cached module file open error '%s': %s", file->module_file, strerror(errno));
			return;
		}
	
		if (fseek(module_fd, file->module_offset, SEEK_SET) < 0)
		{
			DSMCC_DEBUG("Cached module file seek error '%s': %s", file->module_file, strerror(errno));
			fclose(module_fd);
			return;
		}

		data_fd = fopen(fn, "wb");
		copied = 0;
		int err = 0;
		while (copied < file->data_len)
		{
			cp_size = file->data_len - copied > sizeof data_buf ? sizeof data_buf : file->data_len - copied;
			rret = fread(data_buf, 1, cp_size, module_fd);
			if (rret > 0)
			{
				wret = fwrite(data_buf, 1, rret, data_fd);
				if (wret < rret)
				{
					if (wret < 0)
						DSMCC_DEBUG("Write error '%s': %s", fn, strerror(errno));
					else
						DSMCC_DEBUG("Short write '%s'", fn);
					err = 1;
					break;
				}
				copied += wret;
			}
			else
			{
				if (rret < 0)
					DSMCC_DEBUG("Read error '%s': %s", file->module_file, strerror(errno));
				else
					DSMCC_DEBUG("Unexpected EOF '%s'", file->module_file);
				err = 1;
				break;
			}
		}
		fclose(module_fd);
		fclose(data_fd);

		if (err == 1)
		{
			unlink(fn);
			return;
		}

		/* Free data as no longer needed */
		file->module_offset = 0;
		free(file->module_file);
		file->module_file = NULL;
		file->data_len = 0;

		if (filecache->callback)
			(*filecache->callback)(filecache->callback_arg, file->carousel_id, DSMCC_CACHE_FILE_SAVED, dirpath, fn);
	}
	else
	{
		DSMCC_DEBUG("File %s Parent == %p Parent_Dirpath == %s", file->filename, file->parent, file->parent ? file->parent->dirpath : NULL);
	}
}

static void dsmcc_filecache_write_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir;
	struct dsmcc_cached_file *file;
	char dirbuf[1024];

	if (!dir->dirpath)
	{
		dir->dirpath = malloc(strlen(dir->parent->dirpath) + strlen(dir->name) + 2);
		strcpy(dir->dirpath, dir->parent->dirpath);
		strcat(dir->dirpath, "/");
		strcat(dir->dirpath, dir->name);
	}

	snprintf(dirbuf, 1024, "%s/%s", filecache->downloadpath, dir->dirpath);

	/* call callback (but not gateway) */
	if (strlen(dir->name) > 0 && filecache->callback)
	{
		int ok = (*filecache->callback)(filecache->callback_arg, dir->carousel_id, DSMCC_CACHE_DIR_CHECK, dir->dirpath, dirbuf);
		if (!ok)
		{
			DSMCC_DEBUG("Not creating directory %s as requested by callback", dir->dirpath);
			return;
		}
	}

	DSMCC_DEBUG("Writing directory %s to %s", dir->dirpath, dirbuf);

	mkdir(dirbuf, 0755); 

	/* call callback (but not gateway) */
	if (strlen(dir->name) > 0 && filecache->callback)
		(*filecache->callback)(filecache->callback_arg, dir->carousel_id, DSMCC_CACHE_DIR_SAVED, dir->dirpath, dirbuf);

	/* Write out files that had arrived before directory */
	for (file = dir->files; file; file = file->next)
	{
		if (file->module_offset != 0)
		{
			DSMCC_DEBUG("Writing out file %s under new dir %s", file->filename, dir->dirpath);
			dsmcc_filecache_write_file(filecache, file);
		}
	}

	/* Recurse through child directories */
	for (subdir = dir->sub; subdir; subdir = subdir->next)
		dsmcc_filecache_write_dir(filecache, subdir);
}

static struct dsmcc_cached_dir *dsmcc_filecache_cached_dir_find(struct dsmcc_file_cache *filecache, unsigned long car_id, unsigned short module_id, unsigned int key_len, unsigned char *key)
{
	struct dsmcc_cached_dir *dir;
	struct dsmcc_cached_file *file, *filenext;

	if (!key_len || !key)
		DSMCC_DEBUG("Searching for dir %d/%d", module_id, key_len);
	else
		DSMCC_DEBUG("Searching for dir %d/%d/%02x%02x%02x%02x", module_id, key_len, key[0], key[1], key[2], key[3]);

	/* Scan through known dirs and return details if known else NULL */
	if (module_id == 0 && key_len == 0)
	{
		/* Return gateway object. Create if not already */
		if (filecache->gateway == NULL)
		{
			filecache->gateway = malloc(sizeof(struct dsmcc_cached_dir));
			memset(filecache->gateway, 0, sizeof(struct dsmcc_cached_dir));
			filecache->gateway->carousel_id = car_id;
			filecache->gateway->name = (char *) malloc(1);
			filecache->gateway->name[0] = '\0';
			filecache->gateway->dirpath = (char *) malloc(1);
			filecache->gateway->dirpath[0] = '\0';

			/* Attach any subdirs or files that arrived prev. */
			for (file = filecache->orphan_files; file; file = filenext)
			{
				filenext = file->next;
				if ((file->carousel_id == filecache->gateway->carousel_id) && (file->p_module_id == filecache->gateway->module_id) &&
						dsmcc_filecache_key_cmp(file->p_key, file->p_key_len, filecache->gateway->key, filecache->gateway->key_len))
					dsmcc_filecache_attach_file(filecache, filecache->gateway, file);
			}

			for (dir = filecache->orphan_dirs; dir; dir = dir->next) 
				if ((dir->carousel_id == filecache->gateway->carousel_id) && (dir->p_module_id == filecache->gateway->module_id) &&
						dsmcc_filecache_key_cmp(dir->p_key, dir->p_key_len, filecache->gateway->key, filecache->gateway->key_len))
					dsmcc_filecache_attach_dir(filecache, filecache->gateway, dir);

			dsmcc_filecache_write_dir(filecache, filecache->gateway); /* Write files to filesystem */
		}
		return filecache->gateway;
	}

	/* Find dir */
	dir = dsmcc_filecache_find_dir_in_subdirs(filecache->gateway, car_id, module_id, key_len, key);
	if (dir == NULL)
	{
		struct dsmcc_cached_dir *d;

		/* Try looking in orphan dirs */
		for (d = filecache->orphan_dirs; !dir && d; d = d->next)
			dir = dsmcc_filecache_find_dir_in_subdirs(d, car_id, module_id, key_len, key);
	}

	return dir;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_in_dir(struct dsmcc_cached_dir *dir, unsigned long car_id, unsigned int mod_id, unsigned int key_len, unsigned char *key)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_cached_dir *subdir;

	if (!dir)
		return NULL;

	/* Search files in this dir */
	for (file = dir->files; file != NULL; file = file->next)
	{
		if ((file->carousel_id == car_id) && (file->module_id == mod_id) && dsmcc_filecache_key_cmp(file->key, file->key_len, key, key_len))
			return file;
	}

	/* Search sub dirs */
	for (subdir = dir->sub; subdir != NULL; subdir = subdir->next)
	{
		file = dsmcc_filecache_find_file_in_dir(subdir, car_id, mod_id, key_len, key);
		if (file)
			return file;
	}

	return NULL;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file(struct dsmcc_file_cache *filecache, unsigned long car_id, unsigned short module_id, unsigned int key_len, unsigned char *key)
{
	struct dsmcc_cached_file *file;

	/* Try looking in parent-less list */
	for (file = filecache->orphan_files; file != NULL; file = file->next)
	{
		if ((file->carousel_id == car_id) && (file->module_id == module_id) && dsmcc_filecache_key_cmp(file->key, file->key_len, key, key_len))
			return file;
	}

	/* Scan through known files and return details if known else NULL */
	file = dsmcc_filecache_find_file_in_dir(filecache->gateway, car_id, module_id, key_len, key);

	return file;
}

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, unsigned short module_id, unsigned int objkey_len, unsigned char *objkey, struct biop_binding *binding)
{
	struct dsmcc_cached_dir *dir, *subdir;
	struct dsmcc_cached_file *file, *nf;

	dir = dsmcc_filecache_cached_dir_find(filecache, binding->ior->profile_body.obj_loc.carousel_id, binding->ior->profile_body.obj_loc.module_id,
			binding->ior->profile_body.obj_loc.objkey_len, binding->ior->profile_body.obj_loc.objkey);
	if (dir)
		return; /* Already got */

	dir = malloc(sizeof(struct dsmcc_cached_dir));
	memset(dir, 0, sizeof(struct dsmcc_cached_dir));
	dir->name = malloc(binding->name->id_len);
	memcpy(dir->name, binding->name->id, binding->name->id_len);
	dir->carousel_id = binding->ior->profile_body.obj_loc.carousel_id;
	dir->module_id = binding->ior->profile_body.obj_loc.module_id;
	dir->key_len = binding->ior->profile_body.obj_loc.objkey_len;
	if (dir->key_len > 0)
	{
		dir->key = malloc(dir->key_len);
		memcpy(dir->key, binding->ior->profile_body.obj_loc.objkey, dir->key_len);
	}
	dir->p_module_id = module_id;
	dir->p_key_len = objkey_len;
	if (dir->p_key_len > 0)
	{
		dir->p_key = malloc(dir->p_key_len);
		memcpy(dir->p_key, objkey, objkey_len);
	}
	dir->parent = dsmcc_filecache_cached_dir_find(filecache, dir->carousel_id, module_id, objkey_len, objkey);

	if (!dir->p_key_len || !dir->p_key)
		DSMCC_DEBUG("Caching dir %s (with parent %d/%d)", dir->name, dir->p_module_id, dir->p_key_len);
	else
		DSMCC_DEBUG("Caching dir %s (with parent %d/%d/%c%c%c)", dir->name, dir->p_module_id, dir->p_key_len, dir->p_key[0], dir->p_key[1], dir->p_key[2]);

	if (!dir->parent)
	{
		/* Directory not yet known. Add this to dirs list */
		dir->next = filecache->orphan_dirs;
		if (dir->next)
			dir->next->prev = dir;
		filecache->orphan_dirs = dir;
	}
	else
	{
		/* Create under parent directory */
		DSMCC_DEBUG("Caching dir %s under parent %s", dir->name, dir->parent->name);

		dir->next = dir->parent->sub;
		if (dir->next)
			dir->next->prev = dir;
		dir->parent->sub = dir;
	}

	/* Attach any files that arrived previously */
	for (file = filecache->orphan_files; file != NULL; file = nf)
	{
		nf = file->next;
		if ((file->carousel_id == dir->carousel_id) && (file->p_module_id == dir->module_id) &&
				dsmcc_filecache_key_cmp(file->p_key, file->p_key_len, dir->key, dir->key_len))
		{
			DSMCC_DEBUG("Attaching previously arrived file %s to newly created directory %s", file->filename, dir->name);
			dsmcc_filecache_attach_file(filecache, dir, file);
		}
	}

	/* Attach any subdirs that arrived beforehand */
	for (subdir = filecache->orphan_dirs; subdir != NULL; subdir = subdir->next)
		if ((subdir->carousel_id == dir->carousel_id) && (subdir->p_module_id == dir->module_id) &&
				dsmcc_filecache_key_cmp(subdir->p_key, subdir->p_key_len, dir->key, dir->key_len))
			dsmcc_filecache_attach_dir(filecache, dir, subdir);

	if (dir->parent && dir->parent->dirpath)
		dsmcc_filecache_write_dir(filecache, dir); /* Write dir/files to filesystem */
}

void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, unsigned char objkey_len, unsigned char *objkey, unsigned long content_len, struct dsmcc_cached_module *cachep, unsigned int module_offset)
{
	struct dsmcc_cached_file *file;

	/* search for file info */
	file = dsmcc_filecache_find_file(filecache, cachep->carousel_id, cachep->module_id, objkey_len, objkey);
	if (!file)
	{
		/* Not known yet. Save data */
		if (objkey_len == 3)
			DSMCC_DEBUG("Unknown file %ld/%d/%d/%02x%02x%02x, caching data", cachep->carousel_id, cachep->module_id, objkey_len, objkey[0], objkey[1], objkey[2]);
		else if (objkey_len == 4)
			DSMCC_DEBUG("Unknown file %ld/%d/%d/%02x%02x%02x%02x, caching data", cachep->carousel_id, cachep->module_id, objkey_len, objkey[0], objkey[1], objkey[2], objkey[3]);
		else
			DSMCC_DEBUG("Unknown file %ld/%d/%d, caching data", cachep->carousel_id, cachep->module_id, objkey_len);

		file = malloc(sizeof(struct dsmcc_cached_file));
		memset(file, 0, sizeof(struct dsmcc_cached_file));
		file->data_len = content_len;
		file->module_offset = module_offset;
		file->module_file = strdup(cachep->data_file);
		file->carousel_id= cachep->carousel_id;
		file->module_id= cachep->module_id;
		file->key_len= objkey_len;
		if (file->key_len > 0)
		{
			file->key = malloc(file->key_len);
			memcpy(file->key, objkey, file->key_len);
		}

		// Add to nameless files
		file->next = filecache->nameless_files;
		if (file->next)
			file->next->prev = file;
		filecache->nameless_files = file;
	}
	else
	{
		/* Save data */
		if (file->module_offset == 0)
		{
			file->data_len = content_len;
			file->module_offset = module_offset;
			file->module_file = strdup(cachep->data_file);
			dsmcc_filecache_write_file(filecache, file);
		}
		else
		{
			DSMCC_DEBUG("Data for file %s had already been saved", file->filename);
		}
	}
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_data(struct dsmcc_file_cache *filecache, unsigned long car_id, unsigned short mod_id, unsigned int key_len, unsigned char *key)
{
	struct dsmcc_cached_file *last;

	for (last = filecache->nameless_files; last != NULL; last = last->next)
	{
		if ((last->carousel_id == car_id) && (last->module_id == mod_id)
				&& dsmcc_filecache_key_cmp(key, key_len, last->key, last->key_len))
			break;
	}

	if (last)
	{
		/* Found it, remove from list */
		if (last->prev)
			last->prev->next = last->next;
		else
			filecache->nameless_files = last->next;

		if (last->next)
			last->next->prev = last->prev;

		last->prev = NULL;
		last->next = NULL;
	}

	return last;
}

void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, unsigned short mod_id, unsigned int key_len, unsigned char *key, struct biop_binding *binding)
{
	struct dsmcc_cached_file *newfile;
	struct dsmcc_cached_dir *dir;

	DSMCC_DEBUG("Caching file info");

	// Check we do not already have file (or file info) cached 
	if (dsmcc_filecache_find_file(filecache, binding->ior->profile_body.obj_loc.carousel_id, binding->ior->profile_body.obj_loc.module_id,
			binding->ior->profile_body.obj_loc.objkey_len, binding->ior->profile_body.obj_loc.objkey))
		return;

	// See if the data had already arrived for the file 
	newfile = dsmcc_filecache_find_file_data(filecache, binding->ior->profile_body.obj_loc.carousel_id, binding->ior->profile_body.obj_loc.module_id,
			binding->ior->profile_body.obj_loc.objkey_len, binding->ior->profile_body.obj_loc.objkey);
	
	if (!newfile)
	{
		// Create the file from scratch
		DSMCC_DEBUG("Data not arrived for file %s, caching", binding->name->id);
		newfile = malloc(sizeof(struct dsmcc_cached_file));
		memset(newfile, 0, sizeof(struct dsmcc_cached_file));
		newfile->carousel_id = binding->ior->profile_body.obj_loc.carousel_id;
		newfile->module_id = binding->ior->profile_body.obj_loc.module_id;
		newfile->key_len = binding->ior->profile_body.obj_loc.objkey_len;
		if (newfile->key_len > 0)
		{
			newfile->key = malloc(newfile->key_len);
			memcpy(newfile->key, binding->ior->profile_body.obj_loc.objkey, newfile->key_len);
		}
	}
	else
	{
		DSMCC_DEBUG("Data already arrived for file %s", binding->name->id);
	}

	newfile->filename = malloc(binding->name->id_len);
	memcpy(newfile->filename, binding->name->id, binding->name->id_len);

	dir = dsmcc_filecache_cached_dir_find(filecache, newfile->carousel_id, mod_id, key_len, key);
	if (!dir)
	{
		/* Parent directory not yet known */
		DSMCC_DEBUG("Caching info for file %s with unknown parent dir (file info - %ld/%d/%d/%02x%02x%02x%02x)", newfile->filename, newfile->carousel_id, newfile->module_id, newfile->key_len, newfile->key[0], newfile->key[1], newfile->key[2], newfile->key[3]);

		newfile->p_module_id = mod_id;
		newfile->p_key_len = key_len;
		if (newfile->p_key_len > 0)
		{
			newfile->p_key = malloc(newfile->p_key_len);
			memcpy(newfile->p_key, key, key_len);
		}
		else
			newfile->p_key = NULL;
		newfile->parent = NULL;

		newfile->next = filecache->orphan_files;
		if (newfile->next)
			newfile->next->prev = newfile;
		newfile->prev = NULL;
		filecache->orphan_files = newfile;
	}
	else
	{
		DSMCC_DEBUG("Caching info for file %s with known parent dir (file info - %ld/%d/%d/%02x%02x%02x%02x)", newfile->filename, newfile->carousel_id, newfile->module_id, newfile->key_len, newfile->key[0], newfile->key[1], newfile->key[2], newfile->key[3]);

		newfile->p_key_len = dir->key_len;
		if (newfile->p_key_len > 0)
		{
			newfile->p_key = malloc(dir->key_len);
			memcpy(newfile->p_key, dir->key, dir->key_len);
		}
		else
			newfile->p_key = NULL;
		newfile->parent = dir;

		newfile->next = dir->files;
		if (newfile->next)
			newfile->next->prev = newfile;
		newfile->prev = NULL;
		dir->files = newfile;

		if (newfile->module_offset != 0)
			dsmcc_filecache_write_file(filecache, newfile);
	}
}
