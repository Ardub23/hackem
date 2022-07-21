/* NetHack 3.6	trap.h	$NHDT-Date: 1547255912 2019/01/12 01:18:32 $  $NHDT-Branch: NetHack-3.6.2-beta01 $:$NHDT-Revision: 1.17 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Pasi Kallinen, 2016. */
/* NetHack may be freely redistributed.  See license for details. */

/* note for 3.1.0 and later: no longer manipulated by 'makedefs' */

#ifndef TRAP_H
#define TRAP_H

union vlaunchinfo {
    short v_launch_otyp; /* type of object to be triggered */
    coord v_launch2;     /* secondary launch point (for boulders) */
    uchar v_conjoined;   /* conjoined pit locations */
    short v_tnote;       /* boards: 12 notes        */
};

struct trap {
    struct trap *ntrap;
    xchar tx, ty;
    d_level dst; /* destination for portals */
    coord launch;
    Bitfield(ttyp, 5);
    Bitfield(tseen, 1);
    Bitfield(once, 1);
    Bitfield(madeby_u, 1); /* So monsters may take offence when you trap
                              them.  Recognizing who made the trap isn't
                              completely unreasonable, everybody has
                              their own style.  This flag is also needed
                              when you untrap a monster.  It would be too
                              easy to make a monster peaceful if you could
                              set a trap for it and then untrap it. */
    union vlaunchinfo vl;
#define launch_otyp vl.v_launch_otyp
#define launch2 vl.v_launch2
#define conjoined vl.v_conjoined
#define tnote vl.v_tnote
};

extern struct trap *ftrap;
#define newtrap() (struct trap *) alloc(sizeof(struct trap))
#define dealloc_trap(trap) free((genericptr_t)(trap))

/* reasons for statue animation */
#define ANIMATE_NORMAL 0
#define ANIMATE_SHATTER 1
#define ANIMATE_SPELL 2

/* reasons for animate_statue's failure */
#define AS_OK 0            /* didn't fail */
#define AS_NO_MON 1        /* makemon failed */
#define AS_MON_IS_UNIQUE 2 /* statue monster is unique */

/* Note: if adding/removing a trap, adjust trap_engravings[] in mklev.c */

/* unconditional traps */
enum trap_types {
    NO_TRAP      =  0,
    ARROW_TRAP   =  1,
    BOLT_TRAP    =  2,
    DART_TRAP    =  3,
    ROCKTRAP     =  4,
    SQKY_BOARD   =  5,
    BEAR_TRAP    =  6,
    LANDMINE     =  7,
    ROLLING_BOULDER_TRAP = 8,
    SLP_GAS_TRAP =  9,
    RUST_TRAP    = 10,
    FIRE_TRAP    = 11,
    PIT          = 12,
    SPIKED_PIT   = 13,
    HOLE         = 14,
    TRAPDOOR     = 15,
    TELEP_TRAP   = 16,
    LEVEL_TELEP  = 17,
    MAGIC_PORTAL = 18,
    WEB          = 19,
    STATUE_TRAP  = 20,
    MAGIC_TRAP   = 21,
    ANTI_MAGIC   = 22,
    POLY_TRAP    = 23,
    SPEAR_TRAP   = 24,
    MAGIC_BEAM_TRAP  = 25,
    VIBRATING_SQUARE = 26,

    TRAPNUM      = 27
};

#define is_pit(ttyp) ((ttyp) == PIT || (ttyp) == SPIKED_PIT)
#define is_hole(ttyp)  ((ttyp) == HOLE || (ttyp) == TRAPDOOR)
#define undestroyable_trap(ttyp) ((ttyp) == MAGIC_PORTAL         \
                                  || (ttyp) == VIBRATING_SQUARE)
#define is_magical_trap(ttyp) ((ttyp) == TELEP_TRAP          \
                               || (ttyp) == LEVEL_TELEP      \
                               || (ttyp) == MAGIC_TRAP       \
                               || (ttyp) == ANTI_MAGIC       \
                               || (ttyp) == POLY_TRAP        \
                               || (ttyp) == MAGIC_BEAM_TRAP)

#endif /* TRAP_H */
