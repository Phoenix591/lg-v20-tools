/* **********************************************************************
* Copyright (C) 2017 Elliott Mitchell					*
*									*
*	This program is free software: you can redistribute it and/or	*
*	modify it under the terms of the GNU General Public License as	*
*	published by the Free Software Foundation, either version 3 of	*
*	the License, or (at your option) any later version.		*
*									*
*	This program is distributed in the hope that it will be useful,	*
*	but WITHOUT ANY WARRANTY; without even the implied warranty of	*
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the	*
*	GNU General Public License for more details.			*
*									*
*	You should have received a copy of the GNU General Public	*
*	License along with this program.  If not, see			*
*	<http://www.gnu.org/licenses/>.					*
*************************************************************************
*$Id$		*
************************************************************************/


#include <unistd.h>
#include <endian.h>
#include <sys/mman.h>
#include <linux/fs.h>
#include <fcntl.h>
#include <stdio.h>
#include <zlib.h>

#include "kdz.h"
#include "md5.h"


const char kdz_file_magic[KDZ_MAGIC_LEN]={0x28, 0x05, 0x00, 0x00,
0x24, 0x38, 0x22, 0x25};

const char dz_file_magic[DZ_MAGIC_LEN]={0x32, 0x96, 0x18, 0x74};

const char dz_chunk_magic[DZ_MAGIC_LEN]={0x30, 0x12, 0x95, 0x78};


static voidpf zalloc(void *ignore, uInt items, uInt size)
{
	return calloc(items, size);
}

static void zfree(void *ignore, void *addr)
{
	free(addr);
}


struct kdz_file *open_kdzfile(const char *filename)
{
	int fd=-1;
	off_t len;
	char *map=NULL;
	struct kdz_file *ret=NULL;
	struct kdz_chunk *kdz;
	struct dz_file dz;
	int i;
	uint32_t chunks;
	off_t cur;
	MD5_CTX md5;
	char md5out[16];

	if((fd=open(filename, O_RDONLY|O_LARGEFILE))<0) {
		perror("failed open()ing file");
		return ret;
	}

	if((len=lseek(fd, 0, SEEK_END))<(1<<20)) {
		perror("file is too short");
		goto abort;
	}

	if((map=mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0))==MAP_FAILED) {
		perror("mmap() failed");
		goto abort;
	}

	if(memcmp(kdz_file_magic, map, KDZ_MAGIC_LEN)) {
		perror("missing magic number");
		goto abort;
	}

	kdz=(struct kdz_chunk *)(map+KDZ_MAGIC_LEN);
	while((i=strlen(kdz->name))>0) {
		if(!strcmp(kdz->name+i-3, ".dz")) break;

		++kdz;
	}
	if(len==0) {
		perror("failed to find inner DZ file");
		goto abort;
	}

	memcpy(&dz, map+le64toh(kdz->off), sizeof(struct dz_file));

	if(memcmp(dz.magic, dz_file_magic, DZ_MAGIC_LEN)) {
		perror("failed to find inner DZ magic");
		goto abort;
	}

	chunks=le32toh(dz.chunk_count);

	if((ret=malloc(sizeof(*ret)+sizeof(ret->chunks[0])*(chunks+1)))==NULL) {
		perror("memory allocation failure");
		goto abort;
	}

	ret->fd=fd;
	ret->mmap=map;
	ret->mlen=len;
	ret->off=le64toh(kdz->off);

	memcpy(&ret->dz_file, &dz, sizeof(struct dz_file));
	ret->dz_file.major=le32toh(dz.major);
	ret->dz_file.minor=le32toh(dz.minor);
	ret->dz_file.reserved0=le32toh(dz.reserved0);
	ret->dz_file.chunk_count=chunks;

	if(chunks==0||chunks>(1<<20)) {
		perror("chunk count isn't sane");
		goto abort;
	}

/*
	if(verbose)
		printf("%s: DZ file maj=%u, min=%u, dev=\"%s\" chunks=%u\n",
__func__, ret->dz_file.major, ret->dz_file.minor, ret->dz_file.device, chunks);
*/


	cur=ret->off;

	(*pMD5_Init)(&md5);

	for(cur=ret->off, i=0; i<=chunks; ++i) {
		if(cur+sizeof(struct dz_chunk)>len) {
			fprintf(stderr, "Next chunk starts beyond end of file!\n");
			goto abort;
		}

		ret->chunks[i].zoff=cur+sizeof(struct dz_chunk);
		if(i)
			(*pMD5_Update)(&md5, map+cur, sizeof(struct dz_chunk));
		memcpy(&ret->chunks[i].dz, map+cur, sizeof(struct dz_chunk));

		ret->chunks[i].dz.target_size=le32toh(ret->chunks[i].dz.target_size);
		ret->chunks[i].dz.data_size=le32toh(ret->chunks[i].dz.data_size);
		ret->chunks[i].dz.target_addr=le32toh(ret->chunks[i].dz.target_addr);
		ret->chunks[i].dz.trim_count=le32toh(ret->chunks[i].dz.trim_count);
		ret->chunks[i].dz.device=le32toh(ret->chunks[i].dz.device);

/*		if(verbose&i)
			printf("Slice Name: \"%s\"\n", ret->chunks[i].dz.slice_name);
/* */

		cur+=sizeof(struct dz_chunk)+ret->chunks[i].dz.data_size;
	}

	(*pMD5_Final)((unsigned char *)md5out, &md5);

	if(memcmp(md5out, ret->dz_file.md5, sizeof(md5out))) {
		fprintf(stderr, "Header MD5 didn't match!\n");
		fprintf(stderr, "\nHeader val: "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
ret->dz_file.md5[0x0], ret->dz_file.md5[0x1],
ret->dz_file.md5[0x2], ret->dz_file.md5[0x3],
ret->dz_file.md5[0x4], ret->dz_file.md5[0x5],
ret->dz_file.md5[0x6], ret->dz_file.md5[0x7],
ret->dz_file.md5[0x8], ret->dz_file.md5[0x9],
ret->dz_file.md5[0xA], ret->dz_file.md5[0xB],
ret->dz_file.md5[0xC], ret->dz_file.md5[0xD],
ret->dz_file.md5[0xE], ret->dz_file.md5[0xF]);
		fprintf(stderr, "MD5 val:    "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX "
"%02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX %02hhX\n",
md5out[0x0], md5out[0x1], md5out[0x2], md5out[0x3],
md5out[0x4], md5out[0x5], md5out[0x6], md5out[0x7],
md5out[0x8], md5out[0x9], md5out[0xA], md5out[0xB],
md5out[0xC], md5out[0xD], md5out[0xE], md5out[0xF]);
		goto abort;
	}

	return ret;

abort:
	if(fd>=0) {
		close(fd);
		if(map) {
			munmap(map, len);
			if(ret) free(ret);
		}
	}
	return NULL;
}


void close_kdzfile(struct kdz_file *kdz)
{
	if(!kdz) return;

	munmap(kdz->mmap, kdz->mlen);
	close(kdz->fd);
	free(kdz);
}


int report_kdzfile(struct kdz_file *kdz)
{
	int i;
	int fd=-1;
	int dev=-1;
	off64_t blksz;
	z_stream zstr={
		.zalloc=zalloc,
		.zfree=zfree,
	};
	MD5_CTX md5;
	char md5out[16];
	uint32_t crc;
	char *map=NULL;
	off_t mlen;
	char *buf=NULL;
	uint32_t bufsz=0;
	uint32_t cur;
	uint32_t mismatch;

	for(i=1; i<=kdz->dz_file.chunk_count; ++i) {

		if(kdz->chunks[i].dz.device!=dev) {
			char name[16];
			if(fd>=0) close(fd);
			if(map) munmap(map, mlen);
			dev=kdz->chunks[i].dz.device;
			snprintf(name, sizeof(name), "/dev/block/sd%c", 'a'+dev);

			if((fd=open(name, O_RDONLY|O_LARGEFILE))<0) {
				perror("open");
				goto abort;
			}
			blksz=0;
			if(ioctl(fd, BLKSSZGET, &blksz)<0) {
				perror("ioctl");
				goto abort;
			}
			if(bufsz!=blksz) {
				free(buf);
				bufsz=blksz;
				if(!(buf=malloc(bufsz))) {
					fprintf(stderr,
"Memory allocation error, cannot continue\n");
					goto abort;
				}
			}

			if((mlen=lseek(fd, 0, SEEK_END))<0) {
				perror("lseek");
				goto abort;
			}
			if((map=mmap(NULL, mlen, PROT_READ, MAP_SHARED, fd, 0))==MAP_FAILED) {
				map=NULL;
				perror("mmap");
				goto abort;
			}
		}

		(*pMD5_Init)(&md5);
		crc=crc32(0, Z_NULL, 0);

		zstr.next_in=(Bytef *)(kdz->mmap+kdz->chunks[i].zoff);
		zstr.avail_in=kdz->chunks[i].dz.data_size;
		if(inflateInit(&zstr)!=Z_OK) {
			fprintf(stderr, "inflateInit() failed: %s\n", zstr.msg);
			inflateEnd(&zstr);
			goto abort;
		}

		mismatch=0;



		if(kdz->chunks[i].dz.target_size%blksz) {
			printf("Block, not a multiple of block size!\n");
		}

		cur=0;

		while(cur<kdz->chunks[i].dz.target_size) {

			zstr.next_out=(unsigned char *)buf;
			zstr.avail_out=bufsz;

			switch(inflate(&zstr, Z_SYNC_FLUSH)) {
			case Z_OK:
				break;
			case Z_STREAM_END:
				break;
			default:
				printf("Chunk %d(%s): inflate() failed: %s\n",
i, kdz->chunks[i].dz.slice_name, zstr.msg);

				goto abort_block;
			}

			(*pMD5_Update)(&md5, buf, bufsz);
			crc=crc32(crc, (Bytef *)buf, bufsz);

			if(memcmp(map+kdz->chunks[i].dz.target_addr*blksz+cur,
buf, blksz)) ++mismatch;

			cur+=blksz;
		}

		(*pMD5_Final)((unsigned char *)md5out, &md5);

		if(crc!=kdz->chunks[i].dz.crc32)
			printf("Chunk %d(%s): CRC32 mismatch!\n", i, kdz->chunks[i].dz.slice_name);

		if(memcmp(md5out, kdz->chunks[i].dz.md5, sizeof(md5out)))
			printf("Chunk %d(%s): MD5 mismatch!\n", i, kdz->chunks[i].dz.slice_name);

		if(mismatch)
			printf("Chunk %d(%s): %d of %ld blocks mismatched\n", i,
kdz->chunks[i].dz.slice_name, mismatch, kdz->chunks[i].dz.target_size/blksz);
		else
			printf("Chunk %d(%s): all %ld blocks matched\n", i,
kdz->chunks[i].dz.slice_name, kdz->chunks[i].dz.target_size/blksz);

	abort_block:
		inflateEnd(&zstr);
	}

	munmap(map, mlen);
	free(buf);
	close(fd);

	return 0;

abort:
	/* map will only be non-NULL after mmap(), by which time mlen!=0 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
	if(map) munmap(map, mlen);
#pragma GCC diagnostic pop
	if(buf) free(buf);
	if(fd>=0) close(fd);

	inflateEnd(&zstr);

	return 128;
}


/*
check in all cases:
Slice Name: "BackupGPT"
Slice Name: "PrimaryGPT"

update:
Slice Name: "modem"
Slice Name: "system"  // need to preserve lib/modules if possible
Slice Name: "OP"

expect modified:
Slice Name: "recovery"
Slice Name: "recoverybak"
Slice Name: "boot"

expect DirtySanta:
Slice Name: "aboot"
Slice Name: "abootbak"

worry about:

Slice Name: "apdp"
Slice Name: "cust"
Slice Name: "factory"
Slice Name: "msadp"
Slice Name: "persist"
Slice Name: "rct"
Slice Name: "sec"

Slice Name: "cmnlib"
Slice Name: "cmnlib64"
Slice Name: "cmnlib64bak"
Slice Name: "cmnlibbak"
Slice Name: "devcfg"
Slice Name: "devcfgbak"
Slice Name: "hyp"
Slice Name: "hypbak"
Slice Name: "keymaster"
Slice Name: "keymasterbak"
Slice Name: "laf"
Slice Name: "lafbak"
Slice Name: "pmic"
Slice Name: "pmicbak"
Slice Name: "raw_resources"
Slice Name: "raw_resourcesbak"
Slice Name: "rpm"
Slice Name: "rpmbak"
Slice Name: "tz"
Slice Name: "tzbak"
Slice Name: "xbl"
Slice Name: "xbl2"
Slice Name: "xbl2bak"
Slice Name: "xblbak"
*/
