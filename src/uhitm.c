/* NetHack 3.6	uhitm.c	$NHDT-Date: 1573764936 2019/11/14 20:55:36 $  $NHDT-Branch: NetHack-3.6 $:$NHDT-Revision: 1.215 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2012. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include <limits.h>

STATIC_DCL boolean FDECL(known_hitum, (struct monst *, struct obj *, int *,
                                       int, int, struct attack *, int));
STATIC_DCL boolean FDECL(theft_petrifies, (struct obj *));
STATIC_DCL struct obj *FDECL(really_steal, (struct obj *, struct monst *));
STATIC_DCL boolean FDECL(hitum_cleave, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hitum, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hmon_hitmon, (struct monst *, struct obj *, int,
                                       int));
STATIC_DCL int FDECL(joust, (struct monst *, struct obj *));
void NDECL(demonpet);
STATIC_DCL void FDECL(start_engulf, (struct monst *));
STATIC_DCL void NDECL(end_engulf);
STATIC_DCL int FDECL(gulpum, (struct monst *, struct attack *));
STATIC_DCL boolean FDECL(hmonas, (struct monst *, int, BOOLEAN_P));
STATIC_DCL void FDECL(nohandglow, (struct monst *));
STATIC_DCL boolean FDECL(shade_aware, (struct obj *));

extern boolean notonhead; /* for long worms */

/* Used to flag attacks caused by Stormbringer's
 * or The Sword of Kas maliciousness. */
static boolean override_confirmation = FALSE;

#define PROJECTILE(obj) ((obj) && is_ammo(obj))
#define KILL_FAMILIARITY 20

void
erode_armor(mdef, hurt)
struct monst *mdef;
int hurt;
{
    struct obj *target;
     
    /* What the following code does: it keeps looping until it
     * finds a target for the rust monster.
     * Head, feet, etc... not covered by metal, or covered by
     * rusty metal, are not targets.  However, your body always
     * is, no matter what covers it.
     */
    while (1) {
        switch (rn2(5)) {
        case 0:
            target = which_armor(mdef, W_ARMH);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE | EF_DESTROY)
                             == ER_NOTHING)
                continue;
            break;
        case 1:
            target = which_armor(mdef, W_ARMC);
            if (target) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_DESTROY);
                break;
            }
            if ((target = which_armor(mdef, W_ARM)) != (struct obj *) 0) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_DESTROY);
            } else if ((target = which_armor(mdef, W_ARMU))
                       != (struct obj *) 0) {
                (void) erode_obj(target, xname(target), hurt,
                                 EF_GREASE | EF_DESTROY);
            }
            break;
        case 2:
            target = which_armor(mdef, W_ARMS);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE | EF_DESTROY)
                             == ER_NOTHING)
                continue;
            break;
        case 3:
            target = which_armor(mdef, W_ARMG);
            if (target && objdescr_is(target, "old gloves"))
                /* Old gloves are already as damaged as they're going to get */
                break;
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE | EF_DESTROY)
                             == ER_NOTHING)
                continue;
            break;
        case 4:
            target = which_armor(mdef, W_ARMF);
            if (!target
                || erode_obj(target, xname(target), hurt, EF_GREASE | EF_DESTROY)
                             == ER_NOTHING)
                continue;
            break;
        }
        break; /* Out of while loop */
    }
}

/* FALSE means it's OK to attack */
boolean
attack_checks(mtmp, wep)
register struct monst *mtmp;
struct obj *wep; /* uwep for attack(), null for kick_monster() */
{
    int glyph;

    /* if you're close enough to attack, alert any waiting monster */
    mtmp->mstrategy &= ~STRAT_WAITMASK;

    if (u.uswallow && mtmp == u.ustuck)
        return FALSE;

    if (context.forcefight) {
        /* Do this in the caller, after we checked that the monster
         * didn't die from the blow.  Reason: putting the 'I' there
         * causes the hero to forget the square's contents since
         * both 'I' and remembered contents are stored in .glyph.
         * If the monster dies immediately from the blow, the 'I' will
         * not stay there, so the player will have suddenly forgotten
         * the square's contents for no apparent reason.
        if (!canspotmon(mtmp)
            && !glyph_is_invisible(levl[bhitpos.x][bhitpos.y].glyph))
            map_invisible(bhitpos.x, bhitpos.y);
         */
        return FALSE;
    }

    /* cache the shown glyph;
       cases which might change it (by placing or removing
       'rembered, unseen monster' glyph or revealing a mimic)
       always return without further reference to this */
    glyph = glyph_at(bhitpos.x, bhitpos.y);

    /* Put up an invisible monster marker, but with exceptions for
     * monsters that hide and monsters you've been warned about.
     * The former already prints a warning message and
     * prevents you from hitting the monster just via the hidden monster
     * code below; if we also did that here, similar behavior would be
     * happening two turns in a row.  The latter shows a glyph on
     * the screen, so you know something is there.
     */
    if (!canspotmon(mtmp)
        && !glyph_is_warning(glyph) && !glyph_is_invisible(glyph)
        && !(!Blind && mtmp->mundetected && hides_under(mtmp->data))) {
        pline("Wait!  There's %s there you can't see!", something);
        map_invisible(bhitpos.x, bhitpos.y);
        /* if it was an invisible mimic, treat it as if we stumbled
         * onto a visible mimic
         */
        if (M_AP_TYPE(mtmp) && !Protection_from_shape_changers
            /* applied pole-arm attack is too far to get stuck */
            && distu(mtmp->mx, mtmp->my) <= 2) {
            if (!u.ustuck && !mtmp->mflee && dmgtype(mtmp->data, AD_STCK))
                u.ustuck = mtmp;
        }
        /* #H7329 - if hero is on engraved "Elbereth", this will end up
         * assessing an alignment penalty and removing the engraving
         * even though no attack actually occurs.  Since it also angers
         * peacefuls, we're operating as if an attack attempt did occur
         * and the Elbereth behavior is consistent.
         */
        wakeup(mtmp, TRUE); /* always necessary; also un-mimics mimics */
        return TRUE;
    }

    if (M_AP_TYPE(mtmp) && !Protection_from_shape_changers && !sensemon(mtmp)
        && !glyph_is_warning(glyph)) {
        /* If a hidden mimic was in a square where a player remembers
         * some (probably different) unseen monster, the player is in
         * luck--he attacks it even though it's hidden.
         */
        if (glyph_is_invisible(glyph)) {
            seemimic(mtmp);
            return FALSE;
        }
        stumble_onto_mimic(mtmp);
        return TRUE;
    }

    if (mtmp->mundetected && !canseemon(mtmp)
        && !glyph_is_warning(glyph)
        && (hides_under(mtmp->data) || mtmp->data->mlet == S_EEL
            || mtmp->data == &mons[PM_GIANT_LEECH])) {
        mtmp->mundetected = mtmp->msleeping = 0;
        newsym(mtmp->mx, mtmp->my);
        if (glyph_is_invisible(glyph)) {
            seemimic(mtmp);
            return FALSE;
        }
        if (!((Blind ? Blind_telepat : Unblind_telepat) || Detect_monsters)) {
            /*struct obj *obj;*/

            if (!Blind && Hallucination)
                pline("A %s %s appeared!",
                      mtmp->mtame ? "tame" : "wild", l_monnam(mtmp));
            else if (Blind || (is_pool(mtmp->mx, mtmp->my) && !Underwater)) {
                pline("Wait!  There's a hidden monster there!");
            }
            else if (concealed_spot(mtmp->mx, mtmp->my)) {
                struct obj *obj = level.objects[mtmp->mx][mtmp->my];
                pline("Wait!  There's %s hiding under %s%s!", an(l_monnam(mtmp)),
                      obj ? "" : "the ",
                      obj ? doname(obj) : explain_terrain(mtmp->mx, mtmp->my));
            }
            return TRUE;
        }
    }

    /*
     * make sure to wake up a monster from the above cases if the
     * hero can sense that the monster is there.
     */
    if ((mtmp->mundetected || M_AP_TYPE(mtmp)) && sensemon(mtmp)) {
        mtmp->mundetected = 0;
        wakeup(mtmp, TRUE);
    }

    if (flags.confirm && mtmp->mpeaceful
        && !Confusion && !Hallucination && !Stunned) {
        /* Intelligent chaotic weapons (Stormbringer, The Sword of Kas) want blood */
        if (wep && (wep->oartifact == ART_STORMBRINGER
                    || wep->oartifact == ART_SWORD_OF_KAS
                    || (u.twoweap && uswapwep->oartifact == ART_STORMBRINGER)
                    || (u.twoweap && uswapwep->oartifact == ART_SWORD_OF_KAS))) {
            override_confirmation = TRUE;
            return FALSE;
        }
        if (canspotmon(mtmp)) {
            char qbuf[QBUFSZ];

            Sprintf(qbuf, "Really attack %s?", mon_nam(mtmp));
            if (!paranoid_query(ParanoidHit, qbuf)) {
                context.move = 0;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/*
 * It is unchivalrous for a knight to attack the defenseless or from behind.
 */
void
check_caitiff(mtmp)
struct monst *mtmp;
{
    if (u.ualign.record <= -10)
        return;

    if (Role_if(PM_KNIGHT) 
          && u.ualign.type == A_LAWFUL
          && !is_undead(mtmp->data)
          && (!mtmp->mcanmove || mtmp->msleeping
            || (mtmp->mflee && !mtmp->mavenge))) {
        if (u.ualign.record > -11)
            You("caitiff!");
        adjalign(-1);
    } else if (Role_if(PM_SAMURAI) && mtmp->mpeaceful) {
        /* attacking peaceful creatures is bad for the samurai's giri */
        if (u.ualign.record > -11)
            You("dishonorably attack the innocent!");
        adjalign(-1);
    } else if (Role_if(PM_JEDI) && mtmp->mpeaceful) {
        /* as well as for the way of the Jedi */
        if (u.ualign.record > -10)
            You("violate the way of the Jedi!");
        adjalign(-5);
    }
}

int
find_roll_to_hit(mtmp, aatyp, weapon, attk_count, role_roll_penalty)
register struct monst *mtmp;
uchar aatyp;        /* usually AT_WEAP or AT_KICK */
struct obj *weapon; /* uwep or uswapwep or NULL */
int *attk_count, *role_roll_penalty;
{
    int tmp, tmp2, ftmp;
    int wepskill, twowepskill, useskill;

    *role_roll_penalty = 0; /* default is `none' */

    tmp = 1 + (Luck / 3) + abon() + find_mac(mtmp) + u.uhitinc
          + maybe_polyd(youmonst.data->mlevel, (u.ulevel > 20 ? 20 : u.ulevel));

    /* some actions should occur only once during multiple attacks */
    if (!(*attk_count)++) {
        /* knight's chivalry or samurai's giri */
        check_caitiff(mtmp);
    }

    /* adjust vs. (and possibly modify) monster state */
    if (mtmp->mstun)
        tmp += 2;
    if (mtmp->mflee)
        tmp += 2;

    if (mtmp->msleeping) {
        mtmp->msleeping = 0;
        tmp += 2;
    }
    if (!mtmp->mcanmove) {
        tmp += 4;
        if (!rn2(10)) {
            if (!mtmp->mstone || mtmp->mstone > 2)
                mtmp->mcanmove = 1;
            mtmp->mfrozen = 0;
        }
    }

    if (calculate_flankers(&youmonst, mtmp)) {
        /* Scale with monster difficulty */
        ftmp = (int) ((u.ulevel - 4) / 2) + 4;
        tmp += ftmp;
        You("flank %s. [-%dAC]", mon_nam(mtmp), ftmp);
    }
    
    /* level adjustment. maxing out has some benefits */
    if (u.ulevel == 30)
        tmp += 4;

    /* role/race adjustments */
    if (Role_if(PM_MONK) && !Upolyd) {
        if (uarm && 
              (uarm->otyp < ROBE || uarm->otyp > ROBE_OF_WEAKNESS))
            tmp -= (*role_roll_penalty = urole.spelarmr) + 20;
        else if (!uwep && !uarms)
            tmp += (u.ulevel / 3) + 2;
    }
    if (Role_if(PM_JEDI) && !Upolyd) {
        if (((uwep && is_lightsaber(uwep) && uwep->lamplit) ||
             (uswapwep && u.twoweap && is_lightsaber(uswapwep) && uswapwep->lamplit)) 
            && (uarm && (uarm->otyp < ROBE || uarm->otyp > ROBE_OF_WEAKNESS))) {
            char yourbuf[BUFSZ];
            You_cant("use %s %s effectively in this armor...", shk_your(yourbuf, uwep), xname(uwep));
            tmp -= 20;  /* sorry */
        }
    }

    /* elves get a slight bonus to-hit vs orcs */
    if (racial_orc(mtmp)
        && maybe_polyd(is_elf(youmonst.data), Race_if(PM_ELF)))
        tmp++;

    /* orcs should get the same to-hit bonus vs elves */
    if (racial_elf(mtmp)
        && maybe_polyd(is_orc(youmonst.data), Race_if(PM_ORC)))
        tmp++;

    /* Adding iron ball as a weapon skill gives a -4 penalty for
    unskilled vs no penalty for non-weapon objects.  Add 4 to
    compensate. */
    if (uwep && (uwep->otyp == HEAVY_IRON_BALL)) {
        tmp += 4;   /* Compensate for iron ball weapon skill -4
                    penalty for unskilled vs no penalty for non-
                    weapon objects. */
    }

    /* gloves' bonus contributes if unarmed */
    if (!uwep && uarmg)
        tmp += uarmg->spe;

    /* encumbrance: with a lot of luggage, your agility diminishes */
    if ((tmp2 = near_capacity()) != 0)
        tmp -= (tmp2 * 2) - 1;
    if (u.utrap)
        tmp -= 3;

    /*
     * hitval applies if making a weapon attack while wielding a weapon;
     * weapon_hit_bonus applies if doing a weapon attack even bare-handed
     * or if kicking as martial artist
     */
    if (aatyp == AT_WEAP || aatyp == AT_CLAW) {
        if (weapon)
            tmp += hitval(weapon, mtmp);
        tmp += weapon_hit_bonus(weapon);
    } else if (aatyp == AT_KICK && martial_bonus()) {
        tmp += weapon_hit_bonus((struct obj *) 0);
    }

    /* combat boots give +1 to-hit */
    if (uarmf && objdescr_is(uarmf, "combat boots")) 
        tmp += 1;
    
    /* fencing gloves increase weapon accuracy when you have a free off-hand */
    if (weapon && !bimanual(weapon) && !which_armor(mtmp, W_ARMS)) {
        struct obj * otmp = which_armor(mtmp, W_ARMG);
        if (otmp && objdescr_is(otmp, "fencing gloves"))
            tmp += 2;
    }

    /* special class effect uses... */
    if (tech_inuse(T_KIII))
        tmp += 4;
    if (tech_inuse(T_BERSERK) || tech_inuse(T_SOULEATER))
        tmp += 2;
    
    /* if unskilled with a weapon/object type (bare-handed is exempt),
     * you'll never have a chance greater than 75% to land a hit.
     */
    if (uwep && aatyp == AT_WEAP && !u.uswallow) {
        wepskill = P_SKILL(weapon_type(uwep));
        twowepskill = P_SKILL(P_TWO_WEAPON_COMBAT);
        /* use the lesser skill of two-weapon or your primary */
        useskill = (u.twoweap && twowepskill < wepskill) ? twowepskill : wepskill;
        if ((useskill == P_UNSKILLED || useskill == P_ISRESTRICTED) && tmp > 15) {
            tmp = 15;
            if (!rn2(5)) {
                /* using a corpse as a weapon... alrighty then */
                if (uwep->oclass != WEAPON_CLASS && !is_weptool(uwep)) {
                    You("struggle trying to use the %s as a weapon.",
                         aobjnam(uwep, (char *) 0));
                } else if (useskill != P_ISRESTRICTED) {
                    You_feel("like you could use some more practice.");
                } else {
                    You("aren't sure you're doing this the right way...");
                }
            }
        }
    }
    return tmp;
}

/* try to attack; return False if monster evaded;
   u.dx and u.dy must be set */
boolean
attack(mtmp)
register struct monst *mtmp;
{
    register struct permonst *mdat = mtmp->data;

    /* This section of code provides protection against accidentally
     * hitting peaceful (like '@') and tame (like 'd') monsters.
     * Protection is provided as long as player is not: blind, confused,
     * hallucinating or stunned.
     * changes by wwp 5/16/85
     * More changes 12/90, -dkh-. if its tame and safepet, (and protected
     * 07/92) then we assume that you're not trying to attack. Instead,
     * you'll usually just swap places if this is a movement command
     */
    /* Intelligent chaotic weapons (Stormbringer, The Sword of Kas) want blood */
    if (is_safepet(mtmp) && !context.forcefight) {
        if (!uwep || !(uwep->oartifact == ART_STORMBRINGER
                       || uwep->oartifact == ART_SWORD_OF_KAS
                       || (u.twoweap && uswapwep->oartifact == ART_STORMBRINGER)
                       || (u.twoweap && uswapwep->oartifact == ART_SWORD_OF_KAS))) {
            /* There are some additional considerations: this won't work
             * if in a shop or Punished or you miss a random roll or
             * if you can walk thru walls and your pet cannot (KAA) or
             * if your pet is a long worm with a tail.
             * There's also a chance of displacing a "frozen" monster:
             * sleeping monsters might magically walk in their sleep.
             */
            boolean foo = (Punished || !rn2(7)
                           || (is_longworm(mtmp->data) && mtmp->wormno)),
                    inshop = FALSE;
            char *p;

            /* only check for in-shop if don't already have reason to stop */
            if (!foo) {
                for (p = in_rooms(mtmp->mx, mtmp->my, SHOPBASE); *p; p++)
                    if (tended_shop(&rooms[*p - ROOMOFFSET])) {
                        inshop = TRUE;
                        break;
                    }
            }
            if (inshop || foo || (IS_ROCK(levl[u.ux][u.uy].typ)
                                  && !passes_walls(mtmp->data))) {
                char buf[BUFSZ];

                monflee(mtmp, rnd(6), FALSE, FALSE);
                Strcpy(buf, y_monnam(mtmp));
                buf[0] = highc(buf[0]);
                You("stop.  %s is in the way!", buf);
                context.travel = context.travel1 = context.mv = context.run
                    = 0;
                return TRUE;
            } else if (mtmp->mfrozen || mtmp->msleeping || (!mtmp->mcanmove)
                       || (mtmp->data->mmove == 0 && rn2(6))) {
                pline("%s doesn't seem to move!", Monnam(mtmp));
                context.travel = context.travel1 = context.mv = context.run
                    = 0;
                return TRUE;
            } else
                return FALSE;
        }
    }
    
    if (uarmf && uarmf->otyp == STOMPING_BOOTS && !Levitation 
        && mtmp->data->msize <= MZ_SMALL) {
        if (verysmall(mtmp->data) || !rn2(4)) {
            You("stomp on %s!", mon_nam(mtmp));
            xkilled(mtmp, XKILL_GIVEMSG);
            if (!SuperStealth)
                wake_nearby();
            makeknown(uarmf->otyp);
            return TRUE;
        }
    }

    /* possibly set in attack_checks;
       examined in known_hitum, called via hitum or hmonas below */
    override_confirmation = FALSE;
    /* attack_checks() used to use <u.ux+u.dx,u.uy+u.dy> directly, now
       it uses bhitpos instead; it might map an invisible monster there */
    bhitpos.x = u.ux + u.dx;
    bhitpos.y = u.uy + u.dy;
    notonhead = (bhitpos.x != mtmp->mx || bhitpos.y != mtmp->my);
    if (attack_checks(mtmp, uwep))
        return TRUE;

    if (Upolyd && noattacks(youmonst.data)) {
        /* certain "pacifist" monsters don't attack */
        You("have no way to attack monsters physically.");
        mtmp->mstrategy &= ~STRAT_WAITMASK;
        goto atk_done;
    }

    if (check_capacity("You cannot fight while so heavily loaded.")
        /* consume extra nutrition during combat; maybe pass out */
        || overexertion())
        goto atk_done;

    if (u.twoweap && !can_twoweapon())
        untwoweapon();
    
    /* feedback for priests using non-blunt weapons */
    if (uwep && Role_if(PM_PRIEST)
        && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))
        && (is_pierce(uwep) || is_slash(uwep) || is_ammo(uwep))) {
        if (!rn2(4))
            pline("%s has %s you from using %s weapons such as %s!",
                  align_gname(u.ualign.type), rn2(2) ? "forbidden" : "prohibited",
                  is_slash(uwep) ? "edged" : "piercing", ansimpleoname(uwep));
        exercise(A_WIS, FALSE);
        if (!rn2(10)) {
            Your("behavior has displeased %s.",
                 align_gname(u.ualign.type));
            adjalign(-1);
        }
    }

    if (unweapon) {
        unweapon = FALSE;
        if (flags.verbose) {
            if (uwep)
                You("begin bashing monsters with %s.", yname(uwep));
            else if (tech_inuse(T_EVISCERATE))
		        You("begin slashing monsters with your claws.");
            else if (!cantwield(youmonst.data))
                You("begin %s monsters with your %s %s.",
                    ing_suffix(Role_if(PM_MONK) ? "strike" :
                               (Role_if(PM_ROGUE) && context.forcefight) ? "rob" : "bash"),
                    (uarmg && uarmg->oartifact != ART_HAND_OF_VECNA) ? "gloved" : "bare", /* Del Lamb */
                    makeplural(body_part(HAND)));
        }
    }
    exercise(A_STR, TRUE); /* you're exercising muscles */
    /* andrew@orca: prevent unlimited pick-axe attacks */
    u_wipe_engr(3);

    /* Is the "it died" check actually correct? */
    if (mdat->mlet == S_LEPRECHAUN && !mtmp->mfrozen && !mtmp->msleeping
        && !mtmp->mconf && mtmp->mcansee && !rn2(7)
        && (m_move(mtmp, 0) == 2 /* it died */
            || mtmp->mx != u.ux + u.dx
            || mtmp->my != u.uy + u.dy)) { /* it moved */
        You("miss wildly and stumble forwards.");
        return FALSE;
    }

    if ((is_displaced(mtmp->data) || has_displacement(mtmp))
        && !u.uswallow && !rn2(4)) {
        pline_The("image of %s shimmers and vanishes!", mon_nam(mtmp));
        return FALSE;
    }

    if (Underwater && !is_pool(mtmp->mx, mtmp->my)) {
        ; /* no attack, hero will still attempt to move onto solid ground */
        return FALSE;
    }
    
    if (Underwater
        && !grounded(mtmp->data) && is_pool(mtmp->mx, mtmp->my)) {
        char pnambuf[BUFSZ];

        /* save its current description in case of polymorph */
        Strcpy(pnambuf, y_monnam(mtmp));
        mtmp->mtrapped = 0;
        remove_monster(mtmp->mx, mtmp->my);
        place_monster(mtmp, u.ux0, u.uy0);
        newsym(mtmp->mx, mtmp->my);
        newsym(u.ux0, u.uy0);

        You("swim underneath %s.", pnambuf);
        return FALSE;
    }
    
    if (Upolyd)
        (void) hmonas(mtmp, NON_PM, TRUE);
    else
        (void) hitum(mtmp, youmonst.data->mattk);
    mtmp->mstrategy &= ~STRAT_WAITMASK;

 atk_done:
    /* see comment in attack_checks() */
    /* we only need to check for this if we did an attack_checks()
     * and it returned 0 (it's okay to attack), and the monster didn't
     * evade.
     */
    if (context.forcefight && !DEADMONSTER(mtmp) && !canspotmon(mtmp)
        && !glyph_is_invisible(levl[u.ux + u.dx][u.uy + u.dy].glyph)
        && !(u.uswallow && mtmp == u.ustuck))
        map_invisible(u.ux + u.dx, u.uy + u.dy);

    return TRUE;
}

/* really hit target monster; returns TRUE if it still lives */
STATIC_OVL boolean
known_hitum(mon, weapon, mhit, rollneeded, armorpenalty, uattk, dieroll)
register struct monst *mon;
struct obj *weapon;
int *mhit;
int rollneeded, armorpenalty; /* for monks */
struct attack *uattk;
int dieroll;
{
    boolean malive = TRUE,
            /* hmon() might destroy weapon; remember aspect for cutworm */
            slice_or_chop = (weapon && (is_blade(weapon) || is_axe(weapon)));

    if (override_confirmation) {
        /* this may need to be generalized if weapons other than
           Stormbringer acquire similar anti-social behavior... */
        if (flags.verbose) {
            if (weapon->oartifact == ART_STORMBRINGER)
                Your("bloodthirsty blade attacks!");
            else
                Your("vicious blade attacks!");
        }
    }

    if (!*mhit) {
        missum(mon, rollneeded, dieroll, uattk, (rollneeded + armorpenalty > dieroll));
    } else {
        int oldhp = mon->mhp;
        long oldweaphit = u.uconduct.weaphit;

        /* KMH, conduct */
        if (weapon && (weapon->oclass == WEAPON_CLASS || is_weptool(weapon)))
            u.uconduct.weaphit++;

        /* we hit the monster; be careful: it might die or
           be knocked into a different location */
        notonhead = (mon->mx != bhitpos.x || mon->my != bhitpos.y);
        malive = hmon(mon, weapon, HMON_MELEE, dieroll);
        if (malive) {
            /* monster still alive */
            if (!rn2(25) && mon->mhp < mon->mhpmax / 2
                && !(u.uswallow && mon == u.ustuck)) {
                /* maybe should regurgitate if swallowed? */
                monflee(mon, !rn2(3) ? rnd(100) : 0, FALSE, TRUE);

                if (u.ustuck == mon && !u.uswallow && !sticks(youmonst.data))
                    u.ustuck = 0;
            }
            /* Vorpal Blade hit converted to miss */
            /* could be headless monster or worm tail */
            if (mon->mhp == oldhp) {
                *mhit = 0;
                /* a miss does not break conduct */
                u.uconduct.weaphit = oldweaphit;
            }
            if (mon->wormno && *mhit)
                cutworm(mon, bhitpos.x, bhitpos.y, slice_or_chop);
        }

#if 0 /* TODO: Double check the placement of this. */
        /* Lycanthropes sometimes go a little berserk! 
	     * If special is on,  they will multihit and stun!
	     */
	    if (( (Race_if(PM_HUMAN_WEREWOLF) || Role_if(PM_LUNATIC) ) && (mon->mhp > 0)) ||
				tech_inuse(T_EVISCERATE)) {
			if (tech_inuse(T_EVISCERATE)) {
				/*make slashing message elsewhere*/
				if (repeat_hit == 0) {
				/* [max] limit to 4 (0-3) */
				repeat_hit = (tech_inuse(T_EVISCERATE) > 5) ?
							4 : (tech_inuse(T_EVISCERATE) - 2);
				/* [max] limit to 4 */
				mon->mfrozen = (tech_inuse(T_EVISCERATE) > 5) ?
							4 : (tech_inuse(T_EVISCERATE) - 2); 
				}
				mon->mstun = 1;
				mon->mcanmove = 0;
			} else if (!rn2(24)) {
				repeat_hit += rn2(4)+1;
				mon->mfrozen = repeat_hit; /* Lycanthropes suck badly enough already, k? --Amy */
				mon->mcanmove = 0;
				/* Length of growl depends on how angry you get */
				switch (repeat_hit) {
					case 0: /* This shouldn't be possible, but... */
				case 1: pline("Grrrrr!"); break;
				case 2: pline("Rarrrgh!"); break;
				case 3: pline("Grrarrgh!"); break;
				case 4: pline("Rarggrrgh!"); break;
				case 5: pline("Raaarrrrrr!"); break;
				case 6: 
				default:pline("Grrrrrrarrrrg!"); break;
				}
			}
	    }
#endif
        if (u.uconduct.weaphit && !oldweaphit)
            livelog_write_string(LL_CONDUCT,
                    "hit with a wielded weapon for the first time");
    }
    return malive;
}

/* hit the monster next to you and the monsters to the left and right of it;
   return False if the primary target is killed, True otherwise */
STATIC_OVL boolean
hitum_cleave(target, uattk)
struct monst *target; /* non-Null; forcefight at nothing doesn't cleave... */
struct attack *uattk; /* ... but we don't enforce that here; Null works ok */
{
    /* swings will be delivered in alternate directions; with consecutive
       attacks it will simulate normal swing and backswing; when swings
       are non-consecutive, hero will sometimes start a series of attacks
       with a backswing--that doesn't impact actual play, just spoils the
       simulation attempt a bit */
    static boolean clockwise = FALSE;
    unsigned i;
    coord save_bhitpos;
    int count, umort, x = u.ux, y = u.uy;

    /* find the direction toward primary target */
    for (i = 0; i < 8; ++i)
        if (xdir[i] == u.dx && ydir[i] == u.dy)
            break;
    if (i == 8) {
        impossible("hitum_cleave: unknown target direction [%d,%d,%d]?",
                   u.dx, u.dy, u.dz);
        return TRUE; /* target hasn't been killed */
    }
    /* adjust direction by two so that loop's increment (for clockwise)
       or decrement (for counter-clockwise) will point at the spot next
       to primary target */
    i = (i + (clockwise ? 6 : 2)) % 8;
    umort = u.umortality; /* used to detect life-saving */
    save_bhitpos = bhitpos;

    /*
     * Three attacks:  adjacent to primary, primary, adjacent on other
     * side.  Primary target must be present or we wouldn't have gotten
     * here (forcefight at thin air won't 'cleave').  However, the
     * first attack might kill it (gas spore explosion, weak long worm
     * occupying both spots) so we don't assume that it's still present
     * on the second attack.
     */
    for (count = 3; count > 0; --count) {
        struct monst *mtmp;
        int tx, ty, tmp, dieroll, mhit, attknum, armorpenalty;

        /* ++i, wrap 8 to i=0 /or/ --i, wrap -1 to i=7 */
        i = (i + (clockwise ? 1 : 7)) % 8;

        tx = x + xdir[i], ty = y + ydir[i]; /* current target location */
        if (!isok(tx, ty))
            continue;
        mtmp = m_at(tx, ty);
        if (!mtmp) {
            if (glyph_is_invisible(levl[tx][ty].glyph))
                (void) unmap_invisible(tx, ty);
            continue;
        }

        tmp = find_roll_to_hit(mtmp, uattk->aatyp, uwep,
                               &attknum, &armorpenalty);
        dieroll = rnd(20);
        mhit = (tmp > dieroll);
        bhitpos.x = tx, bhitpos.y = ty; /* normally set up by attack() */
        (void) known_hitum(mtmp, uwep, &mhit, tmp, armorpenalty,
                           uattk, dieroll);
        (void) passive(mtmp, uwep, mhit, !DEADMONSTER(mtmp), AT_WEAP, !uwep);

        /* stop attacking if weapon is gone or hero got paralyzed or
           killed (and then life-saved) by passive counter-attack */
        if (!uwep || multi < 0 || u.umortality > umort)
            break;
    }
    /* set up for next time */
    clockwise = !clockwise; /* alternate */
    bhitpos = save_bhitpos; /* in case somebody relies on bhitpos
                             * designating the primary target */

    /* return False if primary target died, True otherwise; note: if 'target'
       was nonNull upon entry then it's still nonNull even if *target died */
    return (target && DEADMONSTER(target)) ? FALSE : TRUE;
}

/* hit target monster; returns TRUE if it still lives */
STATIC_OVL boolean
hitum(mon, uattk)
struct monst *mon;
struct attack *uattk;
{
    boolean malive = TRUE, wep_was_destroyed = FALSE;
    struct obj *wepbefore = uwep;
    struct obj *wearshield = uarms;
    struct obj *weararmor = uarm;
    int armorpenalty, attknum = 0, x = u.ux + u.dx, y = u.uy + u.dy,
                      tmp = find_roll_to_hit(mon, uattk->aatyp, uwep,
                                             &attknum, &armorpenalty);
    int dieroll = rnd(20), oldumort = u.umortality;
    int mhit = (tmp > dieroll || u.uswallow);
    int bash_chance = (P_SKILL(P_SHIELD) == P_MASTER ? !rn2(3) :
                       P_SKILL(P_SHIELD) == P_EXPERT ? !rn2(4) :
                       P_SKILL(P_SHIELD) == P_SKILLED ? !rn2(6) : !rn2(8));

    /* using cursed weapons can sometimes do unexpected things.
       no need to set a condition for cursed secondary weapon
       if twoweaponing, as that isn't possible */
    if (uwep && uwep->cursed && !rn2(7)
        && u.ualign.type != A_NONE) {
        if (!rn2(4)) {
            if (rn2(2))
                You("swing wildly and miss!");
            else
                Your("cursed %s veers wildly off course!!", simpleonames(uwep));
        }
        return 0;
    }

    /* Cleaver attacks three spots, 'mon' and one on either side of 'mon';
       it can't be part of dual-wielding but we guard against that anyway;
       cleave return value reflects status of primary target ('mon') */
    if (uwep && uwep->oartifact == ART_CLEAVER && !u.twoweap
        && !u.uswallow && !u.ustuck && !NODIAG(u.umonnum))
        return hitum_cleave(mon, uattk);

    if (tmp > dieroll)
        exercise(A_DEX, TRUE);

    /* if twoweaponing with stormbringer/sword of kas, don't force both
     * attacks -- only from the actual 'bloodthirsty' weapon(s) */
#define forced_attack(w) ((w) && ((w)->oartifact == ART_STORMBRINGER \
                                  || (w)->oartifact == ART_SWORD_OF_KAS))
    if ((!override_confirmation || forced_attack(uwep))
        && !(multi < 0 || u.umortality > oldumort)) {
        /* bhitpos is set up by caller */
        malive = known_hitum(mon, uwep, &mhit, tmp, armorpenalty, uattk,
                             dieroll);
        if (wepbefore && !uwep)
            wep_was_destroyed = TRUE;
        (void) passive(mon, uwep, mhit, malive, AT_WEAP, wep_was_destroyed);
    }

    /* second attack for two-weapon combat; won't occur if Stormbringer
       or the Sword of Kas overrode confirmation (assumes Stormbringer/
       Sword of Kas is primary weapon), or if hero became paralyzed by
       passive counter-attack, or if hero was killed by passive
       counter-attack and got life-saved, or if monster was killed or
       knocked to different location */
    if (u.twoweap && (!override_confirmation || forced_attack(uswapwep))
                  && !(multi < 0 || u.umortality > oldumort
                       || !malive || m_at(x, y) != mon)) {
        tmp = find_roll_to_hit(mon, uattk->aatyp, uswapwep, &attknum,
                               &armorpenalty);
        dieroll = rnd(20);
        mhit = (tmp > dieroll || u.uswallow);
        malive = known_hitum(mon, uswapwep, &mhit, tmp, armorpenalty, uattk,
                             dieroll);
        /* second passive counter-attack only occurs if second attack hits */
        if (mhit)
            (void) passive(mon, uswapwep, mhit, malive, AT_WEAP, !uswapwep);
    }
#undef forced_attack

    /* second attack for a Monk who has reached Grand Master skill
       in martial arts */
    if (!uwep && Role_if(PM_MONK)
        && P_SKILL(P_MARTIAL_ARTS) == P_GRAND_MASTER
        && !(multi < 0 || u.umortality > oldumort
             || !malive || m_at(x, y) != mon)) {
        if (wearshield) {
            if (!rn2(8))
                Your("extra attack is ineffective while wearing %s.",
                      an(xname(wearshield)));
        } else {
            tmp = find_roll_to_hit(mon, uattk->aatyp, uwep, &attknum,
                                   &armorpenalty);
            dieroll = rnd(20);
            mhit = (tmp > dieroll || u.uswallow);
            malive = known_hitum(mon, uwep, &mhit, tmp, armorpenalty, uattk,
                                 dieroll);
            /* second passive counter-attack only occurs if second attack hits */
            if (mhit)
                (void) passive(mon, uwep, mhit, malive, AT_CLAW, FALSE);
        }
    }

    /* random kick attack for a Monk who has reached Master skill
       or greater in martial arts */
    if (!rn2(3) && !uwep && Role_if(PM_MONK)
        && P_SKILL(P_MARTIAL_ARTS) >= P_MASTER
        && !(u.usteed || u.uswallow || u.uinwater
             || multi < 0 || u.umortality > oldumort
             || !malive || m_at(x, y) != mon)) {
        if (weararmor && !is_robe(uarm)) {
            if (!rn2(8))
                Your("extra kick attack is ineffective while wearing %s.",
                      xname(weararmor));
        } else if (Wounded_legs) {
            /* note: taken from dokick.c */
            long wl = (EWounded_legs & BOTH_SIDES);
            const char *bp = body_part(LEG);

            if (wl == BOTH_SIDES)
                bp = makeplural(bp);
            if (!rn2(3))
                Your("%s%s %s in no shape for kicking.",
                     (wl == LEFT_SIDE) ? "left " : (wl == RIGHT_SIDE) ? "right " : "",
                     bp, (wl == BOTH_SIDES) ? "are" : "is");
        } else {
            tmp = find_roll_to_hit(mon, uattk->aatyp, uarmf, &attknum,
                                   &armorpenalty);
            dieroll = rnd(20);
            mhit = (tmp > dieroll || u.uswallow);
            /* kick passive counter-attack only occurs if kick attack hits,
               kick_monster() will prevent kick attack vs monsters that
               shouldn't be touched bare-skinned for races that can't wear
               boots */
            if (mhit && !DEADMONSTER(mon))
                kick_monster(mon, x, y);
        }
    }

    /* random shield bash if wearing a shield and are skilled
       in using shields */
    if (bash_chance
        && wearshield && P_SKILL(P_SHIELD) >= P_BASIC
        && !(multi < 0 || u.umortality > oldumort
             || u.uinwater || !malive || m_at(x, y) != mon)) {
        tmp = find_roll_to_hit(mon, uattk->aatyp, wearshield, &attknum,
                               &armorpenalty);
        dieroll = rnd(20);
        mhit = (tmp > dieroll || u.uswallow);
        malive = known_hitum(mon, wearshield, &mhit, tmp, armorpenalty, uattk,
                             dieroll);
        /* second passive counter-attack only occurs if second attack hits */
        if (mhit)
            (void) passive(mon, wearshield, mhit, malive, AT_WEAP, FALSE);
    }


    /* Your race may grant extra attacks. Illithids don't use
     * their tentacle attack every turn, Centaurs are strong
     * enough to not need their extra kick attack */
    if (!Upolyd && !(multi < 0 || u.umortality > oldumort
                     || !malive || m_at(x, y) != mon)) {
        int i;
        int race = (flags.female && urace.femalenum != NON_PM)
                    ? urace.femalenum : urace.malenum;
        struct attack *attacks = mons[race].mattk;
        if ((Race_if(PM_ILLITHID) && rn2(4))
            || Race_if(PM_CENTAUR))
            return 0;
        for (i = 0; i < NATTK; i++) {
            if (attacks[i].aatyp != AT_WEAP && attacks[i].aatyp != AT_NONE) {
                malive = hmonas(mon, race, FALSE);
                break;
            }
        }
    }

    return malive;
}

/* general "damage monster" routine; return True if mon still alive */
boolean
hmon(mon, obj, thrown, dieroll)
struct monst *mon;
struct obj *obj;
int thrown; /* HMON_xxx (0 => hand-to-hand, other => ranged) */
int dieroll;
{
    boolean result, anger_guards;

    anger_guards = (mon->mpeaceful
                    && (mon->ispriest || mon->isshk || is_watch(mon->data))
                    && (!(Role_if(PM_ROGUE) && context.forcefight && !Upolyd)));
    result = hmon_hitmon(mon, obj, thrown, dieroll);
    if (mon->ispriest && !rn2(2)
        && (!(Role_if(PM_ROGUE) && context.forcefight && !Upolyd)))
        ghod_hitsu(mon);
    if (anger_guards)
        (void) angry_guards(!!Deaf);
    return result;
}

static const char *const monkattacks[] = {
    "chop", "strike", "jab", "hit", "punch", "pummel", "backhand"
};

/* hallucinating monk */
static const char *const hmonkattacks[] = {
    "lightly caress", "tickle", "gently stroke", "massage", "snuggle"
};

/* piercing weapons */
static const char *const wep_pierce[] = {
    "pierce", "gore", "stab", "impale", "hit"
};

/* slashing weapons */
static const char *const wep_slash[] = {
    "strike", "slash", "cut", "lacerate", "hit"
};

/* blunt weapons and shields */
static const char *const wep_whack[] = {
    "smash", "whack", "bludgeon", "bash", "hit"
};

/* guts of hmon() */
STATIC_OVL boolean
hmon_hitmon(mon, obj, thrown, dieroll)
struct monst *mon;
struct obj *obj;
int thrown; /* HMON_xxx (0 => hand-to-hand, other => ranged) */
int dieroll;
{
    int tmp;
    struct permonst *mdat = mon->data;
    /* The basic reason we need all these booleans is that we don't want
     * a "hit" message when a monster dies, so we have to know how much
     * damage it did _before_ outputting a hit message, but any messages
     * associated with the damage don't come out until _after_ outputting
     * a hit message.
     */
    boolean hittxt = FALSE, destroyed = FALSE, already_killed = FALSE;
    boolean burnmsg = FALSE;
    boolean vapekilled = FALSE; /* WAC added boolean for vamps vaporize */
    boolean get_dmg_bonus = TRUE;
    boolean ispoisoned = FALSE, needpoismsg = FALSE, poiskilled = FALSE,
            unpoisonmsg = FALSE;
    boolean ispotion = FALSE;
    boolean lightobj = FALSE;
    boolean thievery = FALSE;
    boolean iskas = FALSE;
    boolean issecespita = FALSE;
    boolean isvenom  = FALSE;
    boolean valid_weapon_attack = FALSE;
    boolean unarmed = !uwep && (!uarm || is_robe(uarm)) && !uarms;
    boolean hand_to_hand = (thrown == HMON_MELEE
                            /* not grapnels; applied implies uwep */
                            || (thrown == HMON_APPLIED && is_pole(uwep)));
    int jousting = 0;
    int joustdmg;
    struct obj *hated_obj = NULL;
    int wtype;
    struct obj *monwep;
    char saved_oname[BUFSZ];
    int saved_mhp = mon->mhp;

    saved_oname[0] = '\0';

    /* Awaken nearby monsters. A stealthy hero makes much less noise */
    if (!(is_silent(youmonst.data) && helpless(mon))
        && rn2(Stealth ? 10 : 2) && !SuperStealth) {
        int base_combat_noise = combat_noise(&mons[urace.malenum]);
        wake_nearto(mon->mx, mon->my, Stealth ? base_combat_noise / 2
                                              : base_combat_noise);
    }

    wakeup(mon, TRUE);
    if (!obj) { /* attack with bare hands */
        if (noncorporeal(mdat)) {
            tmp = 0;
        } else {
            /* It's unfair to martial arts users that whenever they roll a natural
             * 1 on their hit, they get no bonuses and hit for just that one
             * point of damage.
             * Also grant this bonus to bare handed combat users. */
            valid_weapon_attack = TRUE;

            if (martial_bonus())
                tmp = rnd(4); /* bonus for martial arts */
            else
                tmp = rnd(2);
        }

        /* establish conditions for Rogue's special thieving skill */
        thievery = Role_if(PM_ROGUE) && context.forcefight && !Upolyd;

        /* don't increment thievery skill for regular bare handed attacks */
        if (!uwep && Role_if(PM_ROGUE) && !context.forcefight)
            use_skill(P_THIEVERY, -1);

        /* monks have a chance to break their opponents wielded weapon
         * under certain conditions */
        
        if (Role_if(PM_MONK) && unarmed 
              && P_SKILL(P_MARTIAL_ARTS) == P_GRAND_MASTER) {
            if (dieroll == 5
                && ((monwep = MON_WEP(mon)) != 0 && !is_flimsy(monwep)
                   && !is_mithril(monwep) && !is_crystal(monwep))
                && !obj_resists(
                    monwep, 50 + 15 * (greatest_erosion(monwep)), 100)) {
                setmnotwielded(mon, monwep);
                mon->weapon_check = NEED_WEAPON;
                You("%s your qi.  %s from the force of your blow!",
                    rn2(2) ? "channel" : "focus",
                    Yobjnam2(monwep, (monwep->material == WOOD
                                      || monwep->material == BONE)
                                         ? "splinter"
                                     : (monwep->material == PLATINUM
                                        || monwep->material == GOLD
                                        || monwep->material == SILVER
                                        || monwep->material == COPPER)
                                         ? "break"
                                         : "shatter"));
                m_useupall(mon, monwep);
                /* If someone just shattered MY weapon, I'd flee! */
                if (!rn2(4))
                    monflee(mon, d(2, 3), TRUE, TRUE);
                hittxt = TRUE;
            }
        }
        /* WAC - Hand-to-Hand Combat Techniques */
        if (tech_inuse(T_CHI_STRIKE) && u.uen > 0) {
            You("feel a surge of force.");
            tmp += (u.uen > (10 + (u.ulevel / 5)) ? 
                (10 + (u.ulevel / 5)) : u.uen);
            u.uen -= (10 + (u.ulevel / 5));
            if (u.uen < 0) 
                u.uen = 0;
        }
        if (tech_inuse(T_E_FIST)) {
            int dmgbonus = 0;
            hittxt = TRUE;
            dmgbonus = d(2, 4);
            switch (rn2(5)) {
            case 0: /* Fire */
                if (!Blind)
                    pline("%s is on fire!", Monnam(mon));
                dmgbonus += destroy_mitem(mon, SCROLL_CLASS, AD_FIRE);
                dmgbonus += destroy_mitem(mon, SPBOOK_CLASS, AD_FIRE);
                if (resists_fire(mon)) {
                    shieldeff(mon->mx, mon->my);
                    if (!Blind)
                        pline_The("fire doesn't heat %s!", mon_nam(mon));
                    golemeffects(mon, AD_FIRE, dmgbonus);
                    dmgbonus = 0;
                } else if (!rn2(20)) {
                    dmgbonus += rnd(6);
                }
                /* only potions damage resistant players in destroy_item */
                dmgbonus += destroy_mitem(mon, POTION_CLASS, AD_FIRE);
                damage_mon(mon, dmgbonus, AD_FIRE);
                break;
            case 1: /* Cold */
                if (!Blind)
                    pline("%s is covered in frost!", Monnam(mon));
                if (resists_cold(mon)) {
                    shieldeff(mon->mx, mon->my);
                    if (!Blind)
                        pline_The("frost doesn't chill %s!", mon_nam(mon));
                    golemeffects(mon, AD_COLD, dmgbonus);
                    dmgbonus = 0;
                } else if (!rn2(25)) {
                    dmgbonus += rnd(6);
                }
                dmgbonus += destroy_mitem(mon, POTION_CLASS, AD_COLD);
                damage_mon(mon, dmgbonus, AD_COLD);
                break;
            case 2: /* Elec */
                if (!Blind)
                    pline("%s is zapped!", Monnam(mon));

                dmgbonus += destroy_mitem(mon, WAND_CLASS, AD_ELEC);
                if (resists_elec(mon)) {
                    shieldeff(mon->mx, mon->my);
                    if (!Blind)
                        pline_The("zap doesn't shock %s!", mon_nam(mon));
                    golemeffects(mon, AD_ELEC, dmgbonus);
                    dmgbonus = 0;
                } else if (!rn2(100)) {
                    dmgbonus += rnd(20);
                    if (canseemon(mon))
                        pline("%s is jolted with electricity!", Monnam(mon));
                }
                /* only rings damage resistant players in destroy_item */
                dmgbonus += destroy_mitem(mon, RING_CLASS, AD_ELEC);
                damage_mon(mon, dmgbonus, AD_ELEC);
                break;
            case 3: /* Acid */
                if (!Blind)
                    pline("%s is covered in acid!", Monnam(mon));
                if (resists_acid(mon)) {
                    if (!Blind)
                        pline_The("acid doesn't burn %s!", Monnam(mon));
                    dmgbonus = 0;
                } else if (!rn2(100)) {
                    dmgbonus += rnd(20);
                    if (canseemon(mon))
                        pline("%s is severely burned!", Monnam(mon));
                }
                damage_mon(mon, dmgbonus, AD_ACID);
                break;
            case 4: /* Sonic */
                You("hit the %s with a loud bang!", Monnam(mon));
                if (resists_sonic(mon)) {
                    shieldeff(mon->mx, mon->my);
                    golemeffects(mon, AD_LOUD, dmgbonus);
                    dmgbonus = 0;
                } else if (!rn2(100)) {
                    pline("A sonic boom erupts from your fist!");
                    dmgbonus += d(2, 16);
                    if (canseemon(mon))
                        pline("%s is severely burned!", Monnam(mon));
                }
                dmgbonus += destroy_mitem(mon, RING_CLASS, AD_LOUD);
                dmgbonus += destroy_mitem(mon, WAND_CLASS, AD_LOUD);
                dmgbonus += destroy_mitem(mon, POTION_CLASS, AD_LOUD);
                damage_mon(mon, dmgbonus, AD_LOUD);
                break;
            }
        }/* Techinuse Elemental Fist */	
         
        /* fighting with fists will get the gloves' bonus... */
        if (!uwep && uarmg)
            tmp += uarmg->spe;
            
        
        /* Blessed gloves give bonuses when fighting 'bare-handed'.  So do
        rings or gloves made of a hated material.  Note:  rings are worn
        under gloves, so you don't get both bonuses, and two hated rings
        don't give double bonus. */
        tmp += special_dmgval(&youmonst, mon, (W_ARMG | W_RINGL | W_RINGR),
                              &hated_obj);
    } else {
        if (obj->oartifact == ART_SWORD_OF_KAS)
            iskas = TRUE;
        if (obj->oartifact == ART_SECESPITA)
            issecespita = TRUE;
        if (thrown && obj->oartifact == ART_HOUCHOU) {
            pline("There is a bright flash as %s hits %s.",
                  artiname(obj->oartifact), the(mon_nam(mon)));
        }
        if (obj->oprops & ITEM_VENOM)
            isvenom = TRUE;
        if (!(artifact_light(obj) && obj->lamplit))
            Strcpy(saved_oname, cxname(obj));
        else
            Strcpy(saved_oname, bare_artifactname(obj));
        if (obj->oclass == WEAPON_CLASS 
            || is_weptool(obj)
            || obj->oclass == GEM_CLASS 
            || obj->otyp == SPIKE
            || obj->otyp == HEAVY_IRON_BALL) {
            /* is it not a melee weapon? */
            if (/* if you strike with a bow... */
                is_launcher(obj)
                /* or strike with a missile in your hand... */
                || (!thrown && (is_missile(obj) || is_ammo(obj)))
                /* or melee with Houchou */
                || (!thrown && obj->oartifact == ART_HOUCHOU)
                /* or use a pole at short range and not mounted or not a centaur... */
                || (!thrown && !u.usteed 
                      && !Race_if(PM_CENTAUR) 
                      && is_pole(obj) 
                      && obj->otyp != SPIKED_CHAIN)
                /* or throw a missile without the proper bow... */
                || (is_ammo(obj) && (thrown != HMON_THROWN
                                     || !ammo_and_launcher(obj, uwep)))
                || (is_lightsaber(obj) && !obj->lamplit)) {
                /* then do only 1-2 points of damage */
                if (noncorporeal(mdat) && !shade_glare(obj))
                    tmp = 0;
                else
                    tmp = rnd(2);
                
                /* 5lo: Jedi know how to use an unlit lightsaber as a weapon */
                if (is_lightsaber(obj) && !obj->lamplit && Role_if(PM_JEDI)) {
                    tmp = d(1,4) + obj->spe + (P_SKILL(P_BARE_HANDED_COMBAT) - P_UNSKILLED);
                    use_skill(P_BARE_HANDED_COMBAT, 1);  /* throw them a bone */
                }
                
                if (mon_hates_material(mon, obj->material)) {
                    /* dmgval() already added bonus damage */
                    hated_obj = obj;
                }
                if (!thrown && obj == uwep && obj->otyp == BOOMERANG
                    && rnl(4) == 4 - 1) {
                    boolean more_than_1 = (obj->quan > 1L);

                    pline("As you hit %s, %s%s breaks into splinters.",
                          mon_nam(mon), more_than_1 ? "one of " : "",
                          yname(obj));
                    if (!more_than_1)
                        uwepgone(); /* set unweapon */
                    useup(obj);
                    if (!more_than_1)
                        obj = (struct obj *) 0;
                    hittxt = TRUE;
                    if (!noncorporeal(mdat))
                        tmp++;
                } else if (!thrown && obj == uwep && obj->otyp == CHAKRAM) {
                    if (!rn2(4))
                        Your("chakram is quite awkward to fight with.");
                    tmp = rnd(2);
                }
            } else {
                tmp = dmgval(obj, mon);
                /* Giants are more effective with club-like weapons.
                 * For example, a caveman's starting +1 club will do an average
                 * of 4.444 rather than 3 against large monsters, and 6.4725
                 * rather than 4.5 against small.
                 **/
                if (maybe_polyd(is_giant(youmonst.data), Race_if(PM_GIANT))
                    && objects[obj->otyp].oc_skill == P_CLUB
                    && !noncorporeal(mdat)) {
                    int tmp2 = dmgval(obj, mon);
                    if (tmp < tmp2)
                        tmp = tmp2;
                    tmp++;
                }
                if (tmp <= 1 || mon == u.ustuck
                    /* Cleaver can hit up to three targets at once so don't
                       let it also hit from behind or shatter foes' weapons */
                    || (hand_to_hand && obj->oartifact == ART_CLEAVER)) {
                    ; /* no special bonuses */
                }
                else if (mdat->mlet == S_GIANT && uslinging()
                         && thrown == HMON_THROWN
                         && ammo_and_launcher(obj, uwep)
                         && P_SKILL(P_SLING) >= P_SKILLED && dieroll == 1
                         && !rn2(P_SKILL(P_SLING) == P_SKILLED ? 2 : 1)) {
                    /* With a critical hit, a skilled slinger can bring down
                     * even the mightiest of giants. */
                    tmp = mon->mhp + 100;
                    pline("%s crushes %s forehead!", The(mshot_xname(obj)),
                          s_suffix(mon_nam(mon)));
                    hittxt = TRUE;
                    /* Same as above; account for negative udaminc and skill
                     * damage penalties. (In the really odd situation where for
                     * some reason being Skilled+ gives a penalty?) */
                    get_dmg_bonus = FALSE;
                    tmp -= weapon_dam_bonus(uwep);
                }
                
                else if ((Role_if(PM_ROGUE)
                                  /* Convict gets backstab with spoons */
                                  || (Role_if(PM_CONVICT) && uwep && uwep->otyp == SPOON))
                         && !Upolyd 
                         && hand_to_hand
                         /* multi-shot throwing is too powerful here */
                         && (mon->mflee
                             || !mon->mcanmove
                             || mon->mtrapped
                             || mon->mfrozen
                             || mon->msleeping
                             || mon->mstun
                             || mon->mconf
                             || mon->mblinded)) {
                    /* Cap the contribution of ulevel based on skill level.
                     * Restricted (or no associated skill) - d2 maximum
                     * Unskilled                           - d4 maximum
                     * Basic                               - d10 maximum
                     * Skilled                             - d20 maximum
                     * Expert                              - d30 maximum
                     * Then give Basic and above a small flat bonus that is not
                     * tied to ulevel.
                     */
                    int cap = 2, bonus = 0;
                    const char *adjective, *adverb1, *adverb2;
                    adjective = NULL; /* not "" because it goes to adj_monnam */
                    adverb1 = "deftly ";
                    adverb2 = "";
                    /* "You [adverb1] strike the [adjective] foo [adverb2]!" */
                    if (mon->msleeping) {
                        adjective = "sleeping";
                    }
                    else if (mon->mblinded) {
                        adjective = "blinded";
                    }
                    else if (mon->mconf || mon->mstun) {
                        adjective = "off-balance";
                    }
                    else if (mon->mtrapped || mon->mfrozen || !mon->mcanmove) {
                        adjective = "helpless";
                    }
                    else if (mon->mflee) {
                        adverb1 = "";
                        adverb2 = " from behind";
                    }
                    You("%sstrike %s%s!", adverb1, adj_monnam(mon, adjective),
                        adverb2);
                    if ((wtype = uwep_skill_type()) != P_NONE) {
                        if (P_SKILL(wtype) == P_ISRESTRICTED) {
                            cap = 2;
                        }
                        else if (P_SKILL(wtype) == P_UNSKILLED) {
                            cap = 4;
                        }
                        else {
                            cap = (P_SKILL(wtype) - 1) * 10;
                            bonus = P_SKILL(wtype) - 1;
                        }
                    }
                    if (u.ulevel < cap) {
                        cap = u.ulevel; /* e.g. Expert but only XL 10 */
                    }
                    tmp += rnd(cap) + bonus;
                    hittxt = TRUE;
                } else if (obj == uwep && obj->oclass == WEAPON_CLASS
                           && ((dieroll == 2 && (bimanual(obj) || (Race_if(PM_GIANT))
                                                 || (Role_if(PM_SAMURAI) && obj->otyp == KATANA && !uarms))
                                && ((wtype = uwep_skill_type()) != P_NONE
                                    && P_SKILL(wtype) >= P_SKILLED))
                               || (dieroll == 3 && (Race_if(PM_GIANT)) && (((wtype = uwep_skill_type()) != P_NONE)
                                                                           && P_SKILL(wtype) >= P_BASIC))
                               || (dieroll == 4 && (!rn2(2)) && (Race_if(PM_GIANT))
                                   && (((wtype = uwep_skill_type()) != P_NONE)
                                       && P_SKILL(wtype) >= P_EXPERT)))
                           && ((monwep = MON_WEP(mon)) != 0
                               && !is_flimsy(monwep)
                               && !is_mithril(monwep) /* mithril is super-strong */
                               && !is_crystal(monwep) /* so are weapons made of gemstone */
                               && !obj_resists(monwep,
                                               50 + 15 * (greatest_erosion(obj) - greatest_erosion(monwep)), 100))) {
                    /*
                     * 2.5% chance of shattering defender's weapon when
                     * using a two-handed weapon; less if uwep is rusted.
                     * [dieroll == 2 is most successful non-beheading or
                     * -bisecting hit, in case of special artifact damage;
                     * the percentage chance is (1/20)*(50/100).]
                     * If attacker's weapon is rustier than defender's,
                     * the obj_resists chance is increased so the shatter
                     * chance is decreased; if less rusty, then vice versa.
                     *
                     * Giants have a separate chance when dieroll = 3 or 4.
                     * Chance:      Giant   Other
                     * Unskilled    0%      0%
                     * Basic        2.5%    0%
                     * Skilled      5%      2.5%
                     * Expert       6.25%   2.5%
                     *
                     * Also not that bimanual() is false for all weapons
                     * for giants, which is why the dieroll = 2 conditional
                     * has an exception for giants.
                     */
                    setmnotwielded(mon, monwep);
                    mon->weapon_check = NEED_WEAPON;
                    pline("%s from the force of your blow!",
                          Yobjnam2(monwep, (monwep->material == WOOD || monwep->material == BONE)
                                           ? "splinter" : (monwep->material == PLATINUM || monwep->material == GOLD
                                                           || monwep->material == SILVER || monwep->material == COPPER)
                                                          ? "break" : "shatter"));
                    m_useupall(mon, monwep);
                    /* If someone just shattered MY weapon, I'd flee! */
                    if (!rn2(4))
                        monflee(mon, d(2, 3), TRUE, TRUE);
                    hittxt = TRUE;
                } else if (obj == uwep 
                           && (Role_if(PM_JEDI) && is_lightsaber(obj) && obj->lamplit)
                           && ((wtype = uwep_skill_type()) != P_NONE 
                               && P_SKILL(wtype) >= P_SKILLED) 
                           && ((monwep = MON_WEP(mon)) != 0 
                               && !is_lightsaber(monwep) 
                               && !monwep->oartifact
                               && !is_flimsy(monwep)
                               && !is_mithril(monwep) /* mithril is super-strong */
                               && !is_crystal(monwep) /* so are weapons made of gemstone */
                               && !obj_resists(monwep, 50 + 15 * greatest_erosion(obj), 100))) {
                    /* no cutting other lightsabers :) */
                    /* no cutting artifacts either */
                    setmnotwielded(mon,monwep);
                    mon->weapon_check = NEED_WEAPON;
                    Your("%s cuts %s %s in half!",
                         xname(obj),
                         s_suffix(mon_nam(mon)), xname(monwep));
                    m_useupall(mon, monwep);
                    /* If someone just shattered MY weapon, I'd flee! */
                    if (!rn2(4))
                        monflee(mon, d(2, 3), TRUE, TRUE);
                    hittxt = TRUE;
                }
                /* a minimal hit doesn't exercise proficiency */
                valid_weapon_attack = (tmp > 0);
                if (((obj->oclass == WEAPON_CLASS && obj->oprops) || obj->oartifact)
                    && artifact_hit(&youmonst, mon, obj, &tmp, dieroll)) {
                    if (DEADMONSTER(mon)) /* artifact killed monster */
                        return FALSE;
                    if (tmp == 0)
                        return TRUE;
                    hittxt = TRUE;
                }
                
                /* In SpliceHack, launchers contribute to damage. */
                if (uwep && ammo_and_launcher(obj, uwep) && obj->spe < uwep->spe) {
                    tmp += (uwep->spe - obj->spe);
                }
                
                /* handle the damages of special artifacts */
                if (obj->oartifact && obj->oartifact == ART_LUCKLESS_FOLLY) {
                    tmp -= 2 * Luck;
                }

                if (mon_hates_material(mon, obj->material)) {
                    /* dmgval() already added bonus damage */
                    hated_obj = obj;
                }
                if (artifact_light(obj) && obj->lamplit
                    && mon_hates_light(mon))
                    lightobj = TRUE;
                if ((u.usteed || Race_if(PM_CENTAUR)) && !thrown && tmp > 0
                    && weapon_type(obj) == P_LANCE && mon != u.ustuck) {
                    jousting = joust(mon, obj);
                    /* exercise skill even for minimal damage hits */
                    if (jousting)
                        valid_weapon_attack = TRUE;
                }
                if (thrown == HMON_THROWN
                    && (is_ammo(obj) || is_missile(obj))) {
                    if (ammo_and_launcher(obj, uwep)) {
                        /* Slings hit giants harder (as Goliath) */
                        if ((uwep->otyp == SLING) && racial_giant(mon))
                            tmp *= 2;
                        /* The Longbow of Diana and the Crossbow of Carl
                           impart a damage bonus to the ammo fired from
                           them */
                        if (uwep->oartifact == ART_LONGBOW_OF_DIANA
                            || uwep->oartifact == ART_CROSSBOW_OF_CARL)
                            tmp += rnd(6);
                        /* Elves and Samurai do extra damage using
                         * their bows&arrows; they're highly trained.
                         */
                        if (Role_if(PM_SAMURAI) && obj->otyp == YA
                            && uwep->otyp == YUMI)
                            tmp++;
                        else if (Race_if(PM_ELF) && obj->otyp == ELVEN_ARROW
                                 && uwep->otyp == ELVEN_BOW) {
                            tmp++;
                            /* WAC Extra damage if in special ability*/
						    if (tech_inuse(T_FLURRY)) 
                            tmp += 2;
                        } else if (objects[obj->otyp].oc_skill == P_BOW
					             && tech_inuse(T_FLURRY)) {
                            tmp++;
                        }
                    }
                }
                
                /* MRKR: Hitting with a lit torch */
                if ((obj->otyp == TORCH && obj->lamplit)
                    || obj->otyp == FLAMING_LASH) {
                    burnmsg = TRUE;
                }

                if (obj->opoisoned && is_poisonable(obj))
                    ispoisoned = TRUE;
                
                /* maybe break your glass weapon or monster's glass armor; put
                 * this at the end so that other stuff doesn't have to check obj
                 * && obj->whatever all the time */
                if (hand_to_hand) {
                    if (break_glass_obj(obj))
                        obj = (struct obj *) 0;
                    break_glass_obj(some_armor(mon));
                }
            }
        } else if (obj->oclass == POTION_CLASS) {
            if (obj->quan > 1L)
                obj = splitobj(obj, 1L);
            else
                setuwep((struct obj *) 0);
            freeinv(obj);
            potionhit(mon, obj,
                      hand_to_hand ? POTHIT_HERO_BASH : POTHIT_HERO_THROW);
            obj = NULL;
            if (DEADMONSTER(mon))
                return FALSE; /* killed */
            hittxt = TRUE;
            /* in case potion effect causes transformation */
            mdat = mon->data;
            tmp = (noncorporeal(mdat)) ? 0 : 1;
        } else {
            if (noncorporeal(mdat) && !shade_aware(obj)) {
                tmp = 0;
            } else {
                switch (obj->otyp) {
                case BOULDER:         /* 1d20 */
                case HEAVY_IRON_BALL: /* 1d25 */
                case IRON_CHAIN:      /* 1d4+1 */
                    tmp = dmgval(obj, mon);
                    if (mon_hates_material(mon, obj->material)) {
                        /* dmgval() already added damage, but track hated_obj */
                        hated_obj = obj;
                    }
                    break;
                case SMALL_SHIELD:
                case ELVEN_SHIELD:
                case URUK_HAI_SHIELD:
                case ORCISH_SHIELD:
                case LARGE_SHIELD:
                case TOWER_SHIELD:
                case DWARVISH_ROUNDSHIELD:
                case SHIELD_OF_REFLECTION:
                case SHIELD_OF_LIGHT:
                case SHIELD_OF_MOBILITY:
                case RESONANT_SHIELD:
                    if (uarms && P_SKILL(P_SHIELD) >= P_BASIC) {
                         /* dmgval for shields is just one point,
                            plus whatever material damage applies */
                        tmp = dmgval(obj, mon);

                        /* add extra damage based on the type
                           of shield */
                        if (obj->otyp == SMALL_SHIELD)
                            tmp += rn2(3) + 1;
                        else if  (obj->otyp == TOWER_SHIELD)
                            tmp += rn2(12) + 1;
                        else
                            tmp += rn2(6) + 2;

                        /* sprinkle on a bit more damage if
                           shield skill is high enough */
                        if (P_SKILL(P_SHIELD) >= P_EXPERT)
                            tmp += rnd(4);
                    }
                    if (mon_hates_material(mon, obj->material)) {
                        /* dmgval() already added damage, but track hated_obj */
                        hated_obj = obj;
                    }
                    break;
                case MIRROR:
                    if (breaktest(obj)) {
                        You("break %s.  That's bad luck!", ysimple_name(obj));
                        change_luck(-2);
                        useup(obj);
                        obj = (struct obj *) 0;
                        unarmed = FALSE; /* avoid obj==0 confusion */
                        get_dmg_bonus = FALSE;
                        hittxt = TRUE;
                    }
                    tmp = 1;
                    break;
                case EXPENSIVE_CAMERA:
                    You("succeed in destroying %s.  Congratulations!",
                        ysimple_name(obj));
                    release_camera_demon(obj, u.ux, u.uy);
                    useup(obj);
                    return TRUE;
                case CORPSE: /* fixed by polder@cs.vu.nl */
                    if (touch_petrifies(&mons[obj->corpsenm])) {
                        tmp = 1;
                        hittxt = TRUE;
                        You("hit %s with %s.", mon_nam(mon),
                            corpse_xname(obj, (const char *) 0,
                                         obj->dknown ? CXN_PFX_THE
                                                     : CXN_ARTICLE));
                        obj->dknown = 1;
                        if (resists_ston(mon) || defended(mon, AD_STON))
                            break;
			if (!mon->mstone) {
			    mon->mstone = 5;
			    mon->mstonebyu = TRUE;
			}
                        /* note: hp may be <= 0 even if munstoned==TRUE */
                        return (boolean) !DEADMONSTER(mon);
#if 0
                    } else if (touch_petrifies(mdat)) {
                        ; /* maybe turn the corpse into a statue? */
#endif
                    }
                    tmp = (obj->corpsenm >= LOW_PM ? mons[obj->corpsenm].msize
                                                   : 0) + 1;
                    break;

#define useup_eggs(o)                    \
    do {                                 \
        if (thrown)                      \
            obfree(o, (struct obj *) 0); \
        else                             \
            useupall(o);                 \
        o = (struct obj *) 0;            \
    } while (0) /* now gone */
                case EGG: {
                    long cnt = obj->quan;

                    tmp = 1; /* nominal physical damage */
                    get_dmg_bonus = FALSE;
                    hittxt = TRUE; /* message always given */
                    /* egg is always either used up or transformed, so next
                       hand-to-hand attack should yield a "bashing" mesg */
                    if (obj == uwep)
                        unweapon = TRUE;
                    if (obj->spe && obj->corpsenm >= LOW_PM) {
                        if (obj->quan < 5L)
                            change_luck((schar) - (obj->quan));
                        else
                            change_luck(-5);
                    }

                    if (touch_petrifies(&mons[obj->corpsenm])) {
                        /*learn_egg_type(obj->corpsenm);*/
                        pline("Splat!  You hit %s with %s %s egg%s!",
                              mon_nam(mon),
                              obj->known ? "the" : cnt > 1L ? "some" : "a",
                              obj->known ? mons[obj->corpsenm].mname
                                         : "petrifying",
                              plur(cnt));
                        obj->known = 1; /* (not much point...) */
                        useup_eggs(obj);
                        if (resists_ston(mon) || defended(mon, AD_STON))
                            break;
			if (!mon->mstone) {
			    mon->mstone = 5;
			    mon->mstonebyu = TRUE;
			}
                        return (boolean) (!DEADMONSTER(mon));
                    } else { /* ordinary egg(s) */
                        const char *eggp = (obj->corpsenm != NON_PM
                                            && obj->known)
                                           ? the(mons[obj->corpsenm].mname)
                                           : (cnt > 1L) ? "some" : "an";

                        You("hit %s with %s egg%s.", mon_nam(mon), eggp,
                            plur(cnt));
                        if (touch_petrifies(mdat) && !stale_egg(obj)) {
                            pline_The("egg%s %s alive any more...", plur(cnt),
                                      (cnt == 1L) ? "isn't" : "aren't");
                            if (obj->timed)
                                obj_stop_timers(obj);
                            obj->otyp = ROCK;
                            obj->oclass = GEM_CLASS;
                            obj->oartifact = 0;
                            obj->spe = 0;
                            obj->known = obj->dknown = obj->bknown = 0;
                            obj->owt = weight(obj);
                            if (thrown)
                                place_object(obj, mon->mx, mon->my);
                        } else {
                            pline("Splat!");
                            useup_eggs(obj);
                            exercise(A_WIS, FALSE);
                        }
                    }
                    break;
#undef useup_eggs
                }
                case CLOVE_OF_GARLIC: /* no effect against demons */
                    if (is_undead(mdat) || is_vampshifter(mon)) {
                        monflee(mon, d(2, 4), FALSE, TRUE);
                    }
                    tmp = 1;
                    break;
                case SPRIG_OF_CATNIP:
                    tmp = 0;
                    if (is_feline(mdat)) {
                        if (!Blind)
                            pline("%s chases %s tail!", Monnam(mon), mhis(mon));
                        (void) tamedog(mon, (struct obj *) 0);
                        mon->mconf = 1;
                        if (thrown)
                            obfree(obj, (struct obj *) 0);
                        else
                            useup(obj);
                        return FALSE;
                    } else {
                        You("%s catnip fly everywhere!", Blind ? "feel" : "see");
                        setmangry(mon, TRUE);
                    }
                    
                    if (thrown)
                        obfree(obj, (struct obj *) 0);
                    else
                        useup(obj);
                    hittxt = TRUE; get_dmg_bonus = FALSE;
                    break;
                case CREAM_PIE:
                case SNOWBALL:
                case BLINDING_VENOM:
                    mon->msleeping = 0;
                    if (can_blnd(&youmonst, mon,
                                 (uchar) (((obj->otyp == BLINDING_VENOM)
                                          || (obj->otyp == SNOWBALL))
                                           ? AT_SPIT : AT_WEAP), obj)) {
                        if (Blind) {
                            pline(obj->otyp == CREAM_PIE ? "Splat!"
                                  : obj->otyp == SNOWBALL ? "Thwap!"
                                                          : "Splash!");
                        } else if (obj->otyp == BLINDING_VENOM) {
                            pline_The("venom blinds %s%s!", mon_nam(mon),
                                      mon->mcansee ? "" : " further");
                        } else {
                            char *whom = mon_nam(mon);
                            char *what = The(xname(obj));

                            if (!thrown && obj->quan > 1L)
                                what = An(singular(obj, xname));
                            /* note: s_suffix returns a modifiable buffer */
                            if (haseyes(mdat)
                                && mdat != &mons[PM_FLOATING_EYE])
                                whom = strcat(strcat(s_suffix(whom), " "),
                                              mbodypart(mon, FACE));
                            pline("%s %s over %s!", what,
                                  vtense(what, (obj->otyp == SNOWBALL) ? "spread" : "splash"), whom);
                        }
                        setmangry(mon, TRUE);
                        mon->mcansee = 0;
                        tmp = rn1(25, 21);
                        if (((int) mon->mblinded + tmp) > 127)
                            mon->mblinded = 127;
                        else
                            mon->mblinded += tmp;
                    } else {
                        pline(obj->otyp == CREAM_PIE ? "Splat!"
                              : obj->otyp == SNOWBALL ? "Thwap!" : "Splash!");
                        setmangry(mon, TRUE);
                    }
                    tmp = (obj->otyp == SNOWBALL) ? d(2, 4) : 0;
                    if (thrown)
                        obfree(obj, (struct obj *) 0);
                    else
                        useup(obj);
                    hittxt = TRUE;
                    get_dmg_bonus = FALSE;
                    break;
                case ACID_VENOM: /* thrown (or spit) */
                    if (resists_acid(mon) || defended(mon, AD_ACID)) {
                        Your("venom hits %s harmlessly.", mon_nam(mon));
                        tmp = 0;
                    } else {
                        Your("venom burns %s!", mon_nam(mon));
                        tmp = dmgval(obj, mon);
                    }
                    if (thrown)
                        obfree(obj, (struct obj *) 0);
                    else
                        useup(obj);
                    hittxt = TRUE;
                    get_dmg_bonus = FALSE;
                    break;
                default:
                    /* non-weapons can damage because of their weight */
                    /* (but not too much) */
                    tmp = obj->owt / 100;
                    if (is_wet_towel(obj)) {
                        /* wielded wet towel should probably use whip skill
                           (but not by setting objects[TOWEL].oc_skill==P_WHIP
                           because that would turn towel into a weptool) */
                        tmp += obj->spe;
                        if (rn2(obj->spe + 1)) /* usually lose some wetness */
                            dry_a_towel(obj, -1, TRUE);
                    }
                    if (tmp < 1)
                        tmp = 1;
                    else
                        tmp = rnd(tmp);
                    if (tmp > 6)
                        tmp = 6;
                    /*
                     * Things like wands made of harmful materials can arrive here so
                     * so we need another check for that.
                     */
                    if (mon_hates_material(mon, obj->material)) {
                        /* dmgval() already added damage, but track hated_obj */
                        hated_obj = obj;
                    }
                }
            }
        }
    }

    /****** NOTE: perhaps obj is undefined!! (if !thrown && BOOMERANG)
     *      *OR* if attacking bare-handed!! */

    if (get_dmg_bonus && tmp > 0) {
        tmp += u.udaminc;
        /* If you throw using a propellor, you don't get a strength
         * bonus but you do get an increase-damage bonus.
         */
        if (thrown != HMON_THROWN || !obj || !uwep
            || !ammo_and_launcher(obj, uwep) || uslinging())
            tmp += dbon();
    }

    /* wearing red dragon-scaled armor gives the wearer
       a slight damage boost */
    if (uarm && Is_dragon_scaled_armor(uarm)
        && Dragon_armor_to_scales(uarm) == RED_DRAGON_SCALES)
	tmp += rnd(6);

    /* wielding The Sword of Kas against Vecna does
       triple damage. Has to be wielded in primary hand */
    if (uwep && uwep->oartifact == ART_SWORD_OF_KAS
        && mon && mdat == &mons[PM_VECNA])
        tmp *= 3;

    /* Even though the Sword of Kas is attuned to Vecna,
       it does extra harm to creatures that draw their
       power from him also */
    if (uwep && uwep->oartifact == ART_SWORD_OF_KAS && mon
        && (mdat == &mons[PM_LICH] || mdat == &mons[PM_DEMILICH]
            || mdat == &mons[PM_MASTER_LICH] || mdat == &mons[PM_ARCH_LICH]
            || mdat == &mons[PM_ALHOON]))
        tmp *= 2;

    /* Wooden stakes vs vampires */
    if (uwep && uwep->otyp == WOODEN_STAKE && is_vampire(mdat)) {
        int skill = P_SKILL(weapon_type(uwep));

        if (Role_if(PM_UNDEAD_SLAYER))
            skill += 1;
        if (uwep->oartifact == ART_STAKE_OF_VAN_HELSING)
            skill += 2;
        if (P_SKILL(weapon_type(obj)) >= P_BASIC) {
            /* Scale instakill rate with skill level
             * Basic = 2, Skilled = 3, Expert = 4
             * Being Undead Slayer +1
             * Using Stake of Van Helsing is +1
             */
            if (!rn2(10 - skill)) {
                You("plunge your stake into the heart of %s.", mon_nam(mon));
                vapekilled = TRUE;
            } else {
                You("drive your stake into %s.", mon_nam(mon));
                /* Scale the dmg bonus with skill level */
                tmp += rnd(6) + skill;
            }
            hittxt = TRUE;
        } else {
            You("thrust your stake into %s.", mon_nam(mon));
            tmp += rnd(6);
            hittxt = TRUE;
        }
    }

    /*
     * Ki special ability, see cmd.c in function special_ability.
     * In this case, we do twice damage! Wow!
     *
     * Berserk special ability only does +4 damage. - SW
     * 5lo: Berserk time gets extended with every active hit
     * hackem: Kii also gets extended.
     */
    /* Lycanthrope claws do +level bare hands dmg
            (multi-hit, stun/freeze)..- WAC*/

    if (tech_inuse(T_KIII)) {
        tmp *= 2;
        extend_tech_time(T_KIII, rnd(4));
    }
    if (tech_inuse(T_BERSERK)) {
        tmp += 4;
        extend_tech_time(T_BERSERK, rnd(4));
    }
    if (tech_inuse(T_SOULEATER)) {
        tmp += d((u.ulevel / 4), 8);
        /* Unholy damage, not ignored from fire resistance */
        pline("Dark flames envelop %s!", mon_nam(mon));
        hittxt = TRUE;
    }
    if (tech_inuse(T_EVISCERATE)) {
        tmp += rnd((int) (u.ulevel/2 + 1)) + (u.ulevel/2); /* [max] was only + u.ulevel */
        You("slash %s!", mon_nam(mon));
        hittxt = TRUE;
    }

    if (valid_weapon_attack) {
        struct obj *wep;

        /* to be valid a projectile must have had the correct projector */
        wep = PROJECTILE(obj) ? uwep : obj;
        if (weapon_type(uwep) == P_FIREARM && !matching_firearm(obj, uwep)) {
            tmp = rnd(2);
        } else {
            tmp += weapon_dam_bonus(wep);
            /* [this assumes that `!thrown' implies wielded...] */
            wtype = thrown ? weapon_type(wep) : uwep_skill_type();
            use_skill(wtype, 1);
        }
    }
    
    if (dbl_dmg()) {
        tmp *= 2;
    }

    if (!ispotion && obj /* potion obj will have been freed by here */
        && (ispoisoned || iskas || isvenom)) {
        int nopoison = (10 - (obj->owt / 10));

        if (nopoison < 2)
            nopoison = 2;
        if (Role_if(PM_SAMURAI)) {
            You("dishonorably use a poisoned weapon!");
            adjalign(-sgn(u.ualign.type));
        } else if (u.ualign.type == A_LAWFUL && u.ualign.record > -10) {
            You_feel("like an evil coward for using a poisoned weapon.");
            adjalign(Role_if(PM_KNIGHT) ? -3 : -1);
        }
        if (obj && !rn2(nopoison)
            && !iskas && !isvenom
            && !is_bullet(obj)) {
            /* remove poison now in case obj ends up in a bones file */
            obj->opoisoned = FALSE;
            /* defer "obj is no longer poisoned" until after hit message */
            unpoisonmsg = TRUE;
        }
        if (resists_poison(mon))
            needpoismsg = TRUE;
        else if (rn2(10))
            tmp += rnd(6);
        else
            poiskilled = TRUE;
    }
    if (tmp < 1) {
        /* make sure that negative damage adjustment can't result
           in inadvertently boosting the victim's hit points */
        tmp = (get_dmg_bonus && !noncorporeal(mdat)) ? 1 : 0;
        if (noncorporeal(mdat) && !hittxt
            && thrown != HMON_THROWN && thrown != HMON_KICKED)
            hittxt = shade_miss(&youmonst, mon, obj, FALSE, TRUE);
    }

    if (jousting && !unsolid(mdat)) {
        joustdmg = 5 + (u.ulevel / 3);
        tmp += d(2, (obj == uwep) ? joustdmg : 2); /* [was in dmgval()] */
        You("joust %s%s", mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
        if (jousting < 0) {
            pline("%s shatters on impact!", Yname2(obj));
            /* (must be either primary or secondary weapon to get here) */
            u.twoweap = FALSE; /* untwoweapon() is too verbose here */
            if (obj == uwep)
                uwepgone(); /* set unweapon */
            /* minor side-effect: broken lance won't split puddings */
            useup(obj);
            obj = 0;
        }
        if (mhurtle_to_doom(mon, tmp, &mdat, TRUE))
            already_killed = TRUE;
        hittxt = TRUE;
    } else if (unarmed && tmp > 1 && !thrown && !obj && !Upolyd && !thievery) {
        /* VERY small chance of stunning or confusing opponent if unarmed. */
        if ((rnd(Race_if(PM_GIANT) ? 40 : 100) < P_SKILL(P_BARE_HANDED_COMBAT)
                || (rnd(25) && uarmg && uarmg->otyp == BOXING_GLOVES))
              && !biggermonst(mdat) 
              && !thick_skinned(mdat) 
              && !unsolid(mdat)) {
            if (rn2(2)) {
                if (canspotmon(mon))
                    pline("%s %s from your powerful strike!", Monnam(mon),
                          makeplural(stagger(mdat, "stagger")));
                if (mhurtle_to_doom(mon, tmp, &mdat, FALSE))
                    already_killed = TRUE;
            } else if (!mindless(mon->data)) {
                if (canspotmon(mon))
                    Your("forceful blow knocks %s senseless!", mon_nam(mon));
                /* avoid migrating a dead monster */
                if (mon->mhp > tmp) {
                    mon->mconf = 1;
                    mdat = mon->data; /* in case of a polymorph trap */
                    if (DEADMONSTER(mon))
                        already_killed = TRUE;
                }
            }
            hittxt = TRUE;
        }
    }

    if (thievery) {
        if (!do_pickpocket(mon))
            return 0;
        hittxt = TRUE;
    } else if (!already_killed)
        damage_mon(mon, tmp, AD_PHYS);
    /* adjustments might have made tmp become less than what
       a level draining artifact has already done to max HP */
    if (mon->mhp > mon->mhpmax)
        mon->mhp = mon->mhpmax;
    if (DEADMONSTER(mon))
        destroyed = TRUE;
    if (mon->mtame && tmp > 0) {
        /* do this even if the pet is being killed (affects revival) */
        abuse_dog(mon); /* reduces tameness */
        /* flee if still alive and still tame; if already suffering from
           untimed fleeing, no effect, otherwise increases timed fleeing */
        if (mon->mtame && !destroyed)
            monflee(mon, 10 * rnd(tmp), FALSE, FALSE);
    }
    if ((mdat == &mons[PM_BLACK_PUDDING] || mdat == &mons[PM_BROWN_PUDDING])
        /* pudding is alive and healthy enough to split */
        && mon->mhp > 1 && !mon->mcan
        /* iron weapon using melee or polearm hit [3.6.1: metal weapon too;
           also allow either or both weapons to cause split when twoweap] */
        && obj && (obj == uwep || (u.twoweap && obj == uswapwep))
        && ((!ispotion /* potion obj will have been freed by here */
            && (obj->material == IRON
                /* allow scalpel and tsurugi to split puddings */
                || obj->material == METAL))
            /* but not bashing with darts, arrows or ya */
            && !(is_ammo(obj) || is_missile(obj)))
        && hand_to_hand) {
        struct monst *mclone;
        if ((mclone = clone_mon(mon, 0, 0)) != 0) {
            char withwhat[BUFSZ];

            withwhat[0] = '\0';
            if (u.twoweap && flags.verbose)
                Sprintf(withwhat, " with %s", yname(obj));
            pline("%s divides as you hit it%s!", Monnam(mon), withwhat);
            hittxt = TRUE;
            mintrap(mclone);
        }
    }

    if (!hittxt /*( thrown => obj exists )*/
        && (!destroyed
            || (thrown && m_shot.n > 1 && m_shot.o == obj->otyp))) {
        if (thrown)
            hit(mshot_xname(obj), mon, exclam(tmp));
        else if (!flags.verbose)
            You("hit it.");
        else if (!uwep && Role_if(PM_MONK) && !Upolyd) {
            if (!Blind && Hallucination) {
                You("%s %s%s", hmonkattacks[rn2(SIZE(hmonkattacks))],
                    mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
            } else
                You("%s %s%s", monkattacks[rn2(SIZE(monkattacks))],
                    mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
        } else if (Role_if(PM_BARBARIAN)) {
            /* smite me, oh mighty smiter! */
            You("smite %s%s", mon_nam(mon),
                canseemon(mon) ? exclam(tmp) : ".");
        } else if (!uwep && (has_claws(youmonst.data)
                             || has_claws_undead(youmonst.data)
                             || (Race_if(PM_DEMON) && !Upolyd)
                             || (Race_if(PM_TORTLE) && !Upolyd)
                             || (Race_if(PM_ILLITHID) && !Upolyd))) {
            You("claw %s%s", mon_nam(mon),
                canseemon(mon) ? exclam(tmp) : ".");
        } else {
            if (obj && (obj == uarms) && is_shield(obj)) {
                You("bash %s with %s%s",
                    mon_nam(mon), ysimple_name(obj),
                    canseemon(mon) ? exclam(tmp) : ".");
                /* placing this here, because order of events */
                if (!rn2(10) && P_SKILL(P_SHIELD) >= P_EXPERT) {
                    if (canspotmon(mon))
                        pline("%s %s from the force of your blow!",
                              Monnam(mon), makeplural(stagger(mdat, "stagger")));
                    mon->mstun = 1;
                }
            } else {
                You("%s %s%s",
                    (obj && (is_shield(obj) || is_whack(obj)))
                    ? wep_whack[rn2(SIZE(wep_whack))] : (obj && is_pierce(obj))
                        ? wep_pierce[rn2(SIZE(wep_pierce))] : (obj && is_slash(obj))
                            ? wep_slash[rn2(SIZE(wep_slash))] : "hit",
                    mon_nam(mon), canseemon(mon) ? exclam(tmp) : ".");
           }
        }
    }

    if (burnmsg) {
        /* Torch's fire damage is handled here (unfortunately) */
        if (!Blind) {
            if (can_vaporize(mdat))
                Your("%svaporizes part of %s.", xname(obj), mon_nam(mon));
            else
                pline("%s is on fire!", Monnam(mon));
        } 

        if (completelyburns(mdat)) { /* paper golem or straw golem */
            if (!Blind)
                pline("%s burns completely!", Monnam(mon));
            else
                You("smell burning%s.",
                    (mdat == &mons[PM_PAPER_GOLEM]) 
                    ? " paper" : (   mdat == &mons[PM_STRAW_GOLEM]) 
                    ? " straw" : "");
            xkilled(mon, XKILL_NOMSG | XKILL_NOCORPSE);
            tmp = 0;
            /* Don't return yet; keep hp<1 and tmp=0 for pet msg */
        }
        else {
            tmp += rnd(6);
            if (resists_cold(mon))
                tmp *= 2;

            /* A chance of setting monster's stuff on fire */
            tmp += destroy_mitem(mon, SCROLL_CLASS, AD_FIRE);
            tmp += destroy_mitem(mon, SPBOOK_CLASS, AD_FIRE);
            tmp += destroy_mitem(mon, WEAPON_CLASS, AD_FIRE);
            
            if (resists_fire(mon) || defended(mon, AD_FIRE)) {
                if (!Blind)
                    pline_The("fire doesn't heat %s!", mon_nam(mon));
                golemeffects(mon, AD_FIRE, tmp);
                shieldeff(mon->mx, mon->my);
                tmp = 0;
            }
            tmp += destroy_mitem(mon, POTION_CLASS, AD_FIRE);

            if (obj->otyp == TORCH) {
                if (mdat == &mons[PM_WATER_ELEMENTAL]) {
                    Your("%s goes out.", xname(obj));
                    end_burn(obj, TRUE);
                } else {
                    burn_faster(obj); /* Use up the torch more quickly */
                }
            }
        }
    }

    /* The Hand of Vecna imparts cold damage to attacks,
       whether bare-handed or wielding a weapon */
    if (!destroyed && uarmg
        && uarmg->oartifact == ART_HAND_OF_VECNA && hand_to_hand) {
        if (!Blind)
            pline("%s covers %s in frost!", The(xname(uarmg)),
                  mon_nam(mon));
        if (resists_cold(mon) || defended(mon, AD_COLD)) {
            shieldeff(mon->mx, mon->my);
            if (!Blind)
                pline_The("frost doesn't chill %s!", mon_nam(mon));
            golemeffects(mon, AD_COLD, tmp);
            tmp = 0;
        } else {
            tmp += destroy_mitem(mon, POTION_CLASS, AD_COLD);
            tmp += rnd(5) + 7; /* 8-12 hit points of cold damage */
        }
    }

    if (hated_obj)
        searmsg(&youmonst, mon, hated_obj, FALSE);

    if (lightobj) {
        const char *fmt;
        char *whom = mon_nam(mon);
        char emitlightobjbuf[BUFSZ];

        if (canspotmon(mon)) {
            if (saved_oname[0]) {
                Sprintf(emitlightobjbuf,
                        "%s radiance penetrates deep into",
                        s_suffix(saved_oname));
                Strcat(emitlightobjbuf, " %s!");
                fmt = emitlightobjbuf;
            } else
                fmt = "The light sears %s!";
        } else {
            *whom = highc(*whom); /* "it" -> "It" */
            fmt = "%s is seared!";
        }
        /* note: s_suffix returns a modifiable buffer */
        if (!noncorporeal(mdat) && !amorphous(mdat))
            whom = strcat(s_suffix(whom), " flesh");
        pline(fmt, whom);
    }
    /* Weapons have a chance to id after a certain number of kills with
       them. The more powerful a weapon, the lower this chance is. This
       way, there is uncertainty about when a weapon will ID, but spoiled
       players can make an educated guess. */
    if (destroyed && (obj == uwep) && uwep
        && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))
        && !uwep->known) {
        uwep->wep_kills++;
        if (uwep->wep_kills > KILL_FAMILIARITY
            && !rn2(max(2, uwep->spe) && !uwep->known)) {
            You("have become quite familiar with %s.",
                yobjnam(uwep, (char *) 0));
            uwep->known = TRUE;
            update_inventory();
        }
    }
    /* if a "no longer poisoned" message is coming, it will be last;
       obj->opoisoned was cleared above and any message referring to
       "poisoned <obj>" has now been given; we want just "<obj>" for
       last message, so reformat while obj is still accessible */
    if (unpoisonmsg)
        Strcpy(saved_oname, cxname(obj));

    /* [note: thrown obj might go away during killed()/xkilled() call
       (via 'thrownobj'; if swallowed, it gets added to engulfer's
       minvent and might merge with a stack that's already there)] */

    if (needpoismsg) {
        pline_The("poison doesn't seem to affect %s.", mon_nam(mon));
        if (obj && (obj->oprops & ITEM_VENOM))
            obj->oprops_known |= ITEM_VENOM;
    }
    if (poiskilled) {
        pline_The("poison was deadly...");
        if (!already_killed)
            xkilled(mon, XKILL_NOMSG);
        destroyed = TRUE; /* return FALSE; */
        if (obj && (obj->oprops & ITEM_VENOM))
            obj->oprops_known |= ITEM_VENOM;
    } else if (vapekilled) {
        if (cansee(mon->mx, mon->my))
            pline("%s%ss body vaporizes!", Monnam(mon),
                  canseemon(mon) ? "'" : "");
        if (!already_killed)
            xkilled(mon, XKILL_NOMSG);
    } else if (destroyed) {
        if (!already_killed)
            killed(mon); /* takes care of most messages */
    } else if (u.umconf && hand_to_hand) {
        nohandglow(mon);
        if (!mon->mconf && !resist(mon, SPBOOK_CLASS, 0, NOTELL)) {
            mon->mconf = 1;
            if (!mon->mstun && mon->mcanmove && !mon->msleeping
                && canseemon(mon))
                pline("%s appears confused.", Monnam(mon));
        }
    }
    if (DEADMONSTER(mon) && !ispotion && obj /* potion obj will have been freed by here */
        && (obj == uwep || (u.twoweap && obj == uswapwep)) && issecespita
        && !nonliving(mdat) && u.uen < u.uenmax) {
        int energy = mon->m_lev + 1;
        energy += rn2(energy);
        pline_The("ritual knife captures the evanescent life force.");
        u.uen += energy;
        if (u.uen > u.uenmax)
            u.uen = u.uenmax;
        context.botl = TRUE;
    }

    if (unpoisonmsg) {
        Your("%s %s no longer poisoned.", saved_oname,
             vtense(saved_oname, "are"));
        update_inventory();
    }
    
    if (!destroyed) {
        print_mon_wounded(mon, saved_mhp);
    }

#if 0
    /* debug pline to verify damage dealt from whatever
       object hits its target */
    if (wizard)
        pline("Damage from %s: %d.", simpleonames(obj), tmp);
#endif

    return destroyed ? FALSE : TRUE;
}

STATIC_OVL boolean
shade_aware(obj)
struct obj *obj;
{
    if (!obj)
        return FALSE;
    /*
     * The things in this list either
     * 1) affect shades.
     *  OR
     * 2) are dealt with properly by other routines
     *    when it comes to shades.
     */
    if (obj->otyp == BOULDER
        || obj->otyp == HEAVY_IRON_BALL
        || obj->otyp == IRON_CHAIN      /* dmgval handles those first three */
        || obj->otyp == MIRROR          /* silver in the reflective surface */
        || obj->otyp == CLOVE_OF_GARLIC /* causes shades to flee */
        || obj->material == SILVER
        || obj->material == BONE)
        return TRUE;
    return FALSE;
}

/* joust or martial arts punch is knocking the target back; that might
   kill 'mon' (via trap) before known_hitum() has a chance to do so;
   return True if we kill mon, False otherwise */
boolean
mhurtle_to_doom(mon, tmp, mptr, by_wielded_weapon)
struct monst *mon;
int tmp;
struct permonst **mptr;
boolean by_wielded_weapon;
{
    /* if this hit is breaking the never-hit-with-wielded-weapon conduct
       (handled by caller's caller...) we need to log the message about
       that before mon is killed; without this, the log entry sequence
        N : killed for the first time
        N : hit with a wielded weapon for the first time
       reported on the same turn (N) looks "suboptimal";
       u.uconduct.weaphit has already been incremented => 1 is first hit */
    if (by_wielded_weapon && u.uconduct.weaphit <= 1)
        livelog_write_string(LL_CONDUCT, "hit with a wielded weapon for the first time");

    /* only hurtle if pending physical damage (tmp) isn't going to kill mon */
    if (tmp < mon->mhp) {
        mhurtle(mon, u.dx, u.dy, 1);
        /* update caller's cached mon->data in case mon was pushed into
           a polymorph trap or is a vampshifter whose current form has
           been killed by a trap so that it reverted to original form */
        *mptr = mon->data;
        if (DEADMONSTER(mon))
            return TRUE;
    }
    return FALSE; /* mon isn't dead yet */
}

/* used for hero vs monster and monster vs monster; also handles
   monster vs hero but that won't happen because hero can't be a shade */
boolean
shade_miss(magr, mdef, obj, thrown, verbose)
struct monst *magr, *mdef;
struct obj *obj;
boolean thrown, verbose;
{
    boolean youagr = (magr == &youmonst), youdef = (mdef == &youmonst);

    /* we're using dmgval() for zero/not-zero, not for actual damage amount */
    if (!noncorporeal(mdef->data)
        || (obj && (dmgval(obj, mdef) || shade_glare(obj))))
        return FALSE;

    if (verbose
        && ((youdef || cansee(mdef->mx, mdef->my) || sensemon(mdef))
            || (magr == &youmonst && distu(mdef->mx, mdef->my) <= 2))) {
        static const char harmlessly_thru[] = " harmlessly through ";
        const char *what = (!obj ? "attack" : cxname(obj)),
                   *target = youdef ? "you" : mon_nam(mdef);

        if (!thrown) {
            const char *whose = youagr ? "Your" : s_suffix(Monnam(magr));
            pline("%s %s %s%s%s.", whose, what,
                  vtense(what, "pass"), harmlessly_thru, target);
        } else {
            pline("%s %s%s%s.", The(what), /* note: not pline_The() */
                  vtense(what, "pass"), harmlessly_thru, target);
        }
        if (!youdef && !canspotmon(mdef))
            map_invisible(mdef->mx, mdef->my);
    }
    if (!youdef)
        mdef->msleeping = 0;
    return TRUE;
}

/* check whether slippery clothing protects from hug or wrap attack */
/* [currently assumes that you are the attacker] */
boolean
m_slips_free(mdef, mattk)
struct monst *mdef;
struct attack *mattk;
{
    struct obj *obj;

    if (mattk->adtyp == AD_DRIN
        || (mattk->aatyp == AT_TENT && mattk->adtyp == AD_WRAP)) {
        /* intelligence drain attacks the head */
        if (mdef->data->mlet == S_QUADRUPED || mdef->data->mlet == S_DRAGON
            || mdef->data->mlet == S_UNICORN || mdef->data == &mons[PM_WARG]
            || mdef->data == &mons[PM_JABBERWOCK] || mdef->data == &mons[PM_KI_RIN]
            || mdef->data == &mons[PM_ELDRITCH_KI_RIN])
            obj = which_armor(mdef, W_BARDING);
        else
            obj = which_armor(mdef, W_ARMH);
    } else {
        /* grabbing attacks the body */
        obj = which_armor(mdef, W_ARMC); /* cloak */
        if (!obj)
            obj = which_armor(mdef, W_ARM); /* suit */
        if (!obj)
            obj = which_armor(mdef, W_ARMU); /* shirt */
    }

    /* if monster's cloak/armor is greased, the grab slips off; this
       protection might fail (33% chance) when the armor is cursed.
       Grammar has been altered to accommodate both player vs monster
       and monster vs monster attacks */
    if (obj && (obj->greased || obj->otyp == OILSKIN_CLOAK
                || (obj->oprops & ITEM_OILSKIN))
        && (!obj->cursed || rn2(3))) {
        pline_The("%s %s %s %s!",
                  ((mattk->adtyp == AD_WRAP
                    && youmonst.data != &mons[PM_SALAMANDER])
                   || (mattk->adtyp == AD_DRIN && is_zombie(youmonst.data)))
                     ? "attack slips off of"
                     : "attack cannot hold onto",
                  s_suffix(mon_nam(mdef)), obj->greased ? "greased" : "slippery",
                  /* avoid "slippery slippery cloak"
                     for undiscovered oilskin cloak */
                  (obj->greased || objects[obj->otyp].oc_name_known)
                      ? xname(obj)
                      : cloak_simple_name(obj));

        if (obj->greased && !rn2(2)) {
            pline_The("grease wears off.");
            obj->greased = 0;
        }
        return TRUE;
    }
    return FALSE;
}

/* used when hitting a monster with a lance while mounted;
   1: joust hit; 0: ordinary hit; -1: joust but break lance */
STATIC_OVL int
joust(mon, obj)
struct monst *mon; /* target */
struct obj *obj;   /* weapon */
{
    int skill_rating, joust_dieroll;

    if (Fumbling || Stunned)
        return 0;
    /* sanity check; lance must be wielded in order to joust */
    if (obj != uwep && (obj != uswapwep || !u.twoweap))
        return 0;

    /* if using two weapons, use worse of lance and two-weapon skills */
    skill_rating = P_SKILL(weapon_type(obj)); /* lance skill */
    if (u.twoweap && P_SKILL(P_TWO_WEAPON_COMBAT) < skill_rating)
        skill_rating = P_SKILL(P_TWO_WEAPON_COMBAT);
    if (skill_rating == P_ISRESTRICTED)
        skill_rating = P_UNSKILLED; /* 0=>1 */

    /* odds to joust are expert:80%, skilled:60%, basic:40%, unskilled:20% */
    if ((joust_dieroll = rn2(5)) < skill_rating) {
        if (joust_dieroll == 0 && rnl(50) == (50 - 1) && !unsolid(mon->data)
            && !obj_resists(obj, 0, 100))
            return -1; /* hit that breaks lance */
        return 1;      /* successful joust */
    }
    return 0; /* no joust bonus; revert to ordinary attack */
}

/*
 * Send in a demon pet for the hero.  Exercise wisdom.
 *
 * This function used to be inline to damageum(), but the Metrowerks compiler
 * (DR4 and DR4.5) screws up with an internal error 5 "Expression Too
 * Complex."
 * Pulling it out makes it work.
 */
void
demonpet()
{
    int i;
    struct permonst *pm;
    struct monst *dtmp;

    /* crowned infidels shouldn't get virtually unlimited pets */
    if (!Upolyd && Race_if(PM_DEMON))
        return;

    pline("Some hell-p has arrived!");
    i = !rn2(6) ? ndemon(u.ualign.type) : NON_PM;
    pm = i != NON_PM ? &mons[i] : youmonst.data;
    if ((dtmp = makemon(pm, u.ux, u.uy, NO_MM_FLAGS)) != 0)
        (void) tamedog(dtmp, (struct obj *) 0);
    exercise(A_WIS, TRUE);
}

STATIC_OVL boolean
theft_petrifies(otmp)
struct obj *otmp;
{
    if (uarmg || otmp->otyp != CORPSE
        || !touch_petrifies(&mons[otmp->corpsenm]) || Stone_resistance)
        return FALSE;

#if 0   /* no poly_when_stoned() critter has theft capability */
    if (poly_when_stoned(youmonst.data) && polymon(PM_STONE_GOLEM)) {
        display_nhwindow(WIN_MESSAGE, FALSE);   /* --More-- */
        return TRUE;
    }
#endif

    /* stealing this corpse is fatal... */
    instapetrify(corpse_xname(otmp, "stolen", CXN_ARTICLE));
    /* apparently wasn't fatal after all... */
    return TRUE;
}

/*
 * Player uses theft attack against monster.
 *
 * If the target is wearing body armor, take all of its possessions;
 * otherwise, take one object.  [Is this really the behavior we want?]
 */
void
steal_it(mdef, mattk)
struct monst *mdef;
struct attack *mattk;
{
    struct obj *otmp, *gold = 0, *stealoid, **minvent_ptr;
    int i = rn2(10), dex_pick = 0, no_vis = 0,
        size = 0, enc = 0, other = 0, cap;
    boolean as_mon = could_seduce(&youmonst, mdef, mattk);

    otmp = mdef->minvent;
    if (!otmp || (as_mon && otmp->oclass == COIN_CLASS && !otmp->nobj))
        return; /* nothing to take */

    /* look for worn body armor */
    stealoid = (struct obj *) 0;
    if (as_mon) {
        /* find armor, and move it to end of inventory in the process */
        minvent_ptr = &mdef->minvent;
        while ((otmp = *minvent_ptr) != 0)
            if (otmp->owornmask & W_ARM) {
                if (stealoid)
                    panic("steal_it: multiple worn suits");
                *minvent_ptr = otmp->nobj; /* take armor out of minvent */
                stealoid = otmp;
                stealoid->nobj = (struct obj *) 0;
            } else {
                minvent_ptr = &otmp->nobj;
            }
        *minvent_ptr = stealoid; /* put armor back into minvent */
        gold = findgold(mdef->minvent, TRUE);
    }

    if (stealoid) { /* we will be taking everything */
        if (gender(mdef) == (int) u.mfemale && youmonst.data->mlet == S_NYMPH)
            You("charm %s.  She gladly hands over %sher possessions.",
                mon_nam(mdef), !gold ? "" : "most of ");
        else
            You("seduce %s and %s starts to take off %s clothes.",
                mon_nam(mdef), mhe(mdef), mhis(mdef));
    }

    /* prevent gold from being stolen so that steal-item isn't a superset
       of steal-gold; shuffling it out of minvent before selecting next
       item, and then back in case hero or monster dies (hero touching
       stolen c'trice corpse or monster wielding one and having gloves
       stolen) is less bookkeeping than skipping it within the loop or
       taking it out once and then trying to figure out how to put it back */
    if (as_mon && gold)
        obj_extract_self(gold);

    /* Rogue uses the thievery skill; dexterity directly
       affects how successful a pickpocketing attempt
       will be */
    if (ACURR(A_DEX) <= 6)
        dex_pick += 3;
    else if (ACURR(A_DEX) <= 9)
        dex_pick += 2;
    else if (ACURR(A_DEX) <= 15)
        dex_pick -= 0;
    else if (ACURR(A_DEX) <= 18)
        dex_pick -= 1;
    else if (ACURR(A_DEX) <= 21)
        dex_pick -= 2;
    else if (ACURR(A_DEX) <= 24)
        dex_pick -= 3;
    else if (ACURR(A_DEX) == 25)
        dex_pick -= 4;

    /* bonus if the target can't see the thief */
    if (!m_canseeu(mdef))
        no_vis -= 2;

    /* slight bonus if the thief is small and
       its target is much bigger */
    if ((Race_if(PM_GNOME) || Race_if(PM_HOBBIT))
        && mdef->data->msize >= MZ_LARGE)
        size -= 1;

    /* being encumbered adversely affects
       pickpocketing success rate, to the point
       where it becomes impossible if the level
       of encumbrance is too high */
    if ((cap = near_capacity()) > UNENCUMBERED) {
        switch (cap) {
        case SLT_ENCUMBER:
            enc += 2;
            break; /* burdened */
        case MOD_ENCUMBER:
            enc += 6;
            break; /* stressed */
        case HVY_ENCUMBER:
            enc += 10;
            break; /* strained */
        case EXT_ENCUMBER:
        case OVERLOADED:
            enc += 20;
            break; /* overtaxed/overloaded:
                      can't 'fight' when this overburdened */
        }
    }

    /* other conditions that could affect success, and these
       can stack if multiple conditions are met */
    if (mdef->mfrozen) /* target is immobile or incapacitated */
        other -= 5;
    if (mdef->mconf || mdef->mstun) /* target can't think straight */
        other -= 3;
    if (uarms) /* wearing a shield */
        other += 2;
    if (uarm && is_heavy_metallic(uarm)) /* wearing bulky body armor */
        other += 3;
    if (Wounded_legs) /* hard to move deftly */
        other += 5;
    if (u.usteed) /* steed can hamper stealth */
        other += 5;
    if (!canspotmon(mdef)) /* can't see/sense target */
        other += 6;
    if (Glib) /* slippery fingers */
        other += 7;
    if (Fumbling) /* difficult motor control */
        other += 10;
    if (Confusion || Stunned) /* hard to do much anything if impaired */
        other += 20;

    /* failure routine */
    if (!Upolyd
        && ((i + dex_pick + no_vis + size + enc + other) > P_SKILL(P_THIEVERY))) {
        if (Confusion || Stunned) {
            You("are in no shape to %s anything.",
                rn2(2) ? "pickpocket" : "steal");
        } else {
            Your("attempt to %s %s %s.",
                 rn2(2) ? "pickpocket" : "steal from",
                 mon_nam(mdef), rn2(2) ? "failed" : "was unsuccessful");
            if (!rn2(10) && P_SKILL(P_THIEVERY) == P_BASIC)
                You("could use more practice at pickpocketing.");
            /* There's a chance a monster being pickpocketed will notice.
             * As expected, they're not too happy about it. A good bit
             * of this comes from setmangry()
             */
            if (mdef->mpeaceful) {
                if (rnd(6) > P_SKILL(P_THIEVERY)) {
                    mdef->mpeaceful = 0;
                    if (mdef->ispriest) {
                        if (p_coaligned(mdef)) {
                            You_feel("guilty.");
                            adjalign(-5); /* very bad */
                        } else {
                            adjalign(2);
                        }
                    }
                    if (couldsee(mdef->mx, mdef->my)) {
                        if (humanoid(mdef->data) || mdef->isshk || mdef->isgd)
                            pline("%s notices your pickpocketing attempt and gets angry!",
                                  Monnam(mdef));
                        else if (flags.verbose && !Deaf)
                            growl(mdef);
                    }
                    /* stealing from your own quest leader will anger his or her guardians */
                    if (!context.mon_moving /* should always be the case here */
                        && mdef->data == &mons[quest_info(MS_LEADER)]) {
                        struct monst *mon;
                        struct permonst *q_guardian = &mons[quest_info(MS_GUARDIAN)];
                        int got_mad = 0;

                        /* guardians will sense this theft even if they can't see it */
                        for (mon = fmon; mon; mon = mon->nmon) {
                            if (DEADMONSTER(mon))
                                continue;
                            if (mon->data == q_guardian && mon->mpeaceful) {
                                mon->mpeaceful = 0;
                                if (canseemon(mon))
                                    ++got_mad;
                            }
                        }
                        if (got_mad && !Hallucination) {
                            const char *who = q_guardian->mname;

                            if (got_mad > 1)
                                who = makeplural(who);
                            pline_The("%s %s to be angry too...",
                                      who, vtense(who, "appear"));
                        }
                    }
                    /* make the watch react */
                    if (!context.mon_moving) {
                        struct monst *mon;

                        for (mon = fmon; mon; mon = mon->nmon) {
                            if (DEADMONSTER(mon))
                                continue;
                            if (mon == mdef) /* the mpeaceful test catches this since mtmp */
                                continue;    /* is no longer peaceful, but be explicit...  */

                            if (humanoid(mon->data) || mon->isshk || mon->ispriest) {
                                if (is_watch(mon->data)
                                    && mon->mcansee && m_canseeu(mon)) {
                                    verbalize("Halt!  You're under arrest!");
                                    (void) angry_guards(!!Deaf);
                                }
                            }
                        }
                    }
                }
            }
        }
        /* don't increment the skill if the attempt isn't successful */
        use_skill(P_THIEVERY, -1);
        return;
    }

    while ((otmp = mdef->minvent) != 0) {
        if (gold) /* put 'mdef's gold back after remembering mdef->minvent */
            mpickobj(mdef, gold), gold = 0;
        if (otmp == stealoid) /* special message for final item */
            pline("%s finishes taking off %s suit.", Monnam(mdef),
                    mhis(mdef));
        if (!(otmp = really_steal(otmp, mdef))) /* hero got interrupted... */
            break;
        /* might have dropped otmp, and it might have broken or left level */
        if (!otmp || otmp->where != OBJ_INVENT)
            continue;
        if (theft_petrifies(otmp))
            break; /* stop thieving even though hero survived */

        if (!stealoid)
            break; /* only taking one item */
        if (!Upolyd)
            break; /* no longer have ability to steal */

        /* take gold out of minvent before making next selection; if it
           is the only thing left, the loop will terminate and it will be
           put back below */
        if (as_mon && (gold = findgold(mdef->minvent, TRUE)) != 0)
            obj_extract_self(gold);
    }

    /* put gold back; won't happen if either hero or 'mdef' dies because
       gold will be back in monster's inventory at either of those times
       (so will be present in mdef's minvent for bones, or in its statue
       now if it has just been turned into one) */
    if (gold)
        mpickobj(mdef, gold);

    /* Training up thievery skill can be quite the chore, to the point
       where many players don't bother to use it. Randomly add an
       additional point of skill for a successful attempt to balance
       out the drudgery, without having to revamp how skill points are
       awarded for this specific skill */
    if (rn2(3)) /* 66% chance in favor of the player */
        use_skill(P_THIEVERY, 1);
}

/* Actual mechanics of stealing obj from mdef. This is now its own function
 * because player-as-leprechaun can steal gold items, including gold weapons and
 * armor, etc.
 * Assumes caller handles whatever other messages are necessary; this takes care
 * of the "You steal e - an imaginary widget" message.
 * Returns the resulting object pointer (could have changed, since item may
 * have merged with something in inventory), or null pointer if the player has
 * done something that should interrupt multi-stealing, such as stealing a
 * cockatrice corpse and getting petrified, but then getting lifesaved.*/
STATIC_OVL struct obj *
really_steal(obj, mdef)
struct obj *obj;
struct monst *mdef;
{
    long unwornmask = obj->owornmask;
    /* take the object away from the monster */
    extract_from_minvent(mdef, obj, TRUE, FALSE);
    /* give the object to the character */
    obj = hold_another_object(obj, "You snatched but dropped %s.",
                              doname(obj), "You steal: ");
    /* might have dropped obj, and it might have broken or left level */
    if (!obj || obj->where != OBJ_INVENT)
        return (struct obj *) 0;
    if (theft_petrifies(obj))
        return (struct obj *) 0; /* stop thieving even though hero survived */
    /* more take-away handling, after theft message */
    if (unwornmask & W_WEP) { /* stole wielded weapon */
        possibly_unwield(mdef, FALSE);
    } else if (unwornmask & W_ARMG) { /* stole worn gloves */
        mselftouch(mdef, (const char *) 0, TRUE);
        if (DEADMONSTER(mdef)) /* it's now a statue */
            return (struct obj *) 0; /* can't continue stealing */
    }
    return obj;
}

int
damageum(mdef, mattk, specialdmg)
register struct monst *mdef;
register struct attack *mattk;
int specialdmg; /* blessed and/or silver bonus against various things */
{
    register struct permonst *pd = mdef->data;
    int armpro, tmp = d((int) mattk->damn, (int) mattk->damd);
    boolean negated;
    struct obj *mongold;
    boolean mon_vorpal_wield = (MON_WEP(mdef)
                                && MON_WEP(mdef)->oartifact == ART_VORPAL_BLADE);

    armpro = magic_negation(mdef);
    /* since hero can't be cancelled, only defender's armor applies */
    negated = !(rn2(10) >= 3 * armpro);

    if (maybe_polyd(is_demon(youmonst.data), Race_if(PM_DEMON))
        && !rn2(13) && !uwep
        && u.umonnum != PM_SUCCUBUS && u.umonnum != PM_INCUBUS
        && u.umonnum != PM_BALROG && u.umonnum != PM_DEMON) {
        demonpet();
        return 0;
    }

    if (!mdef)
        return 0;

    switch (mattk->adtyp) {
    case AD_LARV: {
        struct monst* mtmp;
        /* since hero can't be cancelled, only defender's armor applies */
        negated = !(rn2(10) >= 3 * armpro);
        if (!negated 
              && !thick_skinned(mdef->data) 
              && mdef->mhp < 5 
              && !rn2(4)) {
            tmp = mdef->mhp;
            pline("%s burst out of %s!",
                Hallucination ? rndmonnam(NULL) : "larvae",
                mon_nam(mdef));
            mtmp = makemon(&mons[big_to_little(u.umonnum)],
                u.ux, u.uy, MM_EDOG);

            initedog(mtmp);
        }
        break;
    }
    case AD_STUN:
        if (!Blind)
            pline("%s %s for a moment.", Monnam(mdef),
                  makeplural(stagger(pd, "stagger")));
        mdef->mstun = 1;
        goto physical;
    case AD_LEGS:
#if 0
        if (u.ucancelled) {
            tmp = 0;
            break;
        }
#endif
        goto physical;
    case AD_BHED:
        if (!rn2(15) || is_jabberwock(mdef->data)) {
            if (!has_head(mdef->data) || mon_vorpal_wield) {
                if (canseemon(mdef))
                    pline("Somehow, you miss %s wildly.", mon_nam(mdef));
                tmp = 0;
                break;
            }
            if (noncorporeal(mdef->data) || amorphous(mdef->data)) {
                if (canseemon(mdef))
                    You("slice through %s %s.",
                        s_suffix(mon_nam(mdef)), mbodypart(mdef, NECK));
                goto physical;
            }
            if (mdef->data == &mons[PM_CERBERUS]) {
                You("remove one of %s heads!",
                    s_suffix(mon_nam(mdef)));
                if (canseemon(mdef))
                    You("watch in horror as it quickly grows back.");
                tmp = rn2(15) + 10;
                goto physical;
            }
            if (canseemon(mdef))
                You("%s %s!",
                    rn2(2) ? "behead" : "decapitate", mon_nam(mdef));
            if (is_zombie(mdef->data)
                || (is_troll(mdef->data) && !mlifesaver(mdef)))
                mdef->mcan = 1; /* no head? no reviving */
            xkilled(mdef, 0);
            tmp = 0;
            break;
        }
        break;
    case AD_WERE: /* no special effect on monsters */
    case AD_HEAL: /* likewise */
    case AD_PHYS:
 physical:
        if (noncorporeal(pd)) {
            tmp = 0;
            if (!specialdmg)
                impossible("bad shade attack function flow?");
        }
        tmp += specialdmg;

        if (mattk->aatyp == AT_WEAP) {
            /* hmonas() uses known_hitum() to deal physical damage,
               then also damageum() for non-AD_PHYS; don't inflict
               extra physical damage for unusual damage types */
            tmp = 0;
        } else if (mattk->aatyp == AT_KICK
                   || mattk->aatyp == AT_CLAW
                   || mattk->aatyp == AT_TUCH
                   || mattk->aatyp == AT_HUGS) {
            if (thick_skinned(pd))
                tmp = (mattk->aatyp == AT_KICK) ? 0 : (tmp + 1) / 2;
            /* add ring(s) of increase damage */
            if (u.udaminc > 0) {
                /* applies even if damage was 0 */
                tmp += u.udaminc;
            } else if (tmp > 0) {
                /* ring(s) might be negative; avoid converting
                   0 to non-0 or positive to non-positive */
                tmp += u.udaminc;
                if (tmp < 1)
                    tmp = 1;
            }
        }
	if (youmonst.data == &mons[PM_WATER_ELEMENTAL]
            || youmonst.data == &mons[PM_BABY_SEA_DRAGON]
            || youmonst.data == &mons[PM_SEA_DRAGON])
	    goto do_rust;
        break;
    case AD_FIRE:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s is %s!", Monnam(mdef),
                  on_fire(mdef, mattk->aatyp == AT_HUGS ? ON_FIRE_HUG : ON_FIRE));
        if (completelyburns(pd)) { /* paper golem or straw golem */
            if (!Blind)
                pline("%s burns completely!", Monnam(mdef));
            else
                You("smell burning%s.",
                    (pd == &mons[PM_PAPER_GOLEM]) ? " paper"
                      : (pd == &mons[PM_STRAW_GOLEM]) ? " straw" : "");
            xkilled(mdef, XKILL_NOMSG | XKILL_NOCORPSE);
            tmp = 0;
            break;
            /* Don't return yet; keep hp<1 and tmp=0 for pet msg */
        }
        
        if (!mon_underwater(mdef)) {
            tmp += destroy_mitem(mdef, SCROLL_CLASS, AD_FIRE);
            tmp += destroy_mitem(mdef, SPBOOK_CLASS, AD_FIRE);
            tmp += destroy_mitem(mdef, WEAPON_CLASS, AD_FIRE);
        }
        if (resists_fire(mdef) || defended(mdef, AD_FIRE)
            || mon_underwater(mdef)) {
            if (!Blind)
                pline_The("fire doesn't heat %s!", mon_nam(mdef));
            golemeffects(mdef, AD_FIRE, tmp);
            shieldeff(mdef->mx, mdef->my);
            tmp = 0;
        }
        /* only potions damage resistant players in destroy_item */
        tmp += destroy_mitem(mdef, POTION_CLASS, AD_FIRE);
        break;
    case AD_PSYC:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind) {
            You("mentally assault %s!", mon_nam(mdef));
            if (mindless(mdef->data)) {
                shieldeff(mdef->mx, mdef->my);
                tmp = 0;
                pline("%s has no mind, and is immune to your onslaught.",
                      Monnam(mdef));
            } else if (resists_psychic(mdef) || defended(mdef, AD_PSYC)) {
                shieldeff(mdef->mx, mdef->my);
                tmp = 0;
                pline("%s resists your mental assault!", Monnam(mdef));
            } else {
                mdef->mconf = 1;
            }
        }
        break;
    case AD_COLD:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s is covered in frost!", Monnam(mdef));
        if (resists_cold(mdef) || defended(mdef, AD_COLD)) {
            shieldeff(mdef->mx, mdef->my);
            if (!Blind)
                pline_The("frost doesn't chill %s!", mon_nam(mdef));
            golemeffects(mdef, AD_COLD, tmp);
            tmp = 0;
        }
        tmp += destroy_mitem(mdef, POTION_CLASS, AD_COLD);
        break;
    /* currently the only monster that uses AD_LOUD is
     * the Nazgul, and they are M2_NOPOLY, but we'll put this
     * here for completeness sake. we may add other creatures
     * that can use this damage type at some point in the future */
    case AD_LOUD:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Deaf && !Blind)
            pline("%s reels from the noise!", Monnam(mdef));
        if (!rn2(6))
            erode_armor(mdef, ERODE_FRACTURE);
        tmp += destroy_mitem(mdef, RING_CLASS, AD_LOUD);
        tmp += destroy_mitem(mdef, TOOL_CLASS, AD_LOUD);
        tmp += destroy_mitem(mdef, WAND_CLASS, AD_LOUD);
        tmp += destroy_mitem(mdef, POTION_CLASS, AD_LOUD);
        if (pd == &mons[PM_GLASS_GOLEM]) {
            if (canseemon(mdef))
                pline("%s shatters into a million pieces!", Monnam(mdef));
            xkilled(mdef, XKILL_NOMSG | XKILL_NOCORPSE);
            tmp = 0;
            break;
        }
        break;
    case AD_PIER:
        /* Mobat piercing screech attack */
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Deaf && !Blind)
            pline("%s reels from the noise!", Monnam(mdef));

        if (!Blind)
            pline("%s %s for a moment.", Monnam(mdef),
                  makeplural(stagger(pd, "stagger")));
        mdef->mstun = 1;
        break;
    case AD_ELEC:
        if (negated) {
            tmp = 0;
            break;
        }
        if (!Blind)
            pline("%s is zapped!", Monnam(mdef));
        tmp += destroy_mitem(mdef, WAND_CLASS, AD_ELEC);
        if (resists_elec(mdef) || defended(mdef, AD_ELEC)) {
            if (!Blind)
                pline_The("zap doesn't shock %s!", mon_nam(mdef));
            golemeffects(mdef, AD_ELEC, tmp);
            shieldeff(mdef->mx, mdef->my);
            tmp = 0;
        }
        /* only rings damage resistant players in destroy_item */
        tmp += destroy_mitem(mdef, RING_CLASS, AD_ELEC);
        break;
    case AD_ACID:
        if (resists_acid(mdef) || defended(mdef, AD_ACID)
            || mon_underwater(mdef))
            tmp = 0;
        break;
    case AD_STON:
        if (!munstone(mdef, TRUE))
            minstapetrify(mdef, TRUE);
        tmp = 0;
        break;
    case AD_SSEX:
    case AD_SEDU:
    case AD_SITM:
        steal_it(mdef, mattk);
        tmp = 0;
        break;
    case AD_SGLD:
        /* This you as a leprechaun, so steal
           real gold only, no lesser coins */
        mongold = findgold(mdef->minvent, FALSE);
        if (mongold) {
            if (mongold->otyp != GOLD_PIECE) {
                /* stole a gold non-coin object */
                (void) really_steal(mongold, mdef);
            }
            else if (merge_choice(invent, mongold) || inv_cnt(FALSE) < 52) {
                Your("purse feels heavier.");
                obj_extract_self(mongold);
                addinv(mongold);
            } else {
                You("grab %s's gold, but find no room in your knapsack.",
                    mon_nam(mdef));
                obj_extract_self(mongold);
                dropy(mongold);
            }
        }
        exercise(A_DEX, TRUE);
        tmp = 0;
        break;
    case AD_TLPT:
        if (tmp <= 0)
            tmp = 1;
        if (!negated) {
            char nambuf[BUFSZ];
            boolean u_saw_mon = (canseemon(mdef)
                                 || (u.uswallow && u.ustuck == mdef));

            /* record the name before losing sight of monster */
            Strcpy(nambuf, Monnam(mdef));
            if (u_teleport_mon(mdef, FALSE) && u_saw_mon
                && !(canseemon(mdef) || (u.uswallow && u.ustuck == mdef)))
                pline("%s suddenly disappears!", nambuf);
            if (tmp >= mdef->mhp) { /* see hitmu(mhitu.c) */
                if (mdef->mhp == 1)
                    ++mdef->mhp;
                tmp = mdef->mhp - 1;
            }
        }
        break;
    case AD_BLND:
        if (can_blnd(&youmonst, mdef, mattk->aatyp, (struct obj *) 0)) {
            if (!Blind && mdef->mcansee)
                pline("%s is blinded.", Monnam(mdef));
            mdef->mcansee = 0;
            tmp += mdef->mblinded;
            if (tmp > 127)
                tmp = 127;
            mdef->mblinded = tmp;
        }
        tmp = 0;
        break;
    case AD_CURS:
        if (!rn2(10) && !mdef->mcan) {
            if (pd == &mons[PM_CLAY_GOLEM]) {
                if (!Blind)
                    pline("Some writing vanishes from %s head!",
                          s_suffix(mon_nam(mdef)));
                xkilled(mdef, XKILL_NOMSG);
                /* Don't return yet; keep hp<1 and tmp=0 for pet msg */
            } else if (youmonst.data == &mons[PM_GREMLIN]
                       || youmonst.data == &mons[PM_LAVA_GREMLIN]) {
                mdef->mcan = 1;
                You("chuckle.");
            }
        }
        tmp = 0;
        break;
    case AD_DRLI:
        if (!negated && (rnd(5) <= 2)
            && !(resists_drli(mdef) || defended(mdef, AD_DRLI))) {
            int xtmp = d(2, 6);
            
            if (mdef->mhp < xtmp) 
                xtmp = mdef->mhp;
            /* Player vampires are smart enough not to feed while
             * biting if they might have trouble getting it down 
             */
            if (maybe_polyd(is_vampiric(youmonst.data),
                Race_if(PM_VAMPIRIC)) && u.uhunger <= 1420 &&
                mattk->aatyp == AT_BITE && has_blood(pd)) {
                /* For the life of a creature is in the blood (Lev 17:11) */
                if (flags.verbose)
                    You("feed on the lifeblood.");
                /* [ALI] Biting monsters does not count against eating 
                 * conducts. The draining of life is considered to be 
                 * primarily a non-physical effect */
                lesshungry(xtmp * 6);
                /* Feeding increases HP */
                if (Upolyd) {
                    u.mh += xtmp * 2;
                    if (u.mh > u.mhmax)
                        u.mh = u.mhmax;
                } else {
                    u.uhp += xtmp * 2;
                    if (u.uhp > u.uhpmax)
                        u.uhp = u.uhpmax;
                }
            }
            
            if (canseemon(mdef))
                pline("%s suddenly seems weaker!", Monnam(mdef));
            mdef->mhpmax -= xtmp;
            damage_mon(mdef, xtmp, AD_DRLI);
            /* !m_lev: level 0 monster is killed regardless of hit points
               rather than drop to level -1 */
            if (DEADMONSTER(mdef) || !mdef->m_lev) {
                pline("%s dies!", Monnam(mdef));
                xkilled(mdef, XKILL_NOMSG);
            } else
                mdef->m_lev--;
            tmp = 0;
        }
        break;
    case AD_RUST:
do_rust:
        if (pd == &mons[PM_IRON_GOLEM] || pd == &mons[PM_STEEL_GOLEM]) {
            if (canseemon(mdef))
                pline("%s falls to pieces!", Monnam(mdef));
            xkilled(mdef, XKILL_NOMSG);
        }
        erode_armor(mdef, ERODE_RUST);
        tmp = 0;
        break;
    case AD_CORR:
        erode_armor(mdef, ERODE_CORRODE);
        tmp = 0;
        break;
    case AD_DCAY:
        if (pd == &mons[PM_PAPER_GOLEM]
            || pd == &mons[PM_WOOD_GOLEM] || pd == &mons[PM_LEATHER_GOLEM]) {
            if (canseemon(mdef))
                pline("%s falls to pieces!", Monnam(mdef));
            xkilled(mdef, XKILL_NOMSG);
        }
        erode_armor(mdef, ERODE_ROT);
        tmp = 0;
        break;
    case AD_DREN:
        if (!negated && !rn2(4))
            xdrainenergym(mdef, TRUE);
        tmp = 0;
        break;
    case AD_DRST:
    case AD_DRDX:
    case AD_DRCO:
        if (!negated && !rn2(8)) {
            Your("%s was poisoned!", mpoisons_subj(&youmonst, mattk));
            if (resists_poison(mdef)) {
                pline_The("poison doesn't seem to affect %s.", mon_nam(mdef));
            } else {
                if (!rn2(10)) {
                    Your("poison was deadly...");
                    tmp = mdef->mhp;
                } else
                    tmp += rn1(10, 6);
            }
        }
        break;
    case AD_DISE:
        if (!(resists_sick(pd) || defended(mdef, AD_DISE))) {
            if (mdef->mdiseasetime)
                mdef->mdiseasetime -= rnd(3);
            else
                mdef->mdiseasetime = rn1(9, 6);
            if (canseemon(mdef))
                pline("%s looks %s.", Monnam(mdef),
                      mdef->mdiseased ? "even worse" : "diseased");
            mdef->mdiseased = 1;
            mdef->mdiseabyu = TRUE;
        }
        break;
    case AD_DRIN: {
        struct obj *helmet;
        struct obj *barding;

        if (is_zombie(youmonst.data) && rn2(5)) {
            if (!(resists_sick(pd) || defended(mdef, AD_DISE))) {
                if (mdef->msicktime)
                    mdef->msicktime -= rnd(3);
                else
                    mdef->msicktime = rn1(9, 6);
                if (canseemon(mdef))
                    pline("%s looks %s.", Monnam(mdef),
                          mdef->msick ? "much worse" : "rather ill");
                mdef->msick = (can_become_zombie(r_data(mdef))) ? 3 : 1;
                mdef->msickbyu = TRUE;
            }
            break;
        }

        if (notonhead || !has_head(pd)) {
            if (canseemon(mdef))
                pline("%s doesn't seem harmed.", Monnam(mdef));
            tmp = 0;
            if (!Unchanging && pd == &mons[PM_GREEN_SLIME]) {
                if (!Slimed) {
                    You("suck in some slime and don't feel very well.");
                    make_slimed(10L, (char *) 0);
                }
            }
            break;
        }
        if (m_slips_free(mdef, mattk))
            break;

        
        if ((helmet = which_armor(mdef, W_ARMH)) != 0 && (rn2(8) ||
              which_armor(mdef, W_ARMH)->otyp == TINFOIL_HAT)) {
            pline("%s %s blocks your attack to %s head.",
                  s_suffix(Monnam(mdef)), helm_simple_name(helmet),
                  mhis(mdef));
            break;
        }

        if ((barding = which_armor(mdef, W_BARDING)) != 0 && rn2(8)) {
            if (!Blind)
                pline("%s barding blocks your attack to %s %s.",
                      s_suffix(Monnam(mdef)), mhis(mdef),
                      mbodypart(mdef, HEAD));
            break;
        }

        (void) eat_brains(&youmonst, mdef, TRUE, &tmp);
        break;
    }
    case AD_STCK:
        if (!negated && !sticks(pd))
            u.ustuck = mdef; /* it's now stuck to you */
        break;
    case AD_WRAP:
        if (!sticks(pd)) {
            if (!u.ustuck && !rn2(10)) {
                if (m_slips_free(mdef, mattk)) {
                    tmp = 0;
                } else {
                    if (youmonst.data == &mons[PM_MIND_FLAYER_LARVA])
                        You("wrap your tentacles around %s %s, attaching yourself to its %s!",
                            s_suffix(mon_nam(mdef)), mbodypart(mdef, HEAD),
                            mbodypart(mdef, FACE));
                    else
                        You("%s %s!",
                            youmonst.data == &mons[PM_GIANT_CENTIPEDE]
                            ? "coil yourself around"
                                : youmonst.data == &mons[PM_SALAMANDER]
                                    ? "wrap your arms around" : "swing yourself around",
                            mon_nam(mdef));
                    u.ustuck = mdef;
                }
            } else if (u.ustuck == mdef) {
                /* Monsters don't wear amulets of magical breathing */
                if (is_pool(u.ux, u.uy) && !is_swimmer(pd)
                    && !amphibious(pd)
                    && youmonst.data != &mons[PM_MIND_FLAYER_LARVA]) {
                    You("drown %s...", mon_nam(mdef));
                    tmp = mdef->mhp;
                } else if (is_lava(u.ux, u.uy)
                           && !likes_fire(mdef->data)) {
                    You("pull %s into the lava...", mon_nam(mdef));
                    tmp = 0;
                    xkilled(mdef, XKILL_GIVEMSG | XKILL_NOCORPSE);
                    break;
                } else if (mattk->aatyp == AT_HUGS) {
                    pline("%s is being crushed.", Monnam(mdef));
                } else if (mattk->aatyp == AT_TENT) {
                    if (racial_illithid(mdef)) {
                        You("sense %s is kin, and release your grip.",
                            mon_nam(mdef));
                        u.ustuck = 0;
                        tmp = 0;
                    } else if (can_become_flayer(mdef->data)) {
                        You("burrow into %s brain, taking over its body!",
                            s_suffix(mon_nam(mdef)));
                        You("begin to transform...");
                        tmp = 0;
                        xkilled(mdef, XKILL_GIVEMSG | XKILL_NOCORPSE);
                        (void) polymon(PM_MIND_FLAYER);
                        break;
                    } else {
                        You("burrow into %s brain!",
                            s_suffix(mon_nam(mdef)));
                        tmp = mdef->mhp;
                    }
                }
            } else {
                tmp = 0;
                if (flags.verbose) {
                    if (youmonst.data == &mons[PM_SALAMANDER])
                        You("try to grab %s", mon_nam(mdef));
                    else if (youmonst.data == &mons[PM_MIND_FLAYER_LARVA])
                        You("try to attach yourself to %s %s!",
                            s_suffix(mon_nam(mdef)), mbodypart(mdef, FACE));
                    else
                        You("brush against %s %s.", s_suffix(mon_nam(mdef)),
                            youmonst.data == &mons[PM_GIANT_CENTIPEDE]
                            ? "body" : mbodypart(mdef, LEG));
                }
            }
        } else
            tmp = 0;
        break;
    case AD_PLYS:
        if (!negated && mdef->mcanmove && !rn2(3) && tmp < mdef->mhp) {
            if (!Blind)
                pline("%s is frozen by you!", Monnam(mdef));
            paralyze_monst(mdef, rnd(10));
        }
        break;
    case AD_TCKL:
		if (!negated && mdef->mcanmove && !rn2(3) && tmp < mdef->mhp) {
		    if (!Blind) You("mercilessly tickle %s!", mon_nam(mdef));
		    paralyze_monst(mdef, rnd(10));
		}
		break;
    case AD_SLEE:
        if (!negated && !mdef->msleeping && sleep_monst(mdef, rnd(10), -1)) {
            if (!Blind)
                pline("%s is put to sleep by you!", Monnam(mdef));
            slept_monst(mdef);
        }
        break;
    case AD_SLIM:
        if (negated)
            break; /* physical damage only */
        if (!rn2(4) && !slimeproof(pd)) {
            if (!munslime(mdef, TRUE) && !DEADMONSTER(mdef)) {
                /* this assumes newcham() won't fail; since hero has
                   a slime attack, green slimes haven't been geno'd */
                You("turn %s into slime.", mon_nam(mdef));
                if (newcham(mdef, &mons[PM_GREEN_SLIME], FALSE, FALSE))
                    pd = mdef->data;
            }
            /* munslime attempt could have been fatal */
            if (DEADMONSTER(mdef))
                return 2; /* skip death message */
            tmp = 0;
        }
        break;
    case AD_ENCH: /* KMH -- remove enchantment (disenchanter) */
        /* there's no msomearmor() function, so just do damage */
        /* if (negated) break; */
        break;
    case AD_SLOW:
        if (defended(mdef, AD_SLOW))
            break;

        if (!negated && mdef->mspeed != MSLOW) {
            unsigned int oldspeed = mdef->mspeed;

            mon_adjust_speed(mdef, -1, (struct obj *) 0);
            if (mdef->mspeed != oldspeed && canseemon(mdef)) {
                if (canseemon(mdef))
                    pline("%s slows down.", Monnam(mdef));
            }
        }
        break;
    case AD_CONF:
        if (!mdef->mconf) {
            if (canseemon(mdef))
                pline("%s looks confused.", Monnam(mdef));
            mdef->mconf = 1;
        }
        break;
    case AD_POLY:
        if (!negated && tmp < mdef->mhp)
            tmp = mon_poly(&youmonst, mdef, tmp);
        break;
    case AD_WTHR: {
        uchar withertime = max(2, tmp);
        boolean no_effect =
            (nonliving(pd) /* This could use is_fleshy(), but that would
                              make a large set of monsters immune like
                              fungus, blobs, and jellies. */
             || is_vampshifter(mdef) || negated);
        boolean lose_maxhp = (withertime >= 8); /* if already withering */
        tmp = 0; /* doesn't deal immediate damage */

        if (!no_effect) {
            if (canseemon(mdef))
                pline("%s is withering away!", Monnam(mdef));

            if (mdef->mwither + withertime > UCHAR_MAX)
                mdef->mwither = UCHAR_MAX;
            else
                mdef->mwither += withertime;

            if (lose_maxhp && mdef->mhpmax > 1) {
                mdef->mhpmax--;
                mdef->mhp = min(mdef->mhp, mdef->mhpmax);
            }
            mdef->mwither_from_u = TRUE;
        }
        break;
    }
    case AD_PITS:
        if (rn2(2)) {
            if (!u.uswallow) {
                if (!create_pit_under(mdef, &youmonst))
                    tmp = 0;
            }
        }
        break;
    case AD_WEBS:
        if (negated)
            break;
        if (!t_at(mdef->mx, mdef->my)) {
            struct trap *web = maketrap(mdef->mx, mdef->my, WEB);
            if (web) {
                mintrap(mdef);
            }
        }
        break;
    case AD_CALM:	/* KMH -- koala attack */
        /* Certain monsters aren't even made peaceful. */
        if (!mdef->iswiz && mdef->data != &mons[PM_MEDUSA] &&
                        !(mdef->data->mflags3 & M3_COVETOUS) &&
                        !(mdef->data->geno & G_UNIQ)) {
            You("calm %s.", mon_nam(mdef));
            mdef->mpeaceful = 1;
            mdef->mtame = 0;
            tmp = 0;
        }
        break;
    default:
        tmp = 0;
        break;
    }

    mdef->mstrategy &= ~STRAT_WAITFORU; /* in case player is very fast */
    damage_mon(mdef, tmp, mattk->adtyp);
    if (DEADMONSTER(mdef)) {
        if (mdef->mtame && !cansee(mdef->mx, mdef->my)) {
            You_feel("embarrassed for a moment.");
            if (tmp)
                xkilled(mdef, XKILL_NOMSG); /* !tmp but hp<1: already killed */
        } else if (!flags.verbose) {
            You("destroy it!");
            if (tmp)
                xkilled(mdef, XKILL_NOMSG);
        } else if (tmp)
            killed(mdef);
        return 2;
    }
    return 1;
}

/* Hero, as a monster which is capable of an exploding attack mattk, is
 * exploding at a target monster mdef, or just exploding at nothing (e.g. with
 * forcefight) if mdef is null.
 */
int
explum(mdef, mattk)
register struct monst *mdef;
register struct attack *mattk;
{
    register int tmp = d((int) mattk->damn, (int) mattk->damd);
    
    switch (mattk->adtyp) {
    case AD_BLND:
        if (mdef && !resists_blnd(mdef)) {
            pline("%s is blinded by your flash of light!", Monnam(mdef));
            mdef->mblinded = min((int) mdef->mblinded + tmp, 127);
            mdef->mcansee = 0;
        }
        break;
    case AD_HALU:
        if (mdef && haseyes(mdef->data) && mdef->mcansee
            && !mon_prop(mdef, HALLUC_RES)) {
            pline("%s is affected by your flash of light!", Monnam(mdef));
            mdef->mconf = 1;
        }
        break;
    case AD_WIND:
        if (mdef) {
            pline("%s is blasted by wind!", Monnam(mdef));
            mhurtle(mdef, mdef->mx - u.ux, mdef->my - u.uy, tmp);
        }
        tmp = 0;
        break;
    case AD_COLD:
    case AD_FIRE:
    case AD_ELEC:
    case AD_ACID:
        /* See comment in mon_explodes() and in zap.c for an explanation of this
         * math.  Here, the player is causing the explosion, so it should be in
         * the +20 to +29 range instead of negative. */
        explode(u.ux, u.uy, (mattk->adtyp - 1) + 20, tmp, MON_EXPLODE,
                adtyp_to_expltype(mattk->adtyp));
        if (mdef && mattk->adtyp == AD_ACID) {
            if (rn2(4))
                erode_armor(mdef, ERODE_CORRODE);
            if (rn2(2))
                acid_damage(MON_WEP(mdef));
        }
        if (mdef && DEADMONSTER(mdef)) {
            /* Other monsters may have died too, but return 2 if the actual
             * target died. */
            return 2;
        }
        break;
    default:
        break;
    }
    return 1;
}

STATIC_OVL void
start_engulf(mdef)
struct monst *mdef;
{
    if (!Invisible) {
        map_location(u.ux, u.uy, TRUE);
        tmp_at(DISP_ALWAYS, mon_to_glyph(&youmonst, rn2_on_display_rng));
        tmp_at(mdef->mx, mdef->my);
    }
    You("engulf %s!", mon_nam(mdef));
    delay_output();
    delay_output();
}

STATIC_OVL void
end_engulf()
{
    if (!Invisible) {
        tmp_at(DISP_END, 0);
        newsym(u.ux, u.uy);
    }
}

STATIC_OVL int
gulpum(mdef, mattk)
register struct monst *mdef;
register struct attack *mattk;
{
#ifdef LINT /* static char msgbuf[BUFSZ]; */
    char msgbuf[BUFSZ];
#else
    static char msgbuf[BUFSZ]; /* for nomovemsg */
#endif
    register int tmp;
    register int dam = d((int) mattk->damn, (int) mattk->damd);
    boolean fatal_gulp;
    struct obj *otmp;
    struct permonst *pd = mdef->data;

    /* Not totally the same as for real monsters.  Specifically, these
     * don't take multiple moves.  (It's just too hard, for too little
     * result, to program monsters which attack from inside you, which
     * would be necessary if done accurately.)  Instead, we arbitrarily
     * kill the monster immediately for AD_DGST and we regurgitate them
     * after exactly 1 round of attack otherwise.  -KAA
     */

    if (!engulf_target(&youmonst, mdef))
        return 0;

    if (u.uhunger < 1500 && !u.uswallow) {
        for (otmp = mdef->minvent; otmp; otmp = otmp->nobj)
            (void) snuff_lit(otmp);

        /* force vampire in bat, cloud, or wolf form to revert back to
           vampire form now instead of dealing with that when it dies */
        if (is_vampshifter(mdef)
            && newcham(mdef, &mons[mdef->cham], FALSE, FALSE)) {
            You("engulf it, then expel it.");
            if (canspotmon(mdef))
                pline("It turns into %s.", a_monnam(mdef));
            else
                map_invisible(mdef->mx, mdef->my);
            return 1;
        }

        /* engulfing a cockatrice or digesting a Rider or Medusa */
        fatal_gulp = (touch_petrifies(pd) && !Stone_resistance)
                     || (mattk->adtyp == AD_DGST
                         && (is_rider(pd) || (pd == &mons[PM_MEDUSA]
                                              && !Stone_resistance)));

        if ((mattk->adtyp == AD_DGST && !Slow_digestion) || fatal_gulp)
            eating_conducts(pd);

        if (fatal_gulp && !is_rider(pd)) { /* petrification */
            char kbuf[BUFSZ];
            const char *mname = pd->mname;

            if (!type_is_pname(pd))
                mname = an(mname);
            You("englut %s.", mon_nam(mdef));
            Sprintf(kbuf, "swallowing %s whole", mname);
            instapetrify(kbuf);
        } else {
            start_engulf(mdef);
            switch (mattk->adtyp) {
            case AD_DGST: {
                struct obj *sbarding;

                /* slow digestion protects against engulfing */
                if (mon_prop(mdef, SLOW_DIGESTION)) {
                    You("hurriedly regurgitate the indigestible %s.", m_monnam(mdef));
                    end_engulf();
                    return 2;
                }
                if ((sbarding = which_armor(mdef, W_BARDING)) != 0
                    && sbarding->otyp == SPIKED_BARDING) {
                    You("hurriedly regurgitate the indigestible %s.", m_monnam(mdef));
                    end_engulf();
                    return 2;
                }
                /* eating a Rider or its corpse is fatal */
                if (is_rider(pd)) {
                    pline("Unfortunately, digesting any of it is fatal.");
                    end_engulf();
                    Sprintf(killer.name, "unwisely tried to eat %s",
                            pd->mname);
                    killer.format = NO_KILLER_PREFIX;
                    done(DIED);
                    return 0; /* lifesaved */
                }

                if (Slow_digestion) {
                    dam = 0;
                    break;
                }

                /* Use up amulet of life saving */
                if ((otmp = mlifesaver(mdef)) != 0)
                    m_useup(mdef, otmp);

                newuhs(FALSE);
                /* start_engulf() issues "you engulf <mdef>" above; this
                   used to specify XKILL_NOMSG but we need "you kill <mdef>"
                   in case we're also going to get "welcome to level N+1";
                   "you totally digest <mdef>" will be coming soon (after
                   several turns) but the level-gain message seems out of
                   order if the kill message is left implicit */
                xkilled(mdef, XKILL_GIVEMSG | XKILL_NOCORPSE);
                if (!DEADMONSTER(mdef)) { /* monster lifesaved */
                    You("hurriedly regurgitate the sizzling in your %s.",
                        body_part(STOMACH));
                } else {
                    tmp = 1 + (pd->cwt >> 8);
                    if (corpse_chance(mdef, &youmonst, TRUE)
                        && !(mvitals[monsndx(pd)].mvflags & G_NOCORPSE)) {
                        /* nutrition only if there can be a corpse */
                        u.uhunger += (pd->cnutrit + 1) / 2;
                    } else
                        tmp = 0;
                    Sprintf(msgbuf, "You totally digest %s.", mon_nam(mdef));
                    if (tmp != 0) {
                        /* setting afternmv = end_engulf is tempting,
                         * but will cause problems if the player is
                         * attacked (which uses his real location) or
                         * if his See_invisible wears off
                         */
                        You("digest %s.", mon_nam(mdef));
                        if (Slow_digestion)
                            tmp *= 2;
                        nomul(-tmp);
                        multi_reason = "digesting something";
                        nomovemsg = msgbuf;
                    } else
                        pline1(msgbuf);
                    if (pd == &mons[PM_GREEN_SLIME]) {
                        Sprintf(msgbuf, "%s isn't sitting well with you.",
                                The(pd->mname));
                        if (!Unchanging) {
                            make_slimed(5L, (char *) 0);
                        }
                    } else
                        exercise(A_CON, TRUE);
                }
                end_engulf();
                return 2;
            }
            case AD_PHYS:
                if (youmonst.data == &mons[PM_FOG_CLOUD]) {
                    pline("%s is laden with your moisture.", Monnam(mdef));
                    if (amphibious(pd) && !flaming(pd)) {
                        dam = 0;
                        pline("%s seems unharmed.", Monnam(mdef));
                    }
                } else
                    pline("%s is pummeled with your debris!", Monnam(mdef));
                break;
            case AD_WRAP:
                /* suffocation attack; negate damage if breathless */
                if (breathless(mdef->data)) {
                    pline("%s doesn't appear to need air to breathe.",
                          Monnam(mdef));
                    dam = 0;
                }
                else {
                    pline("%s is being suffocated!", Monnam(mdef));
                }
                break;
            case AD_ACID:
                pline("%s is covered with your goo!", Monnam(mdef));
                if (resists_acid(mdef) || defended(mdef, AD_ACID)) {
                    pline("It seems harmless to %s.", mon_nam(mdef));
                    dam = 0;
                }
                break;
            case AD_BLND:
                if (can_blnd(&youmonst, mdef, mattk->aatyp,
                             (struct obj *) 0)) {
                    if (mdef->mcansee)
                        pline("%s can't see in there!", Monnam(mdef));
                    mdef->mcansee = 0;
                    dam += mdef->mblinded;
                    if (dam > 127)
                        dam = 127;
                    mdef->mblinded = dam;
                }
                dam = 0;
                break;
            case AD_ELEC:
                if (rn2(2)) {
                    pline_The("air around %s crackles with electricity.",
                              mon_nam(mdef));
                    if (resists_elec(mdef) || defended(mdef, AD_ELEC)) {
                        pline("%s seems unhurt.", Monnam(mdef));
                        dam = 0;
                    }
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_COLD:
                if (rn2(2)) {
                    if (resists_cold(mdef) || defended(mdef, AD_COLD)) {
                        pline("%s seems mildly chilly.", Monnam(mdef));
                        dam = 0;
                    } else
                        pline("%s is freezing to death!", Monnam(mdef));
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_FIRE:
                if (rn2(2)) {
                    if (resists_fire(mdef) || defended(mdef, AD_FIRE)) {
                        pline("%s seems mildly hot.", Monnam(mdef));
                        dam = 0;
                    } else
                        pline("%s is %s!", Monnam(mdef), on_fire(mdef, ON_FIRE_ENGULF));
                    golemeffects(mdef, (int) mattk->adtyp, dam);
                } else
                    dam = 0;
                break;
            case AD_DREN:
                if (!rn2(4))
                    xdrainenergym(mdef, TRUE);
                dam = 0;
                break;
            }
            end_engulf();
            damage_mon(mdef, dam, mattk->adtyp);
            if (DEADMONSTER(mdef)) {
                killed(mdef);
                if (DEADMONSTER(mdef)) /* not lifesaved */
                    return 2;
            }
            You("%s %s!", is_swallower(youmonst.data) ? "regurgitate"
                                                      : "expel",
                mon_nam(mdef));
            if (Slow_digestion || is_swallower(youmonst.data)) {
                pline("Obviously, you didn't like %s taste.",
                      s_suffix(mon_nam(mdef)));
            }
        }
    }
    return 0;
}

void
missum(mdef, target, roll, mattk, wouldhavehit)
register struct monst *mdef;
register struct attack *mattk;
register int target;
register int roll;
boolean wouldhavehit;
{
    register boolean nearmiss = (target == roll);
    register struct obj *blocker = (struct obj *) 0;

    /* 2 values for blocker:
     * No blocker: (struct obj *) 0
     * Piece of armour: object
     */

    if (target < roll) {
        /* get object responsible,
           work from the closest to the skin outwards */

        /* Try undershirt */
        if (which_armor(mdef, W_ARMU)
            && (which_armor(mdef, W_ARM) == 0)
            && (which_armor(mdef, W_ARMC) == 0)
            && target <= roll) {
            target += armor_bonus(which_armor(mdef, W_ARMU));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMU);
        }

        /* Try body armour */
        if (which_armor(mdef, W_ARM)
            && (which_armor(mdef, W_ARMC) == 0) && target <= roll) {
            target += armor_bonus(which_armor(mdef, W_ARM));
            if (target > roll)
                blocker = which_armor(mdef, W_ARM);
        }

        if (which_armor(mdef, W_ARMG) && !rn2(10)) {
            /* Try gloves */
            target += armor_bonus(which_armor(mdef, W_ARMG));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMG);
        }

        if (which_armor(mdef, W_ARMF) && !rn2(10)) {
            /* Try boots */
            target += armor_bonus(which_armor(mdef, W_ARMF));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMF);
        }

        if (which_armor(mdef, W_ARMH) && !rn2(5)) {
            /* Try helm */
            target += armor_bonus(which_armor(mdef, W_ARMH));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMH);
        }

        if (which_armor(mdef, W_ARMC) && target <= roll) {
            /* Try cloak */
            target += armor_bonus(which_armor(mdef, W_ARMC));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMC);
        }

        if (which_armor(mdef, W_ARMS) && target <= roll) {
            /* Try shield */
            target += armor_bonus(which_armor(mdef, W_ARMS));
            if (target > roll)
                blocker = which_armor(mdef, W_ARMS);
        }

        if (which_armor(mdef, W_BARDING) && target <= roll) {
            /* Try barding (steeds) */
            target += armor_bonus(which_armor(mdef, W_BARDING));
            if (target > roll)
                blocker = which_armor(mdef, W_BARDING);
        }
    }

    if (wouldhavehit) /* monk is missing due to penalty for wearing suit */
        Your("armor is rather cumbersome...");

    if (could_seduce(&youmonst, mdef, mattk))
        You("pretend to be friendly to %s.", mon_nam(mdef));
    else if (canspotmon(mdef) && flags.verbose) {
        if (Role_if(PM_ROGUE)
            && !uwep && context.forcefight && !Upolyd) {
            Your("pickpocketing attempt fails %s.",
                 rn2(2) ? "horribly" : "miserably");
        } else if (nearmiss || !blocker) {
            if (thick_skinned(mdef->data) && !rn2(10))
                pline("%s %s %s your attack.",
                      s_suffix(Monnam(mdef)),
                      (is_dragon(mdef->data) ? "scaly hide"
                                             : (mdef->data == &mons[PM_GIANT_TURTLE]
                                                || is_tortle(mdef->data))
                                                 ? "protective shell"
                                                 : "thick hide"),
                      (rn2(2) ? "blocks" : "deflects"));
            else
                You("%smiss %s.",
                    (nearmiss ? "just " : ""), mon_nam(mdef));
        } else {
            /* Blocker */
            pline("%s %s %s your attack.",
                  s_suffix(Monnam(mdef)),
                  aobjnam(blocker, (char *) 0),
                  (rn2(2) ? "blocks" : "deflects"));
            if (blocker && !uwep && !uarmg
                && Hate_material(blocker->material)) {
                searmsg(mdef, &youmonst, blocker, FALSE);
                /* glancing blow */
                losehp(rnd(sear_damage(blocker->material) / 2),
                       "hitting an adverse material", KILLED_BY);
            }
        }
    } else
        You("%smiss it.", ((flags.verbose && nearmiss) ? "just " : ""));
    if (!mdef->msleeping && mdef->mcanmove)
        wakeup(mdef, TRUE);
}

/* attack monster as a monster; returns True if mon survives */
STATIC_OVL boolean
hmonas(mon, as, weapon_attacks)
register struct monst *mon;
int as;
boolean weapon_attacks; /* skip weapon attacks if false */
{
    struct attack *mattk, alt_attk;
    struct obj *weapon, **originalweapon;
    boolean altwep = FALSE, weapon_used = !weapon_attacks,
            stop_attacking = FALSE;
    int i, tmp, armorpenalty, sum[NATTK], dhit = 0, attknum = 0;
    /* int nsum = 0; */ /* nsum is not currently used */
    int dieroll;
    boolean monster_survived, Old_Upolyd = Upolyd;

    for (i = 0; i < NATTK; i++) {
        sum[i] = 0;
        if (as != NON_PM)
            mattk = &mons[as].mattk[i];
        else
            mattk = getmattk(&youmonst, mon, i, sum, &alt_attk);
        weapon = 0;
        switch (mattk->aatyp) {
        case AT_WEAP:
            /* if (!uwep) goto weaponless; */
            if (!weapon_attacks)
                continue;
 use_weapon:
            /* if we've already hit with a two-handed weapon, we don't
               get to make another weapon attack (note:  monsters who
               use weapons do not have this restriction, but they also
               never have the opportunity to use two weapons) */
            if (weapon_used && sum[i - 1] && uwep && bimanual(uwep))
                continue;
            /* Certain monsters don't use weapons when encountered as enemies,
             * but players who polymorph into them have hands or claws and
             * thus should be able to use weapons.  This shouldn't prohibit
             * the use of most special abilities, either.
             * If monster has multiple claw attacks, only one can use weapon.
             */
            weapon_used = TRUE;
            /* Potential problem: if the monster gets multiple weapon attacks,
             * we currently allow the player to get each of these as a weapon
             * attack.  Is this really desirable?
             */
            /* approximate two-weapon mode; known_hitum() -> hmon() -> &c
               might destroy the weapon argument, but it might also already
               be Null, and we want to track that for passive() */
            originalweapon = (altwep && uswapwep) ? &uswapwep : &uwep;
            if (uswapwep /* set up 'altwep' flag for next iteration */
                /* only consider seconary when wielding one-handed primary */
                && uwep && (uwep->oclass == WEAPON_CLASS || is_weptool(uwep))
                && !bimanual(uwep)
                /* only switch if not wearing shield and not at artifact;
                   shield limitation is iffy since still get extra swings
                   if polyform has them, but it matches twoweap behavior;
                   twoweap also only allows primary to be an artifact, so
                   if alternate weapon is one, don't use it */
                && !uarms && !uswapwep->oartifact
                /* only switch to uswapwep if it's a weapon */
                && (uswapwep->oclass == WEAPON_CLASS || is_weptool(uswapwep))
                /* only switch if uswapwep is not bow, arrows, or darts */
                && !(is_launcher(uswapwep) || is_ammo(uswapwep)
                     || is_missile(uswapwep)) /* dart, shuriken, boomerang */
                /* and not two-handed and not incapable of being wielded */
                && !bimanual(uswapwep)
                && !(Hate_material(uswapwep->material)))
                altwep = !altwep; /* toggle for next attack */
            weapon = *originalweapon;
            if (!weapon) /* no need to go beyond no-gloves to rings; not ...*/
                originalweapon = &uarmg; /*... subject to erosion damage */

            tmp = find_roll_to_hit(mon, AT_WEAP, weapon, &attknum,
                                   &armorpenalty);
            dieroll = rnd(20);
            dhit = (tmp > dieroll || u.uswallow);
            /* caller must set bhitpos */
            monster_survived = known_hitum(mon, weapon, &dhit, tmp,
                                           armorpenalty, mattk, dieroll);
            /* originalweapon points to an equipment slot which might
               now be empty if the weapon was destroyed during the hit;
               passive(,weapon,...) won't call passive_obj() in that case */
            weapon = *originalweapon; /* might receive passive erosion */
            if (!monster_survived) {
                /* enemy dead, before any special abilities used */
                sum[i] = 2;
                break;
            } else
                sum[i] = dhit;
            /* might be a worm that gets cut in half; if so, early return */
            if (m_at(u.ux + u.dx, u.uy + u.dy) != mon) {
                i = NATTK; /* skip additional attacks */
                /* proceed with uswapwep->cursed check, then exit loop */
                goto passivedone;
            }
            /* Do not print "You hit" message; known_hitum already did it. */
            if (dhit && mattk->adtyp != AD_SPEL && mattk->adtyp != AD_PHYS)
                sum[i] = damageum(mon, mattk, 0);
            break;
        case AT_CLAW:
            if (uwep && !cantwield(youmonst.data) && !weapon_used)
                goto use_weapon;
            /*FALLTHRU*/
        case AT_TUCH:
            if (uwep && youmonst.data->mlet == S_LICH && !weapon_used)
                goto use_weapon;
            /*FALLTHRU*/
        case AT_TENT:
            if ((uwep || !uwep || (u.twoweap && uswapwep))
                && (maybe_polyd(is_illithid(youmonst.data),
                    Race_if(PM_ILLITHID)))
                && (touch_petrifies(mon->data)
                    || u.uswallow
                    || is_rider(mon->data)
                    || mon->data == &mons[PM_MEDUSA]
                    || mon->data == &mons[PM_GREEN_SLIME]
                    || mon->data == &mons[PM_SHADE]
                    || (u.ulycn >= LOW_PM
                        && were_beastie(mon->mnum) == u.ulycn
                        && !Role_if(PM_CAVEMAN) && !Race_if(PM_ORC))
                    || (how_resistant(DISINT_RES) == 0
                        && (mon->data == &mons[PM_BLACK_DRAGON]
                            || mon->data == &mons[PM_BABY_BLACK_DRAGON]))))
                break;
            /*FALLTHRU*/
        case AT_BITE:
            /* [ALI] Vampires are also smart. They avoid biting
               monsters if doing so would be fatal */
            if ((i > 0 && is_vampire(youmonst.data))
                && ((how_resistant(DISINT_RES) == 0
                        && (mon->data == &mons[PM_BLACK_DRAGON]
                        || mon->data == &mons[PM_BABY_BLACK_DRAGON]))
                    || is_rider(mon->data) 
                    || touch_petrifies(mon->data) 
                    || mon->data == &mons[PM_MEDUSA] 
                    || mon->data == &mons[PM_GREEN_SLIME] ))
                break;
            if (is_zombie(youmonst.data)
                && mattk->aatyp == AT_BITE
                && mon->data->msize <= MZ_SMALL
                && is_animal(mon->data)
                && !(mon->mfrozen || mon->mstone
                     || mon->mconf || mon->mstun) && rn2(3)) {
                pline("%s nimbly %s your bite!", Monnam(mon),
                      rn2(2) ? "dodges" : "evades");
                break;
            }
            /*FALLTHRU*/
        case AT_STNG:
            if ((uwep || !uwep || (u.twoweap && uswapwep))
                && (maybe_polyd(is_demon(youmonst.data),
                    Race_if(PM_DEMON)))
                && (touch_petrifies(mon->data)
                    || (how_resistant(DISINT_RES) == 0
                        && (mon->data == &mons[PM_BLACK_DRAGON]
                            || mon->data == &mons[PM_BABY_BLACK_DRAGON]))))
                break;
            /*FALLTHRU*/
        case AT_KICK:
        case AT_BUTT:
        /*weaponless:*/
            tmp = find_roll_to_hit(mon, mattk->aatyp, (struct obj *) 0,
                                   &attknum, &armorpenalty);
            dieroll = rnd(20);
            dhit = (tmp > dieroll || u.uswallow);

            if (dhit) {
                int compat, specialdmg;
                struct obj * hated_obj = NULL;
                const char *verb = 0; /* verb or body part */

                if (!u.uswallow
                    && (compat = could_seduce(&youmonst, mon, mattk)) != 0) {
                    You("%s %s %s.",
                        (mon->mcansee && haseyes(mon->data)) ? "smile at"
                                                             : "talk to",
                        mon_nam(mon),
                        (compat == 2) ? "engagingly" : "seductively");
                    /* doesn't anger it; no wakeup() */
                    sum[i] = damageum(mon, mattk, 0);
                    break;
                }
                wakeup(mon, TRUE);
                /* There used to be a bunch of code here to ensure that W_RINGL
                 * and W_RINGR slots got chosen on alternating claw/touch
                 * attacks. There's no such logic for monsters, and if you know
                 * that the ring on one of your hands will be especially
                 * effective, you'll probably keep hitting with that hand. So
                 * just do the default and take whatever the most damaging piece
                 * of gear is. */
                specialdmg = special_dmgval(&youmonst, mon,
                                            attack_contact_slots(&youmonst,
                                                                 mattk->aatyp),
                                            &hated_obj);
                switch (mattk->aatyp) {
                case AT_CLAW:
                    /* verb=="claws" may be overridden below */
                    verb = "claws";
                    break;
                case AT_TUCH:
                    verb = "touch";
                    break;
                case AT_TENT:
                    /* assumes mind flayer's tentacles-on-head rather
                       than sea monster's tentacle-as-arm */
                    verb = "tentacles";
                    break;
                case AT_KICK:
                    verb = "kick";
                    break;
                case AT_BUTT:
                    verb = (has_trunk(youmonst.data)) ? "gore" : "head butt";
                    break;
                case AT_BITE:
                    verb = (has_beak(youmonst.data)) ? "peck" : "bite";
                    break;
                case AT_STNG:
                    verb = "sting";
                    break;
                default:
                    verb = "hit";
                    break;
                }
                if (noncorporeal(mon->data) && !specialdmg) {
                    if (!strcmp(verb, "hit")
                        || (mattk->aatyp == AT_CLAW && humanoid(mon->data)))
                        verb = "attack";
                    Your("%s %s harmlessly through %s.",
                         verb, vtense(verb, "pass"), mon_nam(mon));
                } else {
                    if (mattk->aatyp == AT_TENT) {
                        Your("tentacles suck %s.", mon_nam(mon));
                    } else {
                        if (mattk->aatyp == AT_CLAW)
                            verb = "claw"; /* yes, "claws" */
                        You("%s %s.", verb, mon_nam(mon));
                        if (hated_obj && flags.verbose)
                            searmsg(&youmonst, mon, hated_obj, FALSE);
                    }
                    sum[i] = damageum(mon, mattk, specialdmg);
                }
            } else { /* !dhit */
                missum(mon, tmp, dieroll, mattk, (tmp + armorpenalty > dieroll));
            }
            break;

        case AT_HUGS: {
            int specialdmg;
            struct obj* hated_obj = NULL;
            boolean byhand = hug_throttles(&mons[u.umonnum]), /* rope golem */
                    unconcerned = (byhand && !can_be_strangled(mon));

            if (sticks(mon->data) || u.uswallow || notonhead
                || (byhand && (uwep || !has_head(mon->data)))) {
                /* can't hold a holder due to subsequent ambiguity over
                   who is holding whom; can't hug engulfer from inside;
                   can't hug a worm tail (would immobilize entire worm!);
                   byhand: can't choke something that lacks a head;
                   not allowed to make a choking hug if wielding a weapon
                   (but might have grabbed w/o weapon, then wielded one,
                   and may even be attacking a different monster now) */
                if (byhand && uwep && u.ustuck
                    && !(sticks(u.ustuck->data) || u.uswallow))
                    uunstick();
                continue; /* not 'break'; bypass passive counter-attack */
            }
            /* automatic if prev two attacks succeed, or if
               already grabbed in a previous attack */
            dhit = 1;
            wakeup(mon, TRUE);
            specialdmg = special_dmgval(&youmonst, mon,
                                        attack_contact_slots(&youmonst,
                                                             AT_HUGS),
                                        &hated_obj);
            if (unconcerned) {
                /* strangling something which can't be strangled */
                if (mattk != &alt_attk) {
                    alt_attk = *mattk;
                    mattk = &alt_attk;
                }
                /* change damage to 1d1; not strangling but still
                   doing [minimal] physical damage to victim's body */
                mattk->damn = mattk->damd = 1;
                /* don't give 'unconcerned' feedback if there is extra damage
                   or if it is nearly destroyed or if creature doesn't have
                   the mental ability to be concerned in the first place */
                if (specialdmg || mindless(mon->data)
                    || mon->mhp <= 1 + max(u.udaminc, 1))
                    unconcerned = FALSE;
            }
            if (noncorporeal(mon->data)) {
                const char *verb = byhand ? "grasp" : "hug";

                /* hugging a shade; successful if blessed outermost armor
                   for normal hug, or blessed gloves or silver ring(s) for
                   choking hug; deals damage but never grabs hold */
                if (specialdmg) {
                    You("%s %s%s", verb, mon_nam(mon), exclam(specialdmg));
                    if (hated_obj && flags.verbose)
                        searmsg(&youmonst, mon, hated_obj, FALSE);
                    sum[i] = damageum(mon, mattk, specialdmg);
                } else {
                    Your("%s passes harmlessly through %s.",
                         verb, mon_nam(mon));
                }
                break;
            }
            /* hug attack against ordinary foe */
            if (mon == u.ustuck) {
                pline("%s is being %s%s.", Monnam(mon),
                      byhand ? "throttled" : "crushed",
                      /* extra feedback for non-breather being choked */
                      unconcerned ? " but doesn't seem concerned" : "");
                if (hated_obj && flags.verbose)
                    searmsg(&youmonst, mon, hated_obj, FALSE);
                sum[i] = damageum(mon, mattk, specialdmg);
            } else if (i >= 2 && sum[i - 1] && sum[i - 2]) {
                /* in case we're hugging a new target while already
                   holding something else; yields feedback
                   "<u.ustuck> is no longer in your clutches" */
                if (u.ustuck && u.ustuck != mon)
                    uunstick();
                You("grab %s!", mon_nam(mon));
                u.ustuck = mon;
                if (hated_obj && flags.verbose)
                    searmsg(&youmonst, mon, hated_obj, FALSE);
                sum[i] = damageum(mon, mattk, specialdmg);
            }
            break; /* AT_HUGS */
        }

        case AT_EXPL: /* automatic hit if next to */
            dhit = -1;
            wakeup(mon, TRUE);
            You("explode!");
            sum[i] = explum(mon, mattk);
            break;

        case AT_ENGL:
            tmp = find_roll_to_hit(mon, mattk->aatyp, (struct obj *) 0,
                                   &attknum, &armorpenalty);
            dieroll = rnd(20);
            if ((dhit = (tmp > rnd(20 + i)))) {
                wakeup(mon, TRUE);
                if (noncorporeal(mon->data))
                    Your("attempt to surround %s is harmless.", mon_nam(mon));
                else if (is_fern_spore(mon->data))
                    You("would rather not eat that %s.", mon_nam(mon));
                else {
                    sum[i] = gulpum(mon, mattk);
                    if (sum[i] == 2 && (mon->data->mlet == S_ZOMBIE
                                        || mon->data->mlet == S_MUMMY)
                        && rn2(5) && !Sick_resistance) {
                        You_feel("%ssick.", (Sick) ? "very " : "");
                        mdamageu(mon, rnd(8));
                    }
                }
            } else {
                missum(mon, tmp, dieroll, mattk, FALSE);
            }
            break;

        case AT_MAGC:
            /* No check for uwep; if wielding nothing we want to
             * do the normal 1-2 points bare hand damage...
             */
            if ((youmonst.data->mlet == S_KOBOLD
                 || youmonst.data->mlet == S_ORC
                 || youmonst.data->mlet == S_HUMAN
                 || youmonst.data->mlet == S_GIANT
                 || youmonst.data->mlet == S_VAMPIRE
                 || youmonst.data->mlet == S_GNOME) && !weapon_used)
                goto use_weapon;
            sum[i] = castum(mon, mattk);
             		continue;
            /*FALLTHRU*/

        case AT_NONE:
        case AT_BOOM:
            continue;
        /* Not break--avoid passive attacks from enemy */

        case AT_BREA:
        case AT_SPIT:
        case AT_VOLY:
        case AT_SCRE:
        case AT_GAZE: /* all done using #monster command */
            dhit = 0;
            break;
        case AT_MULTIPLY:
			/* Not a #monster ability -- this is something that the
			 * player must figure out -RJ */
			cloneu();
			break;

        default: /* Strange... */
            impossible("strange attack of yours (%d)", mattk->aatyp);
        }
        if (dhit == -1) {
            u.mh = -1; /* dead in the current form */
            rehumanize();
        }
        stop_attacking = FALSE;
        if (sum[i] == 2) {
            /* defender dead */
            (void) passive(mon, weapon, 1, 0, mattk->aatyp, FALSE);
            stop_attacking = TRUE; /* zombification; DEADMONSTER is false */
            /* nsum = 0; */ /* return value below used to be 'nsum > 0' */
        } else {
            (void) passive(mon, weapon, sum[i], 1, mattk->aatyp, FALSE);
            /* nsum |= sum[i]; */
        }

        /* don't use sum[i] beyond this point;
           'i' will be out of bounds if we get here via 'goto' */
 passivedone:
        /* when using dual weapons, cursed secondary weapon doesn't weld,
           it gets dropped; do the same when multiple AT_WEAP attacks
           simulate twoweap */
        if (uswapwep && weapon == uswapwep && weapon->cursed) {
            drop_uswapwep();
            break; /* don't proceed with additional attacks */
        }
        /* stop attacking if defender has died;
           needed to defer this until after uswapwep->cursed check;
           use stop_attacking to track cases like if the monster was polymorphed
           or zombified and we should not attack it any more this round. */
        if (DEADMONSTER(mon) || stop_attacking)
            break;
        if (Upolyd != Old_Upolyd)
            break; /* No extra attacks if form changed */
        if (multi < 0)
            break; /* If paralyzed while attacking, i.e. floating eye */
    }
    /* return value isn't used, but make it match hitum()'s */
    return !DEADMONSTER(mon);
}

/*      Special (passive) attacks on you by monsters done here.
 */
int
passive(mon, weapon, mhit, malive, aatyp, wep_was_destroyed)
struct monst *mon;
struct obj *weapon; /* uwep or uswapwep or uarmg or uarmf or Null */
boolean mhit;
int malive;
uchar aatyp;
boolean wep_was_destroyed;
{
    register struct permonst *ptr = mon->data;
    register struct obj *m_armor;
    register int i, t, tmp;
    register struct attack *mattk;
    struct obj *otmp;
    mattk = has_erac(mon) ? ERAC(mon)->mattk : ptr->mattk;

    /* If carrying the Candle of Eternal Flame, deal passive fire damage */
    if (m_carrying_arti(mon, ART_CANDLE_OF_ETERNAL_FLAME)) {
        tmp = d(2, 7);
        if (monnear(mon, u.ux, u.uy)) {
            pline("Magic fire suddenly surrounds you!");
            if (how_resistant(FIRE_RES) == 100) {
                shieldeff(u.ux, u.uy);
                pline_The("fire doesn't feel hot.");
                ugolemeffects(AD_FIRE, tmp);
            } else {
                tmp = resist_reduce(tmp, FIRE_RES);
                mdamageu(mon, tmp); /* fire damage */
            }
            destroy_item(SCROLL_CLASS, AD_FIRE);
            destroy_item(POTION_CLASS, AD_FIRE);
            destroy_item(SPBOOK_CLASS, AD_FIRE);
            destroy_item(WEAPON_CLASS, AD_FIRE);
        }
    }
    if (mhit && aatyp == AT_BITE && is_vampiric(youmonst.data)) {
        if (bite_monster(mon))
            return 2;			/* lifesaved */
    }
    for (i = 0; ; i++) {
        if (i >= NATTK)
            return (malive | mhit); /* no passive attacks */
        if (mattk[i].aatyp == AT_NONE)
            break; /* try this one */
    }
    /* Note: tmp not always used */
    if (mattk[i].damn)
        tmp = d((int) mattk[i].damn, (int) mattk[i].damd);
    else if (mattk[i].damd)
        tmp = d((int) mon->m_lev + 1, (int) mattk[i].damd);
    else
        tmp = 0;

    /*  These affect you even if they just died.
     */
    switch (mattk[i].adtyp) {
    case AD_FIRE:
        if (mhit && !mon->mcan && !Underwater) {
            if (aatyp == AT_KICK) {
                if (uarmf && !rn2(6))
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_BURN,
                                     EF_GREASE | EF_VERBOSE | EF_DESTROY);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                (void) passive_obj(mon, weapon, &(mattk[i]));
        }
        break;
    case AD_QUIL:
        if (monnear(mon, u.ux, u.uy) && mhit) {
            if (Blind || !flags.verbose) {
                You("are jabbed by something sharp!");
            } else {
                You("are jabbed by %s spikes!", s_suffix(mon_nam(mon)));
            }
            mdamageu(mon, tmp);
        }
        break;
    case AD_ACID:
        if (mhit && rn2(2)) {
            if (Blind || !flags.verbose)
                You("are splashed!");
            else
                You("are splashed by %s %s!", s_suffix(mon_nam(mon)),
                    hliquid("acid"));

            if (!(Acid_resistance || Underwater))
                mdamageu(mon, tmp);
            else
                monstseesu(M_SEEN_ACID);
            if (!Underwater) {
                if (!rn2(30))
                    erode_armor(&youmonst, ERODE_CORRODE);
            }
        }
        if (mhit && !Underwater) {
            if (aatyp == AT_KICK) {
                if (uarmf && !rn2(6))
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_CORRODE,
                                     EF_GREASE | EF_DESTROY);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                (void) passive_obj(mon, weapon, &(mattk[i]));
        }
        exercise(A_STR, FALSE);
        break;
    case AD_SLEE:
        /* passive sleep attack for orange jelly */
        if (mhit && !mon->mcan) {
            if (Sleep_resistance) {
                You("yawn.");
                break;
            }
            fall_asleep(-rnd(tmp), TRUE);
            if (Blind) 
                You("are put to sleep!");
            else 
                You("are put to sleep by %s!", mon_nam(mon));
        }
        break;
    case AD_DRLI:
        /* hackem: passive drain life for baby and adult deep dragons */
        if (mhit && !mon->mcan) {
            if (Drain_resistance) {
                shieldeff(u.ux, u.uy);
                monstseesu(M_SEEN_DRAIN);
                /* No msg if resisted */
            } else
                losexp("life drainage");
            
        }
        break;
    case AD_STON:
        if (mhit) { /* successful attack */
            long protector = attk_protection((int) aatyp);

            /* hero using monsters' AT_MAGC attack is hitting hand to
               hand rather than casting a spell */
            if (aatyp == AT_MAGC)
                protector = W_ARMG;

            if (protector == 0L /* no protection */
                || (protector == W_ARMG && !uarmg
                    && !uwep && !wep_was_destroyed)
                || (protector == W_ARMF && !uarmf)
                || (protector == W_ARMH && !uarmh)
                || (protector == (W_ARMC | W_ARMG) && (!uarmc || !uarmg))) {
                if (!Stone_resistance
                    && !(poly_when_stoned(youmonst.data)
                         && polymon(PM_STONE_GOLEM))) {
                    done_in_by(mon, STONING); /* "You turn to stone..." */
                    return 2;
                }
            }
        }
        break;
    case AD_RUST:
        if (mhit && !mon->mcan) {
            if (mon->data == &mons[PM_WATER_ELEMENTAL]
                || mon->data == &mons[PM_BABY_SEA_DRAGON]
                || mon->data == &mons[PM_SEA_DRAGON]) {
                if (rn2(2)) {
                    if (Blind || !flags.verbose)
                        You("are splashed!");
                    else
                        You("are splashed by %s water!", s_suffix(mon_nam(mon)));
                }
            }
            if (aatyp == AT_KICK) {
                if (uarmf)
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_RUST,
                                     EF_GREASE | EF_DESTROY);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                (void) passive_obj(mon, weapon, &(mattk[i]));
        }
        break;
    case AD_CORR:
        if (mhit && !mon->mcan && !EAcid_resistance) {
            if (aatyp == AT_KICK) {
                if (uarmf)
                    (void) erode_obj(uarmf, xname(uarmf), ERODE_CORRODE,
                                     EF_GREASE | EF_DESTROY);
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH)
                (void) passive_obj(mon, weapon, &(mattk[i]));
        }
        break;
    case AD_DISN:
        if (mhit && !mon->mcan) { /* successful attack */
            long protector = attk_protection((int) aatyp);
            int chance = (mon->data == &mons[PM_ANTIMATTER_VORTEX] ? rn2(10) : rn2(20));
            tmp = rnd(8) + 1;

            /* hero using monsters' AT_MAGC attack is hitting hand to
               hand rather than casting a spell */
            if (aatyp == AT_MAGC)
                protector = W_ARMG;

            if (protector == 0L /* no protection */
                || (protector == W_ARMG && !uarmg
                    && !uwep && !wep_was_destroyed)
                || (protector == W_ARMF && !uarmf)
                || (protector == W_ARMH && !uarmh)
                || (protector == (W_ARMC | W_ARMG) && (!uarmc || !uarmg))) {
                if (how_resistant(DISINT_RES) == 100) {
                    if (!rn2(3))
                        You("are unaffected by %s %s.",
                            s_suffix(mon_nam(mon)),
                            mon->data == &mons[PM_ANTIMATTER_VORTEX]
                                ? "form" : "hide");
                    monstseesu(M_SEEN_DISINT);
                    stop_occupation();
                } else {
                    if (chance) {
                        You("partially disintegrate!");
                        tmp = resist_reduce(tmp, DISINT_RES);
                        mdamageu(mon, tmp);
                    } else {
                        if (how_resistant(DISINT_RES) < 50) {
                            pline("%s deadly %s disintegrates you!",
                                  s_suffix(Monnam(mon)),
                                  mon->data == &mons[PM_ANTIMATTER_VORTEX]
                                      ? "form" : "hide");
                            u.ugrave_arise = -3;
                            killer.format = NO_KILLER_PREFIX;
                            Sprintf(killer.name, "disintegrated by %s",
                                    an(l_monnam(mon)));
                            done(DIED);
                            return 2;
                        } else {
                            You("partially disintegrate!");
                            tmp = resist_reduce(tmp, DISINT_RES);
                            mdamageu(mon, tmp);
                        }
                    }
                }
            }
            /* odds of obj disintegrating handled in passive_obj()
               except for attack type AT_KICK */
            if (aatyp == AT_KICK) {
                if (uarmf
                    && (mon->data == &mons[PM_ANTIMATTER_VORTEX] ? !rn2(3) : !rn2(6))) {
                    if (rn2(2)
                        && (uarmf->oerodeproof || is_supermaterial(uarmf)))
                        pline("%s being disintegrated!",
                              Yobjnam2(uarmf, "resist"));
                    else
                        (void) destroy_arm(uarmf, FALSE);
                }
            } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                       || aatyp == AT_MAGC || aatyp == AT_TUCH) {
                (void) passive_obj(mon, weapon, &(mattk[i]));
            }
            break;
        }
        break;
    case AD_MAGM:
        /* wrath of gods for attacking Oracle */
        if (Antimagic) {
            shieldeff(u.ux, u.uy);
            You("are hit by magic missiles appearing from thin air!");
            pline("Some missiles bounce off!");
            monstseesu(M_SEEN_MAGR);
            tmp = (tmp + 1) / 2;
            mdamageu(mon, tmp);
        } else {
            if (Role_if(PM_ROGUE) && !uwep
                && context.forcefight && !Upolyd && mon->mpeaceful)
                pline_The("gods notice your deception!");
            /* The Oracle notices too... */
            setmangry(mon, FALSE);
            You("are hit by magic missiles appearing from thin air!");
            mdamageu(mon, tmp);
        }
        break;
    case AD_ENCH: /* KMH -- remove enchantment (disenchanter) */
        if (mhit) {
            if (aatyp == AT_KICK) {
                if (!weapon)
                    break;
            } else if (aatyp == AT_BITE || aatyp == AT_BUTT
                       || (aatyp >= AT_STNG && aatyp < AT_WEAP)) {
                break; /* no object involved */
            }
            (void) passive_obj(mon, weapon, &(mattk[i]));
        }
        break;
    case AD_CNCL:
        if (mhit && !rn2(6)) {
            (void) cancel_monst(&youmonst, weapon, FALSE, TRUE, FALSE);
        }
        break;
    case AD_SLIM:
        if (mhit && !mon->mcan && !rn2(3)) {
            pline("Its slime splashes onto you!");
            if (flaming(youmonst.data)) {
                pline_The("slime burns away!");
                tmp = 0;
            } else if (Unchanging || noncorporeal(youmonst.data)
                       || youmonst.data == &mons[PM_GREEN_SLIME]) {
                You("are unaffected.");
                tmp = 0;
            } else if (!Slimed) {
                You("don't feel very well.");
                make_slimed(10L, (char *) 0);
                delayed_killer(SLIMED, KILLED_BY_AN, mon->data->mname);
            } else {
                pline("Yuck!");
            }
        }
        break;
    default:
        break;
    }

    /*  These only affect you if they still live.
     */
    if (malive && !mon->mcan && rn2(3)) {
        switch (mattk[i].adtyp) {
        case AD_DSRM: /* adherer */
            if (uwep) {
                otmp = uwep;
                Your("weapon sticks to %s!", mon_nam(mon));
                dropx(uwep);
                obj_extract_self(otmp);
                add_to_minv(mon, otmp);
            } else {
                u.ustuck = mon;
                You("stick to %s!", mon_nam(mon));
            }
            break;
        case AD_HYDR: /* grow additional heads (hydra) */
            if (mhit && !mon->mcan && weapon && rn2(3)) {
                if ((is_blade(weapon) || is_axe(weapon))
                      && weapon->oartifact != ART_FIRE_BRAND) {
                    You("decapitate %s, but two more heads spring forth!",
                        mon_nam(mon));
                    grow_up(mon, (struct monst *) 0);
                }
            }
            break;
        case AD_PLYS:
            if (ptr == &mons[PM_FLOATING_EYE]) {
                if (!canseemon(mon)) {
                    break;
                }
                if (mon->mcansee) {
                    if (ureflects("%s gaze is reflected by your %s.",
                                  s_suffix(Monnam(mon)))) {
                        ;
                    } else if (Hallucination && rn2(4)) {
                        /* [it's the hero who should be getting paralyzed
                           and isn't; this message describes the monster's
                           reaction rather than the hero's escape] */
                        pline("%s looks %s%s.", Monnam(mon),
                              !rn2(2) ? "" : "rather ",
                              !rn2(2) ? "numb" : "stupefied");
                    } else if (Free_action) {
                        You("momentarily stiffen under %s gaze!",
                            s_suffix(mon_nam(mon)));
                    } else if (ublindf
                               && ublindf->oartifact == ART_EYES_OF_THE_OVERWORLD) {
                        pline("%s protect you from %s paralyzing gaze.",
                              An(bare_artifactname(ublindf)), s_suffix(mon_nam(mon)));
                        break;
                    } else {
                        You("are frozen by %s gaze!", s_suffix(mon_nam(mon)));
                        nomul((ACURR(A_WIS) > 12 || rn2(4)) ? -tmp : -127);
                        multi_reason = "frozen by a monster's gaze";
                        nomovemsg = 0;
                    }
                } else {
                    pline("%s cannot defend itself.",
                          Adjmonnam(mon, "blind"));
                    if (!rn2(500))
                        change_luck(-1);
                }
            } else if (Free_action) {
                You("momentarily stiffen.");
            } else { /* gelatinous cube */
                You("are frozen by %s!", mon_nam(mon));
                nomovemsg = You_can_move_again;
                nomul(-tmp);
                multi_reason = "frozen by a monster";
                exercise(A_DEX, FALSE);
            }
            break;
        case AD_COLD: /* brown mold, blue jelly, white dragon */
            if (monnear(mon, u.ux, u.uy)) {
                if (how_resistant(COLD_RES) == 100) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_COLD);
                    You_feel("a mild chill.");
                    ugolemeffects(AD_COLD, tmp);
                    break;
                }
                You("are suddenly very cold!");
                tmp = resist_reduce(tmp, COLD_RES);
                mdamageu(mon, tmp); /* cold damage */
                /* monster gets stronger with your heat! */
                if (ptr == &mons[PM_BLUE_JELLY]
                    || ptr == &mons[PM_BROWN_MOLD]) {
                    mon->mhp += tmp / 2;
                    if (mon->mhpmax < mon->mhp)
                        mon->mhpmax = mon->mhp;
                /* at a certain point, the monster will reproduce! */
                    if (mon->mhpmax > ((int) (mon->m_lev + 1) * 8))
                        (void) split_mon(mon, &youmonst);
                }
            }
            break;
        case AD_STUN: /* specifically yellow mold */
            if (!Stunned)
                make_stunned((long) tmp, TRUE);
            break;
        case AD_FIRE:
            if (monnear(mon, u.ux, u.uy)) {
                if (how_resistant(FIRE_RES) == 100
                    || Underwater) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_FIRE);
                    You_feel("mildly warm.");
                    ugolemeffects(AD_FIRE, tmp);
                    break;
                }
                You("are suddenly very hot!");
                tmp = resist_reduce(tmp, FIRE_RES);
                mdamageu(mon, tmp); /* fire damage */
            }
            break;
        case AD_ELEC:
            if (how_resistant(SHOCK_RES) == 100) {
                shieldeff(u.ux, u.uy);
                monstseesu(M_SEEN_ELEC);
                You_feel("a mild tingle.");
                ugolemeffects(AD_ELEC, tmp);
                break;
            }
            You("are jolted with electricity!");
            tmp = resist_reduce(tmp, SHOCK_RES);
            mdamageu(mon, tmp);
	    break;
	case AD_DISE: /* specifically gray fungus */
            diseasemu(ptr);
            break;
        case AD_DRST:
             /* specifically molds */
            if (ptr == &mons[PM_DISGUSTING_MOLD]) {
                if (!Strangled && !Breathless) {
                    You("inhale a cloud of spores!");
                    poisoned("spores", A_STR, "spore cloud", 30, FALSE);
                    break;
                } else {
                    pline("A cloud of spores surrounds you!");
                    break;
                }
            }
             /* specifically green dragons */
            if (how_resistant(POISON_RES) == 100) {
                You("are immune to %s poisonous hide.", s_suffix(mon_nam(mon)));
                monstseesu(M_SEEN_POISON);
            } else {
                i = rn2(20);
                if (i) {
                    You("have been poisoned!");
                    tmp = resist_reduce(tmp, POISON_RES);
                    mdamageu(mon, tmp);
                } else {
                    if (how_resistant(POISON_RES) <= 34) {
                        pline("%s poisonous hide was deadly...",
                              s_suffix(Monnam(mon)));
                        done_in_by(mon, DIED);
                        return 2;
                    }
                }
            }
            break;
        case AD_SLOW: /* specifically orange dragons */
            if (!Slow)
                u_slow_down();
            break;
        default:
            break;
        }
    }

    /* Humanoid monsters wearing various dragon-scaled armor */
    for (m_armor = mon->minvent; m_armor; m_armor = m_armor->nobj) {
        t = rnd(6) + 1;
        if (mhit && !rn2(3)
            && Is_dragon_scaled_armor(m_armor)) {
            int otyp = Dragon_armor_to_scales(m_armor);

            switch (otyp) {
            case GREEN_DRAGON_SCALES:
                if (how_resistant(POISON_RES) == 100) {
                    You("are immune to %s poisonous armor.",
                        s_suffix(mon_nam(mon)));
                    monstseesu(M_SEEN_POISON);
                    break;
                } else {
                    if (rn2(20)) {
                        You("have been poisoned!");
                        t = resist_reduce(t, POISON_RES);
                        mdamageu(mon, t);
                    } else {
                        if (how_resistant(POISON_RES) <= 34) {
                            pline("%s poisonous armor was deadly...",
                                  s_suffix(Monnam(mon)));
                            done_in_by(mon, DIED);
                            return 2;
                        }
                    }
                }
                break;
            case DEEP_DRAGON_SCALES:
                if (!rn2(3)) {
                    if (how_resistant(DRAIN_RES) == 100) {
                        You("are immune to %s wicked armor.",
                            s_suffix(mon_nam(mon)));
                        monstseesu(M_SEEN_DRAIN);
                        break;
                    } else {
                        You_feel("weaker!");
                        losexp("life drainage");
                    }
                }
                break;
            case BLACK_DRAGON_SCALES: {
                long protector = attk_protection((int) aatyp);

                /* hero using monsters' AT_MAGC attack is hitting hand to
                   hand rather than casting a spell */
                if (aatyp == AT_MAGC)
                    protector = W_ARMG;

                if (protector == 0L /* no protection */
                    || (protector == W_ARMG && !uarmg && !uwep
                        && !wep_was_destroyed)
                    || (protector == W_ARMF && !uarmf)
                    || (protector == W_ARMH && !uarmh)
                    || (protector == (W_ARMC | W_ARMG)
                        && (!uarmc || !uarmg))) {
                    if (how_resistant(DISINT_RES) == 100) {
                        if (!rn2(3))
                            You("are unaffected by %s deadly armor.",
                                s_suffix(mon_nam(mon)));
                        monstseesu(M_SEEN_DISINT);
                        break;
                    } else {
                        if (rn2(40)) {
                            You("partially disintegrate!");
                            t = resist_reduce(t, DISINT_RES);
                            mdamageu(mon, t);
                        } else {
                            if (how_resistant(DISINT_RES) < 50) {
                                pline("%s deadly armor disintegrates you!",
                                      s_suffix(Monnam(mon)));
                                u.ugrave_arise = -3;
                                done_in_by(mon, DIED);
                                return 2;
                            } else {
                                You("partially disintegrate!");
                                t = resist_reduce(t, DISINT_RES);
                                mdamageu(mon, t);
                            }
                        }
                    }
                }
            }
                if (!rn2(12)) {
                    if (!(uwep || (u.twoweap && uswapwep))
                        && !wep_was_destroyed
                        && (aatyp == AT_WEAP || aatyp == AT_CLAW
                            || aatyp == AT_MAGC || aatyp == AT_TUCH)) {
                        if (uarmg) {
                            if (uarmg->oartifact == ART_DRAGONBANE)
                                pline("%s %s, but remains %s.", xname(uarmg),
                                      rn2(2) ? "shudders violently"
                                             : "vibrates unexpectedly",
                                      rn2(2) ? "whole" : "intact");
                            else if (rn2(2)
                                     && (uarmg->oerodeproof
                                         || is_supermaterial(uarmg)))
                                pline("%s being disintegrated!",
                                      Yobjnam2(uarmg, "resist"));
                            else
                                (void) destroy_arm(uarmg, FALSE);
                        }
                    }
                    break;
                }
                if (weapon && !rn2(12)) {
                    if (aatyp == AT_KICK) {
                        if (uarmf) {
                            if (rn2(2)
                                && (uarmf->oerodeproof
                                    || is_supermaterial(uarmf)))
                                pline("%s being disintegrated!",
                                      Yobjnam2(uarmf, "resist"));
                            else
                                (void) destroy_arm(uarmf, FALSE);
                        }
                        break;
                    } else if (aatyp == AT_WEAP || aatyp == AT_CLAW
                               || aatyp == AT_MAGC || aatyp == AT_TUCH) {
                        if (obj_resists(weapon, 0, 0)) {
                            pline_The("%s %s and cannot be disintegrated.",
                                      xname(weapon),
                                      rn2(2) ? "resists completely"
                                             : "defies physics");
                            break;
                        } else if (weapon->otyp == BLACK_DRAGON_SCALES
                                   || (Is_dragon_scaled_armor(weapon)
                                       && Dragon_armor_to_scales(weapon)
                                              == BLACK_DRAGON_SCALES)) {
                            pline("%s disintegration-proof and %s intact.",
                                  Yobjnam2(weapon, "are"),
                                  otense(weapon, "remain"));
                            break;
                        } else if (weapon->oartifact && rn2(50)) {
                            pline("%s %s, but remains %s.", Yname2(weapon),
                                  rn2(2) ? "shudders violently"
                                         : "vibrates unexpectedly",
                                  rn2(2) ? "whole" : "intact");
                            break;
                        } else if (rn2(2)
                                   && (weapon->oerodeproof
                                       || is_supermaterial(weapon))) {
                            pline("%s being disintegrated!",
                                  Yobjnam2(weapon, "resist"));
                            break;
                        } else {
                            if (cansee(mon->mx, mon->my))
                                pline("%s disintegrates!", Yname2(weapon));
                            if (weapon == uball || weapon == uchain)
                                unpunish();
                            if (carried(weapon))
                                useup(weapon);
                            else
                                delobj(weapon);
                            break;
                        }
                    }
                }
                update_inventory();
                break;
            case SHIMMERING_DRAGON_SCALES:
                /* These can have a few random effects: confuse, stun, and slow */
                if (!rn2(3)) {
                    if (ublindf && ublindf->oartifact == ART_EYES_OF_THE_OVERWORLD) {
                        pline("%s protect you from %s shimmering armor!",
                              An(bare_artifactname(ublindf)),
                              s_suffix(mon_nam(mon)));
                        break;
                    }
                    switch (rn2(3)) {
                    case 0:
                        /* Passive slow */
                        if (!Slow && !defends(AD_SLOW, uarm)) {
                            You_feel("a little sluggish...");
                            u_slow_down();
                        }
                        break;
                    case 1:
                        /* Passive confuse */
                        if (Confusion)
                            You("are getting even more confused.");
                        else
                            You("are getting confused.");
                        make_confused(HConfusion + d(3, 6), FALSE);
                        break;
                    case 2:
                        /* Passive stun */
                        make_stunned((HStun & TIMEOUT) + (long) d(2, 6),
                                     TRUE);
                        break;
                    }
                }
                break;
            case SEA_DRAGON_SCALES:
                if (canseemon(mon))
                    You("are splashed!");

                if (u.umonnum == PM_IRON_GOLEM || u.umonnum == PM_STEEL_GOLEM) {
                    You("rust to pieces!");
                    /* KMH -- this is okay with unchanging */
                    rehumanize();
                    break;
                } else if (flaming(youmonst.data)) {
                    /* Mega damage vs flaming/firey monsters? */
                    You("are being extinguished!");
                    rehumanize();
                    break;
                }

                /* Copied from rust trap */
                switch (rn2(5)) {
                case 0:
                    pline("A gush of water hits you on the %s!",
                          body_part(HEAD));
                    (void) water_damage(uarmh, helm_simple_name(uarmh),
                                        TRUE, u.ux, u.uy);
                    break;
                case 1:
                    pline("A gush of water hits your left %s!",
                          body_part(ARM));
                    if (water_damage(uarms, "shield", TRUE, u.ux, u.uy)
                        != ER_NOTHING)
                        break;
                    if (u.twoweap || (uwep && bimanual(uwep)))
                        (void) water_damage(u.twoweap ? uswapwep : uwep,
                                            0, TRUE, u.ux, u.uy);
                glovecheck:
                    (void) water_damage(uarmg, "gauntlets", TRUE, u.ux,
                                        u.uy);
                    break;
                case 2:
                    pline("A gush of water hits your right %s!",
                          body_part(ARM));
                    (void) water_damage(uwep, 0, TRUE, u.ux, u.uy);
                    goto glovecheck;
                default:
                    pline("A gush of water hits you!");
                    for (otmp = invent; otmp; otmp = otmp->nobj)
                        if (otmp->lamplit && otmp != uwep
                            && (otmp != uswapwep || !u.twoweap))
                            (void) snuff_lit(otmp);
                    if (uarmc)
                        (void) water_damage(uarmc,
                                            cloak_simple_name(uarmc),
                                            TRUE, u.ux, u.uy);
                    else if (uarm)
                        (void) water_damage(uarm, suit_simple_name(uarm),
                                            TRUE, u.ux, u.uy);
                    else if (uarmu)
                        (void) water_damage(uarmu, "shirt", TRUE, u.ux,
                                            u.uy);
                    update_inventory();
                }

                break;
            case ORANGE_DRAGON_SCALES:
                if (how_resistant(SLEEP_RES) == 100) {
                    break;
                } else {
                    if (!Slow)
                        u_slow_down();
                }
                break;
            case GOLD_DRAGON_SCALES:
                if (!rn2(3)) {
                    /* Using AT_EXPL to simulate yellow light explosion. */
                    if (can_blnd(mon, &youmonst, AT_EXPL,
                                 (struct obj *) 0)) {
                        if (!Blind)
                            pline("%s's golden armor blinds you!", Monnam(mon));
                        /* We could use our attack damage, but instead 3d6 seems
                         * like a reasonable amount of blinding time.
                         */
                        make_blinded(Blinded + (long) d(3, 6), FALSE);
                        if (!Blind)
                            Your1(vision_clears);
                    }
                    tmp = 0;
                }
                break;
            case WHITE_DRAGON_SCALES:
                if (how_resistant(COLD_RES) == 100) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_COLD);
                    You_feel("a mild chill from %s armor.",
                             s_suffix(mon_nam(mon)));
                    ugolemeffects(AD_COLD, t);
                    break;
                } else {
                    if (rn2(20)) {
                        You("are suddenly very cold!");
                        t = resist_reduce(t, COLD_RES);
                        mdamageu(mon, t);
                    } else {
                        pline("%s chilly armor freezes you!",
                              s_suffix(Monnam(mon)));
                        t = resist_reduce(t, COLD_RES);
                        mdamageu(mon, d(3, 6) + t);
                    }
                }
                break;
            case YELLOW_DRAGON_SCALES:
                if (rn2(3))
                    break;

                if (how_resistant(ACID_RES) == 100) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_ACID);
                    You("are covered in %s, but it seems harmless.",
                          hliquid("acid"));
                    ugolemeffects(AD_ACID, t);
                    break;
                }
                if (rn2(10)) {
                    You("are splashed with caustic goo!");
                    t = resist_reduce(t, ACID_RES);
                    mdamageu(mon, t);
                } else {
                    You("are covered in %s!", hliquid("acid"));
                    You("are severely burned!");
                    t = resist_reduce(t, ACID_RES);
                    /* Same damage a spotted jelly would do */
                    mdamageu(mon, d((int) mon->m_lev, 6) + t);
                }

                /* Inventory damage */
                if (rn2(u.twoweap ? 2 : 3))
                    acid_damage(uwep);
                if (u.twoweap && rn2(2))
                    acid_damage(uswapwep);
                if (rn2(4))
                    erode_armor(&youmonst, ERODE_CORRODE);
                break;
            case RED_DRAGON_SCALES:
                if (how_resistant(FIRE_RES) == 100
                    || Underwater) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_FIRE);
                    You_feel("mildly warm from %s armor.",
                             s_suffix(mon_nam(mon)));
                    ugolemeffects(AD_FIRE, t);
                    break;
                } else {
                    if (rn2(20)) {
                        You("are suddenly very hot!");
                        t = resist_reduce(t, FIRE_RES);
                        mdamageu(mon, t);
                    } else {
                        pline("%s fiery armor severely burns you!",
                              s_suffix(Monnam(mon)));
                        t = resist_reduce(t, FIRE_RES);
                        mdamageu(mon, d(3, 6) + t);
                    }
                }
                break;
            case BLUE_DRAGON_SCALES:
                if (how_resistant(SHOCK_RES) == 100) {
                    shieldeff(u.ux, u.uy);
                    monstseesu(M_SEEN_ELEC);
                    You_feel("a mild tingle.");
                    ugolemeffects(AD_ELEC, tmp);
                    break;
                }

                if (rn2(20)) {
                    You("get zapped!");
                    t = resist_reduce(t, SHOCK_RES);
                    mdamageu(mon, t);
                } else {
                    You("are jolted with electricity!");
                    t = resist_reduce(t, SHOCK_RES);
                    mdamageu(mon, d(2, 24) + t);
                }
                if ((int) mon->m_lev > rn2(20))
                    destroy_item(WAND_CLASS, AD_ELEC);
                if ((int) mon->m_lev > rn2(20))
                    destroy_item(RING_CLASS, AD_ELEC);
                break;

            case GRAY_DRAGON_SCALES:
                if (!rn2(6)) {
                    (void) cancel_monst(&youmonst,
                                        (struct obj *) 0, FALSE, TRUE, FALSE);
                }
                break;
            default: /* all other types of armor, just pass on through */
                break;
            }
        }
    }
    return (malive | mhit);
}

/*
 * Special (passive) attacks on an attacking object by monsters done here.
 * Assumes the attack was successful.
 */
int
passive_obj(mon, obj, mattk)
struct monst *mon;
struct obj *obj;          /* null means pick uwep, uswapwep or uarmg */
struct attack *mattk;     /* null means we find one internally */
{
    struct permonst *ptr = mon->data;
    int i, ret = ER_NOTHING;

    /* [this first bit is obsolete; we're not called with Null anymore] */
    /* if caller hasn't specified an object, use uwep, uswapwep or uarmg */
    if (!obj) {
        obj = (u.twoweap && uswapwep && !rn2(2)) ? uswapwep : uwep;
        if (!obj && (mattk->adtyp == AD_FIRE || mattk->adtyp == AD_ACID
                     || mattk->adtyp == AD_RUST || mattk->adtyp == AD_CORR
                     || mattk->adtyp == AD_ENCH || mattk->adtyp == AD_DISN))
            obj = uarmg; /* no weapon? then must be gloves */
        if (!obj)
            return ER_NOTHING; /* no object to affect */
    }

    /* if caller hasn't specified an attack, find one */
    if (!mattk) {
        for (i = 0;; i++) {
            if (i >= NATTK)
                return ER_NOTHING; /* no passive attacks */
            if (ptr->mattk[i].aatyp == AT_NONE)
                break; /* try this one */
        }
        mattk = &(ptr->mattk[i]);
    }

    switch (mattk->adtyp) {
    case AD_FIRE:
        if (!rn2(6) && !mon->mcan && !Underwater
            /* steam vortex: fire resist applies, fire damage doesn't */
            && mon->data != &mons[PM_STEAM_VORTEX]) {
            ret = erode_obj(obj, NULL, ERODE_BURN, EF_GREASE | EF_DESTROY);
        }
        break;
    case AD_ACID:
        if (!rn2(6) && !Underwater) {
            ret = erode_obj(obj, NULL, ERODE_CORRODE, EF_GREASE | EF_DESTROY);
        }
        break;
    case AD_RUST:
        if (!mon->mcan) {
            ret = erode_obj(obj, (char *) 0, ERODE_RUST, EF_GREASE | EF_DESTROY);
        }
        break;
    case AD_CORR:
        if (!mon->mcan) {
            ret = erode_obj(obj, (char *) 0, ERODE_CORRODE, EF_GREASE | EF_DESTROY);
        }
        break;
    case AD_ENCH:
        if (!mon->mcan) {
            if (drain_item(obj, TRUE) && carried(obj)
                && (obj->known || obj->oclass == ARMOR_CLASS)) {
                pline("%s less effective.", Yobjnam2(obj, "seem"));
                ret = ER_DAMAGED;
            }
            break;
        }
        break;    
    case AD_DISN: {
        int chance = (mon->data == &mons[PM_ANTIMATTER_VORTEX] ? !rn2(3) : !rn2(6));
        if (chance && !mon->mcan) {
            /* lets not make the game unwinnable... */
            if (obj_resists(obj, 0, 0)) {
                pline_The("%s %s and cannot be disintegrated.",
                          xname(obj), rn2(2) ? "resists completely" : "defies physics");
                break;
            } else if (obj->oartifact == ART_HAND_OF_VECNA) {
                ; /* no feedback, as we're pretending that the Hand
                     isn't worn, but a part of your body */
                break;
            } else if (obj->oartifact == ART_DRAGONBANE
                       && mon->data != &mons[PM_ANTIMATTER_VORTEX]) {
                pline("%s %s, but remains %s.", xname(obj),
                      rn2(2) ? "shudders violently" : "vibrates unexpectedly",
                      rn2(2) ? "whole" : "intact");
                break;
            } else if (obj->otyp == BLACK_DRAGON_SCALES
                       || (Is_dragon_scaled_armor(obj)
                           && Dragon_armor_to_scales(obj) == BLACK_DRAGON_SCALES)) {
                pline("%s disintegration-proof and %s intact.",
                      Yobjnam2(obj, "are"), otense(obj, "remain"));
                break;
            } else if (obj->oartifact && rn2(50)) {
                pline("%s %s, but remains %s.", Yname2(obj),
                      rn2(2) ? "shudders violently" : "vibrates unexpectedly",
                      rn2(2) ? "whole" : "intact");
                break;
            } else if (rn2(2)
                       && (obj->oerodeproof || is_supermaterial(obj))) {
                pline("%s being disintegrated!",
                      Yobjnam2(obj, "resist"));
                break;
            } else {
                if ((u.uswallow && mon == u.ustuck && !Blind)
                    || cansee(mon->mx, mon->my))
                    pline("%s disintegrates!", Yname2(obj));
                if (obj == uchain || obj == uball)
                    unpunish();
                if (carried(obj))
                    useup(obj);
                else
                    delobj(obj);
                ret = ER_DESTROYED;
            }
            break;
        }
        break;
    }
    default:
        break;
    }
    update_inventory();
    return ret;
}

/* Note: caller must ascertain mtmp is mimicking... */
void
stumble_onto_mimic(mtmp)
struct monst *mtmp;
{
    const char *fmt = "Wait!  That's %s!", *generic = "a monster", *what = 0;

    if (!u.ustuck && !mtmp->mflee && dmgtype(mtmp->data, AD_STCK))
        u.ustuck = mtmp;

    if (Blind) {
        if (!Blind_telepat)
            what = generic; /* with default fmt */
        else if (M_AP_TYPE(mtmp) == M_AP_MONSTER)
            what = a_monnam(mtmp); /* differs from what was sensed */
    } else {
        int glyph = levl[u.ux + u.dx][u.uy + u.dy].glyph;

        if (glyph_is_cmap(glyph) && (glyph_to_cmap(glyph) == S_hcdoor
                                     || glyph_to_cmap(glyph) == S_vcdoor))
            fmt = "The door actually was %s!";
        else if (glyph_is_object(glyph) && glyph_to_obj(glyph) == GOLD_PIECE)
            fmt = "That gold was %s!";

        /* cloned Wiz starts out mimicking some other monster and
           might make himself invisible before being revealed */
        if (mtmp->minvis && !See_invisible)
            what = generic;
        else
            what = a_monnam(mtmp);
    }
    if (what)
        pline(fmt, what);

    wakeup(mtmp, FALSE); /* clears mimicking */
    /* if hero is blind, wakeup() won't display the monster even though
       it's no longer concealed */
    if (!canspotmon(mtmp)
        && !glyph_is_invisible(levl[mtmp->mx][mtmp->my].glyph))
        map_invisible(mtmp->mx, mtmp->my);
}

STATIC_OVL void
nohandglow(mon)
struct monst *mon;
{
    char *hands = makeplural(body_part(HAND));

    if (!u.umconf || mon->mconf)
        return;
    if (u.umconf == 1) {
        if (Blind)
            Your("%s stop tingling.", hands);
        else
            Your("%s stop glowing %s.", hands, hcolor(NH_RED));
    } else {
        if (Blind)
            pline_The("tingling in your %s lessens.", hands);
        else
            Your("%s no longer glow so brightly %s.", hands, hcolor(NH_RED));
    }
    u.umconf--;
}

int
flash_hits_mon(mtmp, otmp)
struct monst *mtmp;
struct obj *otmp; /* source of flash */
{
    int tmp, amt, res = 0, useeit = canseemon(mtmp);

    if (mtmp->msleeping) {
        mtmp->msleeping = 0;
        if (useeit) {
            pline_The("flash awakens %s.", mon_nam(mtmp));
            res = 1;
        }
    } else if (mtmp->data->mlet != S_LIGHT) {
        if (!resists_blnd(mtmp)) {
            tmp = dist2(otmp->ox, otmp->oy, mtmp->mx, mtmp->my);
            if (useeit) {
                pline("%s is blinded by the flash!", Monnam(mtmp));
                res = 1;
            }
            if (mtmp->data == &mons[PM_GREMLIN]) {
                /* Rule #1: Keep them out of the light. */
                amt = otmp->otyp == WAN_LIGHT ? d(1 + otmp->spe, 4)
                                              : rn2(min(mtmp->mhp, 4));
                light_hits_gremlin(mtmp, amt);
            }
            if (!DEADMONSTER(mtmp)) {
                if (!context.mon_moving)
                    setmangry(mtmp, TRUE);
                if (tmp < 9 && !mtmp->isshk && rn2(4))
                    monflee(mtmp, rn2(4) ? rnd(100) : 0, FALSE, TRUE);
                mtmp->mcansee = 0;
                mtmp->mblinded = (tmp < 3) ? 0 : rnd(1 + 50 / tmp);
            }
        }
    }
    return res;
}

void
light_hits_gremlin(mon, dmg)
struct monst *mon;
int dmg;
{
    pline("%s %s!", Monnam(mon),
          (dmg > mon->mhp / 2) ? "wails in agony" : "cries out in pain");
    damage_mon(mon, dmg, AD_BLND);
    wake_nearto(mon->mx, mon->my, 30);
    if (DEADMONSTER(mon)) {
        if (context.mon_moving)
            monkilled(mon, (char *) 0, AD_BLND);
        else
            killed(mon);
    } else if (cansee(mon->mx, mon->my) && !canspotmon(mon)) {
        map_invisible(mon->mx, mon->my);
    }
}

boolean
dbl_dmg()
{
    /* Mystic Eyes bonus applied here */
    if (DeathVision) {
        return TRUE;
    }
    /* If we wield the Staff of Rot and are withering, we get double damage. */
    if (uwep && uwep->oartifact == ART_STAFF_OF_ROT && Withering)  {
        return TRUE;
    }
    return FALSE;
}
/*uhitm.c*/
