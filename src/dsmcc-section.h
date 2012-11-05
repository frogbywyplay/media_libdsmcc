#ifndef DSMCC_SECTION_H
#define DSMCC_SECTION_H

struct dsmcc_dii
{
	unsigned long  download_id;
	unsigned short block_size;
};

struct dsmcc_module_info
{
	unsigned short module_id;
	unsigned long  module_size;
	unsigned char  module_version;
};

struct dsmcc_ddb
{
	unsigned short module_id;
	unsigned char  module_version;
	unsigned short number;
	unsigned int   length;
};

#endif /* DSMCC_SECTION_H */
