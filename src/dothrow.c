/* NetHack 3.6	dothrow.c	$NHDT-Date: 1573688688 2019/11/13 23:44:48 $  $NHDT-Branch: NetHack-3.6 $:$NHDT-Revision: 1.164 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2013. */
/* NetHack may be freely redistributed.  See license for details. */

/* Contains code for 't' (throw) */

#include "hack.h"

STATIC_DCL int FDECL(throw_obj, (struct obj *, int));
STATIC_DCL boolean FDECL(ok_to_throw, (int *));
STATIC_DCL void NDECL(autoquiver);
STATIC_DCL int FDECL(gem_accept, (struct monst *, struct obj *));
STATIC_DCL void FDECL(tmiss, (struct obj *, struct monst *, BOOLEAN_P));
STATIC_DCL int FDECL(throw_gold, (struct obj *));
STATIC_DCL void FDECL(check_shop_obj, (struct obj *, XCHAR_P, XCHAR_P,
                                       BOOLEAN_P));
static boolean harmless_missile(struct obj *);
STATIC_DCL boolean FDECL(toss_up, (struct obj *, BOOLEAN_P));
STATIC_DCL void FDECL(sho_obj_return_to_u, (struct obj * obj));
STATIC_DCL boolean FDECL(mhurtle_step, (genericptr_t, int, int));

/* uwep might already be removed from inventory so test for W_WEP instead;
   for Valk + Mjollnir, caller needs to validate the strength requirement */
#define AutoReturn(o, wmsk) \
    ((((wmsk) & W_WEP) != 0                                             \
      && ((o)->otyp == AKLYS                                            \
          || ((o)->oartifact == ART_MJOLLNIR && Role_if(PM_VALKYRIE)))) \
     || (o)->otyp == BOOMERANG || (o)->otyp == CHAKRAM)

static NEARDATA const char toss_objs[] = { ALLOW_COUNT, COIN_CLASS,
                                           ALL_CLASSES, WEAPON_CLASS, 0 };
/* different default choices when wielding a sling (gold must be included) */
static NEARDATA const char bullets[] = { ALLOW_COUNT, COIN_CLASS, ALL_CLASSES,
                                         GEM_CLASS, 0 };

/* thrownobj (decl.c) tracks an object until it lands */
static struct obj *ammo_stack = 0;

extern boolean notonhead; /* for long worms */

/* Throw the selected object, asking for direction */
STATIC_OVL int
throw_obj(obj, shotlimit)
struct obj *obj;
int shotlimit;
{
    struct obj *otmp;
    int multishot;
    schar skill;
    long wep_mask;
    boolean twoweap, weakmultishot;

    /* ask "in what direction?" */
    if (!getdir((char *) 0)) {
        /* No direction specified, so cancel the throw;
         * might need to undo an object split.
         * We used to use freeinv(obj),addinv(obj) here, but that can
         * merge obj into another stack--usually quiver--even if it hadn't
         * been split from there (possibly triggering a panic in addinv),
         * and freeinv+addinv potentially has other side-effects.
         */
        if (obj->o_id == context.objsplit.parent_oid
            || obj->o_id == context.objsplit.child_oid)
            (void) unsplitobj(obj);
        return 0; /* no time passes */
    }

    /*
     * Throwing money is usually for getting rid of it when
     * a leprechaun approaches, or for bribing an oncoming
     * angry monster.  So throw the whole object.
     *
     * If the money is in quiver, throw one coin at a time,
     * possibly using a sling.
     */
    if (obj->oclass == COIN_CLASS && obj != uquiver)
        return throw_gold(obj);

    if (!canletgo(obj, "throw"))
        return 0;
    if (obj->oartifact == ART_MJOLLNIR && obj != uwep) {
        pline("%s must be wielded before it can be thrown.", The(xname(obj)));
        return 0;
    }
    if ((obj->oartifact == ART_MJOLLNIR && ACURR(A_STR) < STR19(25))
        || (obj->otyp == BOULDER && !racial_throws_rocks(&youmonst))) {
        pline("It's too heavy.");
        return 1;
    }
    if (!u.dx && !u.dy && !u.dz) {
        You("cannot throw an object at yourself.");
        return 0;
    }
    if (is_demon(raceptr(&youmonst)) && obj->material == SILVER) {
        retouch_object(&obj, TRUE);
        return 0;
    }
    u_wipe_engr(2);
    if (!uarmg && obj->otyp == CORPSE && touch_petrifies(&mons[obj->corpsenm])
        && !Stone_resistance) {
        You("throw %s with your bare %s.",
            corpse_xname(obj, (const char *) 0, CXN_PFX_THE),
            /* throwing with one hand, but pluralize since the
               expression "with your bare hands" sounds better */
            makeplural(body_part(HAND)));
        Sprintf(killer.name, "throwing %s bare-handed", killer_xname(obj));
        instapetrify(killer.name);
    }
    if (welded(obj)) {
        weldmsg(obj);
        return 1;
    }
    if (is_wet_towel(obj))
        dry_a_towel(obj, -1, FALSE);
    if (Sokoban && obj->otyp == BOULDER)
        change_luck(-2);

    /* Multishot calculations
     * (potential volley of up to N missiles; default for N is 1)
     */
    multishot = 1;
    skill = objects[obj->otyp].oc_skill;
    /* no point checking if there's only 1 */
    if ((obj->quan > 1L || obj->oartifact == ART_WINDRIDER)
        /* ammo requires corresponding launcher be wielded */
        && (is_ammo(obj) ? matching_launcher(obj, uwep)
                         /* otherwise any stackable (non-ammo) weapon */
                         : obj->oclass == WEAPON_CLASS)
        && !(Confusion || Stunned)) {
        /* some roles don't get a volley bonus until becoming expert */
        weakmultishot = (Role_if(PM_WIZARD) 
                         || Role_if(PM_PRIEST)
                         || (Role_if(PM_HEALER) && skill != P_KNIFE)
                         || (Role_if(PM_NECROMANCER))
                         || (Role_if(PM_ICE_MAGE) && skill != P_KNIFE)
                         || (Role_if(PM_INFIDEL) && skill != P_DAGGER)
                         || (Role_if(PM_TOURIST) && skill != -P_DART)
                         /* poor dexterity also inhibits multishot */
                         || Fumbling || ACURR(A_DEX) <= 6);

        /* Bonus if the player is proficient in this weapon... */
        switch (P_SKILL(weapon_type(obj))) {
        case P_EXPERT:
            multishot++;
        /*FALLTHRU*/
        case P_SKILLED:
            if (!weakmultishot)
                multishot++;
            break;
        default: /* basic or unskilled: no bonus */
            break;
        }
        /* ...or is using a special weapon for their role... */
        switch (Role_switch) {
        case PM_CAVEMAN:
        case PM_CAVEWOMAN:
            /* give bonus for low-tech gear */
            if (skill == -P_SLING || skill == P_SPEAR)
                multishot++;
            break;
        case PM_MONK:
            /* allow higher volley count despite skill limitation */
            if (skill == -P_SHURIKEN)
                multishot++;
            break;
        case PM_RANGER:
            /* arbitrary; encourage use of other missiles beside daggers */
            if (skill != P_DAGGER)
                multishot++;
            break;
        case PM_ROGUE:
            /* possibly should add knives... */
            if (skill == P_DAGGER)
                multishot++;
            break;
        case PM_SAMURAI:
            /* role-specific launcher and its ammo */
            if (obj->otyp == YA && uwep && uwep->otyp == YUMI)
                multishot++;
            break;
        default:
            break; /* No bonus */
        }
        /* ...or using their race's special bow; no bonus for spears */
        if (!weakmultishot)
            switch (Race_switch) {
            case PM_ELF:
                if (obj->otyp == ELVEN_ARROW && uwep
                    && uwep->otyp == ELVEN_BOW)
                    multishot++;
                break;
            case PM_ORC:
                if (obj->otyp == ORCISH_ARROW && uwep
                    && uwep->otyp == ORCISH_BOW)
                    multishot++;
                break;
            case PM_GNOME:
                /* arbitrary; there isn't any gnome-specific gear */
                if (skill == -P_CROSSBOW)
                    multishot++;
                break;
            case PM_GIANT:
                /* Giants are good at throwing things, but not at
                   using bows, crossbows and slings */
                if ((skill == -P_CROSSBOW) || (skill == -P_BOW)
                    || (skill == -P_SLING))
                    multishot = 1;
                break;
            case PM_HOBBIT:
                /* Hobbits are also good with slings and small blades */
                if ((skill == -P_SLING) || (skill == P_KNIFE)
                    || (skill == P_DAGGER))
                    multishot++;
                break;
            case PM_CENTAUR:
                /* Centaurs are experts with the bow and crossbow */
                if ((skill == -P_CROSSBOW) || (skill == -P_BOW))
                    multishot++;
                break;
            case PM_HUMAN:
            case PM_DWARF:
            case PM_ILLITHID:
            case PM_TORTLE:
            default:
                break; /* No bonus */
            }

        if (Race_if(PM_GIANT)) {
            /* Giants are good at throwing things, but not at
             * using bows, crossbows and slings.
             */
            if ((skill == -P_CROSSBOW) || (skill == -P_BOW)
                || (skill == -P_SLING))
                multishot = 1;
        } else if (Race_if(PM_HOBBIT)) {
            /* Normal hobbits are also good with slings and small blades */
            if ((skill == -P_SLING) || (skill == -P_KNIFE)
                || (skill == -P_DAGGER))
                multishot++;
        } else if (Race_if(PM_CENTAUR)) {
            /* Centaurs are experts with the bow and crossbow */
            if ((skill == -P_CROSSBOW) || (skill == -P_BOW))
                multishot++;
        }

        /* Rate of fire is intrinsic to the weapon - cannot be user selected
         * except via altmode
         * Only for valid launchers
         */
        if (uwep && is_firearm(uwep)) {
            if (firearm_rof(uwep->otyp))
                multishot += (firearm_rof(uwep->otyp) - 1);
            if (uwep->altmode == WP_MODE_SINGLE)
            /* weapons switchable b/w full/semi auto */
                multishot = 1;
            else if (uwep->altmode == WP_MODE_BURST)
                multishot = ((multishot > 3) ? (multishot / 3) : 1);
            /* else it is auto == no change */
        }
        if (multishot < 1) 
            multishot = 1;

        /* crossbows are slow to load and probably shouldn't allow multiple
           shots at all, but that would result in players never using them;
           instead, high strength is necessary to load and shoot quickly */
        if (multishot > 1 && skill == -P_CROSSBOW
            && ammo_and_launcher(obj, uwep)
            && (int) ACURRSTR < ((Race_if(PM_GNOME)) || (Race_if(PM_CENTAUR))
                                 ? 16 : 18))
            multishot = rnd(multishot);

        /* Tech: Flurry */
        if (objects[obj->otyp].oc_skill == -P_BOW && tech_inuse(T_FLURRY))
            multishot += 1; /* Let'em rip! */
        if (objects[obj->otyp].oc_skill == -P_SLING && tech_inuse(T_FLURRY))
            multishot += 1; /* Let'em rip! */
        
        multishot = rnd(multishot);
        
        if ((long) multishot > obj->quan && obj->oartifact != ART_WINDRIDER)
            multishot = (int) obj->quan;
        
        /* Shotlimit controls your rate of fire */
        if (shotlimit > 0 && multishot > shotlimit)
            multishot = shotlimit;
    }
    m_shot.s = ammo_and_launcher(obj, uwep) ? TRUE : FALSE;
    /* give a message if shooting more than one, or if player
       attempted to specify a count */
    if (multishot > 1 || shotlimit > 0) {
        /* "You shoot N arrows." or "You throw N daggers." */
        You("%s %d %s.", m_shot.s ? "shoot" : "throw",
            multishot, /* (might be 1 if player gave shotlimit) */
            (multishot == 1) ? singular(obj, xname) : xname(obj));
    }

    wep_mask = obj->owornmask;
    m_shot.o = obj->otyp;
    m_shot.n = multishot;
    ammo_stack = obj;
    for (m_shot.i = 1; m_shot.i <= m_shot.n; m_shot.i++) {
        twoweap = u.twoweap;
        /* split this object off from its slot if necessary */
        if (obj->quan > 1L) {
            otmp = splitobj(obj, 1L);
        } else {
            otmp = obj;
            if (otmp->owornmask)
                remove_worn_item(otmp, FALSE);
        }
        freeinv(otmp);
        throwit(otmp, wep_mask, twoweap);
    }
    ammo_stack = (struct obj *) 0;
    m_shot.n = m_shot.i = 0;
    m_shot.o = STRANGE_OBJECT;
    m_shot.s = FALSE;

    return 1;
}

/* common to dothrow() and dofire() */
STATIC_OVL boolean
ok_to_throw(shotlimit_p)
int *shotlimit_p; /* (see dothrow()) */
{
    /* kludge to work around parse()'s pre-decrement of `multi' */
    *shotlimit_p = (multi || save_cm) ? multi + 1 : 0;
    multi = 0; /* reset; it's been used up */

    if (notake(youmonst.data)) {
        You("are physically incapable of throwing or shooting anything.");
        return FALSE;
    } else if (nohands(youmonst.data)) {
        You_cant("throw or shoot without hands."); /* not body_part(HAND) */
        return FALSE;
    } else if (!freehand()) {
        You_cant("throw or shoot without free use of your %s.",
                 makeplural(body_part(HAND)));
        return FALSE;
    }
    if (check_capacity((char *) 0))
        return FALSE;
    return TRUE;
}

/* t command - throw */
int
dothrow()
{
    register struct obj *obj;
    int shotlimit;

    /*
     * Since some characters shoot multiple missiles at one time,
     * allow user to specify a count prefix for 'f' or 't' to limit
     * number of items thrown (to avoid possibly hitting something
     * behind target after killing it, or perhaps to conserve ammo).
     *
     * Prior to 3.3.0, command ``3t'' meant ``t(shoot) t(shoot) t(shoot)''
     * and took 3 turns.  Now it means ``t(shoot at most 3 missiles)''.
     *
     * [3.6.0:  shot count setup has been moved into ok_to_throw().]
     */
    if (!ok_to_throw(&shotlimit))
        return 0;

    obj = getobj(uslinging() ? bullets : toss_objs, ushooting() ? "shoot" : "throw");

    /* it is also possible to throw food */
    /* (or jewels, or iron balls... ) */

    return obj ? throw_obj(obj, shotlimit) : 0;
}

/* KMH -- Automatically fill quiver */
/* Suggested by Jeffrey Bay <jbay@convex.hp.com> */
static void
autoquiver()
{
    struct obj *otmp, *oammo = 0, *omissile = 0, *omisc = 0, *altammo = 0;

    if (uquiver)
        return;

    /* Scan through the inventory */
    for (otmp = invent; otmp; otmp = otmp->nobj) {
        if (otmp->owornmask || otmp->oartifact || !otmp->dknown) {
            ; /* Skip it */
        } else if (otmp->otyp == ROCK || otmp->otyp == BOULDER
                   || otmp->otyp == SLING_BULLET
                   /* seen rocks or known flint or known glass */
                   || (otmp->otyp == FLINT
                       && objects[otmp->otyp].oc_name_known)
                   || (is_worthless_glass(otmp)
                       && objects[otmp->otyp].oc_name_known)) {
            if (uslinging())
                oammo = otmp;
            else if (ammo_and_launcher(otmp, uswapwep))
                altammo = otmp;
            else if (!omisc)
                omisc = otmp;
        } else if (otmp->oclass == GEM_CLASS) {
            ; /* skip non-rock gems--they're ammo but
                 player has to select them explicitly */
        } else if (is_ammo(otmp)) {
            if (ammo_and_launcher(otmp, uwep))
                /* Ammo matched with launcher (bow+arrow, crossbow+bolt) */
                oammo = otmp;
            else if (ammo_and_launcher(otmp, uswapwep))
                altammo = otmp;
            else
                /* Mismatched ammo (no better than an ordinary weapon) */
                omisc = otmp;
        } else if (is_missile(otmp)) {
            /* Missile (dart, shuriken, etc.) */
            omissile = otmp;
        } else if (otmp->oclass == WEAPON_CLASS && throwing_weapon(otmp)) {
            /* Ordinary weapon */
            if (objects[otmp->otyp].oc_skill == P_DAGGER && !omissile)
                omissile = otmp;
            else if (otmp->otyp == AKLYS)
                continue;
            else
                omisc = otmp;
        }
    }

    /* Pick the best choice */
    if (oammo)
        setuqwep(oammo);
    else if (omissile)
        setuqwep(omissile);
    else if (altammo)
        setuqwep(altammo);
    else if (omisc)
        setuqwep(omisc);

    return;
}

/* f command -- fire: throw from the quiver or use wielded polearm */
int
dofire()
{
    int shotlimit;
    struct obj *obj;

    /*
     * Same as dothrow(), except we use quivered missile instead
     * of asking what to throw/shoot.
     *
     * If quiver is empty, we use autoquiver to fill it when the
     * corresponding option is on.  If the option is off and hero
     * is wielding a thrown-and-return weapon, use the wielded
     * weapon.  If option is off and not wielding such a weapon or
     * if autoquiver doesn't select anything, we ask what to throw.
     * Then we put the chosen item into the quiver slot unless
     * it is already in another slot.  [Matters most if it is a
     * stack but also matters for single item if this throw gets
     * aborted (ESC at the direction prompt).]
     */
    if (!ok_to_throw(&shotlimit))
        return 0;

    if ((obj = uquiver) == 0) {
        if (!flags.autoquiver) {
            if (uwep && AutoReturn(uwep, uwep->owornmask)) {
                obj = uwep;
            /* if we're wielding a polearm, apply it */
            } else if (uwep && is_pole(uwep)) {
                return use_pole(uwep, TRUE);
            } else {
                You("have no ammunition readied.");
            }
        } else {
            autoquiver();
            if ((obj = uquiver) == 0)
                You("have nothing appropriate for your quiver.");
        }
        /* if autoquiver is disabled or has failed, prompt for missile;
           fill quiver with it if it's not wielded or worn */
        if (!obj) {
            /* in case we're using ^A to repeat prior 'f' command, don't
               use direction of previous throw as getobj()'s choice here */
            in_doagain = 0;
            /* choose something from inventory, then usually quiver it */
            obj = getobj(uslinging() ? bullets : toss_objs, 
                         ushooting() ? "shoot": "throw");

            /* Q command doesn't allow gold in quiver */
            if (obj && !obj->owornmask && obj->oclass != COIN_CLASS)
                setuqwep(obj); /* demi-autoquiver */
        }
        /* give feedback if quiver has now been filled */
        if (uquiver) {
            uquiver->owornmask &= ~W_QUIVER; /* less verbose */
            prinv("You ready:", uquiver, 0L);
            uquiver->owornmask |= W_QUIVER;
        }
    }

    return obj ? throw_obj(obj, shotlimit) : 0;
}

/* if in midst of multishot shooting/throwing, stop early */
void
endmultishot(verbose)
boolean verbose;
{
    if (m_shot.i < m_shot.n) {
        if (verbose && !context.mon_moving) {
            You("stop %s after the %d%s %s.",
                m_shot.s ? "firing" : "throwing", m_shot.i, ordin(m_shot.i),
                m_shot.s ? "shot" : "toss");
        }
        m_shot.n = m_shot.i; /* make current shot be the last */
    }
}

/* Object hits floor at hero's feet.
   Called from drop(), throwit(), hold_another_object(). */
void
hitfloor(obj, verbosely)
struct obj *obj;
boolean verbosely; /* usually True; False if caller has given drop message */
{
    if (IS_SOFT(levl[u.ux][u.uy].typ) || u.uinwater || u.uswallow) {
        dropy(obj);
        return;
    }
    if (IS_ALTAR(levl[u.ux][u.uy].typ))
        doaltarobj(obj);
    else if (verbosely)
        pline("%s %s the %s.", Doname2(obj), otense(obj, "hit"),
              surface(u.ux, u.uy));

    if (hero_breaks(obj, u.ux, u.uy, BRK_FROM_INV))
        return;
    if (ship_object(obj, u.ux, u.uy, FALSE))
        return;
    dropz(obj, TRUE);
}

/*
 * Walk a path from src_cc to dest_cc, calling a proc for each location
 * except the starting one.  If the proc returns FALSE, stop walking
 * and return FALSE.  If stopped early, dest_cc will be the location
 * before the failed callback.
 */
boolean
walk_path(src_cc, dest_cc, check_proc, arg)
coord *src_cc;
coord *dest_cc;
boolean FDECL((*check_proc), (genericptr_t, int, int));
genericptr_t arg;
{
    int x, y, dx, dy, x_change, y_change, err, i, prev_x, prev_y;
    boolean keep_going = TRUE;

    /* Use Bresenham's Line Algorithm to walk from src to dest.
     *
     * This should be replaced with a more versatile algorithm
     * since it handles slanted moves in a suboptimal way.
     * Going from 'x' to 'y' needs to pass through 'z', and will
     * fail if there's an obstable there, but it could choose to
     * pass through 'Z' instead if that way imposes no obstacle.
     *     ..y          .Zy
     *     xz.    vs    x..
     * Perhaps we should check both paths and accept whichever
     * one isn't blocked.  But then multiple zigs and zags could
     * potentially produce a meandering path rather than the best
     * attempt at a straight line.  And (*check_proc)() would
     * need to work more like 'travel', distinguishing between
     * testing a possible move and actually attempting that move.
     */
    dx = dest_cc->x - src_cc->x;
    dy = dest_cc->y - src_cc->y;
    prev_x = x = src_cc->x;
    prev_y = y = src_cc->y;

    if (dx < 0) {
        x_change = -1;
        dx = -dx;
    } else
        x_change = 1;
    if (dy < 0) {
        y_change = -1;
        dy = -dy;
    } else
        y_change = 1;

    i = err = 0;
    if (dx < dy) {
        while (i++ < dy) {
            prev_x = x;
            prev_y = y;
            y += y_change;
            err += dx << 1;
            if (err > dy) {
                x += x_change;
                err -= dy << 1;
            }
            /* check for early exit condition */
            if (!(keep_going = (*check_proc)(arg, x, y)))
                break;
        }
    } else {
        while (i++ < dx) {
            prev_x = x;
            prev_y = y;
            x += x_change;
            err += dy << 1;
            if (err > dx) {
                y += y_change;
                err -= dx << 1;
            }
            /* check for early exit condition */
            if (!(keep_going = (*check_proc)(arg, x, y)))
                break;
        }
    }

    if (keep_going)
        return TRUE; /* successful */

    dest_cc->x = prev_x;
    dest_cc->y = prev_y;
    return FALSE;
}

/* hack for hurtle_step() -- it ought to be changed to take an argument
   indicating lev/fly-to-dest vs lev/fly-to-dest-minus-one-land-on-dest
   vs drag-to-dest; original callers use first mode, jumping wants second,
   grappling hook backfire and thrown chained ball need third */
boolean
hurtle_jump(arg, x, y)
genericptr_t arg;
int x, y;
{
    boolean res;
    long save_EWwalking = EWwalking;

    /* prevent jumping over water from being placed in that water */
    EWwalking |= I_SPECIAL;
    res = hurtle_step(arg, x, y);
    EWwalking = save_EWwalking;
    return res;
}

/*
 * Single step for the hero flying through the air from jumping, flying,
 * etc.  Called from hurtle() and jump() via walk_path().  We expect the
 * argument to be a pointer to an integer -- the range -- which is
 * used in the calculation of points off if we hit something.
 *
 * Bumping into monsters won't cause damage but will wake them and make
 * them angry.  Auto-pickup isn't done, since you don't have control over
 * your movements at the time.
 *
 * Possible additions/changes:
 *      o really attack monster if we hit one
 *      o set stunned if we hit a wall or door
 *      o reset nomul when we stop
 *      o creepy feeling if pass through monster (if ever implemented...)
 *      o bounce off walls
 *      o let jumps go over boulders
 */
boolean
hurtle_step(arg, x, y)
genericptr_t arg;
int x, y;
{
    int ox, oy, *range = (int *) arg;
    struct obj *obj;
    struct monst *mon;
    boolean may_pass = TRUE, via_jumping, stopping_short;
    struct trap *ttmp;
    int dmg = 0;

    if (!isok(x, y)) {
        You_feel("the spirits holding you back.");
        return FALSE;
    } else if (!in_out_region(x, y)) {
        return FALSE;
    } else if (*range == 0) {
        return FALSE; /* previous step wants to stop now */
    }
    via_jumping = (EWwalking & I_SPECIAL) != 0L;
    stopping_short = (via_jumping && *range < 2);

    if (!Passes_walls || !(may_pass = may_passwall(x, y))) {
        boolean odoor_diag = (IS_DOOR(levl[x][y].typ)
                              && (levl[x][y].doormask & D_ISOPEN)
                              && (u.ux - x) && (u.uy - y));

        if (IS_ROCK(levl[x][y].typ) || closed_door(x, y) || odoor_diag) {
            const char *s;

            if (odoor_diag)
                You("hit the door edge!");
            pline("Ouch!");
            if (IS_TREES(levl[x][y].typ))
                s = "bumping into a tree";
            else if (IS_ROCK(levl[x][y].typ))
                s = "bumping into a wall";
            else
                s = "bumping into a door";
            dmg = rnd(2 + *range);
            losehp(Maybe_Half_Phys(dmg), s, KILLED_BY);
            wake_nearto(x,y, 10);
            return FALSE;
        }
        if (levl[x][y].typ == IRONBARS) {
            You("crash into some iron bars.");
            dmg = rnd(2 + *range);
            if (Hate_material(IRON)) {
                pline_The("iron hurts to touch!");
                dmg += sear_damage(IRON);
            } else {
                pline("Ouch!");
            }
            losehp(Maybe_Half_Phys(dmg), "crashing into iron bars",
                   KILLED_BY);
            wake_nearto(x,y, 20);
            return FALSE;
        }
        if ((obj = sobj_at(BOULDER, x, y)) != 0) {
            You("bump into a %s.  Ouch!", xname(obj));
            dmg = rnd(2 + *range);
            losehp(Maybe_Half_Phys(dmg), "bumping into a boulder", KILLED_BY);
            wake_nearto(x,y, 10);
            return FALSE;
        }
        if (!may_pass) {
            /* did we hit a no-dig non-wall position? */
            You("smack into something!");
            dmg = rnd(2 + *range);
            losehp(Maybe_Half_Phys(dmg), "touching the edge of the universe",
                   KILLED_BY);
            wake_nearto(x,y, 10);
            return FALSE;
        }
        if ((u.ux - x) && (u.uy - y) && bad_rock(&youmonst, u.ux, y)
            && bad_rock(&youmonst, x, u.uy)) {
            boolean too_much = (invent && (inv_weight() + weight_cap() > 600));

            /* Move at a diagonal. */
            if (bigmonst(youmonst.data) || too_much) {
                You("%sget forcefully wedged into a crevice.",
                    too_much ? "and all your belongings " : "");
                dmg = rnd(2 + *range);
                losehp(Maybe_Half_Phys(dmg), "wedging into a narrow crevice",
                       KILLED_BY);
                wake_nearto(x,y, 10);
                return FALSE;
            }
        }
    }

    if ((mon = m_at(x, y)) != 0
#if 0   /* we can't include these two exceptions unless we know we're
         * going to end up past the current spot rather than on it;
         * for that, we need to know that the range is not exhausted
         * and also that the next spot doesn't contain an obstacle */
        && !(mon->mundetected && hides_under(mon) && (Flying || Levitation))
        && !(mon->mundetected && mon->data->mlet == S_EEL
             && (Flying || Levitation || Wwalking))
#endif
        ) {
        const char *mnam, *pronoun;
        int glyph = glyph_at(x, y);
        boolean stomping = (uarmf && uarmf->otyp == STOMPING_BOOTS 
                            && mon->data->msize <= MZ_SMALL);


        mon->mundetected = 0; /* wakeup() will handle mimic */
        mnam = a_monnam(mon); /* after unhiding */
        pronoun = noit_mhim(mon);
        if (!strcmp(mnam, "it")) {
            mnam = !strcmp(pronoun, "it") ? "something" : "someone";
        }
        if (!glyph_is_monster(glyph) && !glyph_is_invisible(glyph))
            You("find %s by %s %s.", mnam, stomping ? "stomping on" : "bumping into", pronoun);
        else
            You("%s %s.", stomping ? "stomp on" : "bump into", mnam);
        wakeup(mon, FALSE);
        if (!canspotmon(mon))
            map_invisible(mon->mx, mon->my);
        setmangry(mon, FALSE);
        if (touch_petrifies(mon->data)
            /* this is a bodily collision, so check for body armor */
            && !uarmu && !uarm && !uarmc) {
            Sprintf(killer.name, "bumping into %s", mnam);
            instapetrify(killer.name);
        }
        if (touch_petrifies(youmonst.data)
            && !which_armor(mon, W_ARMU | W_ARM | W_ARMC)) {
            minstapetrify(mon, TRUE);
        }
        if (stomping && (verysmall(mon->data) || !rn2(4))) {
            xkilled(mon, XKILL_GIVEMSG);
            makeknown(uarmf->otyp);
            if (Hallucination) 
                verbalize("Woohoo!");
        } else {
            unmul((char *) 0);
            return FALSE;
        }
        wake_nearto(x, y, 10);
        return FALSE;
    }

    if ((u.ux - x) && (u.uy - y)
        && bad_rock(&youmonst, u.ux, y)
        && bad_rock(&youmonst, x, u.uy)) {
        /* Move at a diagonal. */
        if (Sokoban) {
            You("come to an abrupt halt!");
            return FALSE;
        }
    }

    /* caller has already determined that dragging the ball is allowed;
       if ball is carried we might still need to drag the chain */
    if (Punished) {
        int bc_control;
        xchar ballx, bally, chainx, chainy;
        boolean cause_delay;

        if (drag_ball(x, y, &bc_control, &ballx, &bally, &chainx,
                      &chainy, &cause_delay, TRUE))
            move_bc(0, bc_control, ballx, bally, chainx, chainy);
    }

    ox = u.ux;
    oy = u.uy;
    u_on_newpos(x, y); /* set u.<ux,uy>, u.usteed-><mx,my>; cliparound(); */
    newsym(ox, oy);    /* update old position */
    vision_recalc(1);  /* update for new position */
    flush_screen(1);
    /* if terrain type changes, levitation or flying might become blocked
       or unblocked; might issue message, so do this after map+vision has
       been updated for new location instead of right after u_on_newpos() */
    if (levl[u.ux][u.uy].typ != levl[ox][oy].typ)
        switch_terrain();

    if (is_pool(x, y) && !u.uinwater
        && ((Is_waterlevel(&u.uz) && levl[x][y].typ == WATER)
            || !(Levitation || Flying || Wwalking))) {
            /* checks are still here due to needing to be able to hurtle over
             * water */
        multi = 0; /* can move, so drown() allows crawling out of water */
        (void) drown();
        return FALSE;
    } else if (is_pool(x, y) && !u.uinwater 
               && !Is_waterlevel(&u.uz) && !stopping_short) {
        Norep("You move over %s.", an(is_moat(x, y) ? "moat" : "pool"));
    } else if (is_lava(x, y) && !stopping_short) {
        Norep("You move over some lava.");
    } else if (is_open_air(x, y) && !stopping_short) {
        Norep("You pass over the chasm.");
    }

    /* FIXME:
     * Each trap should really trigger on the recoil if it would
     * trigger during normal movement. However, not all the possible
     * side-effects of this are tested [as of 3.4.0] so we trigger
     * those that we have tested, and offer a message for the ones
     * that we have not yet tested.
     */
    if ((ttmp = t_at(x, y)) != 0) {
        if (stopping_short) {
            ; /* see the comment above hurtle_jump() */
        } else if (ttmp->ttyp == MAGIC_PORTAL) {
            dotrap(ttmp, 0);
            return FALSE;
        } else if (ttmp->ttyp == VIBRATING_SQUARE) {
            pline_The("ground vibrates as you pass it.");
            dotrap(ttmp, 0); /* doesn't print messages */
        } else if (ttmp->ttyp == FIRE_TRAP) {
            dotrap(ttmp, 0);
        } else if ((is_pit(ttmp->ttyp) || is_hole(ttmp->ttyp))
                   && Sokoban) {
            /* air currents overcome the recoil in Sokoban;
               when jumping, caller performs last step and enters trap */
            if (!via_jumping)
                dotrap(ttmp, 0);
            *range = 0;
            return TRUE;
        } else {
            if (ttmp->tseen)
                You("pass right over %s.",
                    an(defsyms[trap_to_defsym(ttmp->ttyp)].explanation));
        }
    }
    if (--*range < 0) /* make sure our range never goes negative */
        *range = 0;
    if (*range != 0)
        delay_output();
    return TRUE;
}

STATIC_OVL boolean
mhurtle_step(arg, x, y)
genericptr_t arg;
int x, y;
{
    struct monst *mon = (struct monst *) arg;
    struct monst *mtmp;

    /* TODO: Treat walls, doors, iron bars, etc. specially
     * rather than just stopping before.
     */
    if (!isok(x, y))
        return FALSE;

    if (goodpos(x, y, mon, MM_IGNOREWATER | MM_IGNORELAVA | MM_IGNOREAIR)
        && m_in_out_region(mon, x, y)) {
        int res;

        remove_monster(mon->mx, mon->my);
        newsym(mon->mx, mon->my);
        place_monster(mon, x, y);
        maybe_unhide_at(mon->mx, mon->my);
        newsym(mon->mx, mon->my);
        set_apparxy(mon);
        if (Is_waterlevel(&u.uz) && levl[x][y].typ == WATER)
            return FALSE;
        res = mintrap(mon);
        if (res == 1 || res == 2)
            return FALSE;

        flush_screen(1);
        delay_output();
        return TRUE;
    }
    if ((mtmp = m_at(x, y)) != 0) {
        if (canseemon(mon) || canseemon(mtmp))
            pline("%s bumps into %s.", Monnam(mon), a_monnam(mtmp));
        wakeup(mon, !context.mon_moving);
        wakeup(mtmp, !context.mon_moving);
        if (touch_petrifies(mtmp->data)
            && !which_armor(mon, W_ARMU | W_ARM | W_ARMC)) {
            minstapetrify(mon, !context.mon_moving);
            newsym(mon->mx, mon->my);
        }
        if (touch_petrifies(mon->data)
            && !which_armor(mtmp, W_ARMU | W_ARM | W_ARMC)) {
            minstapetrify(mtmp, !context.mon_moving);
            newsym(mtmp->mx, mtmp->my);
        }
    }

    return FALSE;
}

/*
 * The player moves through the air for a few squares as a result of
 * throwing or kicking something.
 *
 * dx and dy should be the direction of the hurtle, not of the original
 * kick or throw and be only.
 */
void
hurtle(dx, dy, range, verbose)
int dx, dy, range;
boolean verbose;
{
    coord uc, cc;

    /* The chain is stretched vertically, so you shouldn't be able to move
     * very far diagonally.  The premise that you should be able to move one
     * spot leads to calculations that allow you to only move one spot away
     * from the ball, if you are levitating over the ball, or one spot
     * towards the ball, if you are at the end of the chain.  Rather than
     * bother with all of that, assume that there is no slack in the chain
     * for diagonal movement, give the player a message and return.
     */
    if (Punished && !carried(uball)) {
        You_feel("a tug from the iron ball.");
        nomul(0);
        return;
    } else if (Stable) {
        Your("fortitude prevents you from moving.");
        nomul(0);
        return;
    } else if (u.utrap) {
        You("are anchored by the %s.",
            u.utraptype == TT_WEB
                ? "web"
                : u.utraptype == TT_LAVA
                      ? hliquid("lava")
                      : u.utraptype == TT_INFLOOR
                            ? surface(u.ux, u.uy)
                            : u.utraptype == TT_BURIEDBALL ? "buried ball"
                                                           : "trap");
        nomul(0);
        return;
    }

    /* was our hero knocked off their steed? */
    if (u.usteed) {
        newsym(u.ux, u.uy);
        dismount_steed(DISMOUNT_FELL);
        nomul(0);
        return;
    }

    /* make sure dx and dy are [-1,0,1] */
    dx = sgn(dx);
    dy = sgn(dy);

    if (!range || (!dx && !dy) || u.ustuck)
        return; /* paranoia */

    nomul(-range);
    multi_reason = "moving through the air";
    nomovemsg = ""; /* it just happens */
    if (verbose)
        You("%s in the opposite direction.", (range > 1 || !(Flying || Levitation)) ? "hurtle" : "float");
    /* if we're in the midst of shooting multiple projectiles, stop */
    endmultishot(TRUE);
    sokoban_guilt();
    uc.x = u.ux;
    uc.y = u.uy;
    /* this setting of cc is only correct if dx and dy are [-1,0,1] only */
    cc.x = u.ux + (dx * range);
    cc.y = u.uy + (dy * range);
    (void) walk_path(&uc, &cc, hurtle_step, (genericptr_t) &range);
    teleds(cc.x, cc.y, TELEDS_NO_FLAGS);
}

/* Move a monster through the air for a few squares. */
void
mhurtle(mon, dx, dy, range)
struct monst *mon;
int dx, dy, range;
{
    coord mc, cc;

    /* At the very least, debilitate the monster */
    mon->movement = 0;
    mon->mstun = 1;

    /* Is the monster stuck or too heavy to push?
     * (very large monsters have too much inertia, even floaters and flyers)
     */
    if (r_data(mon)->msize >= MZ_HUGE || mon == u.ustuck || mon->mtrapped) {
        if (canseemon(mon))
            pline("%s doesn't budge!", Monnam(mon));
        return;
    }

    /* Make sure dx and dy are [-1,0,1] */
    dx = sgn(dx);
    dy = sgn(dy);
    if (!range || (!dx && !dy))
        return; /* paranoia */
    /* don't let grid bugs be hurtled diagonally */
    if (dx && dy && NODIAG(monsndx(mon->data)))
        return;

    /* Send the monster along the path */
    mc.x = mon->mx;
    mc.y = mon->my;
    cc.x = mon->mx + (dx * range);
    cc.y = mon->my + (dy * range);
    (void) walk_path(&mc, &cc, mhurtle_step, (genericptr_t) mon);
    (void) minliquid(mon);
    return;
}

STATIC_OVL void
check_shop_obj(obj, x, y, broken)
struct obj *obj;
xchar x, y;
boolean broken;
{
    boolean costly_xy;
    struct monst *shkp = shop_keeper(*u.ushops);

    if (!shkp)
        return;

    costly_xy = costly_spot(x, y);
    if (broken || !costly_xy || *in_rooms(x, y, SHOPBASE) != *u.ushops) {
        /* thrown out of a shop or into a different shop */
        if (is_unpaid(obj))
            (void) stolen_value(obj, u.ux, u.uy, (boolean) shkp->mpeaceful,
                                FALSE);
        if (broken)
            obj->no_charge = 1;
    } else if (costly_xy) {
        char *oshops = in_rooms(x, y, SHOPBASE);

        /* ushops0: in case we threw while levitating and recoiled
           out of shop (most likely to the shk's spot in front of door) */
        if (*oshops == *u.ushops || *oshops == *u.ushops0) {
            if (is_unpaid(obj))
                subfrombill(obj, shkp);
            else if (x != shkp->mx || y != shkp->my)
                sellobj(obj, x, y);
        }
    }
}

/* Will 'obj' cause damage if it falls on hero's head when thrown upward?
   Not used to handle things which break when they hit. */
static boolean
harmless_missile(struct obj *obj)
{
    int otyp = obj->otyp;

    /* this list is fairly arbitrary */
    switch (otyp) {
    case SLING:
    case EUCALYPTUS_LEAF:
    case KELP_FROND:
    case SPRIG_OF_WOLFSBANE:
    case FORTUNE_COOKIE:
    case PANCAKE:
        return TRUE;
    case RUBBER_HOSE:
    case BAG_OF_TRICKS:
        return (obj->spe < 1);
    case SACK:
    case OILSKIN_SACK:
    case BAG_OF_HOLDING:
        return !Has_contents(obj);
    default:
        if (obj->oclass == SCROLL_CLASS) /* scrolls but not all paper objs */
            return TRUE;
        if (objects[otyp].oc_material == CLOTH)
            return TRUE;
        break;
    }
    return FALSE;
}

/*
 * Hero tosses an object upwards with appropriate consequences.
 *
 * Returns FALSE if the object is gone.
 */
STATIC_OVL boolean
toss_up(obj, hitsroof)
struct obj *obj;
boolean hitsroof;
{
    const char *action;
    boolean petrifier = ((obj->otyp == EGG || obj->otyp == CORPSE)
                         && touch_petrifies(&mons[obj->corpsenm]));
    /* note: obj->quan == 1 */

    if (!has_ceiling(&u.uz)) {
        action = "flies up into"; /* into "the sky" or "the water above" */
    } else if (hitsroof) {
        if (breaktest(obj)) {
            pline("%s hits the %s.", Doname2(obj), ceiling(u.ux, u.uy));
            breakmsg(obj, !Blind);
            breakobj(obj, u.ux, u.uy, TRUE, TRUE);
            return FALSE;
        }
        action = "hits";
    } else {
        action = "almost hits";
    }
    pline("%s %s the %s, then falls back on top of your %s.", Doname2(obj),
          action, ceiling(u.ux, u.uy), body_part(HEAD));

    /* object now hits you */

    if (obj->oclass == POTION_CLASS) {
        potionhit(&youmonst, obj, POTHIT_HERO_THROW);
    } else if (breaktest(obj)) {
        int otyp = obj->otyp;
        int blindinc;

        /* need to check for blindness result prior to destroying obj */
        blindinc = ((otyp == CREAM_PIE || otyp == BLINDING_VENOM || otyp == SNOWBALL)
                    /* AT_WEAP is ok here even if attack type was AT_SPIT */
                    && can_blnd(&youmonst, &youmonst, AT_WEAP, obj))
                       ? rnd(25)
                       : 0;
        breakmsg(obj, !Blind);
        breakobj(obj, u.ux, u.uy, TRUE, TRUE);
        obj = 0; /* it's now gone */
        switch (otyp) {
        case PINEAPPLE:
            pline("Ouch! %s is covered in spikes!", Doname2(obj));
            break;
        case EGG:
            if (petrifier && !Stone_resistance
                && !(poly_when_stoned(youmonst.data)
                     && polymon(PM_STONE_GOLEM))) {
                /* egg ends up "all over your face"; perhaps
                   visored helmet should still save you here */
                if (uarmh)
                    Your("%s fails to protect you.", helm_simple_name(uarmh));
                goto petrify;
            }
            /*FALLTHRU*/
        case CREAM_PIE:
        case BLINDING_VENOM:
        case SNOWBALL:
            pline("You've got %s all over your %s!",
                  (otyp == SNOWBALL) ? "snow" : "it", body_part(FACE));
            if (blindinc) {
                if ((otyp == BLINDING_VENOM || otyp == SNOWBALL) && !Blind)
                    pline("It blinds you!");
                u.ucreamed += blindinc;
                make_blinded(Blinded + (long) blindinc, FALSE);
                if (!Blind)
                    Your1(vision_clears);
            }
            break;
        default:
            break;
        }
        return FALSE;
    } else if (harmless_missile(obj)) {
        pline("It doesn't hurt.");
        hitfloor(obj, FALSE);
        thrownobj = 0;
    } else { /* neither potion nor other breaking object */
        boolean less_damage = uarmh && (is_hard(uarmh)), artimsg = FALSE;
        int dmg = dmgval(obj, &youmonst);

        if (obj->oartifact
            || (obj->oclass == WEAPON_CLASS && obj->oprops))
            /* need a fake die roll here; rn1(18,2) avoids 1 and 20 */
            artimsg = artifact_hit((struct monst *) 0, &youmonst, obj, &dmg,
                                   rn1(18, 2));

        if (ammo_stack)
            ammo_stack->oprops_known |= obj->oprops_known;

        if (!dmg) { /* probably wasn't a weapon; base damage on weight */
            dmg = (int) obj->owt / 100;
            if (dmg < 1)
                dmg = 1;
            else if (dmg > 6)
                dmg = 6;
            if (obj->otyp == PINEAPPLE)
                dmg = dmg + 2;
            if (obj->otyp == FRUITCAKE)
                dmg = 18;
            if (noncorporeal(youmonst.data) && !shade_glare(obj))
                dmg = 0;
        }
        if (dmg > 2 && less_damage)
            dmg = (dmg > 2 ? dmg - 2 : 2);
        if (dmg > 0)
            dmg += u.udaminc;
        if (dmg < 0)
            dmg = 0; /* beware negative rings of increase damage */
        dmg = Maybe_Half_Phys(dmg);

        if (uarmh) {
            if (obj->owt >= 400 && is_glass(uarmh) && break_glass_obj(uarmh)) {
                ;
            } else if (less_damage && dmg < (Upolyd ? u.mh : u.uhp)) {
                if (!artimsg) {
                    if (dmg > 2)
                        Your("helmet only slightly protects you.");
                    else
                        pline("Fortunately, you are wearing a hard helmet.");
                }

            /* helmet definitely protects you when it blocks petrification */
            } else if (!petrifier) {
                if (flags.verbose)
                    Your("%s does not protect you.", helm_simple_name(uarmh));
            }
        } else if (petrifier && !Stone_resistance
                   && !(poly_when_stoned(youmonst.data)
                        && polymon(PM_STONE_GOLEM))) {
 petrify:
            killer.format = KILLED_BY;
            Strcpy(killer.name, "elementary physics"); /* "what goes up..." */
            You("turn to stone.");
            if (obj)
                dropy(obj); /* bypass most of hitfloor() */
            thrownobj = 0;  /* now either gone or on floor */
            done(STONING);
            return obj ? TRUE : FALSE;
        } else if (Hate_material(obj->material)) {
            /* dmgval() already added extra damage */
            searmsg(&youmonst, &youmonst, obj, FALSE);
            exercise(A_CON, FALSE);
        }
        if (is_open_air(bhitpos.x, bhitpos.y)) {
            thrownobj = 0;
            losehp(dmg, "falling object", KILLED_BY_AN);
            return FALSE;
        }
        hitfloor(obj, TRUE);
        thrownobj = 0;
        losehp(dmg, "falling object", KILLED_BY_AN);
    }
    return TRUE;
}

/* return true for weapon meant to be thrown; excludes ammo */
boolean
throwing_weapon(obj)
struct obj *obj;
{
    return (boolean) (is_missile(obj) 
                      || is_spear(obj)
                      /* daggers and knife (excludes scalpel) */
                      || (is_blade(obj) 
                          && !is_sword(obj)
                          && (objects[obj->otyp].oc_dir & PIERCE))
                      /* special cases [might want to add AXE] */
                      || obj->otyp == HEAVY_WAR_HAMMER 
                      || obj->otyp == AKLYS
                      || obj->otyp == SPIKE
                      || obj->otyp == THROWING_AXE);
}

/* the currently thrown object is returning to you (not for boomerangs) */
STATIC_OVL void
sho_obj_return_to_u(obj)
struct obj *obj;
{
    /* might already be our location (bounced off a wall) */
    if ((u.dx || u.dy) && (bhitpos.x != u.ux || bhitpos.y != u.uy)) {
        int x = bhitpos.x - u.dx, y = bhitpos.y - u.dy;

        tmp_at(DISP_FLASH, obj_to_glyph(obj, rn2_on_display_rng));
        while (isok(x,y) && (x != u.ux || y != u.uy)) {
            tmp_at(x, y);
            delay_output();
            x -= u.dx;
            y -= u.dy;
        }
        tmp_at(DISP_END, 0);
    }
}

/* throw an object, NB: obj may be consumed in the process */
void
throwit(obj, wep_mask, twoweap)
struct obj *obj;
long wep_mask; /* used to re-equip returning boomerang */
boolean twoweap; /* used to restore twoweapon mode if wielded weapon returns */
{
    register struct monst *mon;
    int range, urange;
    boolean crossbowing, gunning, clear_thrownobj = FALSE,
            impaired = (Confusion || Stunned || Blind
                        || Hallucination || Fumbling || Afraid),
            tethered_weapon = (obj->otyp == AKLYS && (wep_mask & W_WEP) != 0);
    /* 5lo: This gets used a lot, so put it here 
     * hackem: Updated so that the lightsaber only auto-returns if thrown from
     * the Jedi's primary hand. Otherwise we run into issues later when re-
     * wielding with wielding-artifact.
     * */
    boolean jedi_forcethrow = 
        (Role_if(PM_JEDI) 
         && is_lightsaber(obj) 
         && obj->lamplit
         && (wep_mask & W_WEP) != 0
         && !impaired 
         && P_SKILL(weapon_type(obj)) >= P_SKILLED);
    
    /* KMH -- Handle Plague here */
    if (uwep && uwep->oartifact == ART_PLAGUE &&
        ammo_and_launcher(obj, uwep) && is_poisonable(obj))
        obj->opoisoned = 1;

    notonhead = FALSE; /* reset potentially stale value */
    if (((obj->cursed && u.ualign.type != A_NONE) /* cursed ammo */
          /* Priests trying to throw pointy things */
          || (Role_if(PM_PRIEST) && (is_pierce(obj) || is_slash(obj)))
          /* or greased */
          || obj->greased
          /* or panicking */
          || Afraid
          /* or flintlock */
          || (ammo_and_launcher(obj, uwep) && uwep->otyp == FLINTLOCK))
        && (u.dx || u.dy) && !rn2(7)) {
        boolean slipok = TRUE;

        if (ammo_and_launcher(obj, uwep)) {
            pline("%s!", Tobjnam(obj, "misfire"));
        } else {
            /* only slip if it's greased or meant to be thrown */
            if (obj->greased || throwing_weapon(obj))
                /* BUG: this message is grammatically incorrect if obj has
                   a plural name; greased gloves or boots for instance. */
                pline("%s as you throw it!", Tobjnam(obj, "slip"));
            else
                slipok = FALSE;
        }
        if (slipok) {
            int dmg = dmgval(obj, &youmonst);

            u.dx = rn2(3) - 1;
            u.dy = rn2(3) - 1;
            if (!u.dx && !u.dy) {
                u.dz = 1;
                You("hit yourself in the %s!", body_part(LEG));

                if (obj->oartifact
                    || (obj->oclass == WEAPON_CLASS && obj->oprops))
                    /* need a fake die roll here; rn1(18,2) avoids 1 and 20 */
                    (void) artifact_hit((struct monst *) 0, &youmonst, obj, &dmg,
                                        rn1(18, 2));

                if (ammo_stack)
                    ammo_stack->oprops_known |= obj->oprops_known;

                if (dmg > 0)
                    dmg += u.udaminc;
                if (dmg < 0)
                    dmg = 0; /* beware negative rings of increase damage */
                dmg = Maybe_Half_Phys(dmg);

                if (Hate_material(obj->material)) {
                    /* dmgval() already added extra damage */
                    searmsg(&youmonst, &youmonst, obj, FALSE);
                    exercise(A_CON, FALSE);
                }
                losehp(dmg, "hitting themselves with a cursed projectile", KILLED_BY);
            }
            impaired = TRUE;
        }
    }

    if ((u.dx || u.dy || (u.dz < 1))
        && calc_capacity((int) obj->owt) > SLT_ENCUMBER
        && (Upolyd ? (u.mh < 5 && u.mh != u.mhmax)
                   : (u.uhp < 10 && u.uhp != u.uhpmax))
        && obj->owt > (unsigned) ((Upolyd ? u.mh : u.uhp) * 2)
        && !Is_airlevel(&u.uz)) {
        You("have so little stamina, %s drops from your grasp.",
            the(xname(obj)));
        exercise(A_CON, FALSE);
        u.dx = u.dy = 0;
        u.dz = 1;
    }

    thrownobj = obj;
    thrownobj->was_thrown = 1;
    iflags.returning_missile = AutoReturn(obj, wep_mask) ? (genericptr_t) obj
                                                         : (genericptr_t) 0;
    /* NOTE:  No early returns after this point or returning_missile
       will be left with a stale pointer. */
    if (is_bomb(obj))
        arm_bomb(obj, TRUE);
        
    if (u.uswallow) {
        if (obj == uball) {
            uball->ox = uchain->ox = u.ux;
            uball->oy = uchain->oy = u.uy;
        }
        mon = u.ustuck;
        bhitpos.x = mon->mx;
        bhitpos.y = mon->my;
        if (tethered_weapon)
            tmp_at(DISP_TETHER, obj_to_glyph(obj, rn2_on_display_rng));
    } else if (u.dz) {
        if (u.dz < 0
            /* Mjollnir must be wielded to be thrown--caller verifies this;
               aklys must be wielded as primary to return when thrown */
            && iflags.returning_missile
            && !impaired) {
            pline("%s the %s and returns to your hand!", Tobjnam(obj, "hit"),
                  ceiling(u.ux, u.uy));
            obj = addinv(obj);
            (void) encumber_msg();
            if (obj->owornmask & W_QUIVER) /* in case addinv() autoquivered */
                setuqwep((struct obj *) 0);
            setuwep(obj);
            u.twoweap = twoweap;
        } else if (u.dz < 0 && jedi_forcethrow) {
            pline("%s the %s and returns to your hand!",
                  Tobjnam(obj, "hit"), ceiling(u.ux,u.uy));
            obj = addinv(obj);
            (void) encumber_msg();
            if (obj->owornmask & W_QUIVER) /* in case addinv() autoquivered */
                setuqwep((struct obj *) 0);
            setuwep(obj);
            u.twoweap = twoweap;
            /*return;*/
        } else if (u.dz < 0) {
            (void) toss_up(obj, rn2(5) && !Underwater);
        } else if (u.dz > 0 && u.usteed && obj->oclass == POTION_CLASS
                   && rn2(6)) {
            /* alternative to prayer or wand of opening/spell of knock
               for dealing with cursed saddle:  throw holy water > */
            potionhit(u.usteed, obj, POTHIT_HERO_THROW);
        } else {
            hitfloor(obj, TRUE);
        }
        clear_thrownobj = TRUE;
        goto throwit_return;

    } else if ((obj->otyp == BOOMERANG || obj->otyp == CHAKRAM) && !Underwater) {
        if (Is_airlevel(&u.uz) || Levitation)
            hurtle(-u.dx, -u.dy, 1, TRUE);
        
        /* Boomerang doesn't return if it hits monster, 
         * chakrams will return (they slice through their targets) */
        if (obj->otyp != CHAKRAM)
            iflags.returning_missile = 0;

        mon = boomhit(obj, u.dx, u.dy);
        if (mon == &youmonst) { /* the thing was caught */
            exercise(A_DEX, TRUE);
            obj = addinv(obj);
            (void) encumber_msg();
            if (wep_mask && !(obj->owornmask & wep_mask)) {
                setworn(obj, wep_mask);
                /* moot; can no longer two-weapon with missile(s) */
                u.twoweap = twoweap;
            }
            clear_thrownobj = TRUE;
            goto throwit_return;
        }
    } else {
        /* crossbow range is independent of strength */
        crossbowing = (ammo_and_launcher(obj, uwep)
                       && weapon_type(uwep) == P_CROSSBOW);
        gunning = (ammo_and_launcher(obj, uwep)
                       && weapon_type(uwep) == P_FIREARM);
        if (gunning && !matching_firearm(obj, uwep)) {
            pline("The quivered ammo doesn't fit the firearm.");
            gunning = FALSE;
        }
        urange = (crossbowing ? 18 : (int) ACURRSTR) / 2;

        /* hard limit this so crossbows will fire further
         * than anything except a superstrong elf wielding a
         * racial bow, or a samurai with his yumi */
        if (urange > 9)
            urange = 9;

        /* balls are easy to throw or at least roll;
         * also, this insures the maximum range of a ball is greater
         * than 1, so the effects from throwing attached balls are
         * actually possible
         */
        if (obj->otyp == HEAVY_IRON_BALL)
            range = urange - (int) (obj->owt / 100);
        else
            range = urange - (int) (obj->owt / 40);
        if (obj == uball) {
            if (u.ustuck)
                range = 1;
            else if (range >= 5)
                range = 5;
        }
        if (range < 1)
            range = 1;

        if (is_ammo(obj)) {
            if (ammo_and_launcher(obj, uwep)) {
                if (gunning)
                    range = firearm_range(uwep->otyp);
                else if (crossbowing) {
                    if (uwep->oartifact == ART_CROSSBOW_OF_CARL)
                        range += 12; /* divine workmanship */
                    else
                        range += 10; /* not strength dependent */
                } else {
                    switch (uwep->otyp) {
                    case ELVEN_BOW:
                    case YUMI:
                        range += urange + 2; /* better workmanship */
                        break;
                    case ORCISH_BOW:
                        range += urange - 2; /* orcish gear sucks */
                        break;
                    case BOW:
                        if (uwep->oartifact == ART_LONGBOW_OF_DIANA)
                            range += urange + 3; /* divine workmanship */
                        else
                            range += urange;
                        break;
                    case SLING:
                        range += (int) urange / 2;
                        break;
                    default:
                        break;
                    }
                }
            } else if (obj->oclass != GEM_CLASS)
                range /= 2;
        }

        if (obj->otyp == SNOWBALL && Role_if(PM_ICE_MAGE)) {
            range += 2; /* Small bonus for snowball spell */
        }
        if (Is_airlevel(&u.uz) || Levitation) {
            /* action, reaction... */
            urange -= range;
            if (urange < 1)
                urange = 1;
            range -= urange;
            if (range < 1)
                range = 1;
        }

        if (obj->otyp == BOULDER)
            range = 20; /* you must be giant */
        else if (obj->oartifact == ART_MJOLLNIR)
            range = (range + 1) / 2; /* it's heavy */
        else if (tethered_weapon) /* primary weapon is aklys */
            /* if an aklys is going to return, range is limited by the
               length of the attached cord [implicit aspect of item] */
            range = min(range, BOLT_LIM / 2);
        else if (obj == uball && u.utrap && u.utraptype == TT_INFLOOR)
            range = 1;

        if (Underwater)
            range = 1;

        mon = bhit(u.dx, u.dy, range,
                   tethered_weapon ? THROWN_TETHERED_WEAPON : THROWN_WEAPON,
                   (int FDECL((*), (MONST_P, OBJ_P))) 0,
                   (int FDECL((*), (OBJ_P, OBJ_P))) 0, &obj);
        thrownobj = obj; /* obj may be null now */

        /* have to do this after bhit() so u.ux & u.uy are correct */
        if (Is_airlevel(&u.uz) || Levitation)
            hurtle(-u.dx, -u.dy, urange, TRUE);

        if (!obj) {
            /* bhit display cleanup was left with this caller
               for tethered_weapon, but clean it up now since
               we're about to return */
            if (tethered_weapon)
                tmp_at(DISP_END, 0);
            goto throwit_return;
        }
    }

    if (mon) {
        boolean obj_gone;

        if (mon->isshk && obj->where == OBJ_MINVENT && obj->ocarry == mon) {
            clear_thrownobj = TRUE;
            goto throwit_return; /* alert shk caught it */
        }
        (void) snuff_candle(obj);
        notonhead = (bhitpos.x != mon->mx || bhitpos.y != mon->my);
        obj_gone = thitmonst(mon, obj);
        if (!obj_gone && ammo_stack)
            ammo_stack->oprops_known |= obj->oprops_known;
        /* Monster may have been tamed; this frees old mon [obsolete] */
        mon = m_at(bhitpos.x, bhitpos.y);

        /* [perhaps this should be moved into thitmonst or hmon] */
        if (mon && mon->isshk
            && (!inside_shop(u.ux, u.uy)
                || !index(in_rooms(mon->mx, mon->my, SHOPBASE), *u.ushops)))
            hot_pursuit(mon);

        if (obj_gone)
            thrownobj = (struct obj *) 0;
    }
    
    /* Handle bombs or rockets */
    if (obj && thrownobj && is_bomb(obj)) {
        if (IS_VENT(levl[bhitpos.x][bhitpos.y].typ)) {
            doventbomb(obj, bhitpos.x, bhitpos.y);
            thrownobj = (struct obj *) 0;
        } else {
            arm_bomb(obj, TRUE);
        }
    }

    if (!thrownobj) {
        /* missile has already been handled */
        if (tethered_weapon)
            tmp_at(DISP_END, 0);
    } else if (u.uswallow && !iflags.returning_missile) {
 swallowit:
        if (obj != uball)
            (void) mpickobj(u.ustuck, obj); /* clears 'thrownobj' */
        else
            clear_thrownobj = TRUE;
        goto throwit_return;
    } else {
        /* Mjollnir must be wielded to be thrown--caller verifies this;
           aklys must be wielded as primary to return when thrown */
        if (iflags.returning_missile || jedi_forcethrow) { /* Mjollnir or aklys */
            if (rn2(100)) {
                /* or a Jedi with a lightsaber */
                if (Role_if(PM_JEDI) && u.uen < 5 && obj->otyp != AKLYS) {
                    You("don't have enough force to call %s. You need at least 5 points of mana!", the(xname(obj)));
                } else {
                    if (Role_if(PM_JEDI) && obj->otyp != AKLYS)
                        u.uen -= 5;

                    if (tethered_weapon)
                        tmp_at(DISP_END, BACKTRACK);
                    else
                        sho_obj_return_to_u(obj); /* display its flight */

                    if (!impaired && rn2(100)) {
                        pline("%s to your hand!", Tobjnam(obj, "return"));
                        obj = addinv(obj);
                        (void) encumber_msg();
                        /* addinv autoquivers an aklys if quiver is empty;
                           if obj is quivered, remove it before wielding */
                        if (obj->owornmask & W_QUIVER)
                            setuqwep((struct obj *) 0);
                        setuwep(obj);
                        u.twoweap = twoweap;
                        retouch_object(&obj, TRUE);
                        if (cansee(bhitpos.x, bhitpos.y))
                            newsym(bhitpos.x, bhitpos.y);
                    } else {
                        int dmg = rn2(2);

                        if (!dmg) {
                            pline(Blind
                                      ? "%s lands %s your %s."
                                      : "%s back to you, landing %s your %s.",
                                  Blind ? Something : Tobjnam(obj, "return"),
                                  Levitation ? "beneath" : "at",
                                  makeplural(body_part(FOOT)));
                        } else {
                            dmg += rnd(3);
                            pline(
                                Blind
                                    ? "%s your %s!"
                                    : "%s back toward you, hitting your %s!",
                                Tobjnam(obj, Blind ? "hit" : "fly"),
                                body_part(ARM));
                            if (obj->oartifact)
                                (void) artifact_hit((struct monst *) 0,
                                                    &youmonst, obj, &dmg, 0);
                            if (Hate_material(obj->material)) {
                                dmg += rnd(sear_damage(obj->material));
                                exercise(A_CON, FALSE);
                                searmsg(NULL, &youmonst, obj, TRUE);
                            }
                            losehp(Maybe_Half_Phys(dmg), killer_xname(obj),
                                   KILLED_BY);
                        }

                        if (u.uswallow)
                            goto swallowit;
                        if (!ship_object(obj, u.ux, u.uy, FALSE))
                            dropy(obj);
                    }
                    clear_thrownobj = TRUE;
                    goto throwit_return;
                }
            } else {
                if (tethered_weapon)
                    tmp_at(DISP_END, 0);
                /* when this location is stepped on, the weapon will be
                   auto-picked up due to 'obj->was_thrown' of 1;
                   addinv() prevents thrown Mjollnir from being placed
                   into the quiver slot, but an aklys will end up there if
                   that slot is empty at the time; since hero will need to
                   explicitly rewield the weapon to get throw-and-return
                   capability back anyway, quivered or not shouldn't matter */
                pline("%s to return!", Tobjnam(obj, "fail"));

                if (u.uswallow)
                    goto swallowit;
                /* continue below with placing 'obj' at target location */
            }
        }

        if ((!IS_SOFT(levl[bhitpos.x][bhitpos.y].typ) && breaktest(obj))
            /* venom [via #monster to spit while poly'd] fails breaktest()
               but we want to force breakage even when location IS_SOFT() */
            || obj->oclass == VENOM_CLASS) {
            tmp_at(DISP_FLASH, obj_to_glyph(obj, rn2_on_display_rng));
            tmp_at(bhitpos.x, bhitpos.y);
            delay_output();
            tmp_at(DISP_END, 0);
            breakmsg(obj, cansee(bhitpos.x, bhitpos.y));
            breakobj(obj, bhitpos.x, bhitpos.y, TRUE, TRUE);
            clear_thrownobj = TRUE;
            goto throwit_return;
            
        }
        if (flooreffects(obj, bhitpos.x, bhitpos.y, "fall")) {
            clear_thrownobj = TRUE;
            goto throwit_return;
        }
        obj_no_longer_held(obj);
        if (mon && mon->isshk && is_pick(obj)) {
            if (cansee(bhitpos.x, bhitpos.y))
                pline("%s snatches up %s.", Monnam(mon), the(xname(obj)));
            if (*u.ushops || obj->unpaid)
                check_shop_obj(obj, bhitpos.x, bhitpos.y, FALSE);
            (void) mpickobj(mon, obj); /* may merge and free obj */
            clear_thrownobj = TRUE;
            goto throwit_return;
        }
        (void) snuff_candle(obj);
        if (!mon && ship_object(obj, bhitpos.x, bhitpos.y, FALSE)) {
            clear_thrownobj = TRUE;
            goto throwit_return;
        }
        thrownobj = (struct obj *) 0;
        place_object(obj, bhitpos.x, bhitpos.y);
        /* container contents might break;
           do so before turning ownership of thrownobj over to shk
           (container_impact_dmg handles item already owned by shop) */
        if (!IS_SOFT(levl[bhitpos.x][bhitpos.y].typ))
            /* <x,y> is spot where you initiated throw, not bhitpos */
            container_impact_dmg(obj, u.ux, u.uy);
        /* charge for items thrown out of shop;
           shk takes possession for items thrown into one */
        if ((*u.ushops || obj->unpaid) && obj != uball)
            check_shop_obj(obj, bhitpos.x, bhitpos.y, FALSE);

        stackobj(obj);
        if (obj == uball)
            drop_ball(bhitpos.x, bhitpos.y);
        if (cansee(bhitpos.x, bhitpos.y))
            newsym(bhitpos.x, bhitpos.y);
        if (obj_sheds_light(obj))
            vision_full_recalc = 1;
    }

 throwit_return:
    iflags.returning_missile = (genericptr_t) 0;
    if (clear_thrownobj)
        thrownobj = (struct obj *) 0;
    return;
}

/* an object may hit a monster; various factors adjust chance of hitting */
int
omon_adj(mon, obj, mon_notices)
struct monst *mon;
struct obj *obj;
boolean mon_notices;
{
    int tmp = 0;

    /* size of target affects the chance of hitting */
    tmp += (mon->data->msize - MZ_MEDIUM); /* -2..+5 */
    /* sleeping target is more likely to be hit */
    if (mon->msleeping) {
        tmp += 2;
        if (mon_notices)
            mon->msleeping = 0;
    }
    /* ditto for immobilized target */
    if (!mon->mcanmove || !mon->data->mmove) {
        tmp += 4;
        if (mon_notices && mon->data->mmove && !rn2(10)) {
	    if (!mon->mstone || mon->mstone > 2)
	        mon->mcanmove = 1;
            mon->mfrozen = 0;
        }
    }
    /* some objects are more likely to hit than others */
    switch (obj->otyp) {
    case PINEAPPLE:
    case FRUITCAKE:
        tmp += 4;
        break;
    case HEAVY_IRON_BALL:
        if (obj != uball)
            tmp += 2;
        break;
    case BOULDER:
        tmp += 6;
        break;
    default:
        if (obj->oclass == WEAPON_CLASS || is_weptool(obj)
            || obj->oclass == GEM_CLASS)
            tmp += hitval(obj, mon);
        break;
    }
    return tmp;
}

/* thrown object misses target monster */
STATIC_OVL void
tmiss(obj, mon, maybe_wakeup)
struct obj *obj;
struct monst *mon;
boolean maybe_wakeup;
{
    const char *missile = mshot_xname(obj);

    /* If the target can't be seen or doesn't look like a valid target,
       avoid "the arrow misses it," or worse, "the arrows misses the mimic."
       An attentive player will still notice that this is different from
       an arrow just landing short of any target (no message in that case),
       so will realize that there is a valid target here anyway. */
    if (!canseemon(mon) || (M_AP_TYPE(mon) && M_AP_TYPE(mon) != M_AP_MONSTER))
        pline("%s %s.", The(missile), otense(obj, "miss"));
    else
        miss(missile, mon);
    if (maybe_wakeup && !rn2(3))
        wakeup(mon, TRUE);
    return;
}

#define quest_arti_hits_leader(obj, mon)      \
    (obj->oartifact && is_quest_artifact(obj) \
     && mon->m_id == quest_status.leader_m_id)

/*
 * Object thrown by player arrives at monster's location.
 * Return 1 if obj has disappeared or otherwise been taken care of,
 * 0 if caller must take care of it.
 * Also used for kicked objects and for polearms/grapnel applied at range.
 */
int
thitmonst(mon, obj)
register struct monst *mon;
register struct obj *obj; /* thrownobj or kickedobj or uwep */
{
    register int tmp;     /* Base chance to hit */
    register int disttmp; /* distance modifier */
    int otyp = obj->otyp, hmode;
    boolean guaranteed_hit = (u.uswallow && mon == u.ustuck);
    boolean hellfiring = (uwep && uwep->oartifact == ART_HELLFIRE);
    int dieroll;

    hmode = (obj == uwep) ? HMON_APPLIED
              : (obj == kickedobj) ? HMON_KICKED
                : HMON_THROWN;

    /* Differences from melee weapons:
     *
     * Dex still gives a bonus, but strength does not.
     * Polymorphed players lacking attacks may still throw.
     * There's a base -1 to hit.
     * No bonuses for fleeing or stunned targets (they don't dodge
     *    melee blows as readily, but dodging arrows is hard anyway).
     * Not affected by traps, etc.
     * Certain items which don't in themselves do damage ignore 'tmp'.
     * Distance and monster size affect chance to hit.
     */
    tmp = -1 + ((Luck/2) + 1) + find_mac(mon) + u.uhitinc
          + maybe_polyd(youmonst.data->mlevel, u.ulevel);
    if (ACURR(A_DEX) < 4)
        tmp -= 3;
    else if (ACURR(A_DEX) < 6)
        tmp -= 2;
    else if (ACURR(A_DEX) < 8)
        tmp -= 1;
    else if (ACURR(A_DEX) >= 14)
        tmp += (ACURR(A_DEX) - 14);

    /* combat boots give +1 to-hit for thrown */
    if (uarmf && objdescr_is(uarmf, "combat boots")) 
        tmp += 1;
    
    /* Modify to-hit depending on distance; but keep it sane.
     * Polearms get a distance penalty even when wielded; it's
     * hard to hit at a distance.
     */
    disttmp = 3 - distmin(u.ux, u.uy, mon->mx, mon->my);
    if (disttmp < -4)
        disttmp = -4;
    tmp += disttmp;

    /* some gloves are a hindrance to proper use of bows */
    if (uarmg && uwep && objects[uwep->otyp].oc_skill == P_BOW) {
        switch (uarmg->otyp) {
        case GAUNTLETS_OF_POWER: /* metal */
        case BOXING_GLOVES: /* bulky */
        case GAUNTLETS:
            tmp -= 2;
            break;
        case GAUNTLETS_OF_FUMBLING: /* you're fumbling and shouldn't really even be able to throw */
            tmp -= 9;
            break;
        case GAUNTLETS_OF_PROTECTION:
        case GAUNTLETS_OF_SWIMMING:
        case ROGUES_GLOVES:
        case GLOVES:
        case MUMMIFIED_HAND: /* the Hand of Vecna */
            break;
        case GAUNTLETS_OF_DEXTERITY: /* these gloves were made with archers in mind */
            tmp += 1;
            break;
        default:
            impossible("Unknown type of gloves (%d)", uarmg->otyp);
            break;
        }
    }
    if (obj->otyp == SNOWBALL && Role_if(PM_ICE_MAGE)) {
        /* This is needed because otherwise we miss way too much */
        if (P_SKILL(P_MATTER_SPELL) >= P_BASIC) 
            tmp += 14;
    }
    if (obj->otyp == SPIKE)
        tmp += 5;  /* For when we are poly'd into a manticore */
    
    tmp += omon_adj(mon, obj, TRUE);
    if (racial_orc(mon)
        && maybe_polyd(is_elf(youmonst.data), Race_if(PM_ELF)))
        tmp++;
    if (guaranteed_hit) {
        tmp += 1000; /* Guaranteed hit */
    }

    if (obj->oclass == GEM_CLASS && is_unicorn(mon->data)) {
        if (mon->msleeping || !mon->mcanmove) {
            tmiss(obj, mon, FALSE);
            return 0;
        } else if (mon->mtame) {
            pline("%s catches and drops %s.", Monnam(mon), the(xname(obj)));
            return 0;
        } else {
            pline("%s catches %s.", Monnam(mon), the(xname(obj)));
            return gem_accept(mon, obj);
        }
    }

    /* don't make game unwinnable if naive player throws artifact
       at leader... (kicked artifact is ok too; HMON_APPLIED could
       occur if quest artifact polearm or grapnel ever gets added) */
    if (hmode != HMON_APPLIED && quest_arti_hits_leader(obj, mon)) {
        /* AIS: changes to wakeup() means that it's now less inappropriate here
           than it used to be, but the manual version works just as well */
        mon->msleeping = 0;
        mon->mstrategy &= ~STRAT_WAITMASK;

        if (mon->mcanmove) {
            pline("%s catches %s.", Monnam(mon), the(xname(obj)));
            if (mon->mpeaceful) {
                boolean next2u = monnear(mon, u.ux, u.uy);

                finish_quest(obj); /* acknowledge quest completion */
                if (mcarried(obj) || q_leader_angered()) {
                    /* quest leader keeps artifact, so don't throw it back.
                     * mcarried indicates player agreed to give it up and it
                     * was subsequently placed into the leader's inventory. */
                    return 1;
                }
                pline("%s %s %s back to you.", Monnam(mon),
                      (next2u ? "hands" : "tosses"), the(xname(obj)));
                if (!next2u)
                    sho_obj_return_to_u(obj);
                obj = addinv(obj); /* back into your inventory */
                (void) encumber_msg();
            } else {
                /* angry leader caught it and isn't returning it */
                if (*u.ushops || obj->unpaid) /* not very likely... */
                    check_shop_obj(obj, mon->mx, mon->my, FALSE);
                (void) mpickobj(mon, obj);
            }
            return 1; /* caller doesn't need to place it */
        }
        return 0;
    }

    dieroll = rnd(20);

    if (mon->mtame && mon->mcanmove &&
            (!is_animal(mon->data)) && (!mindless(mon->data)) &&
            could_use_item(mon, obj, FALSE, FALSE)) {
       if (could_use_item(mon, obj, TRUE, FALSE)) {
           pline("%s catches %s.", Monnam(mon), the(xname(obj)));
           obj_extract_self(obj);
           (void) mpickobj(mon,obj);
           if (attacktype(mon->data, AT_WEAP) &&
               mon->weapon_check == NEED_WEAPON) {
               mon->weapon_check = NEED_HTH_WEAPON;
               (void) mon_wield_item(mon);
           }
           m_dowear(mon, FALSE);
           newsym(mon->mx, mon->my);
           return 1;
       }
       miss(xname(obj), mon);
   } else if (obj->oclass == WEAPON_CLASS 
               || is_weptool(obj)
               || obj->otyp == SPIKE
               || obj->oclass == GEM_CLASS) {
        if (hmode == HMON_KICKED) {
            /* throwing adjustments and weapon skill bonus don't apply */
            tmp -= (is_ammo(obj) ? 5 : 3);
        } else if (is_ammo(obj)) {
            if (!ammo_and_launcher(obj, uwep)) {
                tmp -= 4;
            }     /* Handle bombs or rockets */
            else {
                tmp += uwep->spe - greatest_erosion(uwep);
                tmp += weapon_hit_bonus(uwep);
                if (uwep->oartifact)
                    tmp += spec_abon(uwep, mon);
                /*
                 * Elves and Samurais are highly trained w/bows,
                 * especially their own special types of bow.
                 * Polymorphing won't make you a bow expert.
                 */
                if ((Race_if(PM_ELF) || Role_if(PM_SAMURAI))
                    && (!Upolyd || your_race(youmonst.data))
                    && objects[uwep->otyp].oc_skill == P_BOW) {
                    tmp++;
                    if (Race_if(PM_ELF) && uwep->otyp == ELVEN_BOW)
                        tmp++;
                    else if (Role_if(PM_SAMURAI) && uwep->otyp == YUMI)
                        tmp++;
                }
            }
        } else if (is_bomb(obj)) {
            /* arm_bomb(obj, TRUE); */
            bomb_explode(obj, bhitpos.x, bhitpos.y, TRUE);
            return 1;
        } else { /* thrown non-ammo or applied polearm/grapnel */
            if (otyp == BOOMERANG || obj->otyp == CHAKRAM) /* arbitrary */
                tmp += 4;
            else if (throwing_weapon(obj)) /* meant to be thrown */
                tmp += 2;
            else if (obj == thrownobj) /* not meant to be thrown */
                tmp -= 2;
            /* we know we're dealing with a weapon or weptool handled
               by WEAPON_SKILLS once ammo objects have been excluded */
            tmp += weapon_hit_bonus(obj);
        }

        if (tmp >= dieroll) {
            boolean wasthrown = (thrownobj != 0),
                    /* remember weapon attribute; hmon() might destroy obj */
                    chopper = is_axe(obj);

            /* attack hits mon */
            if (hmode == HMON_APPLIED)
                if (!u.uconduct.weaphit++)
                    livelog_write_string(LL_CONDUCT, "hit with a wielded weapon for the first time");
            if (hmon(mon, obj, hmode, dieroll)) { /* mon still alive */
                if (mon->wormno)
                    cutworm(mon, bhitpos.x, bhitpos.y, chopper);
            }
            /* Priests firing/throwing edged weapons is frowned upon by
               their deity */
            if (Role_if(PM_PRIEST) && (is_pierce(obj) || is_slash(obj))) {
                if (!rn2(4)) {
                    pline("%s %s weapons such as %s %s %s!",
                          ammo_and_launcher(obj, uwep) ? "Firing" : "Throwing",
                          is_slash(obj) ? "edged" : "piercing",
                          ansimpleoname(obj),
                          rn2(2) ? "angers" : "displeases",
                          align_gname(u.ualign.type));
                    adjalign(-1);
                }
                exercise(A_WIS, FALSE);
            }
            exercise(A_DEX, TRUE);

            /* Detonate bolts shot by Hellfire */
#define ZT_FIRE (10 + (AD_FIRE - 1))
            if (hellfiring && ammo_and_launcher(obj, uwep)) {

                if (cansee(bhitpos.x, bhitpos.y))
                    pline("%s explodes in a ball of fire!", Doname2(obj));
                else
                    You_hear("an explosion");
                explode(bhitpos.x, bhitpos.y, ZT_FIRE, d(2, 6),
                        WEAPON_CLASS, EXPL_FIERY);
            }

            /* if hero was swallowed and projectile killed the engulfer,
               'obj' got added to engulfer's inventory and then dropped,
               so we can't safely use that pointer anymore; it escapes
               the chance to be used up here... */
            if (wasthrown && !thrownobj)
                return 1;

            /* projectiles other than magic stones sometimes disappear
               when thrown; projectiles aren't among the types of weapon
               that hmon() might have destroyed so obj is intact */
            if ((objects[otyp].oc_skill < P_NONE
                && objects[otyp].oc_skill > -P_BOOMERANG
                && !objects[otyp].oc_magic) 
                || obj->oartifact == ART_HOUCHOU) {
                /* we were breaking 2/3 of everything unconditionally.
                 * we still don't want anything to survive unconditionally,
                 * but we need ammo to stay around longer on average.
                 */
                int broken, chance;

                chance = 3 + greatest_erosion(obj) - obj->spe;
                if (chance > 1)
                    broken = rn2(chance);
                else
                    broken = !rn2(4);
                if (obj->blessed && !rnl(4))
                    broken = 0;
                if (objects[otyp].oc_skill == -P_FIREARM)
                    broken = 1;
                if (obj->otyp == SPIKE)
                    broken = 1;
                
                /* Flint, sling bullets, and hard gems get an additional chance
                 * because they don't break easily. */
                if (((obj->oclass == GEM_CLASS && objects[otyp].oc_tough)
                     || obj->otyp == FLINT || obj->otyp == SLING_BULLET)
                    && rn2(2)) {
                    broken = FALSE;
                }
                if (obj->oartifact == ART_HOUCHOU) {
                    broken = TRUE;
                }
                
                if (broken) {
                    if (*u.ushops || obj->unpaid)
                        check_shop_obj(obj, bhitpos.x, bhitpos.y, TRUE);
                    if ((wasthrown || thrownobj) && is_bomb(obj)) {
                        bomb_explode(obj, bhitpos.x, bhitpos.y, obj->yours);
                    }
                    obfree(obj, (struct obj *) 0);
                    return 1;
                }
            }
            if (passive_obj(mon, obj, (struct attack *) 0) == ER_DESTROYED)
                return 1;
            if (mon->data == &mons[PM_ADHERER] && !DEADMONSTER(mon)) {
                pline("The %s sticks to %s", xname(obj), mon_nam(mon));
                
                if (uwep && obj == uwep) {
                    dropx(uwep);
                    obj_extract_self(obj);
                    add_to_minv(mon, obj);
                } else 
                    (void) mpickobj(mon, obj);
                return 1;
            }
        } else {
            tmiss(obj, mon, TRUE);
            if (hmode == HMON_APPLIED)
                wakeup(mon, TRUE);
        }

    } else if (otyp == HEAVY_IRON_BALL) {
        exercise(A_STR, TRUE);
        if (tmp >= dieroll) {
            int was_swallowed = guaranteed_hit;

            exercise(A_DEX, TRUE);
            if (!hmon(mon, obj, hmode, dieroll)) { /* mon killed */
                if (was_swallowed && !u.uswallow && obj == uball)
                    return 1; /* already did placebc() */
            }
        } else {
            tmiss(obj, mon, TRUE);
        }

    } else if (otyp == BOULDER) {
        exercise(A_STR, TRUE);
        if (tmp >= dieroll) {
            exercise(A_DEX, TRUE);
            (void) hmon(mon, obj, hmode, dieroll);
        } else {
            tmiss(obj, mon, TRUE);
        }

    } else if ((otyp == EGG || otyp == CREAM_PIE || otyp == BLINDING_VENOM
                || otyp == ACID_VENOM || otyp == SNOWBALL || otyp == PINEAPPLE
                || otyp == FRUITCAKE)
               && (guaranteed_hit || ACURR(A_DEX) > rnd(25))) {
        (void) hmon(mon, obj, hmode, dieroll);
        return 1; /* hmon used it up */

    } else if (obj->oclass == POTION_CLASS
               && (guaranteed_hit || ACURR(A_DEX) > rnd(25))) {
        potionhit(mon, obj, POTHIT_HERO_THROW);
        return 1;

    } else if (obj->otyp == SPRIG_OF_CATNIP
             && is_feline(mon->data)) {
        if (!Blind)
            pline("%s chases %s tail!", Monnam(mon), mhis(mon));
        (void) tamedog(mon, (struct obj *) 0);
        mon->mconf = 1;
        return 1;

    } else if (befriend_with_obj(mon->data, obj)
               || (mon->mtame && dogfood(mon, obj) <= ACCFOOD)) {
        if (tamedog(mon, obj)) {
            return 1; /* obj is gone */
        } else {
            tmiss(obj, mon, FALSE);
            mon->msleeping = 0;
            mon->mstrategy &= ~STRAT_WAITMASK;
        }
    } else if (guaranteed_hit) {
        /* this assumes that guaranteed_hit is due to swallowing */
        wakeup(mon, TRUE);
        if (obj->otyp == CORPSE && touch_petrifies(&mons[obj->corpsenm])) {
            if (is_swallower(u.ustuck->data)) {
                minstapetrify(u.ustuck, TRUE);
                /* Don't leave a cockatrice corpse available in a statue */
                if (!u.uswallow) {
                    delobj(obj);
                    return 1;
                }
            }
        }
        pline("%s into %s %s.", Tobjnam(obj, "vanish"),
              s_suffix(mon_nam(mon)),
              is_swallower(u.ustuck->data) ? "entrails" : "currents");
    } else {
        tmiss(obj, mon, TRUE);
    }

    return 0;
}

STATIC_OVL int
gem_accept(mon, obj)
register struct monst *mon;
register struct obj *obj;
{
    char buf[BUFSZ];
    boolean is_buddy = (sgn(mon_aligntyp(mon)) == u.ualign.type);
    boolean is_gem = obj->material == GEMSTONE;
    int ret = 0;
    static NEARDATA const char nogood[] = " is not interested in your junk.";
    static NEARDATA const char acceptgift[] = " accepts your gift.";
    static NEARDATA const char maybeluck[] = " hesitatingly";
    static NEARDATA const char noluck[] = " graciously";
    static NEARDATA const char addluck[] = " gratefully";

    Strcpy(buf, Monnam(mon));
    mon->mpeaceful = 1;
    mon->mavenge = 0;

    /* object properly identified */
    if (obj->dknown && objects[obj->otyp].oc_name_known) {
        if (is_gem) {
            if (is_buddy) {
                Strcat(buf, addluck);
                change_luck(5);
            } else {
                Strcat(buf, maybeluck);
                change_luck(rn2(7) - 3);
            }
        } else {
            Strcat(buf, nogood);
            goto nopick;
        }
        /* making guesses */
    } else if (has_oname(obj) || objects[obj->otyp].oc_uname) {
        if (is_gem) {
            if (is_buddy) {
                Strcat(buf, addluck);
                change_luck(2);
            } else {
                Strcat(buf, maybeluck);
                change_luck(rn2(3) - 1);
            }
        } else {
            Strcat(buf, nogood);
            goto nopick;
        }
        /* value completely unknown to @ */
    } else {
        if (is_gem) {
            if (is_buddy) {
                Strcat(buf, addluck);
                change_luck(1);
            } else {
                Strcat(buf, maybeluck);
                change_luck(rn2(3) - 1);
            }
        } else {
            Strcat(buf, noluck);
        }
    }
    Strcat(buf, acceptgift);
    if (*u.ushops || obj->unpaid)
        check_shop_obj(obj, mon->mx, mon->my, TRUE);
    (void) mpickobj(mon, obj); /* may merge and free obj */
    ret = 1;

 nopick:
    if (!Blind)
        pline1(buf);
    if (!tele_restrict(mon))
        (void) rloc(mon, TRUE);
    return ret;
}

/*
 * Comments about the restructuring of the old breaks() routine.
 *
 * There are now three distinct phases to object breaking:
 *     breaktest() - which makes the check/decision about whether the
 *                   object is going to break.
 *     breakmsg()  - which outputs a message about the breakage,
 *                   appropriate for that particular object. Should
 *                   only be called after a positive breaktest().
 *                   on the object and, if it going to be called,
 *                   it must be called before calling breakobj().
 *                   Calling breakmsg() is optional.
 *     breakobj()  - which actually does the breakage and the side-effects
 *                   of breaking that particular object. This should
 *                   only be called after a positive breaktest() on the
 *                   object.
 *
 * Each of the above routines is currently static to this source module.
 * There are two routines callable from outside this source module which
 * perform the routines above in the correct sequence.
 *
 *   hero_breaks() - called when an object is to be broken as a result
 *                   of something that the hero has done. (throwing it,
 *                   kicking it, etc.)
 *   breaks()      - called when an object is to be broken for some
 *                   reason other than the hero doing something to it.
 */

/*
 * The hero causes breakage of an object (throwing, dropping it, etc.)
 * Return 0 if the object didn't break, 1 if the object broke.
 */
int
hero_breaks(obj, x, y, breakflags)
struct obj *obj;
xchar x, y;          /* object location (ox, oy may not be right) */
unsigned breakflags;
{
    /* from_invent: thrown or dropped by player; maybe on shop bill;
       by-hero is implicit so callers don't need to specify BRK_BY_HERO */
    boolean from_invent = (breakflags & BRK_FROM_INV) != 0,
            in_view = Blind ? FALSE : (from_invent || cansee(x, y));
    unsigned brk = (breakflags & BRK_KNOWN_OUTCOME);

    /* only call breaktest if caller hasn't already specified the outcome */
    if (!brk)
        brk = breaktest(obj) ? BRK_KNOWN2BREAK : BRK_KNOWN2NOTBREAK;
    if (brk == BRK_KNOWN2NOTBREAK)
        return 0;

    breakmsg(obj, in_view);
    breakobj(obj, x, y, TRUE, from_invent);
    return 1;
}

/*
 * The object is going to break for a reason other than the hero doing
 * something to it.
 * Return 0 if the object doesn't break, 1 if the object broke.
 */
int
breaks(obj, x, y)
struct obj *obj;
xchar x, y; /* object location (ox, oy may not be right) */
{
    boolean in_view = Blind ? FALSE : cansee(x, y);

    if (!breaktest(obj))
        return 0;
    breakmsg(obj, in_view);
    breakobj(obj, x, y, FALSE, FALSE);
    return 1;
}

void
release_camera_demon(obj, x, y)
struct obj *obj;
xchar x, y;
{
    struct monst *mtmp;
    if (!rn2(3)
        && (mtmp = makemon(&mons[rn2(3) ? PM_HOMUNCULUS : PM_IMP], x, y,
                           NO_MM_FLAGS)) != 0) {
        if (canspotmon(mtmp))
            pline("%s is released!", Hallucination
                                         ? An(rndmonnam(NULL))
                                         : "The picture-painting demon");
        mtmp->mpeaceful = !obj->cursed;
        set_malign(mtmp);
    }
}

/*
 * Unconditionally break an object. Assumes all resistance checks
 * and break messages have been delivered prior to getting here.
 */
void
breakobj(obj, x, y, hero_caused, from_invent)
struct obj *obj;
xchar x, y;          /* object location (ox, oy may not be right) */
boolean hero_caused; /* is this the hero's fault? */
boolean from_invent;
{
    boolean fracture = FALSE;
    int am;
    if (IS_ALTAR(levl[x][y].typ))
        am = levl[x][y].altarmask & AM_MASK;
    else
        am = AM_NONE;
    
    switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
    case MIRROR:
        if (hero_caused)
            change_luck(-2);
        break;
    case POT_WATER:      /* really, all potions */
        obj->in_use = 1; /* in case it's fatal */
        if (obj->otyp == POT_OIL && obj->lamplit) {
            explode_oil(obj, x, y);
        } else if ((obj->otyp == POT_VAMPIRE_BLOOD ||
                    obj->otyp == POT_BLOOD) &&
                   am != AM_CHAOTIC && am != AM_NONE) {
            /* ALI: If blood is spilt on a lawful or
             * neutral altar the effect is similar to
             * human sacrifice. There's no effect on
             * chaotic or unaligned altars since it is
             * not sufficient to summon a demon.
             */
            if (hero_caused) {
                /* Regardless of your race/alignment etc.
                 * Lawful and neutral gods really _dont_
                 * like vampire or (presumed) human blood
                 * on their altars.
                 */
                pline("You'll regret this infamous offense!");
                exercise(A_WIS, FALSE);
            }
            /* curse the lawful/neutral altar */
            pline_The("altar is stained with blood.");
            if (!Is_astralevel(&u.uz))
                levl[x][y].altarmask = AM_CHAOTIC;
            angry_priest();
        } else if (distu(x, y) <= 2) {
            if (!breathless(youmonst.data) || haseyes(youmonst.data)) {
                if (obj->otyp != POT_WATER) {
                    if (!breathless(youmonst.data)) {
                        /* [what about "familiar odor" when known?] */
                        You("smell a peculiar odor...");
                    } else {
                        const char *eyes = body_part(EYE);

                        if (eyecount(youmonst.data) != 1)
                            eyes = makeplural(eyes);
                        Your("%s %s.", eyes, vtense(eyes, "water"));
                    }
                }
                potionbreathe(obj);
            }
        }
        /* monster breathing isn't handled... [yet?] */
        break;
    case EXPENSIVE_CAMERA:
        release_camera_demon(obj, x, y);
        break;
    case EGG:
        /* breaking your own eggs is bad luck */
        if (hero_caused && obj->spe && obj->corpsenm >= LOW_PM)
            change_luck((schar) -min(obj->quan, 5L));
        break;
    case BOULDER:
    case STATUE:
        /* caller will handle object disposition;
           we're just doing the shop theft handling */
        fracture = TRUE;
        break;
    default:
        break;
    }

    if (hero_caused) {
        if (from_invent || obj->unpaid) {
            if (*u.ushops || obj->unpaid)
                check_shop_obj(obj, x, y, TRUE);
        } else if (!obj->no_charge && costly_spot(x, y)) {
            /* it is assumed that the obj is a floor-object */
            char *o_shop = in_rooms(x, y, SHOPBASE);
            struct monst *shkp = shop_keeper(*o_shop);

            if (shkp) { /* (implies *o_shop != '\0') */
                static NEARDATA long lastmovetime = 0L;
                static NEARDATA boolean peaceful_shk = FALSE;
                /*  We want to base shk actions on her peacefulness
                    at start of this turn, so that "simultaneous"
                    multiple breakage isn't drastically worse than
                    single breakage.  (ought to be done via ESHK)  */
                if (moves != lastmovetime)
                    peaceful_shk = shkp->mpeaceful;
                if (stolen_value(obj, x, y, peaceful_shk, FALSE) > 0L
                    && (*o_shop != u.ushops[0] || !inside_shop(u.ux, u.uy))
                    && moves != lastmovetime)
                    make_angry_shk(shkp, x, y);
                lastmovetime = moves;
            }
        }
    }
    if (!fracture)
        delobj(obj);
}

/*
 * Check to see if obj is going to break, but don't actually break it.
 * Return 0 if the object isn't going to break, 1 if it is.
 */
boolean
breaktest(obj)
struct obj *obj;
{
    if (obj_resists(obj, 1, 99))
        return 0;
    if (obj->material == GLASS && !obj->oerodeproof
        && !obj->oartifact && obj->oclass != GEM_CLASS)
        return 1;
    switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
    case EXPENSIVE_CAMERA:
    case POT_WATER: /* really, all potions */
    case CREAM_PIE:
    case APPLE_PIE:
    case PUMPKIN_PIE:
    case SLICE_OF_CAKE:
    case MELON:
    case ACID_VENOM:
    case BLINDING_VENOM:
    case BULLET:
    case SHOTGUN_SHELL:
    case SNOWBALL:
    case SPRIG_OF_CATNIP:
    /* In Splice the lash breaks upon throwing, not sure why but we'll leave it. */
    case FLAMING_LASH: 
        return 1;
    case EGG:
        if (obj->corpsenm != PM_PHOENIX)
            return 1;
        /* FALLTHRU */
    default:
        return 0;
    }
}

void
breakmsg(obj, in_view)
struct obj *obj;
boolean in_view;
{
    const char *to_pieces;

    to_pieces = "";
    switch (obj->oclass == POTION_CLASS ? POT_WATER : obj->otyp) {
    default: /* glass or crystal wand */
        if (obj->material != GLASS)
            impossible("breaking odd object?");
        /*FALLTHRU*/
    case LENSES:
    case MIRROR:
    case CRYSTAL_BALL:
    case EXPENSIVE_CAMERA:
        to_pieces = " into a thousand pieces";
    /*FALLTHRU*/
    case POT_WATER: /* really, all potions */
        if (!in_view)
            You_hear("%s shatter!", something);
        else
            pline("%s shatter%s%s!", Doname2(obj),
                  (obj->quan == 1L) ? "s" : "", to_pieces);
        break;
    case FLAMING_LASH:
        if (in_view)
            pline("%s crumbles.", Doname2(obj));
        break;
    case BULLET:
    case SHOTGUN_SHELL:
        break;
    case EGG:
    case MELON:
        pline("Splat!");
        break;
    case CREAM_PIE:
    case APPLE_PIE:
    case PUMPKIN_PIE:
        if (in_view)
            pline("What a mess!");
        break;
    case SLICE_OF_CAKE:
    case FRUITCAKE:
        if (in_view)
            pline("Dirt cake!");
        break;
    case SPRIG_OF_CATNIP:
        if (in_view)
            pline("Catnip flies everywhere!");
        break;
    case ACID_VENOM:
    case BLINDING_VENOM:
        pline("Splash!");
        break;
    case SNOWBALL:
        pline("Thwap!");
        break;
    }
}

/* Possibly destroy a glass object by its use in melee or thrown combat.
 * Return TRUE if destroyed.
 * Separate logic from breakobj because we are not unconditionally breaking the
 * object, and we also need to make sure it's removed from the inventory
 * properly. */
boolean
break_glass_obj(obj)
struct obj* obj;
{
    long unwornmask;
    boolean ucarried;
    if (!obj || !breaktest(obj) || rn2(6))
        return FALSE;
    /* now we are definitely breaking it */

    ucarried = carried(obj);

    /* remove its worn flags */
    unwornmask = obj->owornmask;
    if (!unwornmask) {
        impossible("breaking non-equipped glass obj?");
        return FALSE;
    }
    if (ucarried) { /* hero's item */
        if (obj->quan == 1L) {
            if (obj == uwep) {
                unweapon = TRUE;
            }
            setworn(NULL, unwornmask);
        }
        obj->ox = u.ux, obj->oy = u.uy;
    } else if (mcarried(obj)) { /* monster's item */
        struct monst *mon = obj->ocarry;
        if (obj->quan == 1L) {
            mon->misc_worn_check &= ~unwornmask;
            if (unwornmask & W_WEP) {
                setmnotwielded(mon, obj);
                possibly_unwield(mon, FALSE);
            } else if (unwornmask & W_ARMG) {
                mselftouch(mon, NULL, TRUE);
            }
            /* shouldn't really be needed but... */
            update_mon_intrinsics(mon, obj, FALSE, FALSE);
        }
        obj->ox = mon->mx, obj->oy = mon->my;
    } else {
        impossible("breaking glass obj not in inventory?");
        return FALSE;
    }

    if (obj->quan == 1L) {
        obj->owornmask = 0L;
        pline("%s breaks into pieces!", Yname2(obj));
    } else {
        pline("One of %s breaks into pieces!", yname(obj));
        obj = splitobj(obj, 1L);
    }
    breakobj(obj, obj->ox, obj->oy, !context.mon_moving, TRUE);
    if (ucarried)
        update_inventory();
    return TRUE;
}

STATIC_OVL int
throw_gold(obj)
struct obj *obj;
{
    int range, odx, ody;
    register struct monst *mon;

    if (!u.dx && !u.dy && !u.dz) {
        You("cannot throw gold at yourself.");
        return 0;
    }
    freeinv(obj);
    if (u.uswallow) {
        pline(is_swallower(u.ustuck->data) ? "%s in the %s's entrails."
                                        : "%s into %s.",
              "The money disappears", mon_nam(u.ustuck));
        add_to_minv(u.ustuck, obj);
        return 1;
    }

    if (u.dz) {
        if (u.dz < 0 && !Is_airlevel(&u.uz) && !Underwater
            && !Is_waterlevel(&u.uz)) {
            pline_The("gold hits the %s, then falls back on top of your %s.",
                      ceiling(u.ux, u.uy), body_part(HEAD));
            /* some self damage? */
            if (uarmh)
                pline("Fortunately, you are wearing %s!",
                      an(helm_simple_name(uarmh)));
        }
        bhitpos.x = u.ux;
        bhitpos.y = u.uy;
    } else {
        /* consistent with range for normal objects */
        range = (int) ((ACURRSTR) / 2 - obj->owt / 40);

        /* see if the gold has a place to move into */
        odx = u.ux + u.dx;
        ody = u.uy + u.dy;
        if (!ZAP_POS(levl[odx][ody].typ) || closed_door(odx, ody)) {
            bhitpos.x = u.ux;
            bhitpos.y = u.uy;
        } else {
            mon = bhit(u.dx, u.dy, range, THROWN_WEAPON,
                       (int FDECL((*), (MONST_P, OBJ_P))) 0,
                       (int FDECL((*), (OBJ_P, OBJ_P))) 0, &obj);
            if (!obj)
                return 1; /* object is gone */
            if (mon) {
                if (ghitm(mon, obj)) /* was it caught? */
                    return 1;
            } else {
                if (ship_object(obj, bhitpos.x, bhitpos.y, FALSE))
                    return 1;
            }
        }
    }

    if (flooreffects(obj, bhitpos.x, bhitpos.y, "fall"))
        return 1;
    if (u.dz > 0)
        pline_The("gold hits the %s.", surface(bhitpos.x, bhitpos.y));
    place_object(obj, bhitpos.x, bhitpos.y);
    if (*u.ushops)
        sellobj(obj, bhitpos.x, bhitpos.y);
    stackobj(obj);
    newsym(bhitpos.x, bhitpos.y);
    return 1;
}

int
firearm_range(otyp)
int otyp;
{
    switch(otyp) {
    case AUTO_SHOTGUN:
        return 4;
    case SHOTGUN:
        return 5;
    case FLINTLOCK:
        return 8;
    case SUBMACHINE_GUN:
        return 10;
    case PISTOL:
        return 15;
    case HEAVY_MACHINE_GUN:
    case ASSAULT_RIFLE:
        return 20;
    case RIFLE:
        return 22;
    case SNIPER_RIFLE:
        return 25;
    default:
        impossible("No firearm for firearm-range!");
        return BOLT_LIM;
    }
}

int
firearm_rof(otyp)
int otyp;
{
    switch(otyp) {
    case SNIPER_RIFLE:
        return -3;
    case FLINTLOCK:
        return -2;
    case RIFLE:
    case SHOTGUN:
        return -1;
    case PISTOL:
        return 0;
    case AUTO_SHOTGUN:
        return 2;
    case SUBMACHINE_GUN:
        return 3;
    case ASSAULT_RIFLE:
        return 5;    
    case HEAVY_MACHINE_GUN:
        return 8;
    default:
        impossible("No firearm for rate-of-fire!");
        return 0;
    }
}

/*dothrow.c*/
