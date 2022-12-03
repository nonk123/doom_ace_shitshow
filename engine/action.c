// kgsws' ACE Engine
////
// Action functions for DECORATE.
#include "sdk.h"
#include "engine.h"
#include "utils.h"
#include "dehacked.h"
#include "decorate.h"
#include "action.h"
#include "mobj.h"
#include "player.h"
#include "weapon.h"
#include "hitscan.h"
#include "inventory.h"
#include "sound.h"
#include "stbar.h"
#include "map.h"
#include "special.h"
#include "demo.h"
#include "render.h"
#include "config.h"
#include "textpars.h"

#define MAKE_FLAG(x)	{.name = #x, .bits = x}

typedef struct
{
	const uint8_t *name;
	uint32_t bits;
} dec_arg_flag_t;

typedef struct dec_arg_s
{
	const uint8_t *name;
	uint8_t *(*handler)(uint8_t*,const struct dec_arg_s*);
	uint16_t offset;
	uint16_t optional;
	const dec_arg_flag_t *flags;
} dec_arg_t;

typedef struct
{
	uint32_t size;
	const void *def;
	dec_arg_t arg[];
} dec_args_t;

typedef struct
{
	const uint8_t *name;
	void *func;
	const dec_args_t *args;
	void (*check)(const void*);
} dec_action_t;

typedef struct
{
	uint8_t special; // so far u8 is enough but ZDoom has types > 255
	const uint8_t *name;
} __attribute__((packed)) dec_linespec_t; // 'packed' for DOS use only

//

static const uint8_t *action_name;

void *parse_action_func;
void *parse_action_arg;

static const dec_action_t mobj_action[];
static const dec_linespec_t special_action[];

//
// common

static const dec_arg_flag_t flags_empty[] =
{
	// just terminator, these flags are not supported
	{NULL}
};

//
// argument parsers

static uint8_t *handle_sound(uint8_t *kw, const dec_arg_t *arg)
{
	*((uint16_t*)(parse_action_arg + arg->offset)) = sfx_by_alias(tp_hash64(kw));
	return tp_get_keyword();
}

static uint8_t *handle_mobjtype(uint8_t *kw, const dec_arg_t *arg)
{
	int32_t type;

	type = mobj_check_type(tp_hash64(kw));
	if(type < 0)
		type = MOBJ_IDX_UNKNOWN;

	*((uint16_t*)(parse_action_arg + arg->offset)) = type;

	return tp_get_keyword();
}

static uint8_t *handle_bool(uint8_t *kw, const dec_arg_t *arg)
{
	uint32_t tmp;

	if(!strcmp(kw, "true"))
		tmp = 1;
	else
	if(!strcmp(kw, "false"))
		tmp = 0;
	else
	if(doom_sscanf(kw, "%d", &tmp) != 1)
		I_Error("[DECORATE] Unable to parse number '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);

	*((uint8_t*)(parse_action_arg + arg->offset)) = !!tmp;

	return tp_get_keyword();
}

static uint8_t *handle_bool_invert(uint8_t *kw, const dec_arg_t *arg)
{
	kw = handle_bool(kw, arg);

	*((uint8_t*)(parse_action_arg + arg->offset)) ^= 1;

	return kw;
}

static uint8_t *handle_s8(uint8_t *kw, const dec_arg_t *arg)
{
	uint32_t tmp;

	uint_fast8_t negate;

	if(kw[0] == '-')
	{
		kw = tp_get_keyword();
		if(!kw)
			return NULL;
		negate = 1;
	} else
		negate = 0;

	if(doom_sscanf(kw, "%u", &tmp) != 1 || tmp > 127)
		I_Error("[DECORATE] Unable to parse integer '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);

	if(negate)
		tmp = -tmp;

	*((int8_t*)(parse_action_arg + arg->offset)) = tmp;

	return tp_get_keyword();
}

static uint8_t *handle_u16(uint8_t *kw, const dec_arg_t *arg)
{
	uint32_t tmp;

	if(doom_sscanf(kw, "%u", &tmp) != 1 || tmp > 65535)
		I_Error("[DECORATE] Unable to parse integer '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);

	*((uint16_t*)(parse_action_arg + arg->offset)) = tmp;

	return tp_get_keyword();
}

static uint8_t *handle_fixed(uint8_t *kw, const dec_arg_t *arg)
{
	fixed_t value;
	uint_fast8_t negate;

	if(kw[0] == '-')
	{
		kw = tp_get_keyword();
		if(!kw)
			return NULL;
		negate = 1;
	} else
		negate = 0;

	if(tp_parse_fixed(kw, &value))
		I_Error("[DECORATE] Unable to parse fixed_t '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);

	if(negate)
		value = -value;

	*((fixed_t*)(parse_action_arg + arg->offset)) = value;

	return tp_get_keyword();
}

static uint8_t *handle_angle(uint8_t *kw, const dec_arg_t *arg)
{
	kw = handle_fixed(kw, arg);

	if(kw)
	{
		fixed_t value = *((fixed_t*)(parse_action_arg + arg->offset));
		value = (11930464 * (int64_t)value) >> 16;
		*((fixed_t*)(parse_action_arg + arg->offset)) = value;
	}

	return kw;
}

static uint8_t *handle_translation(uint8_t *kw, const dec_arg_t *arg)
{
	void *ptr = r_translation_by_name(strlwr(kw));
	*((void**)(parse_action_arg + arg->offset)) = ptr;
	return tp_get_keyword();
}

static uint8_t *handle_damage(uint8_t *kw, const dec_arg_t *arg)
{
	uint32_t lo, hi, mul, add;

	lo = 0;
	hi = 0;
	mul = 0;
	add = 0;

	if(!strcmp(kw, "random"))
	{
		// function entry
		kw = tp_get_keyword();
		if(!kw)
			return NULL;
		if(kw[0] != '(')
			return NULL;

		// low
		kw = tp_get_keyword();
		if(!kw)
			return NULL;

		if(doom_sscanf(kw, "%u", &lo) != 1 || lo > 511)
			return NULL;

		// comma
		kw = tp_get_keyword();
		if(!kw)
			return NULL;
		if(kw[0] != ',')
			return NULL;

		// high
		kw = tp_get_keyword();
		if(!kw)
			return NULL;

		if(doom_sscanf(kw, "%u", &hi) != 1 || hi > 511)
			return NULL;

		// function end
		kw = tp_get_keyword();
		if(!kw)
			return NULL;
		if(kw[0] != ')')
			return NULL;

		// check range
		if(lo > hi)
			return NULL;
		if(hi - lo > 255)
			return NULL;

		// next
		kw = tp_get_keyword();
		if(!kw)
			return NULL;

		if(kw[0] == '*')
		{
			// multiply
			kw = tp_get_keyword();
			if(!kw)
				return NULL;

			if(doom_sscanf(kw, "%u", &mul) != 1 || !mul || mul > 16)
				return NULL;

			mul--;

			// next
			kw = tp_get_keyword();
			if(!kw)
				return NULL;
		}

		if(kw[0] == '+')
		{
			// add
			kw = tp_get_keyword();
			if(!kw)
				return NULL;

			if(doom_sscanf(kw, "%u", &add) != 1 || add > 511)
				return NULL;

			// next
			kw = tp_get_keyword();
			if(!kw)
				return NULL;
		}

		add = DAMAGE_CUSTOM(lo, hi, mul, add);
	} else
	{
		// direct value
		if(doom_sscanf(kw, "%u", &add) != 1 || add > 1000000)
			I_Error("[DECORATE] Unable to parse integer '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);
		kw = tp_get_keyword();
	}

	*((uint32_t*)(parse_action_arg + arg->offset)) = add;

	return kw;
}

static uint8_t *handle_flags(uint8_t *kw, const dec_arg_t *arg)
{
	const dec_arg_flag_t *flag;

	while(1)
	{
		if(kw[0] != '0')
		{
			flag = arg->flags;
			while(flag->name)
			{
				if(!strcmp(flag->name, kw))
					break;
				flag++;
			}

			if(!flag->name)
				I_Error("[DECORATE] Unknown flag '%s' for action '%s' in '%s'!", kw, action_name, parse_actor_name);

			*((uint32_t*)(parse_action_arg + arg->offset)) |= flag->bits;
		}

		kw = tp_get_keyword(kw, arg);
		if(!kw)
			return NULL;

		if(kw[0] == ',' || kw[0] == ')')
			return kw;

		if(kw[0] != '|')
			I_Error("[DECORATE] Unable to parse flags for action '%s' in '%s'!", action_name, parse_actor_name);

		kw = tp_get_keyword(kw, arg);
		if(!kw)
			return NULL;
	}
}

//
// slope to angle

static inline uint32_t SlopeDiv(unsigned num, unsigned den)
{
	unsigned ans;

	if(den < 512)
		return SLOPERANGE;

	ans = (num << 3) / (den >> 8);

	return ans <= SLOPERANGE ? ans : SLOPERANGE;
}

angle_t slope_to_angle(fixed_t slope)
{
	if(slope > 0)
	{
		if(slope < FRACUNIT)
			return tantoangle[SlopeDiv(slope, FRACUNIT)];
		else
			return ANG90 - 1 - tantoangle[SlopeDiv(FRACUNIT, slope)];
	} else
	{
		slope = -slope;
		if(slope < FRACUNIT)
			return -tantoangle[SlopeDiv(slope, FRACUNIT)];
		else
			return ANG270 + tantoangle[SlopeDiv(FRACUNIT, slope)];
	}
}

//
// player aim

static uint32_t player_aim(player_t *pl, angle_t *angle, fixed_t *slope, uint32_t seeker)
{
	// TODO: cache result for same-tick attacks?
	mobj_t *mo = pl->mo;
	fixed_t sl;
	angle_t an = *angle;

	if(pl->info_flags & PLF_AUTO_AIM || map_level_info->flags & MAP_FLAG_NO_FREELOOK)
	{
		// autoaim enabled
		sl = P_AimLineAttack(mo, an, AIMRANGE);
		if(!linetarget)
		{
			an += 1 << 26;
			sl = P_AimLineAttack(mo, an, AIMRANGE);

			if(!linetarget)
			{
				an -= 2 << 26;
				sl = P_AimLineAttack(mo, an, AIMRANGE);

				if(!linetarget)
				{
					*slope = 0;
					return 0;
				}
			}
		}
		*slope = sl;
		*angle = an;
		return 1;
	} else
	{
		// autoaim disabled
		*slope = 0;
		// seeker missile check
		linetarget = NULL;
		if(seeker)
			P_AimLineAttack(mo, an, AIMRANGE);
		return 0;
	}
}

//
// player ammo

static uint32_t remove_ammo(mobj_t *mo)
{
	player_t *pl = mo->player;
	mobjinfo_t *weap = pl->readyweapon;
	uint32_t take;

	// ammo check
	if(!weapon_has_ammo(mo, weap, pl->attackdown))
		return 1;

	// which ammo
	take = pl->attackdown;
	if(take > 1)
	{
		if(weap->eflags & MFE_WEAPON_ALT_USES_BOTH)
			take |= 1;
	} else
	{
		if(weap->eflags & MFE_WEAPON_PRIMARY_USES_BOTH)
			take |= 2;
	}

	// ammo use
	if(take & 1 && weap->weapon.ammo_type[0])
		inventory_take(mo, weap->weapon.ammo_type[0], weap->weapon.ammo_use[0]);
	if(take & 2 && weap->weapon.ammo_type[1])
		inventory_take(mo, weap->weapon.ammo_type[1], weap->weapon.ammo_use[1]);

	return 0;
}

//
// projectile spawn

void missile_stuff(mobj_t *mo, mobj_t *source, mobj_t *target, angle_t angle, angle_t pitch, fixed_t slope)
{
	fixed_t speed;

	if(mo->info->fast_speed && (fastparm || gameskill == sk_nightmare))
		speed = mo->info->fast_speed;
	else
		speed = mo->info->speed;

	if(source && mo->flags2 & MF2_SPAWNSOUNDSOURCE)
		S_StartSound(SOUND_CHAN_WEAPON(source), mo->info->seesound);
	else
		S_StartSound(mo, mo->info->seesound);

	mo->target = source;
	if(mo->flags1 & MF1_SEEKERMISSILE)
		mo->tracer = target;
	mo->angle = angle;

	if(slope || pitch)
	{
		if(slope)
			pitch += slope_to_angle(slope);
		pitch >>= ANGLETOFINESHIFT;
		mo->momz = FixedMul(speed, finesine[pitch]);
		speed = FixedMul(speed, finecosine[pitch]);
	}

	angle >>= ANGLETOFINESHIFT;
	mo->momx = FixedMul(speed, finecosine[angle]);
	mo->momy = FixedMul(speed, finesine[angle]);

	if(mo->flags1 & MF1_RANDOMIZE && mo->tics > 0)
	{
		mo->tics -= P_Random() & 3;
		if(mo->tics <= 0)
			mo->tics = 1;
	}

	// this is an extra addition
	// projectile spawn can be called in pickup function,
	// that creates nested 'P_TryMove' call
	// which breaks basicaly everything
	// so let's avoid that
	if(source->custom_inventory)
		return;

	mo->x += mo->momx >> 1;
	mo->y += mo->momy >> 1;
	mo->z += mo->momz >> 1;

	if(!P_TryMove(mo, mo->x, mo->y))
	{
		if(mo->flags & MF_SHOOTABLE)
			mobj_damage(mo, NULL, NULL, 100000, 0);
		else
			explode_missile(mo);
	}
}

//
// shatter / burst

static void shatter_spawn(mobj_t *mo, uint32_t type)
{
	uint32_t count, i;

	mo->momx = 0;
	mo->momy = 0;
	mo->momz = 0;

	count = FixedMul(mo->info->height, mo->radius) >> (FRACBITS + 5);
	if(count < 4)
		count = 4;

	count += (P_Random() % (count / 4));
	if(count < 8)
		count = 8;

	// limit the amount for low-end PCs
	count -= count >> 2;
	if(count > 100)
		count = 100;

	do
	{
		mobj_t *th;
		fixed_t x, y, z;

		x = -mo->radius + ((mo->radius >> 7) * P_Random());
		x += mo->x;
		y = -mo->radius + ((mo->radius >> 7) * P_Random());
		y += mo->y;
		z = (mo->info->height >> 8) * P_Random();
		z += mo->z;

		th = P_SpawnMobj(x, y, z, type);
		th->momx = -FRACUNIT + (P_Random() << 9);
		th->momy = -FRACUNIT + (P_Random() << 9);
		th->momz = 4 * FixedDiv(th->z - mo->z, mo->info->height);
	} while(--count);
}

//
// common args

static const dec_args_t args_SingleMobjtype =
{
	.size = sizeof(args_singleType_t),
	.arg =
	{
		{"type", handle_mobjtype, offsetof(args_singleType_t, type)},
		// terminator
		{NULL}
	}
};

//
// original weapon attacks
// these are not available for DECORATE
// and so are only used in primary fire

__attribute((regparm(2),no_caller_saved_registers))
void A_OldProjectile(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	uint16_t count;
	uint16_t proj = (uint32_t)st->arg;
	player_t *pl = mo->player;
	uint16_t ammo = pl->readyweapon->weapon.ammo_type[0];
	fixed_t slope, z;
	angle_t angle, pitch;
	mobj_t *th;

	if(proj == 35) // BFG
		count = dehacked.bfg_cells;
	else
		count = 1;

	if(proj == 34) // plasma
	{
		uint32_t state;
		state = pl->readyweapon->st_weapon.flash;
		state += P_Random() & 1;
		pl->psprites[1].state = states + state;
		pl->psprites[1].tics = 0;
	}

	inventory_take(mo, ammo, count);

	if(demoplayback == DEMO_OLD)
	{
		P_SpawnPlayerMissile(mo, proj);
		return;
	}

	pitch = 0;
	angle = mo->angle;

	if(!player_aim(pl, &angle, &slope, 0))
		pitch = mo->pitch;

	z = mo->z;
	z += mo->height / 2;
	z += mo->info->player.attack_offs;

	th = P_SpawnMobj(mo->x, mo->y, z, proj);
	missile_stuff(th, mo, NULL, angle, pitch, slope);
}

__attribute((regparm(2),no_caller_saved_registers))
void A_OldBullets(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	uint16_t count;
	uint16_t sound = (uint32_t)st->arg;
	player_t *pl = mo->player;
	uint16_t ammo = pl->readyweapon->weapon.ammo_type[0];
	uint16_t hs, vs;
	state_t *state;
	angle_t angle;

	if(sound == 4)
		count = 2;
	else
		count = 1;

	if(!inventory_take(mo, ammo, count))
		return;

	state = states + pl->readyweapon->st_weapon.flash;

	switch(sound)
	{
		case 86:
			state += pl->psprites[0].state - &states[52];
		case 1:
			count = 1;
			hs = pl->refire ? 18 : 0;
			vs = 0;
		break;
		case 2:
			count = 7;
			hs = 18;
			vs = 0;
		break;
		default:
			count = 20;
			hs = 19;
			vs = 5;
		break;
	}

	S_StartSound(SOUND_CHAN_WEAPON(mo), sound);

	// mobj to 'missile' animation
	if(mo->animation != ANIM_MISSILE && mo->info->state_missile)
		mobj_set_animation(mo, ANIM_MISSILE);

	pl->psprites[1].state = state;
	pl->psprites[1].tics = 0;

	angle = mo->angle;

	if(demoplayback != DEMO_OLD)
	{
		if(!player_aim(pl, &angle, &bulletslope, 0))
			bulletslope = finetangent[(pl->mo->pitch + ANG90) >> ANGLETOFINESHIFT];
	} else
		P_BulletSlope(mo);

	for(uint32_t i = 0; i < count; i++)
	{
		uint32_t damage;
		angle_t aaa;
		fixed_t sss;

		damage = 5 + 5 * (P_Random() % 3);
		aaa = angle;
		if(hs)
			aaa += (P_Random() - P_Random()) << hs;
		sss = bulletslope;
		if(vs)
			sss += (P_Random() - P_Random()) << 5;

		P_LineAttack(mo, aaa, MISSILERANGE, sss, damage);
	}
}

//
// weapon (logic)

const args_singleFixed_t def_LowerRaise =
{
	.value = 6 * FRACUNIT
};

static const dec_args_t args_LowerRaise =
{
	.size = sizeof(args_singleFixed_t),
	.def = &def_LowerRaise,
	.arg =
	{
		{"speed", handle_fixed, offsetof(args_singleFixed_t, value), 1},
		// terminator
		{NULL}
	}
};

__attribute((regparm(2),no_caller_saved_registers))
void A_Lower(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	const args_singleFixed_t *arg = st->arg;

	if(!pl)
		return;

	if(mo->custom_inventory)
		return;

	pl->weapon_ready = 0;
	pl->psprites[0].sy += arg->value;

	if(pl->psprites[0].sy < WEAPONBOTTOM)
		return;

	pl->psprites[0].sy = WEAPONBOTTOM;

	if(pl->playerstate == PST_DEAD)
	{
		if(pl->readyweapon->st_weapon.deadlow)
			stfunc(mo, pl->readyweapon->st_weapon.deadlow);
		return;
	}

	if(!pl->health)
	{
		// Voodoo Doll ...
		for(uint32_t i = 0; i < NUMPSPRITES; i++)
			pl->psprites[i].state = NULL;
		return;
	}

	pl->readyweapon = pl->pendingweapon;
	pl->pendingweapon = NULL;

	if(!pl->readyweapon)
		return;

	pl->stbar_update |= STU_WEAPON_NOW;

	S_StartSound(SOUND_CHAN_WEAPON(mo), pl->readyweapon->weapon.sound_up);

	stfunc(mo, pl->readyweapon->st_weapon.raise);
}

__attribute((regparm(2),no_caller_saved_registers))
void A_Raise(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	const args_singleFixed_t *arg = st->arg;

	if(!pl)
		return;

	if(mo->custom_inventory)
		return;

	pl->weapon_ready = 0;
	pl->psprites[0].sy -= arg->value;

	if(pl->psprites[0].sy > WEAPONTOP)
		return;

	pl->psprites[0].sy = WEAPONTOP;

	stfunc(mo, pl->readyweapon->st_weapon.ready);
}

__attribute((regparm(2),no_caller_saved_registers))
void A_GunFlash(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	uint32_t state;
	player_t *pl = mo->player;

	if(!pl)
		return;

	if(mo->custom_inventory)
		return;

	// mobj to 'missile' animation
	if(mo->animation != ANIM_MISSILE && mo->info->state_missile)
		mobj_set_animation(mo, ANIM_MISSILE);

	if(pl->attackdown > 1)
		state = pl->readyweapon->st_weapon.flash_alt;
	else
		state = pl->readyweapon->st_weapon.flash;

	if(!state)
		return;

	// just hope that this is called in 'weapon PSPR'
	pl->psprites[1].state = states + state;
	pl->psprites[1].tics = 0;
}

__attribute((regparm(2),no_caller_saved_registers))
void A_CheckReload(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(!mo->player)
		return;
	if(mo->custom_inventory)
		return;
	weapon_check_ammo(mo->player);
}

//
// weapon (light)

__attribute((regparm(2),no_caller_saved_registers))
void A_Light0(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(!mo->player)
		return;
	mo->player->extralight = 0;
}

__attribute((regparm(2),no_caller_saved_registers))
void A_Light1(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(!mo->player)
		return;
	mo->player->extralight = 1;
}

__attribute((regparm(2),no_caller_saved_registers))
void A_Light2(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(!mo->player)
		return;
	mo->player->extralight = 2;
}

//
// weapon (attack)

__attribute((regparm(2),no_caller_saved_registers))
void A_WeaponReady(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	pspdef_t *psp;

	if(!pl)
		return;

	if(mo->custom_inventory)
		return;

	if(extra_config.center_weapon != 2)
		pl->weapon_ready = 0;

	psp = pl->psprites;

	// mobj animation (not shooting)
	if(mo->animation == ANIM_MELEE || mo->animation == ANIM_MISSILE)
		mobj_set_animation(mo, ANIM_SPAWN);

	// sound
	if(	pl->readyweapon->weapon.sound_ready &&
		st == states + pl->readyweapon->st_weapon.ready
	)
		S_StartSound(SOUND_CHAN_WEAPON(mo), pl->readyweapon->weapon.sound_ready);

	// new selection
	if(pl->pendingweapon || !pl->health)
	{
		stfunc(mo, pl->readyweapon->st_weapon.lower);
		if(demoplayback != DEMO_OLD)
			// this has to be set regardless of weapon mode, for (new) demo compatibility
			pl->psprites[0].sy = WEAPONTOP;
		return;
	}

	// primary attack
	if(pl->cmd.buttons & BT_ATTACK)
	{
		if(weapon_fire(pl, 1, 0))
			return;
	}

	// secondary attack
	if(pl->cmd.buttons & BT_ALTACK)
	{
		if(weapon_fire(pl, 2, 0))
			return;
	}

	// not shooting
	pl->attackdown = 0;

	// enable bob
	pl->weapon_ready = 1;

	// weapon bob, for old demo
	if(demoplayback == DEMO_OLD)
	{
		angle_t angle = (128 * leveltime) & FINEMASK;
		pl->psprites[0].sx = FRACUNIT + FixedMul(pl->bob, finecosine[angle]);
		angle &= FINEANGLES / 2 - 1;
		pl->psprites[0].sy = WEAPONTOP + FixedMul(pl->bob, finesine[angle]);
	}
}

__attribute((regparm(2),no_caller_saved_registers))
void A_ReFire(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	uint32_t btn;

	if(!pl)
		return;

	if(mo->custom_inventory)
		return;

	if(pl->attackdown > 1)
		btn = BT_ALTACK;
	else
		btn = BT_ATTACK;

	if(	pl->cmd.buttons & btn &&
		!pl->pendingweapon &&
		pl->health
	) {
		pl->refire++;
		weapon_fire(pl, pl->attackdown, 1);
		return;
	}

	pl->refire = 0;
	weapon_check_ammo(pl);
}

//
// basic sounds

static __attribute((regparm(2),no_caller_saved_registers))
void A_Pain(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(mo->info->extra_type == ETYPE_PLAYERPAWN)
	{
		uint32_t hp = (mo->health - 1) / 25;
		if(hp > 3)
			hp = 3;
		S_StartSound(mo, mo->info->player.sound.pain[hp]);
		return;
	}
	S_StartSound(mo, mo->info->painsound);
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_Scream(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	S_StartSound(mo->flags1 & MF1_BOSS ? NULL : mo, mo->info->deathsound);
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_XScream(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(mo->info->extra_type == ETYPE_PLAYERPAWN)
	{
		S_StartSound(mo, mo->info->player.sound.gibbed);
		return;
	}
	S_StartSound(mo, 31);
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_PlayerScream(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(mo->info->extra_type == ETYPE_PLAYERPAWN)
	{
		if(mo->health < -mo->info->spawnhealth / 2)
			S_StartSound(mo, mo->info->player.sound.xdeath);
		else
			S_StartSound(mo, mo->info->player.sound.death);
		return;
	}
	S_StartSound(mo, mo->info->deathsound);
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_ActiveSound(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	S_StartSound(mo, mo->info->activesound);
}

//
// A_StartSound

static const dec_arg_flag_t flags_StartSound[] =
{
	MAKE_FLAG(CHAN_BODY), // default, 0
	MAKE_FLAG(CHAN_WEAPON),
	MAKE_FLAG(CHAN_VOICE),
	// terminator
	{NULL}
};

static const dec_args_t args_StartSound =
{
	.size = sizeof(args_StartSound_t),
	.arg =
	{
		{"sound", handle_sound, offsetof(args_StartSound_t, sound)},
		{"slot", handle_flags, offsetof(args_StartSound_t, slot), 1, flags_StartSound},
		// terminator
		{NULL}
	}
};

static __attribute((regparm(2),no_caller_saved_registers))
void A_StartSound(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_StartSound_t *arg = st->arg;
	void *origin;

	switch(arg->slot)
	{
		case CHAN_WEAPON:
			origin = SOUND_CHAN_WEAPON(mo);
		break;
		case CHAN_VOICE:
			origin = mo;
		break;
		default:
			origin = SOUND_CHAN_BODY(mo);
		break;
	}

	S_StartSound(origin, arg->sound);
}

//
// A_FaceTarget

static __attribute((regparm(2),no_caller_saved_registers))
void A_FaceTarget(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(!mo->target)
		return;

	mo->flags &= ~MF_AMBUSH;
	mo->angle = R_PointToAngle2(mo->x, mo->y, mo->target->x, mo->target->y);

	if(mo->target->flags & MF_SHADOW)
		mo->angle += (P_Random() - P_Random()) << 21;
}

//
// A_NoBlocking

static __attribute((regparm(2),no_caller_saved_registers))
void A_NoBlocking(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	mo->flags &= ~MF_SOLID;

	if(mo->info->extra_type == ETYPE_PLAYERPAWN)
		// no item drops for player classes
		return;

	// drop items
	for(mobj_dropitem_t *drop = mo->info->dropitem.start; drop < (mobj_dropitem_t*)mo->info->dropitem.end; drop++)
	{
		mobj_t *item;

		if(drop->chance < 255 && drop->chance > P_Random())
			continue;

		item = P_SpawnMobj(mo->x, mo->y, mo->z + (8 << FRACBITS), drop->type);
		item->flags |= MF_DROPPED;
		item->angle = P_Random() << 24;
		item->momx = 2*FRACUNIT - (P_Random() << 9);
		item->momy = 2*FRACUNIT - (P_Random() << 9);
		item->momz = (3 << 15) + (P_Random() << 10);
	}
}

//
// A_Look

static __attribute((regparm(2),no_caller_saved_registers))
void A_Look(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	doom_A_Look(mo);
}

//
// A_Chase

static __attribute((regparm(2),no_caller_saved_registers))
void A_Chase(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	fixed_t speed;

	if(mo->info->fast_speed && fastparm || gameskill == sk_nightmare)
		speed = mo->info->fast_speed;
	else
		speed = mo->info->speed;

	// workaround for non-fixed speed
	mo->info->speed = (speed + (FRACUNIT / 2)) >> FRACBITS;
	doom_A_Chase(mo);
	mo->info->speed = speed;
}

//
// A_SpawnProjectile

static const args_SpawnProjectile_t def_SpawnProjectile =
{
	.spawnheight = 32 * FRACUNIT
};

static const dec_arg_flag_t flags_SpawnProjectile[] =
{
	MAKE_FLAG(CMF_AIMOFFSET),
	MAKE_FLAG(CMF_AIMDIRECTION),
	MAKE_FLAG(CMF_TRACKOWNER),
	MAKE_FLAG(CMF_CHECKTARGETDEAD),
	MAKE_FLAG(CMF_ABSOLUTEPITCH),
	MAKE_FLAG(CMF_OFFSETPITCH),
	MAKE_FLAG(CMF_SAVEPITCH),
	MAKE_FLAG(CMF_ABSOLUTEANGLE),
	// terminator
	{NULL}
};

static const dec_args_t args_SpawnProjectile =
{
	.size = sizeof(args_SpawnProjectile_t),
	.def = &def_SpawnProjectile,
	.arg =
	{
		{"missiletype", handle_mobjtype, offsetof(args_SpawnProjectile_t, missiletype)},
		{"spawnheight", handle_fixed, offsetof(args_SpawnProjectile_t, spawnheight), 1},
		{"spawnofs_xy", handle_fixed, offsetof(args_SpawnProjectile_t, spawnofs_xy), 1},
		{"angle", handle_angle, offsetof(args_SpawnProjectile_t, angle), 1},
		{"flags", handle_flags, offsetof(args_SpawnProjectile_t, flags), 1, flags_SpawnProjectile},
		{"pitch", handle_angle, offsetof(args_SpawnProjectile_t, pitch), 1},
//		{"ptr", handle_pointer, offsetof(args_SpawnProjectile_t, ptr), 1},
		// terminator
		{NULL}
	}
};

static __attribute((regparm(2),no_caller_saved_registers))
void A_SpawnProjectile(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	angle_t angle = mo->angle;
	fixed_t pitch = 0;
	int32_t dist = 0;
	fixed_t x, y, z;
	mobj_t *th, *target;
	const args_SpawnProjectile_t *arg = st->arg;

	x = mo->x;
	y = mo->y;
	z = mo->z + arg->spawnheight;

	if(arg->flags & CMF_TRACKOWNER && mo->flags & MF_MISSILE)
	{
		mo = mo->target;
		if(!mo)
			return;
	}

	if(arg->flags & CMF_AIMDIRECTION)
		target = NULL;
	else
	{
		target = mo->target; // TODO: flags
		if(!target)
			return;
	}

	if(target && arg->flags & CMF_CHECKTARGETDEAD && target->health <= 0)
		return;

	if(target && arg->flags & CMF_AIMOFFSET)
	{
		angle = R_PointToAngle2(x, y, target->x, target->y);
		dist = P_AproxDistance(target->x - x, target->y - y);
	}

	if(arg->spawnofs_xy)
	{
		angle_t a = (mo->angle - ANG90) >> ANGLETOFINESHIFT;
		x += FixedMul(arg->spawnofs_xy, finecosine[a]);
		y += FixedMul(arg->spawnofs_xy, finesine[a]);
	}

	if(!(arg->flags & (CMF_AIMOFFSET|CMF_AIMDIRECTION)))
	{
		angle = R_PointToAngle2(x, y, target->x, target->y);
		dist = P_AproxDistance(target->x - x, target->y - y);
	}

	if(arg->flags & CMF_ABSOLUTEANGLE)
		angle = arg->angle;
	else
	{
		angle += arg->angle;
		if(target && target->flags & MF_SHADOW)
			angle += (P_Random() - P_Random()) << 20;
	}

	th = P_SpawnMobj(x, y, z, arg->missiletype);

	if(arg->flags & (CMF_AIMDIRECTION|CMF_ABSOLUTEPITCH))
		pitch = -arg->pitch;
	else
	if(th->info->speed)
	{
		dist /= th->info->speed;
		if(dist <= 0)
			dist = 1;
		dist = ((target->z + 32 * FRACUNIT) - z) / dist; // TODO: maybe aim for the middle?
		th->momz = FixedDiv(dist, th->info->speed);
		if(arg->flags & CMF_OFFSETPITCH)
			pitch = -arg->pitch;
	}

	missile_stuff(th, mo, target, angle, pitch, th->momz);
}

//
// A_FireProjectile

static const dec_arg_flag_t flags_FireProjectile[] =
{
	MAKE_FLAG(FPF_AIMATANGLE),
	MAKE_FLAG(FPF_TRANSFERTRANSLATION),
	MAKE_FLAG(FPF_NOAUTOAIM),
	// terminator
	{NULL}
};

static const dec_args_t args_FireProjectile =
{
	.size = sizeof(args_SpawnProjectile_t),
	.arg =
	{
		{"missiletype", handle_mobjtype, offsetof(args_SpawnProjectile_t, missiletype)},
		{"angle", handle_angle, offsetof(args_SpawnProjectile_t, angle), 1},
		{"useammo", handle_bool_invert, offsetof(args_SpawnProjectile_t, noammo), 1},
		{"spawnofs_xy", handle_fixed, offsetof(args_SpawnProjectile_t, spawnofs_xy), 1},
		{"spawnheight", handle_fixed, offsetof(args_SpawnProjectile_t, spawnheight), 1},
		{"flags", handle_flags, offsetof(args_SpawnProjectile_t, flags), 1, flags_FireProjectile},
		{"pitch", handle_angle, offsetof(args_SpawnProjectile_t, pitch), 1},
		// terminator
		{NULL}
	}
};

static __attribute((regparm(2),no_caller_saved_registers))
void A_FireProjectile(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	const args_SpawnProjectile_t *arg = st->arg;
	mobj_t *th;
	angle_t angle, pitch;
	fixed_t slope;
	fixed_t x, y, z;

	if(!pl)
		return;

	if(	!mo->custom_inventory &&
		!arg->noammo &&
		remove_ammo(mo)
	)
		return;

	pitch = -arg->pitch;
	angle = mo->angle;

	if(arg->flags & FPF_AIMATANGLE)
		angle += arg->angle;

	if(!player_aim(pl, &angle, &slope, mobjinfo[arg->missiletype].flags1 & MF1_SEEKERMISSILE))
		pitch += mo->pitch;

	if(arg->angle && !(arg->flags & FPF_AIMATANGLE))
		angle = mo->angle + arg->angle;

	x = mo->x;
	y = mo->y;
	z = mo->z;
	z += mo->height / 2;
	z += mo->info->player.attack_offs;
	z += arg->spawnheight;

	if(arg->spawnofs_xy)
	{
		angle_t a = (mo->angle - ANG90) >> ANGLETOFINESHIFT;
		x += FixedMul(arg->spawnofs_xy, finecosine[a]);
		y += FixedMul(arg->spawnofs_xy, finesine[a]);
	}

	th = P_SpawnMobj(x, y, z, arg->missiletype);
	if(th->flags & MF_MISSILE)
		missile_stuff(th, mo, linetarget, angle, pitch, slope);

	if(arg->flags & FPF_TRANSFERTRANSLATION)
		th->translation = mo->translation;
}

//
//  

static const args_BulletAttack_t def_FireBullets =
{
	.pufftype = 37,
	.flags = FBF_USEAMMO,
	.range = 8192 * FRACUNIT,
};

static const dec_arg_flag_t flags_FireBullets[] =
{
	MAKE_FLAG(FBF_USEAMMO),
	MAKE_FLAG(FBF_NOFLASH),
	MAKE_FLAG(FBF_NORANDOM),
	MAKE_FLAG(FBF_NORANDOMPUFFZ),
	// terminator
	{NULL}
};

static const dec_args_t args_FireBullets =
{
	.size = sizeof(args_BulletAttack_t),
	.def = &def_FireBullets,
	.arg =
	{
		{"spread_horz", handle_angle, offsetof(args_BulletAttack_t, spread_hor)},
		{"spread_vert", handle_angle, offsetof(args_BulletAttack_t, spread_ver)},
		{"numbullets", handle_s8, offsetof(args_BulletAttack_t, blt_count)},
		{"damage", handle_damage, offsetof(args_BulletAttack_t, damage)},
		{"pufftype", handle_mobjtype, offsetof(args_BulletAttack_t, pufftype), 1},
		{"flags", handle_flags, offsetof(args_BulletAttack_t, flags), 1, flags_FireBullets},
		{"range", handle_fixed, offsetof(args_BulletAttack_t, range), 1},
		// terminator
		{NULL}
	}
};

void check_FireBullets(const void *ptr)
{
	const args_BulletAttack_t *arg = ptr;
	if(arg->damage & DAMAGE_IS_CUSTOM && !(arg->flags & FBF_NORANDOM))
		I_Error("[DECORATE] A_FireBullets: missing 'FBF_NORANDOM' in '%s'.", parse_actor_name);
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_FireBullets(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	player_t *pl = mo->player;
	const args_BulletAttack_t *arg = st->arg;
	uint32_t spread, count;
	angle_t angle;
	fixed_t slope;

	if(!pl)
		return;

	if(	!mo->custom_inventory &&
		arg->flags & FBF_USEAMMO &&
		remove_ammo(mo)
	)
		return;

	if(mobjinfo[arg->pufftype].replacement)
		mo_puff_type = mobjinfo[arg->pufftype].replacement;
	else
		mo_puff_type = arg->pufftype;
	mo_puff_flags = arg->flags;

	if(arg->blt_count < 0)
	{
		count = -arg->blt_count;
		spread = 1;
	} else
	{
		count = arg->blt_count;
		if(!count)
			spread = 0;
		else
		if(count > 1)
			spread = 1;
		else
			spread = pl->refire;
	}

	if(!(arg->flags & FBF_NOFLASH))
		A_GunFlash(mo, NULL, NULL);

	angle = mo->angle;
	if(!player_aim(pl, &angle, &slope, 0))
		slope = finetangent[(pl->mo->pitch + ANG90) >> ANGLETOFINESHIFT];

	for(uint32_t i = 0; i < count; i++)
	{
		angle_t aaa;
		fixed_t sss;
		uint32_t damage = arg->damage;

		if(!(arg->flags & FBF_NORANDOM))
			damage *= (P_Random() % 3) + 1;

		aaa = angle;
		sss = slope;

		if(spread)
		{
			if(arg->spread_hor)
			{
				aaa -= arg->spread_hor;
				aaa += (arg->spread_hor >> 7) * P_Random();
			}
			if(arg->spread_ver)
			{
				sss -= arg->spread_ver;
				sss += (arg->spread_ver >> 7) * P_Random();
			}
		}

		P_LineAttack(mo, aaa, arg->range, sss, damage);
	}

	// must restore original puff!
	mo_puff_type = 37;
	mo_puff_flags = 0;
}

//
// A_GiveInventory

static const dec_args_t args_GiveInventory =
{
	.size = sizeof(args_GiveInventory_t),
	.arg =
	{
		{"type", handle_mobjtype, offsetof(args_GiveInventory_t, type)},
		{"amount", handle_u16, offsetof(args_GiveInventory_t, amount), 1},
//		{"ptr", handle_pointer, offsetof(args_GiveInventory_t, ptr), 1},
		// terminator
		{NULL}
	}
};

static __attribute((regparm(2),no_caller_saved_registers))
void A_GiveInventory(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_GiveInventory_t *arg = st->arg;
	mobj_give_inventory(mo, arg->type, arg->amount);
}

//
// A_SetTranslation

static const dec_args_t args_SetTranslation =
{
	.size = sizeof(args_singlePointer_t),
	.arg =
	{
		{"translation", handle_translation, offsetof(args_singlePointer_t, value)},
		// terminator
		{NULL}
	}
};

__attribute((regparm(2),no_caller_saved_registers))
void A_SetTranslation(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_singlePointer_t *arg = st->arg;
	mo->translation = arg->value;
}

//
// A_CheckPlayerDone

__attribute((regparm(2),no_caller_saved_registers))
void A_CheckPlayerDone(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	if(mo->player)
		return;

	mo->tics = 1;
	mobj_remove(mo);
}

//
// A_SetAngle

static const dec_args_t args_SetAngle =
{
	.size = sizeof(args_SetAngle_t),
	.arg =
	{
		{"angle", handle_angle, offsetof(args_SetAngle_t, angle)},
		{"flags", handle_flags, offsetof(args_SetAngle_t, flags), 1, flags_empty},
//		{"ptr", handle_pointer, offsetof(args_SetAngle_t, ptr), 1},
		// terminator
		{NULL}
	}
};

static __attribute((regparm(2),no_caller_saved_registers))
void A_SetAngle(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_SetAngle_t *arg = st->arg;
	mo->angle = arg->angle;
}

//
// chunks

static __attribute((regparm(2),no_caller_saved_registers))
void A_Burst(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_singleType_t *arg = st->arg;

	if(mo->player)
		I_Error("[DECORATE] A_Burst with player connected!");

	shatter_spawn(mo, arg->type);

	// hide, remove later - keep sound playing
	mo->render_style = RS_INVISIBLE;
	mo->state = states + STATE_UNKNOWN_ITEM;
	mo->tics = 35 * 4;

	P_UnsetThingPosition(mo);

	mo->flags &= ~(MF_SOLID | MF_SHOOTABLE);
	mo->flags |= MF_NOBLOCKMAP | MF_NOSECTOR;
	mo->special.tid = 0;
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_SkullPop(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_singleType_t *arg = st->arg;
	mobj_t *th;

	if(!mo->player)
		return;

	mo->player->flags |= PF_NO_BODY;

	th = P_SpawnMobj(mo->x, mo->y, mo->z + mo->info->player.view_height, arg->type);
	th->momx = (P_Random() - P_Random()) << 9;
	th->momy = (P_Random() - P_Random()) << 9;
	th->momz = FRACUNIT*2 + (P_Random() << 6);
	th->angle = mo->angle;
	th->player = mo->player;
	th->player->mo = th;
	th->target = mo->target;
	th->inventory = mo->inventory;
	mo->player = NULL;
	mo->inventory = NULL;
}

//
// freeze death

static __attribute((regparm(2),no_caller_saved_registers))
void A_FreezeDeath(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	mo->height <<= 2;
	mo->flags |= MF_NOBLOOD | MF_SHOOTABLE | MF_SOLID | MF_SLIDE;
	mo->flags1 |= MF1_PUSHABLE | MF1_TELESTOMP;
	mo->flags2 |= MF2_ICECORPSE;
	mo->iflags |= MFI_CRASHED;
	mo->tics = 75 + P_Random() + P_Random();
	mo->render_style = RS_NORMAL;

	if(!(mo->flags1 & MF1_DONTFALL))
		mo->flags &= ~MF_NOGRAVITY;

	S_StartSound(mo, SFX_FREEZE);

	if(mo->player)
	{
		mo->player->bonuscount = 0;
		mo->player->damagecount = 0;
		mo->player->flags |= PF_IS_FROZEN;
	}
}

__attribute((regparm(2),no_caller_saved_registers))
void A_GenericFreezeDeath(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	A_FreezeDeath(mo, st, stfunc);
	mo->frame &= ~FF_FULLBRIGHT;
	mo->translation = render_translation + TRANSLATION_ICE * 256;
}

__attribute((regparm(2),no_caller_saved_registers))
void A_FreezeDeathChunks(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	mobj_t *th;

	if(!(mo->iflags & MFI_SHATTERING) && (mo->momx || mo->momy || mo->momz))
	{
		mo->tics = 105;
		return;
	}

	S_StartSound(mo, SFX_ICEBREAK);

	// drop items
	A_NoBlocking(mo, st, stfunc);

	if(mo->player)
	{
		mo->player->flags |= PF_NO_BODY;
		th = P_SpawnMobj(mo->x, mo->y, mo->z + mo->info->player.view_height, MOBJ_IDX_ICE_CHUNK_HEAD);
		th->momx = (P_Random() - P_Random()) << 9;
		th->momy = (P_Random() - P_Random()) << 9;
		th->momz = FRACUNIT*2 + (P_Random() << 6);
		th->angle = mo->angle;
		th->player = mo->player;
		th->player->mo = th;
		th->target = mo->target;
		th->inventory = mo->inventory;
		mo->player = NULL;
		mo->inventory = NULL;
	}

	shatter_spawn(mo, MOBJ_IDX_ICE_CHUNK);

	// hide, remove later - keep sound playing
	mo->render_style = RS_INVISIBLE;
	mo->state = states + STATE_UNKNOWN_ITEM;
	mo->tics = 35 * 4;

	P_UnsetThingPosition(mo);

	mo->flags &= ~(MF_SOLID | MF_SHOOTABLE);
	mo->flags |= MF_NOBLOCKMAP | MF_NOSECTOR;
	mo->special.tid = 0;
}

__attribute((regparm(2),no_caller_saved_registers))
void A_IceSetTics(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	mo->tics = 70 + (P_Random() & 63);
}

//
// activate line special

static int32_t parse_arg_value(mobj_t *mo, int32_t value)
{
	if(value <= 32000)
		return value;
	return mo->special.arg[value - 32001];
}

static __attribute((regparm(2),no_caller_saved_registers))
void A_LineSpecial(mobj_t *mo, state_t *st, stfunc_t stfunc)
{
	const args_lineSpecial_t *arg = st->arg;

	spec_special = arg->special;
	spec_arg[0] = parse_arg_value(mo, arg->arg[0]);
	spec_arg[1] = parse_arg_value(mo, arg->arg[1]);
	spec_arg[2] = parse_arg_value(mo, arg->arg[2]);
	spec_arg[3] = parse_arg_value(mo, arg->arg[3]);
	spec_arg[4] = parse_arg_value(mo, arg->arg[4]);

	spec_activate(NULL, mo, 0);
}

//
// parser

uint8_t *action_parser(uint8_t *name)
{
	uint8_t *kw;
	const dec_action_t *act = mobj_action;

	// find action
	while(act->name)
	{
		if(!strcmp(act->name, name))
			break;
		act++;
	}

	if(!act->name)
	{
		// try line specials
		const dec_linespec_t *spec = special_action;

		while(spec->special)
		{
			if(!strcmp(spec->name, name))
				break;
			spec++;
		}

		if(spec->special)
		{
			args_lineSpecial_t *arg;
			uint32_t idx = 0;

			// set defaults
			arg = dec_es_alloc(sizeof(args_lineSpecial_t));
			arg->special = spec->special;
			arg->arg[0] = 0;
			arg->arg[1] = 0;
			arg->arg[2] = 0;
			arg->arg[3] = 0;
			arg->arg[4] = 0;

			// enter function
			kw = tp_get_keyword();
			if(!kw || kw[0] != '(')
				return NULL;

			// parse
			while(1)
			{
				int32_t value;

				// get value
				kw = tp_get_keyword();
				if(!kw)
					return NULL;

				if(idx >= 5)
					I_Error("[DECORATE] Too many arguments for action '%s' in '%s'!", spec->name, parse_actor_name);

				// check for 'args'
				if(!strcmp(kw, "args"))
				{
					kw = tp_get_keyword();
					if(!kw || kw[0] != '[')
						return NULL;

					kw = tp_get_keyword();
					if(!kw)
						return NULL;

					if(doom_sscanf(kw, "%d", &value) != 1 || value < 0 || value > 4)
						I_Error("[DECORATE] Unable to parse number '%s' for action '%s' in '%s'!", kw, spec->name, parse_actor_name);

					kw = tp_get_keyword();
					if(!kw || kw[0] != ']')
						return NULL;

					value += 32001;
				} else
				{
					// parse numeric value
					if(doom_sscanf(kw, "%d", &value) != 1 || value < -32000 || value > 32000)
						I_Error("[DECORATE] Unable to parse number '%s' for action '%s' in '%s'!", kw, spec->name, parse_actor_name);
				}

				arg->arg[idx++] = value;

				// get comma or end
				kw = tp_get_keyword();
				if(!kw)
					return NULL;

				if(kw[0] == ')')
					break;

				if(kw[0] != ',')
					return NULL;
			}

			parse_action_func = A_LineSpecial;
			parse_action_arg = arg;

			// return next keyword
			return tp_get_keyword();
		}

		// not found
		I_Error("[DECORATE] Unknown action '%s' in '%s'!", name, parse_actor_name);
	}

	// action function
	parse_action_func = act->func;

	// next keyword
	kw = tp_get_keyword();
	if(!kw)
		return NULL;

	if(act->args)
	{
		const dec_arg_t *arg = act->args->arg;

		parse_action_arg = dec_es_alloc(act->args->size);
		if(act->args->def)
			memcpy(parse_action_arg, act->args->def, act->args->size);
		else
			memset(parse_action_arg, 0, act->args->size);

		action_name = act->name;

		if(kw[0] != '(')
		{
			if(!arg->optional)
				I_Error("[DECORATE] Missing argument '%s' for action '%s' in '%s'!", arg->name, act->name, parse_actor_name);
		} else
		{
			while(arg->name)
			{
				kw = tp_get_keyword();
				if(!kw)
					return NULL;

				if(kw[0] == ')')
				{
args_end:
					if(arg->name && !arg->optional)
						I_Error("[DECORATE] Missing argument '%s' for action '%s' in '%s'!", arg->name, act->name, parse_actor_name);
					// check validity
					if(act->check)
						act->check(parse_action_arg);
					// return next keyword
					return tp_get_keyword();
				}

				kw = arg->handler(kw, arg);
				if(!kw || (kw[0] != ',' && kw[0] != ')'))
					I_Error("[DECORATE] Failed to parse arguments for action '%s' in '%s'!", act->name, parse_actor_name);

				arg++;

				if(kw[0] == ')')
					goto args_end;

				if(!arg->name)
					I_Error("[DECORATE] Too many arguments for action '%s' in '%s'!", act->name, parse_actor_name);
			}
		}
	}

	return kw;
}

//
// table of actions

static const dec_action_t mobj_action[] =
{
	// weapon
	{"a_lower", A_Lower, &args_LowerRaise},
	{"a_raise", A_Raise, &args_LowerRaise},
	{"a_gunflash", A_GunFlash},
	{"a_checkreload", A_CheckReload},
	{"a_light0", A_Light0},
	{"a_light1", A_Light1},
	{"a_light2", A_Light2},
	{"a_weaponready", A_WeaponReady},
	{"a_refire", A_ReFire},
	// sound
	{"a_pain", A_Pain},
	{"a_scream", A_Scream},
	{"a_xscream", A_XScream},
	{"a_playerscream", A_PlayerScream},
	{"a_activesound", A_ActiveSound},
	{"a_startsound", A_StartSound, &args_StartSound},
	// basic control
	{"a_facetarget", A_FaceTarget},
	{"a_noblocking", A_NoBlocking},
	// "AI"
	{"a_look", A_Look},
	{"a_chase", A_Chase},
	// enemy attack
	{"a_spawnprojectile", A_SpawnProjectile, &args_SpawnProjectile},
	// player attack
	{"a_fireprojectile", A_FireProjectile, &args_FireProjectile},
	{"a_firebullets", A_FireBullets, &args_FireBullets, check_FireBullets},
	// chunks
	{"a_burst", A_Burst, &args_SingleMobjtype},
	{"a_skullpop", A_SkullPop, &args_SingleMobjtype},
	// freeze death
	{"a_freezedeath", A_FreezeDeath},
	{"a_genericfreezedeath", A_GenericFreezeDeath},
	{"a_freezedeathchunks", A_FreezeDeathChunks},
	// render
	{"a_settranslation", A_SetTranslation, &args_SetTranslation},
	// misc
	{"a_checkplayerdone", A_CheckPlayerDone},
	{"a_setangle", A_SetAngle, &args_SetAngle},
	// inventory
	{"a_giveinventory", A_GiveInventory, &args_GiveInventory},
	// terminator
	{NULL}
};

//
// table of line specials

static const dec_linespec_t special_action[] =
{
	{2, "polyobj_rotateleft"},
	{3, "polyobj_rotateright"},
	{4, "polyobj_move"},
	{7, "polyobj_doorswing"},
	{8, "polyobj_doorslide"},
	{10, "door_close"},
	{12, "door_raise"},
	{13, "door_lockedraise"},
	{19, "thing_stop"},
	{20, "floor_lowerbyvalue"},
	{23, "floor_raisebyvalue"},
	{28, "floor_raiseandcrush"},
	{35, "floor_raisebyvaluetimes8"},
	{36, "floor_lowerbyvaluetimes8"},
	{40, "ceiling_lowerbyvalue"},
	{41, "ceiling_raisebyvalue"},
	{44, "ceiling_crushstop"},
	{62, "plat_downwaitupstay"},
	{64, "plat_upwaitdownstay"},
	{70, "teleport"},
	{71, "teleport_nofog"},
	{72, "thrustthing"},
	{73, "damagething"},
	{74, "teleport_newmap"},
	{76, "teleportother"},
	{97, "ceiling_lowerandcrushdist"},
	{99, "floor_raiseandcrushdoom"},
	{110, "light_raisebyvalue"},
	{111, "light_lowerbyvalue"},
	{112, "light_changetovalue"},
	{113, "light_fade"},
	{114, "light_glow"},
	{116, "light_strobe"},
	{117, "light_stop"},
	{119, "thing_damage"},
	{127, "thing_setspecial"},
	{128, "thrustthingz"},
	{130, "thing_activate"},
	{131, "thing_deactivate"},
	{132, "thing_remove"},
	{133, "thing_destroy"},
	{134, "thing_projectile"},
	{135, "thing_spawn"},
	{136, "thing_projectilegravity"},
	{137, "thing_spawnnofog"},
	{139, "thing_spawnfacing"},
	{172, "plat_upnearestwaitdownstay"},
	{176, "thing_changetid"},
	{179, "changeskill"},
	{195, "ceiling_crushraiseandstaya"},
	{196, "ceiling_crushandraisea"},
	{198, "ceiling_raisebyvaluetimes8"},
	{199, "ceiling_lowerbyvaluetimes8"},
	{200, "generic_floor"},
	{201, "generic_ceiling"},
	{206, "plat_downwaitupstaylip"},
	{212, "sector_setcolor"},
	{213, "sector_setfade"},
	{239, "floor_raisebyvaluetxty"},
	{243, "exit_normal"},
	{244, "exit_secret"},
	// terminator
	{0}
};

