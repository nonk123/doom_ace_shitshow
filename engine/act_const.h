//
// numerical constants for decorate

//

#define GFF_NOEXTCHANGE	1

//

#define CHAN_BODY	0
#define CHAN_WEAPON	1
#define CHAN_VOICE	2

#define ATTN_NORM	2991
#define ATTN_NONE	0

//

#define VAF_DMGTYPEAPPLYTODIRECT	1

//

#define WRF_NOBOB	1
#define WRF_NOFIRE	(WRF_NOPRIMARY | WRF_NOSECONDARY)
#define WRF_NOSWITCH	2
#define WRF_DISABLESWITCH	4
#define WRF_NOPRIMARY	8
#define WRF_NOSECONDARY	16

//

#define CMF_AIMOFFSET	1
#define CMF_AIMDIRECTION	2
#define CMF_TRACKOWNER	4
#define CMF_CHECKTARGETDEAD	8
#define CMF_ABSOLUTEPITCH	16
#define CMF_OFFSETPITCH	32
#define CMF_SAVEPITCH	64
#define CMF_ABSOLUTEANGLE	128

#define FPF_AIMATANGLE	1
#define FPF_TRANSFERTRANSLATION	2
#define FPF_NOAUTOAIM	4

//

#define FBF_USEAMMO	1
#define FBF_NOFLASH	2
#define FBF_NORANDOM	4
#define FBF_NORANDOMPUFFZ	8

#define CBAF_AIMFACING	1
#define CBAF_NORANDOM	2
#define CBAF_NORANDOMPUFFZ	4

//

#define CPF_USEAMMO	1
#define CPF_PULLIN	2
#define CPF_NORANDOMPUFFZ	4
#define CPF_NOTURN	8

//

#define SMF_LOOK	1
#define SMF_PRECISE	2

//

#define SXF_TRANSFERTRANSLATION	0x0001
#define SXF_ABSOLUTEPOSITION	0x0002
#define SXF_ABSOLUTEANGLE	0x0004
#define SXF_ABSOLUTEVELOCITY	0x0008
#define SXF_SETMASTER	0x0010
#define SXF_NOCHECKPOSITION	0x0020
#define SXF_TELEFRAG	0x0040
#define SXF_TRANSFERAMBUSHFLAG	0x0080
#define SXF_TRANSFERPITCH	0x0100
#define SXF_TRANSFERPOINTERS	0x0200
#define SXF_USEBLOODCOLOR	0x0400
#define SXF_CLEARCALLERTID	0x0800
#define SXF_SETTARGET	0x1000
#define SXF_SETTRACER	0x2000
#define SXF_NOPOINTERS	0x4000
#define SXF_ORIGINATOR	0x8000
#define SXF_ISTARGET	0x10000
#define SXF_ISMASTER	0x20000
#define SXF_ISTRACER	0x40000
#define SXF_MULTIPLYSPEED	0x80000

//

#define CVF_RELATIVE	1
#define CVF_REPLACE	2

//

#define WARPF_ABSOLUTEOFFSET	0x0001
#define WARPF_ABSOLUTEANGLE	0x0002
#define WARPF_ABSOLUTEPOSITION	0x0004
#define WARPF_USECALLERANGLE	0x0008
#define WARPF_NOCHECKPOSITION	0x0010
#define WARPF_STOP	0x0020
#define WARPF_MOVEPTR	0x0040
#define WARPF_COPYVELOCITY	0x0080
#define WARPF_COPYPITCH	0x0100

//

#define XF_HURTSOURCE	1
#define XF_NOTMISSILE	2
#define XF_THRUSTZ	4
#define XF_NOSPLASH	8

//

#define PTROP_NOSAFEGUARDS	2991

//

enum
{
	AAPTR_DEFAULT,
	AAPTR_TARGET,
	AAPTR_TRACER,
	AAPTR_MASTER,
	AAPTR_PLAYER1,
	AAPTR_PLAYER2,
	AAPTR_PLAYER3,
	AAPTR_PLAYER4,
	AAPTR_NULL,
};
