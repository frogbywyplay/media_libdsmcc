#include <stdlib.h>
#include <string.h>
#include "dsmcc-descriptor.h"

void
dsmcc_desc_free(struct descriptor *desc) {
		free(desc);
}

void
dsmcc_desc_process_type(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_type *type = &desc->data.type;

	type->text = (char *)malloc(desc->len);

	memcpy(type->text, Data, desc->len);

}

void
dsmcc_desc_process_name(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_name *name = &desc->data.name;

	name->text = (char *)malloc(desc->len);

	memcpy(name->text, Data, desc->len);

}

void
dsmcc_desc_process_info(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_info *info = &desc->data.info;

	memcpy(info->lang_code, Data, 3);

	info->text = (char *)malloc(desc->len - 3);

	memcpy(info->text, Data+3, desc->len - 3);
}

void
dsmcc_desc_process_modlink(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_modlink *modlink = &desc->data.modlink;

	modlink->position = Data[0];
//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Modlink -> Position = %d\n", modlink->position);

	modlink->module_id = (Data[1] << 8) | Data[2];
//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Modlink -> Mod Id = %d\n", modlink->module_id);

}

void
dsmcc_desc_process_crc32(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_crc32 *crc32 = &desc->data.crc32;

	crc32->crc = (Data[0] << 24) | (Data[1] << 16) | (Data[2] << 8) |
			Data[3];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> CRC32 -> CRC = %ld\n", crc32->crc);

}

void
dsmcc_desc_process_location(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_location *location = &desc->data.location;

	location = malloc(sizeof(struct descriptor_location));

	location->location_tag = Data[0];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Location -> Tag = %d\n", location->location_tag);

}

void
dsmcc_desc_process_dltime(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_dltime *dltime = &desc->data.dltime;

	dltime->download_time = (Data[0] << 24) | (Data[1] << 16) |
				(Data[2] << 8 ) | Data[3];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Download -> Time = %ld\n", dltime->download_time);

}

void
dsmcc_desc_process_grouplink(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_grouplink *grouplink = &desc->data.grouplink;

	grouplink->position = Data[0];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Grouplink -> Position = %d\n", grouplink->position);

	grouplink->group_id = (Data[1] << 24) | (Data[2] << 16) |
			      (Data[3] << 8 ) | Data[4];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Grouplink -> Group Id = %ld\n", grouplink->group_id);

}

void
dsmcc_desc_process_compressed(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_compressed *compressed = &desc->data.compressed;

	compressed->method = Data[0];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Compressed -> Method = %d\n", compressed->method);

	compressed->original_size = (Data[1] << 24) | (Data[2] << 16) |
				    (Data[3] << 8)  | Data[4];

//	fprintf(desc_fd, "Info -> Module -> Descriptor -> Compressed -> Size = %ld\n", compressed->original_size);

}

/*
void
dsmcc_desc_process_pirvate(unsigned char *Data, struct descriptor *desc) {
	struct descriptor_private *private = &desc->data.private;

	private = malloc(sizeof(struct descriptor_private));

	private->descriptor = (char *)malloc(desc->len);
	memcpy(private->descriptor, Data, desc->len);

	return private;
}
*/

struct descriptor *
dsmcc_desc_process_one(unsigned char *Data, int *offset) {
	struct descriptor *desc;
	int off=0;

	desc = malloc(sizeof(struct descriptor));
	desc->tag = Data[0];
	desc->len = Data[1];

/*	fprintf(desc_fd, "Info -> Module -> Descriptor -> Tag = %d\n", 
				desc->tag);
	fprintf(desc_fd, "Info -> Module -> Descriptor -> Length = %d\n", 
				desc->len);
				
	fflush(desc_fd);
*/
	off += 2;

	switch(desc->tag) {
		case 0x01:
			dsmcc_desc_process_type(Data+2, desc);
			break;
		case 0x02:
			dsmcc_desc_process_name(Data+2, desc);
			break;
		case 0x03:
			dsmcc_desc_process_info(Data+2, desc);
			break;
		case 0x04:
			dsmcc_desc_process_modlink(Data+2, desc);
			break;
		case 0x05:
			dsmcc_desc_process_crc32(Data+2, desc);
			break;
		case 0x06:
			dsmcc_desc_process_location(Data+2, desc);
			break;
		case 0x07:
			dsmcc_desc_process_dltime(Data+2, desc);
			break;
		case 0x08:
			dsmcc_desc_process_grouplink(Data+2, desc);
			break;
		case 0x09:
			dsmcc_desc_process_compressed(Data+2, desc);
			break;
			/*
		case 0x0A:	Subgroup Association
			*/
		default:
			break;
/*
			if(desc->tag >= 0x80 && 
					desc->tag <= 0xFF)
				dsmcc_desc_process_private(Data+2, desc);
			}  else if( MHP tag ) 
*/
	}

	off+= desc->len;

	*offset += off;

	return desc;

}

struct descriptor *
dsmcc_desc_process(unsigned char *Data, int data_len, int *offset){
	int index = 0;
	struct descriptor *desc, *list, *l;
	
	desc = list = NULL;

	while(data_len > index) {
//		fprintf(desc_fd, "Data_len %d Index %d\n", data_len, index);
		desc = dsmcc_desc_process_one(Data+index, &index);
		if(list == NULL) {
			list = desc;
			list->next = NULL;
		} else {
			for(l=list;l->next!=NULL;l=l->next) { ; }
			l->next = desc;
			desc->next = NULL;
		}
	}

	*offset += index;

	return list;
}

