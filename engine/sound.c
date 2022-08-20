// kgsws' ACE Engine
////
// Subset of SNDINFO support.
#include "sdk.h"
#include "engine.h"
#include "utils.h"
#include "wadfile.h"
#include "textpars.h"
#include "sound.h"

#define NUMSFX	109
#define NUM_SFX_HOOKS	8
#define SFX_PRIORITY	666
#define RNG_RECURSION	8

typedef struct old_sfxinfo_s
{
	uint8_t *name;
	uint32_t single;
	uint32_t priority;
	struct old_sfxinfo_s *link;
	int32_t pitch;
	int32_t volume;
	void *data;
	int32_t usefulness;
	int32_t lumpnum;
} old_sfxinfo_t;

//

static old_sfxinfo_t *S_sfx;

static uint32_t numsfx;
static sfxinfo_t *sfxinfo;

static hook_t sfx_hooks[NUM_SFX_HOOKS];

//
// hooks

static __attribute((regparm(2),no_caller_saved_registers))
uint32_t sound_start_check(void *mo, uint32_t idx)
{
	sfxinfo_t *sfx;
	uint32_t count = 0;

	// only 16 bits are valid
	idx &= 0xFFFF;

	if(!idx || idx >= numsfx)
		return 0;

	sfx = sfxinfo + idx;

	while(sfx->rng_count)
	{
		if(count > RNG_RECURSION)
			I_Error("[SNDINFO] Potentially recursive sound!");

		if(sfx->rng_count > 1)
			idx = P_Random() % sfx->rng_count;
		else
			idx = 0;

		idx = sfx->rng_id[idx];
		sfx = sfxinfo + idx;

		count++;
	}

	if(sfx->lumpnum < 0)
		return 0;

	return idx;
}

//
// funcs

static sfxinfo_t *sfx_find(uint64_t alias)
{
	// never search for internal sounds
	for(uint32_t i = NUMSFX; i < numsfx; i++)
	{
		if(sfxinfo[i].alias == alias)
			return sfxinfo + i;
	}

	return NULL;
}

static sfxinfo_t *sfx_create(uint64_t alias)
{
	uint32_t last = numsfx;

	numsfx++;
	if(numsfx >= 0x10000)
		I_Error("[SNDINFO] So. Many. Sounds.");

	sfxinfo = ldr_realloc(sfxinfo, numsfx * sizeof(sfxinfo_t));

	sfxinfo[last].alias = alias;
	sfxinfo[last].priority = SFX_PRIORITY;
	sfxinfo[last].reserved = 0;
	sfxinfo[last].rng_count = 0;
	sfxinfo[last].data = NULL;
	sfxinfo[last].usefulness = -1;
	sfxinfo[last].lumpnum = -1;

	return sfxinfo + last;
}

static sfxinfo_t *sfx_get(uint8_t *name)
{
	sfxinfo_t *ret;
	uint64_t alias = sfx_alias(name);

	ret = sfx_find(alias);
	if(ret)
		return ret;

	return sfx_create(alias);
}

//
// callbacks

static void cb_sndinfo(lumpinfo_t *li)
{
	uint8_t *kw, *name;
	sfxinfo_t *sfx;
	int32_t lump;

	tp_load_lump(li);

	while(1)
	{
		kw = tp_get_keyword();
		if(!kw)
			return;

		if(kw[0] == '$')
		{
			// special directive
			if(!strcmp(kw + 1, "random"))
			{
				uint32_t count = 0;
				uint16_t id[5];

				// alias
				kw = tp_get_keyword();
				if(!kw)
					goto error_end;

				name = kw;

				// block start
				kw = tp_get_keyword();
				if(!kw)
					goto error_end;
				if(kw[0] != '{')
					I_Error("[SNDINFO] Expected '{', found '%s'!", kw);

				// get random sounds
				while(1)
				{
					// get sound alias
					kw = tp_get_keyword();
					if(!kw)
						goto error_end;
					if(kw[0] == '}')
					{
						// done
						break;
					}

					if(count >= 5)
						I_Error("[SNDINFO] Too many random sounds!");

					sfx = sfx_get(kw);
					id[count] = sfx - sfxinfo;
					count++;
				}

				// update existing or create new entry
				sfx = sfx_get(name);
				sfx->rng_count = count;
				for(uint32_t i = 0; i < count; i++)
					sfx->rng_id[i] = id[i];

				continue;
			} else
			if(!strcmp(kw + 1, "limit"))
			{
				// ZDoom compatibility; just ignore this one
				// This MIGHT be supported in the future.
				tp_get_keyword(); // skip alias
				kw = tp_get_keyword(); // skip number
				if(!kw)
					goto error_end;
				continue;
			} else
				I_Error("[SNDINFO] Unsuported directive '%s'!", kw);
		}

		name = kw;

		// sound lump
		kw = tp_get_keyword();
		if(!kw)
			goto error_end;

		// get lump
		lump = wad_check_lump(kw);

		// update existing or create new entry
		sfx = sfx_get(name);
		sfx->lumpnum = lump;
		sfx->rng_count = 0;
	}

error_end:
	I_Error("[SNDINFO] Incomplete definition!");
}

//
// API

uint16_t sfx_by_alias(uint64_t alias)
{
	// never search for internal sounds
	for(uint32_t i = NUMSFX; i < numsfx; i++)
	{
		if(sfxinfo[i].alias == alias)
			return i;
	}
	return 0;
}

uint16_t sfx_by_name(uint8_t *name)
{
	return sfx_by_alias(sfx_alias(name));
}

uint64_t sfx_alias(uint8_t *name)
{
	// converts string name into 64bit alias (pseudo-hash)
	uint64_t ret = 0;
	uint32_t sub = 0;

	while(*name)
	{
		uint8_t in = *name++;

		if(in > 0x60)
			in ^= 0x20;

		in &= 0x3F;

		ret ^= (uint64_t)in << sub;
		ret ^= (uint64_t)in >> (64 - sub);

		sub += 6;
		if(sub >= 64)
			sub -= 64;
	}

	return ret;
}

void init_sound()
{
	uint8_t temp[16];

	*((uint16_t*)temp) = 0x5344;

	doom_printf("[ACE] init sounds\n");
	ldr_alloc_message = "Sound info memory allocation failed!";

	// allocate memory for internal sounds
	sfxinfo = ldr_malloc(NUMSFX * sizeof(sfxinfo_t));
	numsfx = NUMSFX;

	// process internal sounds
	for(uint32_t i = 1; i < NUMSFX; i++)
	{
		uint8_t *name;

		if(S_sfx[i].link)
			name = S_sfx[i].link->name;
		else
			name = S_sfx[i].name;
		strncpy(temp + 2, name, 6);

		sfxinfo[i].alias = 0;
		sfxinfo[i].priority = S_sfx[i].priority;
		sfxinfo[i].reserved = 0;
		sfxinfo[i].rng_count = 0;
		sfxinfo[i].data = NULL;
		sfxinfo[i].usefulness = -1;
		sfxinfo[i].lumpnum = wad_check_lump(temp);
	}

	// process SNDINFO
	wad_handle_lump("SNDINFO", cb_sndinfo);

	// patch CODE
	for(uint32_t i = 0; i < NUM_SFX_HOOKS; i++)
		sfx_hooks[i].value += (uint32_t)sfxinfo;
	utils_install_hooks(sfx_hooks, NUM_SFX_HOOKS);
}

//
// hooks

static hook_t sfx_hooks[NUM_SFX_HOOKS] =
{
	// patch access to 'S_sfx'
	{0x0003F160, CODE_HOOK | HOOK_UINT32, 0},
	{0x0003F3C2, CODE_HOOK | HOOK_UINT32, 28},
	{0x0003F3D4, CODE_HOOK | HOOK_UINT32, 28},
	{0x0003F3DE, CODE_HOOK | HOOK_UINT32, 24},
	{0x0003F401, CODE_HOOK | HOOK_UINT32, 24},
	{0x0003F40C, CODE_HOOK | HOOK_UINT32, 32},
	{0x0003F41D, CODE_HOOK | HOOK_UINT32, 24},
	{0x0003F42A, CODE_HOOK | HOOK_UINT32, 24},
};

static const hook_t hooks[] __attribute__((used,section(".hooks"),aligned(4))) =
{
	// import variables
	{0x0001488C, DATA_HOOK | HOOK_IMPORT, (uint32_t)&S_sfx},
	// custom sound ID check and translation
	// invalid sounds are skipped instead of causing error
	{0x0003F13E, CODE_HOOK | HOOK_CALL_ACE, (uint32_t)sound_start_check},
	{0x0003F143, CODE_HOOK | HOOK_UINT16, 0xC085},
	{0x0003F146, CODE_HOOK | HOOK_JMP_DOOM, 0x0003F365},
	{0x0003F145, CODE_HOOK | HOOK_UINT16, 0x840F},
	{0x0003F14B, CODE_HOOK | HOOK_UINT32, 0x07EBC789},
};
