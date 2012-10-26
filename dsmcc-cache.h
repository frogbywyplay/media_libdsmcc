#ifndef DSMCC_CACHE_H
#define DSMCC_CACHE_H

	/* Very quick hack to resolve  circular dependencies between biop
	   and cache. */

struct biop_binding;
struct biop_message;

	/* and receiver */
struct cache_module_data;

struct cache_dir {
	struct cache_dir *next, *prev, *parent, *sub; /* TODO uugh! */
	struct cache_file *files;
	char *name;
	char *dirpath;
	unsigned long carousel_id;
	unsigned short module_id;
	unsigned int key_len;
	char *key;
	unsigned long p_carousel_id;	/* TODO this is a hack */
	unsigned short p_module_id;
	unsigned int p_key_len;
	char *p_key;
};

struct cache_file {
	unsigned long carousel_id;
	unsigned short module_id;
	unsigned int key_len;
	char *key;
	unsigned int data_len;
	char *filename;
	unsigned long offset;
	char *module_data_file;
	char complete;
	struct cache_file *next, *prev;;
	struct cache_dir *parent;
	unsigned long p_carousel_id;	/* TODO this is a hack */
	unsigned short p_module_id;
	unsigned int p_key_len;
	char *p_key;
};

struct file_info {
        char *filename;
        char *path;
        unsigned int size;
        char arrived;
        char written;
        struct file_info *next;
};


struct cache {
	struct cache_dir *gateway;
	struct cache_dir  *dir_cache;
	struct cache_file *file_cache;
	struct cache_file *data_cache;
	int num_files, total_files;
	int num_dirs, total_dirs;
	char *name;
	struct file_info *files;
};

void dsmcc_cache_init(struct cache *, const char *);
void dsmcc_cache_free(struct cache *);

void dsmcc_cache_dir_info(struct cache *, unsigned short, unsigned int, char *, struct biop_binding *);
void dsmcc_cache_file_info(struct cache *, unsigned short, unsigned int, char *, struct biop_binding *); 
void dsmcc_cache_file(struct cache *, struct biop_message *, struct cache_module_data *);

#endif
