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

struct dsmcc_entry_id
{
	uint16_t module_id;
	uint32_t key_len;
	uint8_t *key;
};

struct dsmcc_cached_dir
{
	struct dsmcc_entry_id id;

	char *name;
	char *dirpath;

	struct dsmcc_entry_id parent_id;

	struct dsmcc_cached_file *files;

	struct dsmcc_cached_dir *parent, *sub;
	struct dsmcc_cached_dir *next, *prev;
};

struct dsmcc_cached_file
{
	struct dsmcc_entry_id id;

	char        *filename;
	unsigned int data_len;

	struct dsmcc_entry_id    parent_id;
	struct dsmcc_cached_dir *parent;

	char        *module_file;
	unsigned int module_offset;

	struct dsmcc_cached_file *next, *prev;
};

struct dsmcc_file_cache
{
	struct dsmcc_object_carousel *carousel;

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
	car->filecache = calloc(1, sizeof(struct dsmcc_file_cache));
	car->filecache->carousel = car;

	car->filecache->downloadpath = strdup(downloadpath);
	if (downloadpath[strlen(downloadpath) - 1] == '/')
		car->filecache->downloadpath[strlen(downloadpath) - 1] = '\0';
	dsmcc_mkdir(car->filecache->downloadpath, 0755);

	car->filecache->callback = cache_callback;
	car->filecache->callback_arg = cache_callback_arg;
}

static void dsmcc_filecache_free_id_content(struct dsmcc_entry_id *id)
{
	if (id && id->key)
		free(id->key);
}

static void dsmcc_filecache_free_file(struct dsmcc_cached_file *file)
{
	dsmcc_filecache_free_id_content(&file->id);
	if (file->filename)
		free(file->filename);
	if (file->module_file)
		free(file->module_file);
	if (file->parent_id.key)
		free(file->parent_id.key);
	free(file);
}

static void dsmcc_filecache_free_files(struct dsmcc_cached_file *file)
{
	struct dsmcc_cached_file *next;

	if (!file)
		return;

	while (file)
	{
		next = file->next;
		dsmcc_filecache_free_file(file);
		file = next;
	}
}

static void dsmcc_filecache_free_dir(struct dsmcc_cached_dir *dir)
{
	dsmcc_filecache_free_id_content(&dir->id);
	dsmcc_filecache_free_id_content(&dir->parent_id);
	if (dir->name)
		free(dir->name);
	if (dir->dirpath)
		free(dir->dirpath);
	dsmcc_filecache_free_files(dir->files);
	free(dir);
}

static void dsmcc_filecache_free_dirs(struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir, *subdirnext;

	if (!dir)
		return;

	if (dir->sub)
	{
		for (subdir = dir->sub; subdir; subdir = subdirnext)
		{
			subdirnext = subdir->next;
			dsmcc_filecache_free_dirs(subdir);
		}
	}

	dsmcc_filecache_free_dir(dir);
}

void dsmcc_filecache_free(struct dsmcc_object_carousel *car)
{
	dsmcc_filecache_free_files(car->filecache->orphan_files);
	dsmcc_filecache_free_files(car->filecache->nameless_files);
	dsmcc_filecache_free_dirs(car->filecache->orphan_dirs);

	if (car->filecache->gateway)
		dsmcc_filecache_free_dirs(car->filecache->gateway);

	free(car->filecache->downloadpath);
	free(car->filecache);
	car->filecache = NULL;
}

static void dsmcc_filecache_add_file_to_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	file->next = *list_head;
	if (file->next)
		file->next->prev = file;
	file->prev = NULL;
	*list_head = file;
}

static void dsmcc_filecache_add_dir_to_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	dir->next = *list_head;
	if (dir->next)
		dir->next->prev = dir;
	dir->prev = NULL;
	*list_head = dir;
}

static void dsmcc_filecache_fill_id(struct dsmcc_entry_id *id, uint16_t module_id, uint32_t key_len, uint8_t *key)
{
	id->module_id = module_id;
	id->key_len = key_len;
	if (id->key_len > 0)
	{
		id->key = malloc(id->key_len);
		memcpy(id->key, key, id->key_len);
	}
	else
		id->key = NULL;
}

static void dsmcc_filecache_copy_id(struct dsmcc_entry_id *dst, struct dsmcc_entry_id *src)
{
	dsmcc_filecache_fill_id(dst, src->module_id, src->key_len, src->key);
}

static void dsmcc_filecache_id_str(char *buffer, int buffersize, struct dsmcc_entry_id *id)
{
	int i;

	snprintf(buffer, buffersize, "%hu:%d:", id->module_id, id->key_len);
	i = strlen(buffer);
	buffer += i;
	buffersize -= i;

	for (i = 0; i < id->key_len; i++)
	{
		snprintf(buffer, buffersize, "%02hhx", id->key[i]);
		buffer += 2;
		buffersize -= 2;
	}
}

static int dsmcc_filecache_id_cmp(struct dsmcc_entry_id *id1, struct dsmcc_entry_id *id2)
{
	if (id1->module_id != id2->module_id)
		return 0;

	if (id1->key_len != id2->key_len)
		return 0;

	return memcmp(id1->key, id2->key, id1->key_len) == 0;
}

static struct dsmcc_cached_dir *dsmcc_filecache_find_dir_in_subdirs(struct dsmcc_cached_dir *parent, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_dir *dir, *subdir;

	if (!parent)
		return NULL;

	if (dsmcc_filecache_id_cmp(&parent->id, id))
		return parent;

	/* Search sub dirs */
	for (subdir = parent->sub; subdir != NULL; subdir = subdir->next)
	{
		dir = dsmcc_filecache_find_dir_in_subdirs(subdir, id);
		if (dir)
			return dir;
	}

	return NULL;
}

static void dsmcc_filecache_attach_orphan_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir, struct dsmcc_cached_file *file)
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
	dsmcc_filecache_add_file_to_list(&dir->files, file);
}

static void dsmcc_filecache_attach_orphan_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *parent, struct dsmcc_cached_dir *dir)
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
	dsmcc_filecache_add_dir_to_list(&parent->sub, dir);
}

static void dsmcc_filecache_write_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_file *file)
{
	FILE *data_fd, *module_fd;
	char fn[1024], dirpath[1024];
	char data_buf[4096];
	unsigned int copied, cp_size;
	int rret, wret;

	if (file->parent && file->parent->dirpath)
	{
		snprintf(dirpath, 1024, "%s/%s", file->parent->dirpath, file->filename);
		snprintf(fn, 1024, "%s%s%s", filecache->downloadpath, dirpath[0] == '/' ? "" : "/", dirpath);

		if (filecache->callback)
		{
			int ok = (*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE_CHECK, dirpath, fn);
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
			(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE_SAVED, dirpath, fn);
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
		int ok = (*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR_CHECK, dir->dirpath, dirbuf);
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
		(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR_SAVED, dir->dirpath, dirbuf);

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

static struct dsmcc_cached_dir *dsmcc_filecache_cached_dir_find(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_dir *dir, *dirnext;
	struct dsmcc_cached_file *file, *filenext;

	if (dsmcc_log_enabled(DSMCC_LOG_DEBUG))
	{
		char buffer[32];
		dsmcc_filecache_id_str(buffer, 32, id);
		DSMCC_DEBUG("Searching for dir %s", buffer);
	}

	/* Scan through known dirs and return details if known else NULL */
	if (id->key_len == 0)
	{
		/* Return gateway object. Create if not already */
		if (filecache->gateway == NULL)
		{
			filecache->gateway = calloc(1, sizeof(struct dsmcc_cached_dir));
			filecache->gateway->name = strdup("");
			filecache->gateway->dirpath = strdup("");

			/* Attach any subdirs or files that arrived prev. */

			for (file = filecache->orphan_files; file; file = filenext)
			{
				filenext = file->next;
				if (dsmcc_filecache_id_cmp(&file->parent_id, &filecache->gateway->id))
					dsmcc_filecache_attach_orphan_file(filecache, filecache->gateway, file);
			}

			for (dir = filecache->orphan_dirs; dir; dir = dirnext)
			{
				dirnext = dir->next;
				if (dsmcc_filecache_id_cmp(&dir->parent_id, &filecache->gateway->id))
					dsmcc_filecache_attach_orphan_dir(filecache, filecache->gateway, dir);
			}

			dsmcc_filecache_write_dir(filecache, filecache->gateway); /* Write files to filesystem */
		}
		return filecache->gateway;
	}

	/* Find dir */
	dir = dsmcc_filecache_find_dir_in_subdirs(filecache->gateway, id);
	if (dir == NULL)
	{
		struct dsmcc_cached_dir *d;

		/* Try looking in orphan dirs */
		for (d = filecache->orphan_dirs; !dir && d; d = d->next)
			dir = dsmcc_filecache_find_dir_in_subdirs(d, id);
	}

	return dir;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_in_dir(struct dsmcc_cached_dir *dir, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_cached_dir *subdir;

	if (!dir)
		return NULL;

	/* Search files in this dir */
	for (file = dir->files; file != NULL; file = file->next)
		if (dsmcc_filecache_id_cmp(&file->id, id))
			return file;

	/* Search sub dirs */
	for (subdir = dir->sub; subdir != NULL; subdir = subdir->next)
		if ((file = dsmcc_filecache_find_file_in_dir(subdir, id)) != NULL)
			return file;

	return NULL;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *file;

	/* Try looking in parent-less list */
	for (file = filecache->orphan_files; file != NULL; file = file->next)
	{
		if (dsmcc_filecache_id_cmp(&file->id, id))
			return file;
	}

	/* Scan through known files and return details if known else NULL */
	file = dsmcc_filecache_find_file_in_dir(filecache->gateway, id);

	return file;
}

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t objkey_len, uint8_t *objkey, struct biop_binding *binding)
{
	struct dsmcc_entry_id tmpid;
	struct dsmcc_cached_dir *dir, *subdir, *subdirnext;
	struct dsmcc_cached_file *file, *filenext;

	if (filecache->carousel->cid != binding->ior->profile_body.obj_loc.carousel_id)
	{
		DSMCC_ERROR("Got a request to cache dir info for an invalid carousel %u (expected %u)", binding->ior->profile_body.obj_loc.carousel_id, filecache->carousel->cid);
		return;
	}

	tmpid.module_id = binding->ior->profile_body.obj_loc.module_id;
	tmpid.key_len = binding->ior->profile_body.obj_loc.objkey_len;
	tmpid.key = binding->ior->profile_body.obj_loc.objkey;
	dir = dsmcc_filecache_cached_dir_find(filecache, &tmpid);
	if (dir)
		return; /* Already got */

	dir = calloc(1, sizeof(struct dsmcc_cached_dir));
	dir->name = strdup(binding->name->id);
	dsmcc_filecache_fill_id(&dir->id, binding->ior->profile_body.obj_loc.module_id, binding->ior->profile_body.obj_loc.objkey_len, binding->ior->profile_body.obj_loc.objkey);
	dsmcc_filecache_fill_id(&dir->parent_id, module_id, objkey_len, objkey);
	dir->parent = dsmcc_filecache_cached_dir_find(filecache, &dir->parent_id);

	if (dsmcc_log_enabled(DSMCC_LOG_DEBUG))
	{
		char buffer[32];
		dsmcc_filecache_id_str(buffer, 32, &dir->parent_id);
		DSMCC_DEBUG("Caching dir %s (with parent %s)", dir->name, buffer);
	}

	if (!dir->parent)
	{
		/* Directory not yet known. Add this to dirs list */
		dsmcc_filecache_add_dir_to_list(&filecache->orphan_dirs, dir);
	}
	else
	{
		/* Create under parent directory */
		dsmcc_filecache_add_dir_to_list(&dir->parent->sub, dir);
	}

	/* Attach any files that arrived previously */
	for (file = filecache->orphan_files; file != NULL; file = filenext)
	{
		filenext = file->next;
		if (dsmcc_filecache_id_cmp(&file->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived file %s to newly created directory %s", file->filename, dir->name);
			dsmcc_filecache_attach_orphan_file(filecache, dir, file);
		}
	}

	/* Attach any subdirs that arrived beforehand */
	for (subdir = filecache->orphan_dirs; subdir != NULL; subdir = subdirnext)
	{
		subdirnext = subdir->next;
		if (dsmcc_filecache_id_cmp(&subdir->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived dir %s to newly created directory %s", subdir->name, dir->name);
			dsmcc_filecache_attach_orphan_dir(filecache, dir, subdir);
		}
	}

	if (dir->parent && dir->parent->dirpath)
		dsmcc_filecache_write_dir(filecache, dir); /* Write dir/files to filesystem */
}

void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, uint16_t module_id, uint8_t key_len, uint8_t *key, const char *module_file, int offset, int length)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_entry_id tmpid;

	tmpid.module_id = module_id;
	tmpid.key_len = key_len;
	tmpid.key = key;

	/* search for file info */
	file = dsmcc_filecache_find_file(filecache, &tmpid);
	if (!file)
	{
		/* Not known yet. Save data */

		if (dsmcc_log_enabled(DSMCC_LOG_DEBUG))
		{
			char buffer[32];
			dsmcc_filecache_id_str(buffer, 32, &tmpid);
			DSMCC_DEBUG("Unknown file %s in carousel %u, caching data", buffer, filecache->carousel->cid);
		}

		file = calloc(1, sizeof(struct dsmcc_cached_file));
		file->module_file = strdup(module_file);
		file->module_offset = offset;
		file->data_len = length;
		dsmcc_filecache_fill_id(&file->id, module_id, key_len, key);

		// Add to nameless files
		dsmcc_filecache_add_file_to_list(&filecache->nameless_files, file);
	}
	else
	{
		/* Save data */
		if (file->module_offset == 0)
		{
			file->module_file = strdup(module_file);
			file->module_offset = offset;
			file->data_len = length;
			dsmcc_filecache_write_file(filecache, file);
		}
		else
		{
			DSMCC_DEBUG("Data for file %s had already been saved", file->filename);
		}
	}
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_data(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *last;

	for (last = filecache->nameless_files; last != NULL; last = last->next)
	{
		if (dsmcc_filecache_id_cmp(&last->id, id))
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

void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key_len, uint8_t *key, struct biop_binding *binding)
{
	struct dsmcc_cached_file *newfile;
	struct dsmcc_entry_id tmpid;

	if (filecache->carousel->cid != binding->ior->profile_body.obj_loc.carousel_id)
	{
		DSMCC_ERROR("Got a request to cache dir info for an invalid carousel %ld (expected %ld)", binding->ior->profile_body.obj_loc.carousel_id, filecache->carousel->cid);
		return;
	}

	tmpid.module_id = binding->ior->profile_body.obj_loc.module_id;
	tmpid.key_len = binding->ior->profile_body.obj_loc.objkey_len;
	tmpid.key = binding->ior->profile_body.obj_loc.objkey;

	// Check we do not already have file (or file info) cached 
	if (dsmcc_filecache_find_file(filecache, &tmpid))
		return;

	// See if the data had already arrived for the file 
	newfile = dsmcc_filecache_find_file_data(filecache, &tmpid);

	if (!newfile)
	{
		// Create the file from scratch
		DSMCC_DEBUG("Data not arrived for file %s, caching", binding->name->id);
		newfile = calloc(1, sizeof(struct dsmcc_cached_file));
		dsmcc_filecache_copy_id(&newfile->id, &tmpid);
	}
	else
	{
		DSMCC_DEBUG("Data already arrived for file %s", binding->name->id);
	}

	dsmcc_filecache_fill_id(&newfile->parent_id, module_id, key_len, key);
	newfile->filename = strdup(binding->name->id);

	newfile->parent = dsmcc_filecache_cached_dir_find(filecache, &newfile->parent_id);

	if (dsmcc_log_enabled(DSMCC_LOG_DEBUG))
	{
		char buffer[32], buffer2[32];;
		dsmcc_filecache_id_str(buffer, 32, &newfile->id);
		dsmcc_filecache_id_str(buffer2, 32, &newfile->parent_id);
		DSMCC_DEBUG("Caching info in carousel %u for file %s (%s) with %s parent dir %s", filecache->carousel->cid, newfile->filename, buffer, newfile->parent ? "known" : "unknown", buffer2);
	}

	if (!newfile->parent)
	{
		/* Parent directory not yet known */
		dsmcc_filecache_add_file_to_list(&filecache->orphan_files, newfile);
	}
	else
	{
		/* Parent directory is known */
		dsmcc_filecache_add_file_to_list(&newfile->parent->files, newfile);

		if (newfile->module_offset != 0)
			dsmcc_filecache_write_file(filecache, newfile);
	}
}
