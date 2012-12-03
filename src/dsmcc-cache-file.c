#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-module.h"

struct dsmcc_cached_dir
{
	struct dsmcc_object_id id;

	char *name;
	char *path;
	bool  written;

	struct dsmcc_object_id   parent_id;
	struct dsmcc_cached_dir *parent;

	struct dsmcc_cached_file *files;
	struct dsmcc_cached_dir  *subdirs;

	struct dsmcc_cached_dir *next, *prev;
};

struct dsmcc_cached_file
{
	struct dsmcc_object_id id;

	char *name;
	char *path;
	bool  written;

	struct dsmcc_object_id   parent_id;
	struct dsmcc_cached_dir *parent;

	char *data_file;
	int   data_size;

	struct dsmcc_cached_file *next, *prev;
};

struct dsmcc_file_cache
{
	struct dsmcc_object_carousel *carousel;

	struct dsmcc_cached_dir  *gateway;
	struct dsmcc_cached_dir  *orphan_dirs;
	struct dsmcc_cached_file *orphan_files;
	struct dsmcc_cached_file *nameless_files;
};

static int idcmp(struct dsmcc_object_id *id1, struct dsmcc_object_id *id2)
{
	if (id1->module_id != id2->module_id)
		return 0;

	if (id1->key_mask != id2->key_mask)
		return 0;

	return (id1->key & id1->key_mask) == (id2->key & id2->key_mask);
}

static void init_filecache(struct dsmcc_object_carousel *carousel)
{
	if (!carousel->filecache)
	{
		carousel->filecache = calloc(1, sizeof(struct dsmcc_file_cache));
		carousel->filecache->carousel = carousel;
	}
}

static void free_files(struct dsmcc_cached_file *file, bool keep_files)
{
	struct dsmcc_cached_file *next;

	if (!file)
		return;

	while (file)
	{
		next = file->next;
		if (file->name)
			free(file->name);
		if (file->path)
		{
			if (!keep_files)
				unlink(file->path);
			free(file->path);
		}
		if (file->data_file)
			free(file->data_file);
		free(file);
		file = next;
	}
}

static void free_dirs(struct dsmcc_cached_dir *dir, bool keep_files)
{
	struct dsmcc_cached_dir *subdir, *subdirnext;

	if (!dir)
		return;

	subdir = dir->subdirs;
	while (subdir)
	{
		subdirnext = subdir->next;
		free_dirs(subdir, keep_files);
		subdir = subdirnext;
	}

	free_files(dir->files, keep_files);

	if (dir->name)
		free(dir->name);
	if (dir->path)
	{
		if (!keep_files)
			rmdir(dir->path);
		free(dir->path);
	}
	free(dir);
}

void dsmcc_filecache_free(struct dsmcc_object_carousel *carousel, bool keep_files)
{
	if (!carousel->filecache)
		return;

	free_dirs(carousel->filecache->gateway, keep_files);
	free_dirs(carousel->filecache->orphan_dirs, 0);
	free_files(carousel->filecache->orphan_files, 0);
	free_files(carousel->filecache->nameless_files, 0);
	free(carousel->filecache);
	carousel->filecache = NULL;
}

static void add_file_to_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	file->next = *list_head;
	if (file->next)
		file->next->prev = file;
	file->prev = NULL;
	*list_head = file;
}

static void remove_file_from_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	if (file->prev)
		file->prev->next = file->next;
	else
		*list_head = file->next;
	if (file->next)
		file->next->prev = file->prev;
	file->prev = NULL;
	file->next = NULL;
}

static struct dsmcc_cached_file *find_file_in_list(struct dsmcc_cached_file *list_head, struct dsmcc_object_id *id)
{
	struct dsmcc_cached_file *file;

	for (file = list_head; file != NULL; file = file->next)
		if (idcmp(&file->id, id))
			break;

	return file;
}

static void add_dir_to_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	dir->next = *list_head;
	if (dir->next)
		dir->next->prev = dir;
	dir->prev = NULL;
	*list_head = dir;
}

static void remove_dir_from_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	if (dir->prev)
		dir->prev->next = dir->next;
	else
		*list_head = dir->next;
	if (dir->next)
		dir->next->prev = dir->prev;
	dir->prev = NULL;
	dir->next = NULL;
}

static struct dsmcc_cached_dir *find_dir_in_subdirs(struct dsmcc_cached_dir *parent, struct dsmcc_object_id *id)
{
	struct dsmcc_cached_dir *dir, *subdir;

	if (!parent)
		return NULL;

	if (idcmp(&parent->id, id))
		return parent;

	/* Search sub dirs */
	for (subdir = parent->subdirs; subdir != NULL; subdir = subdir->next)
	{
		dir = find_dir_in_subdirs(subdir, id);
		if (dir)
			return dir;
	}

	return NULL;
}

static void link_file(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_file *file)
{
	char *fn;

	/* Skip already written files or files for which data has not yet arrived */
	if (file->written || !file->data_file)
		return;

	DSMCC_DEBUG("Writing out file %s under dir %s", file->name, file->parent->path);

	file->path = malloc(strlen(file->parent->path) + strlen(file->name) + 2);
	sprintf(file->path, "%s/%s", file->parent->path, file->name);

	fn = malloc(strlen(carousel->downloadpath) + strlen(file->path) + 2);
	sprintf(fn, "%s%s%s", carousel->downloadpath, file->path[0] == '/' ? "" : "/", file->path);

	if (carousel->cache_callback && !(*carousel->cache_callback)(carousel->cache_callback_arg, carousel->cid, DSMCC_CACHE_FILE, DSMCC_CACHE_CHECK, file->path, NULL))
	{
		DSMCC_DEBUG("Skipping file %s as requested", file->path);
		goto cleanup;
	}

	DSMCC_DEBUG("Linking data from %s to %s", file->data_file, fn);
	if (dsmcc_file_link(fn, file->data_file, file->data_size))
	{
		file->written = 1;

		if (carousel->cache_callback)
			(*carousel->cache_callback)(carousel->cache_callback_arg, carousel->cid, DSMCC_CACHE_FILE, DSMCC_CACHE_CREATED, file->path, fn);
	}

cleanup:
	free(fn);
}

static void write_dir(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_dir *dir)
{
	bool gateway;
	struct dsmcc_cached_dir *subdir;
	struct dsmcc_cached_file *file;
	char *dn = NULL;

	/* Skip already written dirs */
	if (dir->written)
		return;

	gateway = (dir == carousel->filecache->gateway);

	if (gateway)
	{
		DSMCC_DEBUG("Writing out gateway dir");
		dir->path = strdup("");
		dn = strdup(carousel->downloadpath);
	}
	else
	{
		DSMCC_DEBUG("Writing out dir %s under dir %s", dir->name, dir->parent->path);
		dir->path = malloc(strlen(dir->parent->path) + strlen(dir->name) + 2);
		sprintf(dir->path, "%s/%s", dir->parent->path, dir->name);
		dn = malloc(strlen(carousel->downloadpath) + strlen(dir->path) + 2);
		sprintf(dn, "%s/%s", carousel->downloadpath, dir->path);
	}

	/* call callback (except for gateway) */
	if (!gateway && carousel->cache_callback)
	{
		if (!(*carousel->cache_callback)(carousel->cache_callback_arg, carousel->cid, DSMCC_CACHE_DIR, DSMCC_CACHE_CHECK, dir->path, NULL))
		{
			DSMCC_DEBUG("Skipping directory %s as requested", dir->path);
			goto end;
		}
	}

	DSMCC_DEBUG("Creating directory %s", dn);
	mkdir(dn, 0755); 
	dir->written = 1;

	/* register and call callback (except for gateway) */
	if (!gateway && carousel->cache_callback)
		(*carousel->cache_callback)(carousel->cache_callback_arg, carousel->cid, DSMCC_CACHE_DIR, DSMCC_CACHE_CREATED, dir->path, dn);

	/* Write out files that had arrived before directory */
	for (file = dir->files; file; file = file->next)
		link_file(carousel, file);

	/* Recurse through child directories */
	for (subdir = dir->subdirs; subdir; subdir = subdir->next)
		write_dir(carousel, subdir);

end:
	if (dn)
		free(dn);
}

static struct dsmcc_cached_dir *find_cached_dir(struct dsmcc_file_cache *filecache, struct dsmcc_object_id *id)
{
	struct dsmcc_cached_dir *dir;

	DSMCC_DEBUG("Searching for dir 0x%04hx:0x%08x:0x%08x", id->module_id, id->key, id->key_mask);

	/* Find dir */
	dir = find_dir_in_subdirs(filecache->gateway, id);
	if (dir == NULL)
	{
		struct dsmcc_cached_dir *d;

		/* Try looking in orphan dirs */
		for (d = filecache->orphan_dirs; !dir && d; d = d->next)
			dir = find_dir_in_subdirs(d, id);
	}

	return dir;
}

void dsmcc_filecache_cache_dir(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *parent_id, struct dsmcc_object_id *id, const char *name)
{
	struct dsmcc_file_cache *filecache;
	struct dsmcc_cached_dir *dir, *subdir, *subdirnext;
	struct dsmcc_cached_file *file, *filenext;

	init_filecache(carousel);
	filecache = carousel->filecache;

	dir = find_cached_dir(filecache, id);
	if (dir)
		return; /* Already got */

	dir = calloc(1, sizeof(struct dsmcc_cached_dir));
	dir->name = name ? strdup(name) : NULL;
	memcpy(&dir->id, id, sizeof(struct dsmcc_object_id));

	/* Check if parent ID is NULL, i.e. if this is the gateway */
	if (!parent_id)
	{
		DSMCC_DEBUG("Caching gateway dir 0x%04hx:0x%08x:0x%08x", id->module_id, id->key, id->key_mask);

		filecache->gateway = dir;
	}
	else
	{
		DSMCC_DEBUG("Caching dir %s (0x%04hx:0x%08x:0x%08x) with parent 0x%04hx:0x%08x:0x%08x", dir->name,
				id->module_id, id->key, id->key_mask, parent_id->module_id, parent_id->key, parent_id->key_mask);

		memcpy(&dir->parent_id, parent_id, sizeof(struct dsmcc_object_id));
		dir->parent = find_cached_dir(filecache, &dir->parent_id);
		if (!dir->parent)
		{
			/* Directory not yet known. Add this to dirs list */
			add_dir_to_list(&filecache->orphan_dirs, dir);
		}
		else
		{
			/* Create under parent directory */
			add_dir_to_list(&dir->parent->subdirs, dir);
		}
	}

	/* Attach any files that arrived previously */
	for (file = filecache->orphan_files; file != NULL; file = filenext)
	{
		filenext = file->next;
		if (idcmp(&file->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived file %s to newly created directory %s", file->name, dir->name);

			/* detach from orphan files */
			remove_file_from_list(&filecache->orphan_files, file);

			/* attach to dir */
			file->parent = dir;
			add_file_to_list(&dir->files, file);
		}
	}

	/* Attach any subdirs that arrived beforehand */
	for (subdir = filecache->orphan_dirs; subdir != NULL; subdir = subdirnext)
	{
		subdirnext = subdir->next;
		if (idcmp(&subdir->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived dir %s to newly created directory %s", subdir->name, dir->name);

			/* detach from orphan dirs */
			remove_dir_from_list(&filecache->orphan_dirs, subdir);

			/* attach to parent */
			subdir->parent = dir;
			add_dir_to_list(&dir->subdirs, subdir);
		}
	}

	/* Write dir/files to filesystem */
	if (dir == filecache->gateway || (dir->parent && dir->parent->written))
		write_dir(carousel, dir);
}

static struct dsmcc_cached_file *find_file_in_dir(struct dsmcc_cached_dir *dir, struct dsmcc_object_id *id)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_cached_dir *subdir;

	if (!dir)
		return NULL;

	/* Search files in this dir */
	file = find_file_in_list(dir->files, id);
	if (file)
		return file;

	/* Search sub dirs */
	for (subdir = dir->subdirs; subdir != NULL; subdir = subdir->next)
		if ((file = find_file_in_dir(subdir, id)) != NULL)
			return file;

	return NULL;
}

static struct dsmcc_cached_file *find_file(struct dsmcc_file_cache *filecache, struct dsmcc_object_id *id)
{
	struct dsmcc_cached_file *file;

	/* Try looking in parent-less list */
	file = find_file_in_list(filecache->orphan_files, id);
	if (file)
		return file;

	/* Scan through known files and return details if known else NULL */
	file = find_file_in_dir(filecache->gateway, id);

	return file;
}

void dsmcc_filecache_cache_file(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *parent_id, struct dsmcc_object_id *id, const char *name)
{
	struct dsmcc_file_cache *filecache;
	struct dsmcc_cached_file *file;

	init_filecache(carousel);
	filecache = carousel->filecache;

	/* Check we do not already have file info or data cached  */
	if (find_file(filecache, id))
		return;

	/* See if the data had already arrived for the file */
	file = find_file_in_list(filecache->nameless_files, id);
	if (file)
	{
		DSMCC_DEBUG("Data already arrived for file %s", name);
		remove_file_from_list(&filecache->nameless_files, file);
	}
	else
	{
		DSMCC_DEBUG("Data not arrived for file %s, caching file info", name);
		file = calloc(1, sizeof(struct dsmcc_cached_file));
		memcpy(&file->id, id, sizeof(struct dsmcc_object_id));
	}

	memcpy(&file->parent_id, parent_id, sizeof(struct dsmcc_object_id));
	file->name = strdup(name);
	file->parent = find_cached_dir(filecache, &file->parent_id);

	DSMCC_DEBUG("Caching info in carousel %u for file %s (0x%04hx:0x%08x/0x%08x) with parent dir 0x%04hx:0x%08x/0x%08x",
			filecache->carousel->cid, file->name, file->id.module_id, file->id.key, file->id.key_mask,
			file->parent_id.module_id, file->parent_id.key, file->parent_id.key_mask);

	/* Check if parent directory is known */
	if (file->parent)
	{
		add_file_to_list(&file->parent->files, file);
		link_file(carousel, file);
	}
	else
		add_file_to_list(&filecache->orphan_files, file);
}

void dsmcc_filecache_cache_data(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *id, const char *data_file, uint32_t data_size)
{
	struct dsmcc_file_cache *filecache;
	struct dsmcc_cached_file *file;

	init_filecache(carousel);
	filecache = carousel->filecache;

	/* search for file info */
	file = find_file(filecache, id);
	if (!file)
	{
		/* Not known yet. Save data */
		DSMCC_DEBUG("Nameless file 0x%04x:0x%08x/0x%08x in carousel %u, caching data", id->module_id, id->key, id->key_mask, filecache->carousel->cid);

		file = calloc(1, sizeof(struct dsmcc_cached_file));
		memcpy(&file->id, id, sizeof(struct dsmcc_object_id));

		file->data_file = strdup(data_file);
		file->data_size = data_size;

		/* Add to nameless files */
		add_file_to_list(&filecache->nameless_files, file);
	}
	else
	{
		/* Save data */
		if (!file->written)
		{
			file->data_file = strdup(data_file);

			link_file(carousel, file);
		}
		else
		{
			DSMCC_DEBUG("Data for file %s in dir %s had already been saved", file->name, file->parent->path);
		}
	}
}
