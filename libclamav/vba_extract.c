/*
 *  Extract VBA source code for component MS Office Documents
 *
 *  Copyright (C) 2004 trog@uncon.org
 *
 *  This code is based on the OpenOffice and libgsf sources.
 *                  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <ctype.h>

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include "vba_extract.h"
#include "others.h"

#define FALSE (0)
#define TRUE (1)

typedef struct vba_version_tag {
	unsigned char signature[4];
	const char *name;
	int vba_version;
	int is_mac;
} vba_version_t;


#if WORDS_BIGENDIAN == 0
#define vba_endian_convert_16(v)       (v)
#else
static uint16_t vba_endian_convert_16(uint16_t v)
{
        return ((v >> 8) + (v << 8));
}
#endif
 
#if WORDS_BIGENDIAN == 0
#define vba_endian_convert_32(v)    (v)
#else
static uint32_t vba_endian_convert_32(uint32_t v)
{
        return ((v >> 24) | ((v & 0x00FF0000) >> 8) |
                ((v & 0x0000FF00) << 8) | (v << 24));
}
#endif

typedef struct byte_array_tag {
	unsigned int length;
	unsigned char *data;
} byte_array_t;

#define NUM_VBA_VERSIONS 10
vba_version_t vba_version[] = {
	{ { 0x5e, 0x00, 0x00, 0x01 }, "Office 97",              5, FALSE},
	{ { 0x5f, 0x00, 0x00, 0x01 }, "Office 97 SR1",          5, FALSE },
	{ { 0x65, 0x00, 0x00, 0x01 }, "Office 2000 alpha?",     6, FALSE },
	{ { 0x6b, 0x00, 0x00, 0x01 }, "Office 2000 beta?",      6, FALSE },
	{ { 0x6d, 0x00, 0x00, 0x01 }, "Office 2000",            6, FALSE },
	{ { 0x70, 0x00, 0x00, 0x01 }, "Office XP beta 1/2",     6, FALSE },
	{ { 0x73, 0x00, 0x00, 0x01 }, "Office XP",              6, FALSE },
        { { 0x79, 0x00, 0x00, 0x01 }, "Office 2003",            6, FALSE },
	{ { 0x60, 0x00, 0x00, 0x0e }, "MacOffice 98",           5, TRUE },
	{ { 0x62, 0x00, 0x00, 0x0e }, "MacOffice 2001",         5, TRUE },
};

#define VBA56_DIRENT_RECORD_COUNT (2 + /* magic */              \
                                   4 + /* version */            \
                                   2 + /* 0x00 0xff */          \
                                  22)  /* unknown */
#define VBA56_DIRENT_HEADER_SIZE (VBA56_DIRENT_RECORD_COUNT +   \
                                  2 +  /* type1 record count */ \
                                  2)   /* unknown */

/* Function: vba_readn
        Try hard to read the requested number of bytes
*/
int vba_readn(int fd, void *buff, unsigned int count)
{
        int retval;
        unsigned int todo;
        unsigned char *current;
 
        todo = count;
        current = (unsigned char *) buff;
 
        do {
                retval = read(fd, current, todo);
                if (retval == 0) {
                        return (count - todo);
                }
                if (retval < 0) {
                        return -1;
                }
                todo -= retval;
                current += retval;
        } while (todo > 0);
 
        return count;
}

/* Function: vba_writen
        Try hard to write the specified number of bytes
*/
int vba_writen(int fd, void *buff, unsigned int count)
{
        int retval;
        unsigned int todo;
        unsigned char *current;

        todo = count;
        current = (unsigned char *) buff;

        do {
                retval = write(fd, current, todo);
                if (retval < 0) {
                        return -1;
                }
                todo -= retval;
                current += retval;
        } while (todo > 0);

        return count;
}

char *get_unicode_name(char *name, int size)
{
        int i, j;
        char *newname;

	 if (*name == 0 || size == 0) {
                return NULL;
        }

        newname = (char *) cli_malloc(size*2);
        if (!newname) {
                return NULL;
        }
        j=0;
        for (i=0 ; i < size; i+=2) {
                if (isprint(name[i])) {
                        newname[j++] = name[i];
                } else {
                        if (name[i] < 10 && name[i] >= 0) {
                                newname[j++] = '_';
                                newname[j++] = name[i] + '0';
                        }
                        newname[j++] = '_';
                }
        }
        newname[j] = '\0';
        return newname;
}

static void vba56_test_middle(int fd)
{
	char test_middle[20];
	static const uint8_t middle_str[20] = {
		0x00, 0x00, 0xe1, 0x2e, 0x45, 0x0d, 0x8f, 0xe0,
		0x1a, 0x10, 0x85, 0x2e, 0x02, 0x60, 0x8c, 0x4d,
		0x0b, 0xb4, 0x00, 0x00
	};

        if (vba_readn(fd, &test_middle, 20) != 20) {
                return;
        }

	if (memcmp(test_middle, middle_str, 20) != 0) {
	        lseek(fd, -20, SEEK_CUR);
	}
	return;
}

static void vba56_test_end(int fd)
{
	char test_end[20];
	static const uint8_t end_str[20] =
	{
		0x00, 0x00, 0x2e, 0xc9, 0x27, 0x8e, 0x64, 0x12,
		0x1c, 0x10, 0x8a, 0x2f, 0x04, 0x02, 0x24, 0x00,
		0x9c, 0x02, 0x00, 0x00
	};

        if (vba_readn(fd, &test_end, 20) != 20) {
                return;
        }

        if (memcmp(test_end, end_str, 20) != 0) {
                lseek(fd, -20, SEEK_CUR);
        }
        return;
}


vba_project_t *vba56_dir_read(const char *dir)
{
	unsigned char magic[2];
	unsigned char version[4];
	unsigned char *buff, *name;
        unsigned char vba56_signature[] = { 0xcc, 0x61 };
	int16_t record_count, length;
	uint16_t ooff;
	uint8_t byte_count;
	uint32_t offset;
	uint32_t LidA;  /* Language identifiers */
	uint32_t LidB;
	uint16_t CharSet;
	uint16_t LenA;
	uint32_t UnknownB;
	uint32_t UnknownC;
	uint16_t LenB;
	uint16_t LenC;
	uint16_t LenD;
	int i, j, fd;
	vba_project_t *vba_project;
	char *fullname;

	unsigned char fixed_octet[8] = { 0x06, 0x02, 0x01, 0x00, 0x08, 0x02, 0x00, 0x00 };

	cli_dbgmsg("in vba56_dir_read()\n");

	fullname = (char *) cli_malloc(strlen(dir) + 15);
	sprintf(fullname, "%s/_VBA_PROJECT", dir);
        fd = open(fullname, O_RDONLY);

        if (fd == -1) {
                cli_dbgmsg("Can't open %s\n", fullname);
		free(fullname);
                return NULL;
        }
	free(fullname);

	if (vba_readn(fd, &magic, 2) != 2) {
		close(fd);
		return NULL;
	}
	if (memcmp(magic, vba56_signature, 2) != 0) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &version, 4) != 4) {
		close(fd);
		return NULL;
	}
	for (i=0 ; i < NUM_VBA_VERSIONS ; i++) {
		if (memcmp(version, vba_version[i].signature, 4) == 0) {
			break;
		}
	}

	if (i == NUM_VBA_VERSIONS) {
		cli_dbgmsg("Unknown VBA version signature x0%x0x%x0x%x0x%x\n",
			version[0], version[1], version[2], version[3]);
		close(fd);
		return NULL;
	}

	cli_dbgmsg("VBA Project: %s, VBA Version=%d\n", vba_version[i].name,
				vba_version[i].vba_version);


	/*****************************************/

	/* two bytes, should be equal to 0x00ff */
	if (vba_readn(fd, &ooff, 2) != 2) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &LidA, 4) != 4) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &LidB, 4) != 4) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &CharSet, 2) != 2) {
		close(fd);
		return NULL;
	}
	if (vba_readn(fd, &LenA, 2) != 2) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &UnknownB, 4) != 4) {
		close(fd);
		return NULL;
	}
	if (vba_readn(fd, &UnknownC, 4) != 4) {
		close(fd);
		return NULL;
	}

	if (vba_readn(fd, &LenB, 2) != 2) {
		close(fd);
		return NULL;
	}
	if (vba_readn(fd, &LenC, 2) != 2) {
		close(fd);
		return NULL;
	}
	if (vba_readn(fd, &LenD, 2) != 2) {
		close(fd);
		return NULL;
	}

        LidA = vba_endian_convert_32(LidA);
        LidB = vba_endian_convert_32(LidB);
        CharSet = vba_endian_convert_16(CharSet);
        LenA = vba_endian_convert_16(LenA);
        LenB = vba_endian_convert_16(LenB);
        LenC = vba_endian_convert_16(LenC);
        LenD = vba_endian_convert_16(LenD);

	cli_dbgmsg(" LidA: %d\n LidB: %d\n CharSet: %d\n", LidA, LidB, CharSet);
	cli_dbgmsg(" LenA: %d\n UnknownB: %d\n UnknownC: %d\n", LenA, UnknownB, UnknownC);
	cli_dbgmsg(" LenB: %d\n LenC: %d\n LenD: %d\n", LenB, LenC, LenD);

	record_count = LenC;
	/*******************************************/

	/* REPLACED THIS CODE WITH THE CODE ABOVE */
	/* read the rest of the header. most of this is unknown */
/*	buff = (char *) cli_malloc(24);
	if (!buff || vba_readn(fd, buff, 24) != 24) {
		close(fd);
		return NULL;
	}
	free(buff);

	if (vba_readn(fd, &record_count, 2) != 2) {
		close(fd);
		return NULL;
	}
	cli_dbgmsg("Record count: %d\n", record_count); */
	/* read two bytes and throw them away */
/*	if (vba_readn(fd, &length, 2) != 2) {
		close(fd);
		return NULL;
	}*/

	for (;;) {

		if (vba_readn(fd, &length, 2) != 2) {
			return NULL;
		}
		length = vba_endian_convert_16(length);
		if (length < 6) {
			lseek(fd, -2, SEEK_CUR);
			break;
		}
		cli_dbgmsg ("record: %d.%d, length: %d, ", record_count, i, length);
		buff = (unsigned char *) cli_malloc(length);
		if (!buff) {
			cli_errmsg("cli_malloc failed\n");
			close(fd);
			return NULL;
		}
		if (vba_readn(fd, buff, length) != length) {
			cli_errmsg("read name failed\n");
			close(fd);
			return NULL;
		}
		name = get_unicode_name(buff, length);
		cli_dbgmsg("name: %s\n", name);
		free(buff);

                /* Ignore twelve bytes from entries of type 'G'.
		   Type 'C' entries come in pairs, the second also
		   having a 12 byte trailer */
		/* TODO: Need to check if types H(same as G) and D(same as C) exist */
                if (!strncmp ("*\\G", name, 3)) {
			buff = (unsigned char *) cli_malloc(12);
                        if (vba_readn(fd, buff, 12) != 12) {
				cli_errmsg("failed to read blob\n");
                                free(buff);
				free(name);
				close(fd);
				return NULL;
                        }
			free(buff);
                } else if (!strncmp("*\\C", name, 3)) {
			if (i == 1) {
				buff = (unsigned char *) cli_malloc(12);
                        	if (vba_readn(fd, buff, 12) != 12) {
					cli_errmsg("failed to read blob\n");
                                	free(buff);
					free(name);
					close(fd);
					return NULL;
                        	}
				free(buff);
				i = 0;
			} else {
				i = 1;
				record_count++;
			}
		} else {
			/* Unknown type - probably ran out of strings - rewind */
			lseek(fd, -(length+2), SEEK_CUR);
			free(name);
			break;
		}
		free(name);
		vba56_test_middle(fd);
	}

	/* may need to seek forward 20 bytes here. Bleh! */
	vba56_test_end(fd);

	if (vba_readn(fd, &record_count, 2) != 2) {
		close(fd);
		return NULL;
	}
	record_count = vba_endian_convert_16(record_count);
	cli_dbgmsg("\nVBA Record count: %d\n", record_count);
	/*if (record_count <= 0) {
		close(fd);
		return TRUE;
	}*/

	lseek(fd, 2*record_count, SEEK_CUR);
	lseek(fd, 4, SEEK_CUR);

	/* Read fixed octet */
	buff = (unsigned char *) cli_malloc(8);
	if (!buff) {
		close(fd);
		return NULL;
	}
	if (vba_readn(fd, buff, 8) != 8) {
		free(buff);
		close(fd);
		return NULL;
	}
	if (!memcmp(buff, fixed_octet, 8)) {
		free(buff);
		close(fd);
		return NULL;
	}
	free(buff);
	cli_dbgmsg("Read fixed octet ok\n");

	/* junk some more stuff */
	do {
		if (vba_readn(fd, &ooff, 2) != 2) {
			close(fd);
			return NULL;
		}
	} while(ooff != 0xFFFF);
	
	if (vba_readn(fd, &ooff, 2) != 2) {
		close(fd);
		return NULL;
	}

	/* no idea what this stuff is */
	if (ooff != 0xFFFF) {
		ooff = vba_endian_convert_16(ooff);
		lseek(fd, ooff, SEEK_CUR);
	}
	if (vba_readn(fd, &ooff, 2) != 2) {
		close(fd);
		return NULL;
	}
	if (ooff != 0xFFFF) {
		ooff = vba_endian_convert_16(ooff);
		lseek(fd, ooff, SEEK_CUR);
	}
	lseek(fd, 100, SEEK_CUR);

	if (vba_readn(fd, &record_count, 2) != 2) {
		close(fd);
		return NULL;
	}
	record_count = vba_endian_convert_16(record_count);
	cli_dbgmsg("\nVBA Record count: %d\n", record_count);
	
	vba_project = (vba_project_t *) cli_malloc(sizeof(struct vba_project_tag));
	vba_project->name = (char **) cli_malloc(sizeof(char *) * record_count);
	vba_project->dir = strdup(dir);
	vba_project->offset = (uint32_t *) cli_malloc (sizeof(uint32_t) *
					record_count);
	vba_project->count = record_count;
	for (i=0 ; i < record_count ; i++) {
		if (vba_readn(fd, &length, 2) != 2) {
			goto out_error;
		}
		length = vba_endian_convert_16(length);
		buff = (unsigned char *) cli_malloc(length);
		if (!buff) {
			cli_dbgmsg("cli_malloc failed\n");
			goto out_error;
		}
		if (vba_readn(fd, buff, length) != length) {
			cli_dbgmsg("read name failed\n");
			free(buff);
			goto out_error;
		}
		vba_project->name[i] = get_unicode_name(buff, length);
		cli_dbgmsg("project name: %s, ", vba_project->name[i]);
		free(buff);

		/* some kind of string identifier ?? */
		if (vba_readn(fd, &length, 2) != 2) {
			free(vba_project->name[i]);
			goto out_error;
		}
		length = vba_endian_convert_16(length);
		lseek(fd, length, SEEK_CUR);

		/* unknown stuff */
		if (vba_readn(fd, &ooff, 2) != 2) {
			free(vba_project->name[i]);
			goto out_error;
		}
		ooff = vba_endian_convert_16(ooff);
		if (ooff == 0xFFFF) {
			lseek(fd, 2, SEEK_CUR);
			if (vba_readn(fd, &ooff, 2) != 2) {
				free(vba_project->name[i]);
				goto out_error;
			}
			ooff = vba_endian_convert_16(ooff);
			lseek(fd, ooff, SEEK_CUR);
		} else {
			lseek(fd, 2 + ooff, SEEK_CUR);
		}

		lseek(fd, 8, SEEK_CUR);
		if (vba_readn(fd, &byte_count, 1) != 1) {
			free(vba_project->name[i]);
			goto out_error;
		}
		for (j=0 ; j<byte_count; j++) {
			lseek(fd, 8, SEEK_CUR);
		}
		lseek(fd, 6, SEEK_CUR);
		if (vba_readn(fd, &offset, 4) != 4) {
			free(vba_project->name[i]);
			goto out_error;
		}
		offset = vba_endian_convert_32(offset);
		vba_project->offset[i] = offset;
		cli_dbgmsg("offset:%d\n", offset);
		lseek(fd, 2, SEEK_CUR);
	}
	
	
	{ /* There appears to be some code in here */
	
	off_t foffset;

		foffset = lseek(fd, 0, SEEK_CUR);
		cli_dbgmsg("\nOffset: 0x%x\n", (unsigned int)foffset);
	}
	close(fd);
	return vba_project;

out_error:
	/* Note: only to be called from the above loop
	   when i == number of allocated stings */
	for (j=0 ; j<i ; j++) {
		free(vba_project->name[j]);
	}
	free(vba_project->name);
	free(vba_project->dir);
	free(vba_project->offset);
	free(vba_project);
	close(fd);
	return NULL;
}

#define VBA_COMPRESSION_WINDOW 4096

void byte_array_append(byte_array_t *array, unsigned char *src, unsigned int len)
{
	if (array->length == 0) {
		array->data = (unsigned char *) cli_malloc(len);
		array->length = len;
		memcpy(array->data, src, len);
	} else {
		array->data = realloc(array->data, array->length+len);
		memcpy(array->data+array->length, src, len);
		array->length += len;
	}
}

unsigned char *vba_decompress(int fd, uint32_t offset, int *size)
{
	unsigned int i, pos=0, shift, win_pos, clean=TRUE, mask, distance;
	uint8_t flag;
	uint16_t token, len;
	unsigned char buffer[VBA_COMPRESSION_WINDOW];
	byte_array_t result;
	
	result.length=0;
	result.data=NULL;
	
	lseek(fd, offset+3, SEEK_SET); /* 1byte ?? , 2byte length ?? */ 
	
	while (vba_readn(fd, &flag, 1) == 1) {
		for (mask = 1; mask < 0x100; mask<<=1) {
			if (flag & mask) {
				if (vba_readn(fd, &token, 2) != 2) {
					if (result.data) {
						free(result.data);
					}
					if (size) {
						*size = 0;
					}
					return NULL;
				}
				token = vba_endian_convert_16(token);
				win_pos = pos % VBA_COMPRESSION_WINDOW;
				if (win_pos <= 0x80) {
					if (win_pos <= 0x20) {
						shift = (win_pos <= 0x10) ? 12:11;
					} else {
						shift = (win_pos <= 0x40) ? 10:9;
					}
				} else {
					if (win_pos <= 0x200) {
						shift = (win_pos <= 0x100) ? 8:7;
					} else if (win_pos <= 0x800) {
						shift = (win_pos <= 0x400) ? 6:5;
					} else {
						shift = 4;
					}
				}
				len = (token & ((1 << shift) -1)) + 3;
				distance = token >> shift;
				clean = TRUE;
				
				for (i=0 ; i < len; i++) {
					unsigned int srcpos;
					unsigned char c;
					
					srcpos = (pos - distance - 1) % VBA_COMPRESSION_WINDOW;
					c = buffer[srcpos];
					buffer[pos++ % VBA_COMPRESSION_WINDOW]= c;
				}
			} else {
				if ((pos != 0) &&
					((pos % VBA_COMPRESSION_WINDOW) == 0) && clean) {
					
					if (vba_readn(fd, &token, 2) != 2) {
						if (result.data) {
							free(result.data);
						}
						if (size) {
                                         	       *size = 0;
                                        	}
						return NULL;
					}
					clean = FALSE;
					byte_array_append(&result, buffer, VBA_COMPRESSION_WINDOW);
					break;
				}
				if (vba_readn(fd, buffer+(pos%VBA_COMPRESSION_WINDOW), 1) == 1){
					pos++;
				}
				clean = TRUE;
			}
		}
	}
			
	if (pos % VBA_COMPRESSION_WINDOW) {
		byte_array_append(&result, buffer, pos % VBA_COMPRESSION_WINDOW);
	}
	if (size) {
		*size = result.length;
	}
	return result.data;

}

/*
int vba_dump(vba_project_t *vba_project)
{
	int i, fd;
	unsigned char *data;
	char *fullname;

	for (i=0 ; i<vba_project->count ; i++) {
	
		cli_dbgmsg("\n\n*****************************\n");
		cli_dbgmsg("Deocding file: %s\n", vba_project->name[i]);
		cli_dbgmsg("*****************************\n");
		fullname = (char *) cli_malloc(strlen(vba_project->dir) + strlen(vba_project->name[i]) + 2);
		sprintf(fullname, "%s/%s", vba_project->dir, vba_project->name[i]);
		fd = open(fullname, O_RDONLY);
		free(fullname);
		if (fd == -1) {
			cli_dbgmsg("Open failed\n");
			return FALSE;
		}
		
		data = vba_decompress(fd, vba_project->offset[i], NULL);
		cli_dbgmsg("%s\n", data);
		close(fd);

	}
	return TRUE;
}

int main(int argc, char *argv[])
{
        int retval;
	char *dirname=NULL;
	vba_project_t *vba_project;
	
        while ((retval = getopt(argc, argv, "d:w")) != -1) {
                switch (retval) {
                        case 'd':
                                dirname = optarg;
                                break;
                        case ':':
                                cli_dbgmsg("missing option parameter\n");
                                exit(-1);
                        case '?':
                                cli_dbgmsg("unknown option\n");
                                break;
                }
        }
 
	vba_project = vba56_dir_read(dirname);

	if (vba_project != NULL) {
		vba_dump(vba_project);
	}
	return TRUE;
}
*/
