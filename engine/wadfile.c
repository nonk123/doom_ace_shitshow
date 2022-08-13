// kgsws' ACE Engine
////
#include "sdk.h"
#include "utils.h"
#include "engine.h"
#include "wadfile.h"

//

uint8_t **wadfiles;
uint32_t *numlumps;
lumpinfo_t **lumpinfo;
void ***lumpcache;

uint32_t lumpcount;

//
// funcs

static void wad_add(uint8_t *name)
{
	// TODO: single file support (.lmp)
	int32_t fd;
	uint32_t tmp;
	wadhead_t head;
	lumpinfo_t *info;
	wadlump_t *lump = (void*)0x0002D0A0 + doom_data_segment; // drawsegs
	uint32_t totalsize;

	doom_printf("   %s ", name);

	fd = doom_open_RD(name);
	if(fd < 0)
	{
		doom_printf("- failed to open\n");
		return;
	}

	totalsize = strlen(name);
	if(totalsize > 4 && (*((uint32_t*)(name + totalsize - 4)) | 0x20202020) != 0x6461772E)
	{
		// load entire file as a single lump
		uint8_t *ptr, *dst;

		tmp = lumpcount + 1;
		tmp *= sizeof(lumpinfo_t);

		// realloc lump info
		info = doom_realloc(*lumpinfo, tmp);
		if(!info)
		{
			doom_printf("- realloc failed\n");
			doom_close(fd);
			return;
		}

		*lumpinfo = info;
		info += lumpcount;
		lumpcount = lumpcount + 1;

		// copy file name
		ptr = name + totalsize - 4;

		while(ptr > name && ptr[-1] != '\\' && ptr[-1] != '/')
			*ptr--;

		info->wame = 0;
		dst = info->name;
		while(*ptr && *ptr != '.' && ptr < info->name + 8)
		{
			uint8_t in = *ptr++;
			if(in >= 'a' && in <= 'z')
				in &= ~0x20;
			*dst++ = in;
		}

		// create entry
		info->fd = fd;
		info->offset = 0;
		info->size = doom_filelength(fd);

		doom_printf("- file, %u bytes; %.8s\n", info->size, info->name);

		return;
	}

	tmp = doom_read(fd, &head, sizeof(wadhead_t));
	if(	tmp != sizeof(wadhead_t) ||
		(head.id != 0x44415749 && head.id != 0x44415750)
	){
		doom_printf("- invalid\n");
		doom_close(fd);
		return;
	}

	tmp = lumpcount + head.numlumps;
	tmp *= sizeof(lumpinfo_t);

	// realloc lump info
	info = doom_realloc(*lumpinfo, tmp);
	if(!info)
	{
		doom_printf("- realloc failed\n");
		doom_close(fd);
		return;
	}

	*lumpinfo = info;
	info += lumpcount;
	lumpcount = lumpcount + head.numlumps;

	doom_lseek(fd, head.diroffs, SEEK_SET);

	// load lumps
	totalsize = 0;
	for(uint32_t i = 0; i < head.numlumps; i++, info++)
	{
		wadlump_t *lmp = lump + (i & 1023);

		if(lmp == lump)
		{
			uint32_t size = (head.numlumps - i) * sizeof(wadlump_t);

			if(size > 1024 * sizeof(wadlump_t))
				size = 1024 * sizeof(wadlump_t);

			tmp = doom_read(fd, lump, size);
			if(tmp != size)
				// reading failed; fill with dummy values
				memset(lump, 0, size);
		}

		info->wame = lmp->wame;
		info->fd = fd;
		info->offset = lmp->offset;
		info->size = lmp->size;

		totalsize += lmp->size;
	}

	doom_printf("- %u entries, %u bytes\n", head.numlumps, totalsize);
}

//
// API

void wad_init()
{
	doom_printf("[ACE] wad_init\n");

	*lumpinfo = doom_malloc(1);

	for(uint32_t i = 0; i < MAXWADFILES && wadfiles[i]; i++)
		wad_add(wadfiles[i]);

	if(!lumpcount)
		I_Error("Umm. WADs are empty ...");

	*lumpcache = doom_malloc(lumpcount * sizeof(void*));
	if(!*lumpcache)
		I_Error("Failed to allocate lump cache.");

	*numlumps = lumpcount;
	memset(*lumpcache, 0, lumpcount * sizeof(void*));
}

int32_t wad_check_lump(uint8_t *name)
{
	uint64_t wame;
	uint32_t idx;
	lumpinfo_t *li = *lumpinfo;

	// search as 64bit number
	strncpy((uint8_t*)&wame, name, 8);

	// do a backward search
	idx = lumpcount;
	do
	{
		idx--;
		if(li[idx].wame == wame)
			return idx;
	} while(idx);

	// not found
	return -1;
}

int32_t wad_get_lump(uint8_t *name)
{
	int32_t idx;

	idx = wad_check_lump(name);
	if(idx < 0)
		I_Error("Can't find lump %s!", name);

	return idx;
}

uint32_t wad_read_lump(void *dest, int32_t idx, uint32_t limit)
{
	int32_t ret;
	lumpinfo_t *li = *lumpinfo + idx;

	if(li->size < limit)
		limit = li->size;

	doom_lseek(li->fd, li->offset, SEEK_SET);
	ret = doom_read(li->fd, dest, limit);
	if(ret != limit)
		I_Error("Lump %.8s read failed!", li->name);

	return limit;
}

void *wad_cache_lump(int32_t idx, uint32_t *size)
{
	void *data;
	lumpinfo_t *li = *lumpinfo + idx;

	if(!li->size)
		I_Error("Lump %.8s is empty!");

	data = doom_malloc(li->size);
	if(!data)
		I_Error("Lump %.8s allocation failed!", li->name);

	wad_read_lump(data, idx, li->size);

	if(size)
		*size = li->size;

	return data;
}

//
// hooks
static const hook_t hooks[] __attribute__((used,section(".hooks"),aligned(4))) =
{
	// disable 'W_InitMultipleFiles'
	{0x00038A30, CODE_HOOK | HOOK_UINT8, 0xC3},
	// disable call to 'W_Reload' in 'P_SetupLevel'
	{0x0002E858, CODE_HOOK | HOOK_SET_NOPS, 5},
	// import variables
	{0x00029730, DATA_HOOK | HOOK_IMPORT, (uint32_t)&wadfiles},
	{0x00074FA0, DATA_HOOK | HOOK_IMPORT, (uint32_t)&numlumps},
	{0x00074FA4, DATA_HOOK | HOOK_IMPORT, (uint32_t)&lumpinfo},
	{0x00074F94, DATA_HOOK | HOOK_IMPORT, (uint32_t)&lumpcache},
};
