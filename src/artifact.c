/* NetHack 3.6	artifact.c	$NHDT-Date: 1553363416 2019/03/23 17:50:16 $  $NHDT-Branch: NetHack-3.6.2-beta01 $:$NHDT-Revision: 1.129 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2013. */
/* NetHack may be freely redistributed.  See license for details. */

#include "hack.h"
#include "artifact.h"
#include "artilist.h"
#include "qtext.h"
#include <limits.h>

/*
 * Note:  both artilist[] and artiexist[] have a dummy element #0,
 *        so loops over them should normally start at #1.  The primary
 *        exception is the save & restore code, which doesn't care about
 *        the contents, just the total size.
 */

extern boolean notonhead; /* for long worms */

#define get_artifact(o) \
    (((o) && (o)->oartifact) ? &artilist[(int) (o)->oartifact] : 0)

STATIC_DCL boolean FDECL(bane_applies, (const struct artifact *,
                                        struct monst *));
STATIC_DCL int FDECL(spec_applies, (const struct artifact *, struct monst *));
STATIC_DCL int FDECL(arti_invoke, (struct obj *));
STATIC_DCL boolean FDECL(Mb_hit, (struct monst * magr, struct monst *mdef,
                                struct obj *, int *, int, BOOLEAN_P, char *));
STATIC_DCL unsigned long FDECL(abil_to_spfx, (long *));
STATIC_DCL uchar FDECL(abil_to_adtyp, (long *));
STATIC_DCL int FDECL(glow_strength, (int));
STATIC_DCL boolean FDECL(untouchable, (struct obj *, BOOLEAN_P));
STATIC_DCL int FDECL(count_surround_traps, (int, int));
STATIC_DCL boolean FDECL(can_we_zap, (int, int, int));

/* The amount added to the victim's total hit points to insure that the
   victim will be killed even after damage bonus/penalty adjustments.
   Most such penalties are small, and 200 is plenty; the exception is
   half physical damage.  3.3.1 and previous versions tried to use a very
   large number to account for this case; now, we just compute the fatal
   damage by adding it to 2 times the total hit points instead of 1 time.
   Note: this will still break if they have more than about half the number
   of hit points that will fit in a 15 bit integer. */
#define FATAL_DAMAGE_MODIFIER 200

/* coordinate effects from spec_dbon() with messages in artifact_hit() */
STATIC_OVL int spec_dbon_applies = 0;

/* flags including which artifacts have already been created */
static boolean artiexist[1 + NROFARTIFACTS + 1];
/* and a discovery list for them (no dummy first entry here) */
STATIC_OVL xchar artidisco[NROFARTIFACTS];

STATIC_DCL void NDECL(hack_artifacts);
STATIC_DCL void FDECL(fix_artifact, (struct obj *));

STATIC_DCL const char *FDECL(adtyp_str, (int, BOOLEAN_P));

/* handle some special cases; must be called after u_init() */
STATIC_OVL void
hack_artifacts()
{
    struct artifact *art;
    int alignmnt = aligns[flags.initalign].value;

    /* Fix up the alignments of "gift" artifacts */
    for (art = artilist + 1; art->otyp; art++)
        if (art->role == Role_switch && art->alignment != A_NONE)
            art->alignment = alignmnt;

    /* Excalibur can be used by any lawful character, not just knights
       Dirge also... */
    if (!Role_if(PM_KNIGHT)) {
        artilist[ART_EXCALIBUR].role = NON_PM;
        artilist[ART_DIRGE].role = NON_PM;
    }

    if (Role_if(PM_PRIEST)) {
        /* Demonbane should always be in the Priest's list of available toys
         * even though this might look a little odd when it comes out Chaotic */
        artilist[ART_DEMONBANE].alignment = alignmnt;
        /* For crowning purposes */
        artilist[ART_MJOLLNIR].alignment = alignmnt;
    }

    /* Fix up the quest artifact */
    if (urole.questarti) {
        artilist[urole.questarti].alignment = alignmnt;
        artilist[urole.questarti].role = Role_switch;
    }
    return;
}

/* zero out the artifact existence list */
void
init_artifacts()
{
    (void) memset((genericptr_t) artiexist, 0, sizeof artiexist);
    (void) memset((genericptr_t) artidisco, 0, sizeof artidisco);
    hack_artifacts();
}

void
save_artifacts(fd)
int fd;
{
    bwrite(fd, (genericptr_t) artiexist, sizeof artiexist);
    bwrite(fd, (genericptr_t) artidisco, sizeof artidisco);
}

void
restore_artifacts(fd)
int fd;
{
    mread(fd, (genericptr_t) artiexist, sizeof artiexist);
    mread(fd, (genericptr_t) artidisco, sizeof artidisco);
    hack_artifacts(); /* redo non-saved special cases */
}

/* Some artifacts may need additional tweaking when created.
 * Called when the artifact is christened.
 */
STATIC_DCL void
fix_artifact(otmp)
struct obj *otmp;
{
    if (otmp->oartifact == ART_IDOL_OF_MOLOCH)
        set_corpsenm(otmp, PM_HORNED_DEVIL);
}

const char *
artiname(artinum)
int artinum;
{
    if (artinum <= 0 || artinum > NROFARTIFACTS)
        return "";
    return artilist[artinum].name;
}

/* Finds a matching SPFX for a prop - used in mondata.c */
unsigned long
arti_prop_spfx(prop)
int prop;
{
    switch (prop) {
        case SEARCHING:
            return SPFX_SEEK;
	case WARNING:
            return SPFX_WARN;
	case TELEPAT:
            return SPFX_ESP;
	case HALLUC_RES:
            return SPFX_HALRES;
	case STEALTH:
            return SPFX_STLTH;
	case REGENERATION:
            return SPFX_REGEN;
	case TELEPORT_CONTROL:
            return SPFX_TCTRL;
	case ENERGY_REGENERATION:
            return SPFX_EREGEN;
	case HALF_SPDAM:
            return SPFX_HSPDAM;
	case HALF_PHDAM:
            return SPFX_HPHDAM;
	case REFLECTING:
            return SPFX_REFLECT;
    }
     return 0L;
}

/* return the material of a given artifact */
short
artifact_material(artinum)
int artinum;
{
    if (artinum <= 0 || artinum > NROFARTIFACTS) {
        impossible("invalid artifact number %d to artifact_material", artinum);
        return 0;
    }
    return artilist[artinum].material;
}

/*
   Make an artifact.  If a specific alignment is specified, then an object of
   the appropriate alignment is created from scratch, or 0 is returned if
   none is available.  (If at least one aligned artifact has already been
   given, then unaligned ones also become eligible for this.)
   If no alignment is given, then 'otmp' is converted
   into an artifact of matching type, or returned as-is if that's not
   possible.
   For the 2nd case, caller should use ``obj = mk_artifact(obj, A_NONE);''
   for the 1st, ``obj = mk_artifact((struct obj *)0, some_alignment);''.
 */
struct obj *
mk_artifact(otmp, alignment)
struct obj *otmp;   /* existing object; ignored if alignment specified */
aligntyp alignment; /* target alignment, or A_NONE */
{
    const struct artifact *a;
    int m, n, altn;
    boolean by_align = alignment != A_NONE || !otmp;
    short o_typ = by_align ? 0 : otmp->otyp;
    boolean unique = !by_align && otmp && objects[o_typ].oc_unique;
    short eligible[NROFARTIFACTS];

    /* don't add properties to special weapons */
    if (otmp && otmp->oprops)
        return otmp;
    if (otmp && otmp->oartifact)
        return otmp;

    n = altn = 0;    /* no candidates found yet */
    eligible[0] = 0; /* lint suppression */
    /* gather eligible artifacts */
    for (m = 1, a = &artilist[m]; a->otyp; a++, m++) {
        if (artiexist[m])
            continue;
        if ((a->spfx & SPFX_NOGEN) || unique)
            continue;

        if (!by_align) {
            /* looking for a particular type of item; not producing a
               divine gift so we don't care about role's first choice */
            if (a->otyp == o_typ)
                eligible[n++] = m;
            continue; /* move on to next possibility */
        }

        /* we're looking for an alignment-specific item
           suitable for hero's role+race */
        if ((a->alignment == alignment || a->alignment == A_NONE)
            /* avoid enemies' equipment */
            && (a->race == NON_PM || !race_hostile(&mons[a->race]))
            && !Hate_material(artifact_material(m))
            && !(Race_if(PM_GIANT) && (a->mtype & MH_GIANT))
            && !(Role_if(PM_PRIEST) && (is_slash(a) || is_pierce(a)))) {
            /* when a role-specific first choice is available, use it */
            if (Role_if(a->role)) {
                /* make this be the only possibility in the list */
                eligible[0] = m;
                n = 1;
                break; /* skip all other candidates */
            } else if (by_align && Role_if(PM_PIRATE)) {
                continue; /* pirates are not gifted artifacts */
            }
            /* found something to consider for random selection */
            if (a->alignment != A_NONE || u.ugifts > 0) {
                /* right alignment, or non-aligned with at least 1
                   previous gift bestowed, makes this one viable */
                eligible[n++] = m;
            } else {
                /* non-aligned with no previous gifts;
                   if no candidates have been found yet, record
                   this one as a[nother] fallback possibility in
                   case all aligned candidates have been used up
                   (via wishing, naming, bones, random generation) */
                if (!n)
                    eligible[altn++] = m;
                /* [once a regular candidate is found, the list
                   is overwritten and `altn' becomes irrelevant] */
            }
        }
    }

    /* resort to fallback list if main list was empty */
    if (!n)
        n = altn;

    if (n) {
        /* found at least one candidate; pick one at random */
        m = eligible[rn2(n)]; /* [0..n-1] */
        a = &artilist[m];

        /* make an appropriate object if necessary, then christen it */
        if (by_align)
            otmp = mksobj((int) a->otyp, TRUE, FALSE);

        if (otmp) {
            /* prevent erosion from generating */
            otmp->oeroded = otmp->oeroded2 = 0;
            otmp = oname(otmp, a->name);
            otmp->oartifact = m;
            artiexist[m] = TRUE;
            fix_artifact(otmp);
        }
    } else {
        otmp = create_oprop(otmp, FALSE);
    }
    return otmp;
}

/* Create an item with special properties, or grant the item those properties */
struct obj *
create_oprop(obj, allow_detrimental)
register struct obj *obj;
boolean allow_detrimental;
{
    register struct obj *otmp = obj;
    int i, j;

    if (!otmp) {
        int type = 0, skill = P_NONE,
            candidates[128], ccount,
            threshold = P_EXPERT;
        /* This probably is only ever done for weapons, y'know?
         * Find an appropriate type of weapon */
        while (threshold > P_UNSKILLED) {
            ccount = 0;
            for (i = P_FIRST_WEAPON; i < P_LAST_WEAPON; i++) {
                if (P_MAX_SKILL(i) >= max(threshold, P_BASIC)
                    && P_SKILL(i) >= threshold)
                    candidates[ccount++] = i;
                if (ccount >= 128)
                    break;
            }
            if (ccount == 0) {
                threshold--;
                continue;
            }
            skill = candidates[rn2(ccount)];
            ccount = 0;
            for (i = ARROW; i <= CROSSBOW; i++) {
                if (abs(objects[i].oc_skill) == skill)
                    candidates[ccount++] = i;
                if (ccount == 128)
                    break;
            }
            if (!ccount) {
                impossible("found no weapons for skill %d?", skill);
                threshold--;
                continue;
            }
            type = candidates[rn2(ccount)];
            break;
        }
        /* Now make one, if we can */
        if (type != 0)
            otmp = mksobj(type, TRUE, FALSE);
        else
            otmp = mkobj(WEAPON_CLASS, FALSE);
    }

    /* Don't spruce up certain objects */
    if (otmp->oartifact)
        return otmp;
    else if (objects[otmp->otyp].oc_unique)
        return otmp;
    /* already magical items obtain properties
     * a tenth as often */
    else if ((objects[otmp->otyp].oc_magic)
        && rn2(10))
        return otmp;
    else if (otmp && Is_dragon_armor(otmp))
        return otmp;
    else if is_bomb(otmp)
        return otmp;

    /* properties only added to weapons and armor */
    if (otmp->oclass != WEAPON_CLASS
        && !is_weptool(otmp) && otmp->oclass != ARMOR_CLASS)
        return otmp;

    /* it is rare to have an object spawn with more
     * than one object property, but much better odds
     * than EvilHack! */
    while (!otmp->oprops || !rn2(3141)) {
        i = rn2(MAX_ITEM_PROPS);
        j = 1 << i; /* pick an object property */

        if (otmp->oprops & j)
            continue;

        if ((j & (ITEM_FUMBLING | ITEM_HUNGER))
            && !allow_detrimental)
            continue;

        /* check for restrictions */
        if ((otmp->oclass == WEAPON_CLASS || is_weptool(otmp))
            && (j & (ITEM_DRLI | ITEM_FUMBLING | ITEM_HUNGER)))
            continue;

        if (is_launcher(otmp)
            &&  (j & (ITEM_FIRE | ITEM_FROST | ITEM_SHOCK | ITEM_SCREAM
                      | ITEM_VENOM | ITEM_ACID | ITEM_DRLI | ITEM_OILSKIN)))
            continue;

        if ((is_ammo(otmp) || is_missile(otmp))
            && (j & (ITEM_DRLI | ITEM_OILSKIN | ITEM_ESP | ITEM_EXCEL
                     | ITEM_SEARCHING | ITEM_WARNING | ITEM_FUMBLING | ITEM_HUNGER)))
            continue;

        if ((otmp->oprops & (ITEM_FIRE | ITEM_FROST | ITEM_SHOCK | ITEM_SCREAM | ITEM_VENOM | ITEM_ACID | ITEM_DRLI))
                    && (j & (ITEM_FIRE | ITEM_FROST | ITEM_SHOCK | ITEM_SCREAM | ITEM_VENOM | ITEM_ACID | ITEM_DRLI)))
            continue; /* these are mutually exclusive */

        if (otmp->material != CLOTH && (j & ITEM_OILSKIN))
            continue;

        if (otmp->otyp == OILSKIN_CLOAK && (j & ITEM_OILSKIN))
            continue;
        
        if (otmp->otyp == ROGUES_GLOVES && (j & ITEM_SEARCHING))
            continue;

        otmp->oprops |= j;
    }

    /* Fix it up as necessary */
    if (otmp->oprops
        && (otmp->oclass == WEAPON_CLASS
            || otmp->oclass == ARMOR_CLASS)
        && !(otmp->oprops & (ITEM_FUMBLING | ITEM_HUNGER))) {
        if (!rn2(8)) {
            blessorcurse(otmp, 8);
            if (otmp->cursed)
                otmp->spe = -rne(3);
            else
                otmp->spe = rne(3);
        }
    }

    if (otmp->oprops & (ITEM_FUMBLING | ITEM_HUNGER)) {
        if (!otmp->cursed)
            curse(otmp);
        otmp->spe = -rne(3);
    }
    return otmp;
}

/*
 * Returns the full name (with articles and correct capitalization) of an
 * artifact named "name" if one exists, or NULL, it not.
 * The given name must be rather close to the real name for it to match.
 * The object type of the artifact is returned in otyp if the return value
 * is non-NULL.
 */
const char *
artifact_name(name, otyp)
const char *name;
short *otyp;
{
    register const struct artifact *a;
    register const char *aname;

    if (!strncmpi(name, "the ", 4))
        name += 4;

    for (a = artilist + 1; a->otyp; a++) {
        aname = a->name;
        if (!strncmpi(aname, "the ", 4))
            aname += 4;
        if (!strcmpi(name, aname)) {
            *otyp = a->otyp;
            return a->name;
        }
    }
    return (char *) 0;
}

boolean
exist_artifact(otyp, name)
int otyp;
const char *name;
{
    register const struct artifact *a;
    boolean *arex;

    if (otyp && *name)
        for (a = artilist + 1, arex = artiexist + 1; a->otyp; a++, arex++)
            if ((int) a->otyp == otyp && !strcmp(a->name, name))
                return *arex;
    return FALSE;
}

void
artifact_exists(otmp, name, mod)
struct obj *otmp;
const char *name;
boolean mod;
{
    register const struct artifact *a;

    if (otmp && *name)
        for (a = artilist + 1; a->otyp; a++)
            if (a->otyp == otmp->otyp && !strcmp(a->name, name)) {
                register int m = (int) (a - artilist);
                otmp->oartifact = (char) (mod ? m : 0);
                otmp->age = 0;
                if (otmp->otyp == RIN_INCREASE_DAMAGE)
                    otmp->spe = 0;
                artiexist[m] = mod;
                if (mod)
                    fix_artifact(otmp);
                break;
            }
    return;
}

int
nartifact_exist()
{
    int a = 0;
    int n = SIZE(artiexist);

    while (n > 1)
        if (artiexist[--n])
            a++;

    return a;
}

boolean
spec_ability(otmp, abil)
struct obj *otmp;
unsigned long abil;
{
    const struct artifact *arti = get_artifact(otmp);

    if (arti && (arti->spfx & abil) != 0L)
        return TRUE;
    return (boolean) (arti && (arti->cspfx & abil) != 0L);
}

/*
 * Used to check if an artifact can glow in warning to monsters of some races.
 * If it does it returns the bitmap of warned races. If it doesn't it returns 0.
 */
int
has_glow_warning(obj)
struct obj *obj;
{
    const struct artifact *arti = get_artifact(obj);
    if (arti && (arti->spfx & SPFX_WARN) && (arti->spfx & SPFX_DFLAGH))
        return arti->mtype;
    return 0;
}

/* used so that callers don't need to known about SPFX_ codes */
boolean
confers_luck(obj)
struct obj *obj;
{
    /* might as well check for this too */
    if (obj->otyp == LUCKSTONE)
        return TRUE;
    
    if (obj->oprops & ITEM_EXCEL
        && obj->owornmask & (W_ARMOR | W_WEP | W_SWAPWEP))
        return TRUE;

    return (boolean) (obj->oartifact && spec_ability(obj, SPFX_LUCK));
}

/* used to check whether a monster is getting reflection from an artifact */
boolean
arti_reflects(obj)
struct obj *obj;
{
    const struct artifact *arti = get_artifact(obj);

    if (arti) {
        /* while being worn */
        if ((obj->owornmask & ~W_ART) && (arti->spfx & SPFX_REFLECT))
            return TRUE;
        /* just being carried */
        if (arti->cspfx & SPFX_REFLECT)
            return TRUE;
    }
    return FALSE;
}

/* decide whether this obj is effective when attacking against shades
 * or any incorporeal monster */
boolean
shade_glare(obj)
struct obj *obj;
{
    const struct artifact *arti;

    /* any silver object is effective; bone too, though it gets no bonus */
    if (obj->material == SILVER || obj->material == BONE)
        return TRUE;
    /* non-silver artifacts with bonus against undead also are effective */
    arti = get_artifact(obj);
    if (arti && (arti->spfx & SPFX_DFLAGH) && arti->mtype == MH_UNDEAD)
        return TRUE;
    /* [if there was anything with special bonus against noncorporeals,
       it would be effective too] */
    /* otherwise, harmless to shades */
    return FALSE;
}

/* returns 1 if name is restricted for otmp->otyp */
boolean
restrict_name(otmp, name)
struct obj *otmp;
const char *name;
{
    register const struct artifact *a;
    const char *aname, *odesc, *other;
    boolean sametype[NUM_OBJECTS];
    int i, lo, hi, otyp = otmp->otyp, ocls = objects[otyp].oc_class;

    if (!*name)
        return FALSE;
    if (!strncmpi(name, "the ", 4))
        name += 4;

    /* decide what types of objects are the same as otyp;
       if it's been discovered, then only itself matches;
       otherwise, include all other undiscovered objects
       of the same class which have the same description
       or share the same pool of shuffled descriptions */
    (void) memset((genericptr_t) sametype, 0, sizeof sametype); /* FALSE */
    sametype[otyp] = TRUE;
    if (!objects[otyp].oc_name_known
        && (odesc = OBJ_DESCR(objects[otyp])) != 0) {
        obj_shuffle_range(otyp, &lo, &hi);
        for (i = bases[ocls]; i < NUM_OBJECTS; i++) {
            if (objects[i].oc_class != ocls)
                break;
            if (!objects[i].oc_name_known
                && (other = OBJ_DESCR(objects[i])) != 0
                && (!strcmp(odesc, other) || (i >= lo && i <= hi)))
                sametype[i] = TRUE;
        }
    }

    /* Since almost every artifact is SPFX_RESTR, it doesn't cost
       us much to do the string comparison before the spfx check.
       Bug fix:  don't name multiple elven daggers "Sting".
     */
    for (a = artilist + 1; a->otyp; a++) {
        if (!sametype[a->otyp])
            continue;
        aname = a->name;
        if (!strncmpi(aname, "the ", 4))
            aname += 4;
        if (!strcmp(aname, name))
            return (boolean) ((a->spfx & (SPFX_NOGEN | SPFX_RESTR)) != 0
                              || otmp->quan > 1L);
    }

    return FALSE;
}

boolean
attacks(adtyp, otmp)
int adtyp;
struct obj *otmp;
{
    const struct artifact *weap;

    if ((weap = get_artifact(otmp)) != 0)
        return (boolean) (weap->attk.adtyp == adtyp);

    if (weap && adtyp == AD_DREN)
        return TRUE;

    if (!weap && otmp->oprops
        && (otmp->oclass == WEAPON_CLASS || is_weptool(otmp))) {
        if (adtyp == AD_FIRE && (otmp->oprops & ITEM_FIRE))
            return TRUE;
        if (adtyp == AD_COLD && (otmp->oprops & ITEM_FROST))
            return TRUE;
        if (adtyp == AD_ELEC && (otmp->oprops & ITEM_SHOCK))
            return TRUE;
        if (adtyp == AD_LOUD && (otmp->oprops & ITEM_SCREAM))
            return TRUE;
        if (adtyp == AD_DRST && (otmp->oprops & ITEM_VENOM))
            return TRUE;
        if (adtyp == AD_ACID && (otmp->oprops & ITEM_ACID))
            return TRUE;
    }
    return FALSE;
}

boolean
defends(adtyp, otmp)
int adtyp;
struct obj *otmp;
{
    const struct artifact *weap;

    if (!otmp)
        return FALSE;
    if ((weap = get_artifact(otmp)) != 0)
        return (boolean) (weap->defn.adtyp == adtyp);
    if (Is_dragon_armor(otmp)) {
        int otyp = Dragon_armor_to_scales(otmp);

        switch (adtyp) {
        case AD_MAGM: /* magic missiles => general magic resistance */
            return (otyp == GRAY_DRAGON_SCALES
                    || otyp == CHROMATIC_DRAGON_SCALES);
        case AD_BLND: /* Blinding attacks */
            return (otyp == SILVER_DRAGON_SCALES);
        case AD_FIRE:
            return (otyp == RED_DRAGON_SCALES); /* red but not gold */
        case AD_DRLI:
        case AD_CLOB: /* Knockback/hurtle */
            return (otyp == DEEP_DRAGON_SCALES); /* Deep roots hold you firm */
        case AD_COLD:
            return (otyp == WHITE_DRAGON_SCALES);
        case AD_DRST: /* drain strength => poison */
        case AD_DISE: /* blocks disease but not slime */
            return (otyp == GREEN_DRAGON_SCALES);
        case AD_SLEE: /* sleep */
            return (otyp == ORANGE_DRAGON_SCALES);
        case AD_DISN: /* disintegration */
            return (otyp == BLACK_DRAGON_SCALES);
        case AD_ELEC: /* electricity == lightning */
        case AD_SLOW: /* confers speed so blocks speed removal */
            return (otyp == BLUE_DRAGON_SCALES);
        case AD_ACID:
        case AD_STON: /* petrification resistance */
            return (otyp == YELLOW_DRAGON_SCALES);
        default:
            break;
        }
    }
    return FALSE;
}

/* used for monsters */
boolean
defends_when_carried(adtyp, otmp)
int adtyp;
struct obj *otmp;
{
    const struct artifact *weap;

    if ((weap = get_artifact(otmp)) != 0)
        return (boolean) (weap->cary.adtyp == adtyp);
    return FALSE;
}

/* determine whether an item confers Protection */
boolean
protects(otmp, being_worn)
struct obj *otmp;
boolean being_worn;
{
    const struct artifact *arti;

    if (being_worn && objects[otmp->otyp].oc_oprop == PROTECTION)
        return TRUE;
    arti = get_artifact(otmp);
    if (!arti)
        return FALSE;
    return (boolean) ((arti->cspfx & SPFX_PROTECT) != 0
                      || (being_worn && (arti->spfx & SPFX_PROTECT) != 0));
}

/*
 * a potential artifact has just been worn/wielded/picked-up or
 * unworn/unwielded/dropped.  Pickup/drop only set/reset the W_ART mask.
 */
void
set_artifact_intrinsic(otmp, on, wp_mask)
struct obj *otmp;
boolean on;
long wp_mask;
{
    long *mask = 0;
    register const struct artifact *art, *oart = get_artifact(otmp);
    register struct obj *obj;
    register uchar dtyp;
    register long spfx;

    if (!oart && !otmp->oprops)
        return;

    /* effects from the defn field */
    if (oart)
        dtyp = (wp_mask != W_ART) ? oart->defn.adtyp : oart->cary.adtyp;
    else
        dtyp = 0;

    if (dtyp == AD_FIRE)
        mask = &EFire_resistance;
    else if (dtyp == AD_COLD)
        mask = &ECold_resistance;
    else if (dtyp == AD_ELEC)
        mask = &EShock_resistance;
    else if (dtyp == AD_MAGM)
        mask = &EAntimagic;
    else if (dtyp == AD_DISN)
        mask = &EDisint_resistance;
    else if (dtyp == AD_DRST)
        mask = &EPoison_resistance;
    else if (dtyp == AD_SLEE)
        mask = &ESleep_resistance;
    else if (dtyp == AD_DRLI)
        mask = &EDrain_resistance;
    else if (dtyp == AD_CLOB)
        mask = &EStable;
    else if (dtyp == AD_ACID)
        mask = &EAcid_resistance;
    else if (dtyp == AD_STON)
        mask = &EStone_resistance;
    else if (dtyp == AD_DISE)
        mask = &ESick_resistance;
    else if (dtyp == AD_PLYS)
        mask = &Free_action;
    else if (dtyp == AD_LOUD)
        mask = &ESonic_resistance;

    if (mask && wp_mask == W_ART && !on) {
        /* find out if some other artifact also confers this intrinsic;
           if so, leave the mask alone */
        for (obj = invent; obj; obj = obj->nobj) {
            if (obj != otmp && obj->oartifact) {
                art = get_artifact(obj);
                if (art && art->cary.adtyp == dtyp) {
                    mask = (long *) 0;
                    break;
                }
            }
        }
    }
    if (mask) {
        if (on)
            *mask |= wp_mask;
        else
            *mask &= ~wp_mask;
    }

    /* intrinsics from the spfx field; there could be more than one */
    spfx = 0;
    if (oart) {
        spfx = (wp_mask != W_ART) ? oart->spfx : oart->cspfx;
    } else if (wp_mask == W_WEP || wp_mask == W_SWAPWEP) {
        if (otmp->oprops & ITEM_SEARCHING)
            spfx |= SPFX_SEARCH;
        if (otmp->oprops & ITEM_WARNING)
            spfx |= SPFX_WARN;
        if (otmp->oprops & ITEM_ESP)
            spfx |= SPFX_ESP;
    }

    if (spfx && wp_mask == W_ART && !on) {
        /* don't change any spfx also conferred by other artifacts */
        for (obj = invent; obj; obj = obj->nobj)
            if (obj != otmp && obj->oartifact) {
                art = get_artifact(obj);
                if (art)
                    spfx &= ~art->cspfx;
            }
    }

    if (spfx & SPFX_SEARCH) {
        if (on)
            ESearching |= wp_mask;
        else
            ESearching &= ~wp_mask;
    }
    if (otmp->oartifact == ART_ORIGIN && wp_mask == W_WEP) {
        if (on) {
            Your("mind is flooded with magical knowledge.");
        } else {
            You_feel("less in touch with your magical abilities.");
        }
    }
    if (spfx & SPFX_HALRES) {
        /* make_hallucinated must (re)set the mask itself to get
         * the display right */
        /* restoring needed because this is the only artifact intrinsic
         * that can print a message--need to guard against being printed
         * when restoring a game
         */
        if (u.uroleplay.hallu && on) {
            u.uroleplay.hallu = FALSE;
            pline_The("world no longer makes any sense to you!");
        }
        (void) make_hallucinated((long) !on,
                                 program_state.restoring ? FALSE : TRUE,
                                 wp_mask);
    }
    if (spfx & SPFX_ESP) {
        if (on)
            ETelepat |= wp_mask;
        else
            ETelepat &= ~wp_mask;
        see_monsters();
    }
    if (spfx & SPFX_STLTH) {
        if (on)
            EStealth |= wp_mask;
        else
            EStealth &= ~wp_mask;
    }
    
    if (spfx & SPFX_REGEN) {
        if (on)
            ERegeneration |= wp_mask;
        else
            ERegeneration &= ~wp_mask;
    }
    if (spfx & SPFX_TCTRL) {
        if (on)
            ETeleport_control |= wp_mask;
        else
            ETeleport_control &= ~wp_mask;
    }
    if (spfx & SPFX_WARN) {
        if (spec_mh(otmp)) {
            /* Since multiple artifacts can potentially warn of the same monster type
            * we need to recalculate everything on any use or unuse. */
            EWarn_of_mon = 0;
            context.warntype.obj = 0;
            for (obj = invent; obj; obj = obj->nobj) {
                if (obj == otmp && !on) {
                    glow_warning_effects(otmp);
                    obj->lastwarncnt = 0;
                } else if (obj->owornmask && has_glow_warning(obj)) {
                    EWarn_of_mon |= obj->owornmask;
                    context.warntype.obj |= spec_mh(obj);
                }
            }
            see_monsters();
        } else {
            if (on)
                EWarning |= wp_mask;
            else
                EWarning &= ~wp_mask;
        }
    }
    if (spfx & SPFX_EREGEN) {
        if (on)
            EEnergy_regeneration |= wp_mask;
        else
            EEnergy_regeneration &= ~wp_mask;
    }
    if (spfx & SPFX_HSPDAM) {
        if (on)
            EHalf_spell_damage |= wp_mask;
        else
            EHalf_spell_damage &= ~wp_mask;
    }
    if (spfx & SPFX_HPHDAM) {
        if (on)
            EHalf_physical_damage |= wp_mask;
        else
            EHalf_physical_damage &= ~wp_mask;
    }
    if (spfx & SPFX_XRAY) {
        /* this assumes that no one else is using xray_range */
        if (on)
            u.xray_range = 3;
        else
            u.xray_range = -1;
        vision_full_recalc = 1;
    }
    if (spfx & SPFX_REFLECT) {
        if (otmp->oartifact == ART_MAGIC_MIRROR_OF_MERLIN) {
            if (on)
                EReflecting |= wp_mask;
            else
                EReflecting &= ~wp_mask;
        } else if (otmp
            && (otmp->oartifact == ART_LONGBOW_OF_DIANA
                || otmp->oartifact == ART_CROSSBOW_OF_CARL
                || otmp->oartifact == ART_LIGHTSABER_PROTOTYPE)
            && (wp_mask & W_WEP)) { /* wielding various reflecting artifacts */
            if (on)
                EReflecting |= wp_mask;
            else
                EReflecting &= ~wp_mask;
        } else if (otmp && otmp->oartifact == ART_DRAGONBANE
            && (wp_mask & W_ARMG)) { /* or in Dragonbane's case, wear them */
            if (on)
                EReflecting |= wp_mask;
            else
                EReflecting &= ~wp_mask;
        }
    }

    if (spfx & SPFX_PROTECT) {
        if (on)
            EProtection |= wp_mask;
        else
            EProtection &= ~wp_mask;
    }
    if (spfx & SPFX_BREATHE) {
        if (on)
            EMagical_breathing |= wp_mask;
        else
            EMagical_breathing &= ~wp_mask;
    }

    if (wp_mask == W_ART && !on && oart && oart->inv_prop) {
        /* might have to turn off invoked power too */
        if (oart->inv_prop <= LAST_PROP
            && (u.uprops[oart->inv_prop].extrinsic & W_ARTI))
            (void) arti_invoke(otmp);
    }
}

/* touch_artifact()'s return value isn't sufficient to tell whether it
   dished out damage, and tracking changes to u.uhp, u.mh, Lifesaved
   when trying to avoid second wounding is too cumbersome */
STATIC_VAR boolean touch_blasted; /* for retouch_object() */

/*
 * creature (usually hero) tries to touch (pick up or wield) an artifact obj.
 * Returns 0 if the object refuses to be touched.
 * This routine does not change any object chains.
 * Ignores such things as gauntlets, assuming the artifact is not
 * fooled by such trappings.
 */
int
touch_artifact(obj, mon)
struct obj *obj;
struct monst *mon;
{
    register const struct artifact *oart = get_artifact(obj);
    boolean badclass, badalign, self_willed, yours;

    touch_blasted = FALSE;
    if (!oart)
        return 1;

    /* [ALI] Thiefbane has a special affinity with shopkeepers */
    if ((mon->isshk || mon->data == &mons[PM_ONE_EYED_SAM]) &&
        obj->oartifact == ART_THIEFBANE) 
        return 1;

    yours = (mon == &youmonst);
    /* all quest artifacts are self-willed; if this ever changes, `badclass'
       will have to be extended to explicitly include quest artifacts */
    self_willed = ((oart->spfx & SPFX_INTEL) != 0);
    if (yours) {
        u.uconduct.artitouch++;
        badclass = self_willed
                   && ((oart->role != NON_PM && !Role_if(oart->role))
                       || (oart->race != NON_PM && !Race_if(oart->race)));
        badalign = ((oart->spfx & SPFX_RESTR) != 0
                    && oart->alignment != A_NONE
                    && (oart->alignment != u.ualign.type
                        || u.ualign.record < 0));
    } else if (!is_covetous(mon->data) && !is_mplayer(mon->data)) {
        badclass = self_willed && oart->role != NON_PM
                   && oart != &artilist[ART_EXCALIBUR];
        badalign = (oart->spfx & SPFX_RESTR) && oart->alignment != A_NONE
                   && (oart->alignment != mon_aligntyp(mon));
    } else { /* an M3_WANTSxxx monster or a fake player */
        /* special monsters trying to take the Amulet, invocation tools or
           quest item can touch anything except `spec_applies' artifacts */
        if (mon->data == &mons[PM_WARDEN_ARIANNA] 
            && obj->oartifact == ART_IRON_SPOON_OF_LIBERATION)
            badclass = badalign = TRUE;
        badclass = badalign = FALSE;
    }
    /* weapons which attack specific categories of monsters are
       bad for them even if their alignments happen to match */
    if (!badalign)
        badalign = bane_applies(oart, mon);

    if (((badclass || badalign) && self_willed)
        || (badalign && (!yours || !rn2(4)))) {
        int dmg;
        char buf[BUFSZ];

        if (!yours)
            return 0;
        You("are blasted by %s power!", s_suffix(the(xname(obj))));
        touch_blasted = TRUE;
        dmg = d((Antimagic ? 2 : 4), (self_willed ? 10 : 4));
        /* add half of the usual material damage bonus */
        if (Hate_material(obj->material)) {
            dmg += (rnd(sear_damage(obj->material)) / 2) + 1;
        }
        Sprintf(buf, "touching %s", oart->name);
        losehp(dmg, buf, KILLED_BY); /* magic damage, not physical */
        exercise(A_WIS, FALSE);
    }
    
    /* can pick it up unless you're totally non-synch'd with the artifact */
    /* --hackem: Elemental mages have special restrictions */
    if ((badclass && badalign && self_willed)
        || (yours && obj->oartifact == ART_FROST_BRAND && Role_if(PM_FLAME_MAGE))
        || (yours && obj->oartifact == ART_DEEP_FREEZE && Role_if(PM_FLAME_MAGE))
        || (yours && obj->oartifact == ART_FIRE_BRAND && Role_if(PM_ICE_MAGE))
        || (yours && obj->oartifact == ART_FIREWALL && Role_if(PM_ICE_MAGE))) {
        if (yours) {
            if (!carried(obj))
                pline("%s your grasp!", Tobjnam(obj, "evade"));
            else
                pline("%s beyond your control!", Tobjnam(obj, "are"));
        }
        return 0;
    }
    if (Punished && (obj != uball)) {
        unpunish(); /* Remove a mundane heavy iron ball */
    }
    if (oart == &artilist[ART_CROSSBOW_OF_CARL]) {
        if (yours ? Role_if(PM_RANGER) && Race_if(PM_GNOME)
                  : racial_gnome(mon))
            obj->owt = 24; /* Magically lightened,
                              same weight as the Longbow of Diana */
    }
    return 1;
}

/* decide whether an artifact itself is vulnerable to a particular type
   of erosion damage, independent of the properties of its bearer */
boolean
arti_immune(obj, dtyp)
struct obj *obj;
int dtyp;
{
    register const struct artifact *weap = get_artifact(obj);

    if (!weap)
        return FALSE;
    if (dtyp == AD_PHYS)
        return FALSE; /* nothing is immune to phys dmg */
    return (boolean) (weap->attk.adtyp == dtyp
                      || weap->defn.adtyp == dtyp
                      || weap->cary.adtyp == dtyp);
}

STATIC_OVL boolean
bane_applies(oart, mon)
const struct artifact *oart;
struct monst *mon;
{
    struct artifact atmp;

    if (oart && (oart->spfx & SPFX_DBONUS) != 0) {
        atmp = *oart;
        atmp.spfx &= SPFX_DBONUS; /* clear other spfx fields */
        if (spec_applies(&atmp, mon))
            return TRUE;
    }
    return FALSE;
}

/* decide whether an artifact's special attacks apply against mtmp */
STATIC_OVL int
spec_applies(weap, mtmp)
register const struct artifact *weap;
struct monst *mtmp;
{
    struct permonst *ptr;
    boolean yours;

    if (!(weap->spfx & (SPFX_DBONUS | SPFX_ATTK)))
        return (weap->attk.adtyp == AD_PHYS);

    yours = (mtmp == &youmonst);
    ptr = mtmp->data;

    if (weap->spfx & SPFX_DMONS) {
        return (ptr == &mons[(int) weap->mtype]);
    } else if (weap->spfx & SPFX_DCLAS) {
        return ((weap->mtype == (unsigned long) ptr->mlet)
                || (has_erac(mtmp) && weap->mtype ==
                    (unsigned long) mons[ERAC(mtmp)->rmnum].mlet));
    } else if (weap->spfx & SPFX_DFLAG1) {
        return (((has_erac(mtmp) && (ERAC(mtmp)->mflags1 & weap->mtype))
                || (ptr->mflags1 & weap->mtype) != 0L));
    } else if (weap->spfx & SPFX_DFLAGH) {
        return ((has_erac(mtmp) && (ERAC(mtmp)->mrace & weap->mtype))
                || (!has_erac(mtmp) && (ptr->mhflags & weap->mtype))
                || (yours
                    && ((!Upolyd && (urace.selfmask & weap->mtype))
                        || ((weap->mtype & MH_WERE) && u.ulycn >= LOW_PM))));
    } else if (weap->spfx & SPFX_DALIGN) {
        return yours ? (u.ualign.type != weap->alignment)
                     : (mon_aligntyp(mtmp) == A_NONE
                        || sgn(mon_aligntyp(mtmp)) != weap->alignment);
    } else if (weap->spfx & SPFX_ATTK) {
        if (defended(mtmp, (int) weap->attk.adtyp))
            return FALSE;

        switch (weap->attk.adtyp) {
        case AD_FIRE:
            return !(!yours ? resists_fire(mtmp)
                            : (how_resistant(FIRE_RES) > 99) ? TRUE : FALSE);
        case AD_COLD:
            return !(!yours ? resists_cold(mtmp)
                            : (how_resistant(COLD_RES) > 99) ? TRUE : FALSE);
        case AD_ELEC:
            return !(!yours ? resists_elec(mtmp)
                            : (how_resistant(SHOCK_RES) > 99) ? TRUE : FALSE);
        case AD_MAGM:
        case AD_STUN:
            return !(yours ? Antimagic : (rn2(100) < ptr->mr));
        case AD_DRST:
            return !(!yours ? resists_poison(mtmp)
                            : (how_resistant(POISON_RES) > 99) ? TRUE : FALSE);
        case AD_SLEE:
            return !(!yours ? resists_sleep(mtmp)
                            : (how_resistant(SLEEP_RES) > 99) ? TRUE : FALSE);
        case AD_DRLI:
            return !(yours ? Drain_resistance : resists_drli(mtmp));
        case AD_LOUD:
            return !(yours ? Sonic_resistance : resists_sonic(mtmp));
        case AD_DREN:
            return !nonliving(ptr);
        case AD_STON:
            return !(yours ? Stone_resistance : resists_ston(mtmp));
        case AD_ACID:
            return !(yours ? Acid_resistance : resists_acid(mtmp));
        case AD_WIND:
            return !(yours ? (Stable && bigmonst(youmonst.data) ) : !bigmonst(mtmp->data));
        case AD_DISE:
            return !(yours ? Sick_resistance : resists_sick(ptr));
        case AD_DETH:
            return !immune_death_magic(ptr);
        case AD_WTHR:
            return !(nonliving(mtmp->data) && is_vampshifter(mtmp));
        default:
            impossible("Weird weapon special attack.");
        }
    }
    return 0;
}

/* return the MH flags of monster that an artifact's special attacks apply
 * against */
long
spec_mh(otmp)
struct obj *otmp;
{
    const struct artifact *artifact = get_artifact(otmp);

    if (artifact)
        return artifact->mtype;
    return 0L;
}

/* special attack to-hit bonus */
int
spec_abon(otmp, mon)
struct obj *otmp;
struct monst *mon;
{
    const struct artifact *weap = get_artifact(otmp);

    /* no need for an extra check for `NO_ATTK' because this will
       always return 0 for any artifact which has that attribute */

    if (weap && weap->attk.damn && spec_applies(weap, mon))
        /*return rnd((int) weap->attk.damn);*/
        /* SLASH'EM style flat damage for artifacts */
        return (int)weap->attk.damn;
    return 0;
}

/* special damage bonus */
int
spec_dbon(otmp, mon, tmp)
struct obj *otmp;
struct monst *mon;
int tmp;
{
    register const struct artifact *weap = get_artifact(otmp);
    boolean yours = (mon == &youmonst);
    spec_dbon_applies = FALSE;

    if (!weap && otmp->oprops
        && (otmp->oclass == WEAPON_CLASS || is_weptool(otmp))) {
        /* until we know otherwise... */
        if ((attacks(AD_FIRE, otmp)
            && ((yours) ? !(Fire_resistance || Underwater)
                        : !(resists_fire(mon) || mon_underwater(mon))))
                || (attacks(AD_COLD, otmp)
                    && ((yours) ? (!Cold_resistance) : (!resists_cold(mon))))
                        || (attacks(AD_ELEC, otmp)
                            && ((yours) ? (!Shock_resistance) : (!resists_elec(mon))))
                                || (attacks(AD_DRST, otmp)
                                    && ((yours) ? (!Poison_resistance) : (!resists_poison(mon))))
                                        || (attacks(AD_SLEE, otmp)
                                            && ((yours) ? (!Sleep_resistance) : (!resists_sleep(mon))))
                                                || (attacks(AD_ACID, otmp)
                                                    && ((yours) ? !(Acid_resistance || Underwater)
                                                    : !(resists_acid(mon) || mon_underwater(mon))))
                                                        || (attacks(AD_LOUD, otmp)
                                                            && ((yours) ? (!Sonic_resistance) : (!resists_sonic(mon))))
                                                                || (attacks(AD_DISE, otmp)
                                                                    && ((yours) ? (!Sick_resistance) : (!resists_sick(mon->data))))
                                                                        || (attacks(AD_DETH, otmp)
                                                                            && !(nonliving(mon->data) || is_demon(mon->data)))) {


            spec_dbon_applies = TRUE;
            /* majority of ITEM_VENOM damage
             * handled in src/uhitm.c */
            if (otmp->oprops & ITEM_VENOM)
                return rnd(2);
            else
                return rnd(5) + 3;
        }
        if ((otmp->oprops & ITEM_DRLI)
            && ((yours) ? (!Drain_resistance) : (!resists_drli(mon)))) {
            spec_dbon_applies = TRUE;
            return 0;
        }
        return 0;
    }

    if (!weap || (weap->attk.adtyp == AD_PHYS /* check for `NO_ATTK' */
                  && weap->attk.damn == 0 && weap->attk.damd == 0))
        spec_dbon_applies = FALSE;
    else if (otmp->oartifact == ART_GRIMTOOTH
             || otmp->oartifact == ART_VORPAL_BLADE
             || otmp->oartifact == ART_MASTER_SWORD
             || otmp->oartifact == ART_STAKE_OF_VAN_HELSING
             || otmp->oartifact == ART_ANGELSLAYER
             || otmp->oartifact == ART_DRAMBORLEG)
        /* Grimtooth, Vorpal Blade, Angelslayer, and Dramborleg have
           SPFX settings to warn against elves, jabberwocks, angels,
           and demons respectively, but we want its damage bonus to
           apply to all targets, so bypass spec_applies() */
        spec_dbon_applies = TRUE;
    else
        spec_dbon_applies = spec_applies(weap, mon);

    if (spec_dbon_applies)
        return weap->attk.damd ? (int) weap->attk.damd : max(tmp, 1);
    return 0;
}

/* add identified artifact to discoveries list */
void
discover_artifact(m)
xchar m;
{
    int i;

    /* look for this artifact in the discoveries list;
       if we hit an empty slot then it's not present, so add it */
    for (i = 0; i < NROFARTIFACTS; i++)
        if (artidisco[i] == 0 || artidisco[i] == m) {
            artidisco[i] = m;
            return;
        }
    /* there is one slot per artifact, so we should never reach the
       end without either finding the artifact or an empty slot... */
    impossible("couldn't discover artifact (%d)", (int) m);
}

/* used to decide whether an artifact has been fully identified */
boolean
undiscovered_artifact(m)
xchar m;
{
    int i;

    /* look for this artifact in the discoveries list;
       if we hit an empty slot then it's undiscovered */
    for (i = 0; i < NROFARTIFACTS; i++)
        if (artidisco[i] == m)
            return FALSE;
        else if (artidisco[i] == 0)
            break;
    return TRUE;
}

/* display a list of discovered artifacts; return their count */
int
disp_artifact_discoveries(tmpwin)
winid tmpwin; /* supplied by dodiscover() */
{
    int i, m, otyp;
    char buf[BUFSZ];

    for (i = 0; i < NROFARTIFACTS; i++) {
        if (artidisco[i] == 0)
            break; /* empty slot implies end of list */
        if (tmpwin == WIN_ERR)
            continue; /* for WIN_ERR, we just count */

        if (i == 0)
            putstr(tmpwin, iflags.menu_headings, "Artifacts");
        m = artidisco[i];
        otyp = artilist[m].otyp;
        Sprintf(buf, "  %s [%s %s]", artiname(m),
                align_str(artilist[m].alignment), simple_typename(otyp));
        putstr(tmpwin, 0, buf);
    }
    return i;
}

/*
 * Magicbane's intrinsic magic is incompatible with normal
 * enchantment magic.  Thus, its effects have a negative
 * dependence on spe.  Against low mr victims, it typically
 * does "double athame" damage, 2d4.  Occasionally, it will
 * cast unbalancing magic which effectively averages out to
 * 4d4 damage (3d4 against high mr victims), for spe = 0.
 *
 * Prior to 3.4.1, the cancel (aka purge) effect always
 * included the scare effect too; now it's one or the other.
 * Likewise, the stun effect won't be combined with either
 * of those two; it will be chosen separately or possibly
 * used as a fallback when scare or cancel fails.
 *
 * [Historical note: a change to artifact_hit() for 3.4.0
 * unintentionally made all of Magicbane's special effects
 * be blocked if the defender successfully saved against a
 * stun attack.  As of 3.4.1, those effects can occur but
 * will be slightly less likely than they were in 3.3.x.]
 */

enum mb_effect_indices {
    MB_INDEX_PROBE = 0,
    MB_INDEX_STUN,
    MB_INDEX_SCARE,
    MB_INDEX_CANCEL,

    NUM_MB_INDICES
};

#define MB_MAX_DIEROLL 8 /* rolls above this aren't magical */
static const char *const mb_verb[2][NUM_MB_INDICES] = {
    { "probe", "stun", "scare", "cancel" },
    { "prod", "amaze", "tickle", "purge" },
};

/* called when someone is being hit by Magicbane or Butcher */
STATIC_OVL boolean
Mb_hit(magr, mdef, mb, dmgptr, dieroll, vis, hittee)
struct monst *magr, *mdef; /* attacker and defender */
struct obj *mb;            /* Magicbane or Butcher */
int *dmgptr;               /* extra damage target will suffer */
int dieroll;               /* d20 that has already scored a hit */
boolean vis;               /* whether the action can be seen */
char *hittee;              /* target's name: "you" or mon_nam(mdef) */
{
    struct permonst *old_uasmon;
    const char *verb;
    boolean youattack = (magr == &youmonst), youdefend = (mdef == &youmonst),
            resisted = FALSE, do_stun, do_confuse, result;
    int attack_indx, fakeidx, scare_dieroll = MB_MAX_DIEROLL / 2;

    result = FALSE; /* no message given yet */
    /* the most severe effects are less likely at higher enchantment */
    if (mb->spe >= 3)
        scare_dieroll /= (1 << (mb->spe / 3));
    /* if target successfully resisted the artifact damage bonus,
       reduce overall likelihood of the assorted special effects */
    if (!spec_dbon_applies)
        dieroll += 1;

    /* might stun even when attempting a more severe effect, but
       in that case it will only happen if the other effect fails;
       extra damage will apply regardless; 3.4.1: sometimes might
       just probe even when it hasn't been enchanted */
    do_stun = (max(mb->spe, 0) < rn2(spec_dbon_applies ? 11 : 7));

    /* the special effects also boost physical damage; increments are
       generally cumulative, but since the stun effect is based on a
       different criterium its damage might not be included; the base
       damage is either 1d4 (athame) or 2d4 (athame+spec_dbon) depending
       on target's resistance check against AD_STUN (handled by caller)
       [note that a successful save against AD_STUN doesn't actually
       prevent the target from ending up stunned] */
    attack_indx = MB_INDEX_PROBE;
    *dmgptr += rnd(4); /* (2..3)d4 */
    if (do_stun) {
        attack_indx = MB_INDEX_STUN;
        *dmgptr += rnd(4); /* (3..4)d4 */
    }
    if (dieroll <= scare_dieroll
        && !mindless(mdef->data) && !unique_corpstat(mdef->data)
        && mb->oartifact != ART_BUTCHER) {
        attack_indx = MB_INDEX_SCARE;
        *dmgptr += rnd(4); /* (3..5)d4 */
    }
    if (dieroll <= (scare_dieroll / 2)
        && mb->oartifact != ART_BUTCHER) {
        attack_indx = MB_INDEX_CANCEL;
        *dmgptr += rnd(4); /* (4..6)d4 */
    }

    /* give the hit message prior to inflicting the effects */
    verb = mb_verb[!!Hallucination][attack_indx];
    if (youattack || youdefend || vis) {
        result = TRUE;
        if (mb->oartifact == ART_MAGICBANE)
            pline_The("magic-absorbing staff %s %s!",
                      vtense((const char *) 0, verb), hittee);
        else
            pline_The("massive triple-headed flail %s %s!",
                      vtense((const char *) 0, verb), hittee);
        /* assume probing has some sort of noticeable feedback
           even if it is being done by one monster to another */
        if (attack_indx == MB_INDEX_PROBE && !canspotmon(mdef))
            map_invisible(mdef->mx, mdef->my);
    }

    /* now perform special effects */
    switch (attack_indx) {
    case MB_INDEX_CANCEL:
        old_uasmon = youmonst.data;
        /* No mdef->mcan check: even a cancelled monster can be polymorphed
         * into a golem, and the "cancel" effect acts as if some magical
         * energy remains in spellcasting defenders to be absorbed later.
         */
        if (!cancel_monst(mdef, mb, youattack, FALSE, FALSE)) {
            resisted = TRUE;
        } else {
            do_stun = FALSE;
            if (youdefend) {
                if (youmonst.data != old_uasmon)
                    *dmgptr = 0; /* rehumanized, so no more damage */
                if (u.uenmax > 0) {
                    u.uenmax--;
                    if (u.uen > 0)
                        u.uen--;
                    context.botl = TRUE;
                    You("lose magical energy!");
                }
            } else {
                if (mdef->data == &mons[PM_CLAY_GOLEM])
                    mdef->mhp = 1; /* cancelled clay golems will die */
                if (youattack && attacktype(mdef->data, AT_MAGC)) {
                    u.uenmax++;
                    u.uen++;
                    context.botl = TRUE;
                    You("absorb magical energy!");
                }
            }
        }
        break;

    case MB_INDEX_SCARE:
        if (youdefend) {
            if (Antimagic) {
                resisted = TRUE;
            } else {
                nomul(-3);
                multi_reason = "being scared stiff";
                nomovemsg = "";
                if (magr && magr == u.ustuck && sticks(youmonst.data)) {
                    u.ustuck = (struct monst *) 0;
                    You("release %s!", mon_nam(magr));
                }
            }
        } else {
            if (rn2(2) && resist(mdef, WEAPON_CLASS, 0, NOTELL))
                resisted = TRUE;
            else
                monflee(mdef, 3, FALSE, (mdef->mhp > *dmgptr));
        }
        if (!resisted)
            do_stun = FALSE;
        break;

    case MB_INDEX_STUN:
        do_stun = TRUE; /* (this is redundant...) */
        break;

    case MB_INDEX_PROBE:
        if (youattack && (mb->spe == 0 || !rn2(3 * abs(mb->spe)))) {
            pline_The("%s is insightful.", verb);
            /* pre-damage status */
            probe_monster(mdef);
        }
        break;
    }
    /* stun if that was selected and a worse effect didn't occur */
    if (do_stun) {
        if (youdefend)
            make_stunned(((HStun & TIMEOUT) + 3L), FALSE);
        else
            mdef->mstun = 1;
        /* avoid extra stun message below if we used mb_verb["stun"] above */
        if (attack_indx == MB_INDEX_STUN)
            do_stun = FALSE;
    }
    /* lastly, all this magic can be confusing... */
    do_confuse = !rn2(12);
    if (do_confuse) {
        if (youdefend)
            make_confused((HConfusion & TIMEOUT) + 4L, FALSE);
        else
            mdef->mconf = 1;
    }

    /* now give message(s) describing side-effects; Use fakename
       so vtense() won't be fooled by assigned name ending in 's' */
    fakeidx = youdefend ? 1 : 0;
    if (youattack || youdefend || vis) {
        (void) upstart(hittee); /* capitalize */
        if (resisted) {
            pline("%s %s!", hittee, vtense(fakename[fakeidx], "resist"));
            shieldeff(youdefend ? u.ux : mdef->mx,
                      youdefend ? u.uy : mdef->my);
        }
        if ((do_stun || do_confuse) && flags.verbose) {
            char buf[BUFSZ];

            buf[0] = '\0';
            if (do_stun)
                Strcat(buf, "stunned");
            if (do_stun && do_confuse)
                Strcat(buf, " and ");
            if (do_confuse)
                Strcat(buf, "confused");
            pline("%s %s %s%c", hittee, vtense(fakename[fakeidx], "are"), buf,
                  (do_stun && do_confuse) ? '!' : '.');
        }
    }

    return result;
}

/* Function used when someone attacks someone else with an artifact
 * weapon.  Only adds the special (artifact) damage, and returns a 1 if it
 * did something special (in which case the caller won't print the normal
 * hit message).  This should be called once upon every artifact attack;
 * dmgval() no longer takes artifact bonuses into account.  Possible
 * extension: change the killer so that when an orc kills you with
 * Stormbringer it's "killed by Stormbringer" instead of "killed by an orc".
 */
boolean
artifact_hit(magr, mdef, otmp, dmgptr, dieroll)
struct monst *magr, *mdef;
struct obj *otmp;
int *dmgptr;
int dieroll; /* needed for Magicbane and vorpal blades */
{
    boolean youattack = (magr == &youmonst);
    boolean youdefend = (mdef == &youmonst);
    boolean vis = (!youattack && magr && cansee(magr->mx, magr->my))
                  || (!youdefend && cansee(mdef->mx, mdef->my))
                  || (youattack && u.uswallow && mdef == u.ustuck && !Blind);
    boolean realizes_damage, msgprinted = FALSE;
    const char *wepdesc;
    static const char you[] = "you";
    char hittee[BUFSZ];
    struct artifact* atmp;
    int j, k, mdx, mdy, permdmg, chance;
    int time = 1; /* For Mouser's Scalpel */

    Strcpy(hittee, youdefend ? you : mon_nam(mdef));
    
    if (!youattack && magr) {
        mdx = sgn(mdef->mx - magr->mx);
        mdy = sgn(mdef->my - magr->my);
    }
    /* The following takes care of most of the damage, but not all--
     * the exception being for level draining, which is specially
     * handled.  Messages are done in this function, however.
     */
    *dmgptr += spec_dbon(otmp, mdef, *dmgptr);
    permdmg = 0;

    if (youattack && youdefend) {
        impossible("attacking yourself with weapon?");
        return FALSE;
    }

    realizes_damage = (youdefend || vis
                       /* feel the effect even if not seen */
                       || (youattack && mdef == u.ustuck));

    /* the four basic attacks: fire, cold, shock and missiles */
    if (attacks(AD_FIRE, otmp)) {
        if (realizes_damage) {
            if (otmp->oartifact == ART_FIRE_BRAND
                  || otmp->oartifact == ART_FIREWALL) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("fiery %s hits %s.", 
                                      otmp->oartifact == ART_FIRE_BRAND 
                                          ? "blade" : "staff", hittee);
                    } else {
                        pline_The("fiery %s %s %s%c", 
                                  otmp->oartifact == ART_FIRE_BRAND ? "blade" : "staff",
                                  can_vaporize(mdef->data)
                                      ? "vaporizes part of"
                                      : mon_underwater(mdef) ? "hits" : "burns",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("fiery %s %s %s%c",
                              otmp->oartifact == ART_FIRE_BRAND ? "blade" : "staff",
                              !spec_dbon_applies
                                  ? "hits"
                                  : can_vaporize(mdef->data)
                                      ? "vaporizes part of"
                                        : mon_underwater(mdef) ? "hits" : "burns",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else if (otmp->oartifact == ART_ANGELSLAYER) {
                boolean angel = youdefend ? is_angel(youmonst.data)
                                          : is_angel(mdef->data);
                boolean vaporize = youdefend ? can_vaporize(youmonst.data)
                                             : can_vaporize(mdef->data);
                boolean no_burn = youdefend ? (how_resistant(FIRE_RES) == 100
                                               || Underwater)
                                            : (resists_fire(mdef)
                                               || defended(mdef, AD_FIRE)
                                               || mon_underwater(mdef));
                
                if (!spec_dbon_applies) {
                    if ((youdefend || canseemon(mdef))
                        && (youattack || cansee(magr->mx, magr->my)))
                        pline_The("infernal trident hits %s.", hittee);
                } else if (!rn2(10) && angel) {
                    /* instant incineration */
                    if (youdefend || canseemon(mdef))
                        pline("%s's eldritch flame consumes %s!", 
                              artiname(otmp->oartifact), hittee);
                    if (youdefend) {
                        u.ugrave_arise = (NON_PM - 2); /* no corpse */
                        killer.format = NO_KILLER_PREFIX;
                        Sprintf(killer.name, "incinerated by %s", the(xname(otmp)));
                        done(DIED);
                        *dmgptr = 1;
                    } else { /* you or mon hit monster */
                        if (youattack) {
                            xkilled(mdef, XKILL_NOMSG | XKILL_NOCORPSE);
                        } else {
                            monkilled(mdef, 0, AD_FIRE);
                        }
                    }
                } else {
                    if (youdefend || canseemon(mdef))
                        pline_The("infernal trident %s %s%c",
                                  vaporize ? "vaporizes part of"
                                           : no_burn ? "hits" : "burns",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else if (otmp->oclass == WEAPON_CLASS
                       && (otmp->oprops & ITEM_FIRE)) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("%s hits %s.",
                                      distant_name(otmp, xname), hittee);
                    } else {
                        pline_The("%s %s %s%c",
                                  distant_name(otmp, xname),
                                  can_vaporize(mdef->data)
                                      ? "vaporizes part of"
                                      : mon_underwater(mdef) ? "hits" : "burns",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("%s %s %s%c",
                              distant_name(otmp, xname),
                              !spec_dbon_applies
                                  ? "hits"
                                  : can_vaporize(mdef->data)
                                      ? "vaporizes part of"
                                      : mon_underwater(mdef) ? "hits" : "burns",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            }
            if (completelyburns(mdef->data) || is_wooden(mdef->data)
                || mdef->data == &mons[PM_GREEN_SLIME]) {
                if (youdefend && !Underwater) {
                    You("ignite and turn to ash!");
                    losehp((Upolyd ? u.mh : u.uhp) + 1, "immolation",
                           NO_KILLER_PREFIX);
                } else {
                    if (!mon_underwater(mdef)) {
                        pline("%s ignites and turns to ash!", Monnam(mdef));
                        mondead(mdef);
                    }
                }
            }
            if ((otmp->oprops & ITEM_FIRE) && spec_dbon_applies)
                otmp->oprops_known |= ITEM_FIRE;
        }
        if (youdefend ? !Underwater : !mon_underwater(mdef)) {
            if (!rn2(4))
                (void) destroy_mitem(mdef, POTION_CLASS, AD_FIRE);
            if (!rn2(4))
                (void) destroy_mitem(mdef, SCROLL_CLASS, AD_FIRE);
            if (!rn2(7))
                (void) destroy_mitem(mdef, SPBOOK_CLASS, AD_FIRE);
            if (rn2(3))
                (void) destroy_mitem(mdef, WEAPON_CLASS, AD_FIRE);
        }
        if (youdefend && Slimed && !Underwater)
            burn_away_slime();
        msgprinted = TRUE;
        return realizes_damage;
    }
    if (attacks(AD_COLD, otmp)) {
        if (realizes_damage) {
            if (otmp->oartifact == ART_FROST_BRAND 
             || otmp->oartifact == ART_DEEP_FREEZE) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("ice-cold blade hits %s.", hittee);
                    } else {
                        pline_The("ice-cold blade %s %s%c",
                                  can_freeze(mdef->data)
                                      ? "freezes part of"
                                      : "freezes",
                                  hittee,  !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("ice-cold blade %s %s%c",
                              !spec_dbon_applies
                                  ? "hits"
                                  : can_freeze(mdef->data)
                                      ? "freezes part of"
                                      : "freezes",
                              hittee,  !spec_dbon_applies ? '.' : '!');
                }
            } else if (otmp->oclass == WEAPON_CLASS
                       && (otmp->oprops & ITEM_FROST)) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("%s hits %s.",
                                      distant_name(otmp, xname), hittee);
                    } else {
                        pline_The("%s %s %s%c",
                                  distant_name(otmp, xname),
                                  can_freeze(mdef->data)
                                      ? "freezes part of"
                                      : "freezes",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("%s %s %s%c",
                              distant_name(otmp, xname),
                              !spec_dbon_applies
                                  ? "hits"
                                  : can_freeze(mdef->data)
                                      ? "freezes part of"
                                      : "freezes",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            }
            if ((otmp->oprops & ITEM_FROST) && spec_dbon_applies)
                otmp->oprops_known |= ITEM_FROST;
        }
        if (!rn2(4))
            (void) destroy_mitem(mdef, POTION_CLASS, AD_COLD);
        msgprinted = TRUE;
        return realizes_damage;
    }
    if (attacks(AD_ELEC, otmp)) {
        if (realizes_damage) {
            if (otmp->oartifact == ART_MJOLLNIR) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("massive hammer hits %s.", hittee);
                    } else {
                        pline_The("massive hammer hits!  Lightning strikes %s%c",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("massive hammer hits%s %s%c",
                              !spec_dbon_applies
                                  ? ""
                                  : "!  Lightning strikes",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else if (otmp->oclass == WEAPON_CLASS
                       && (otmp->oprops & ITEM_SHOCK)) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("%s hits %s.",
                                      distant_name(otmp, xname), hittee);
                    } else {
                        pline_The("%s %s %s%c",
                                  distant_name(otmp, xname),
                                  rn2(2) ? "jolts" : "shocks",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("%s %s %s%c",
                              distant_name(otmp, xname),
                              !spec_dbon_applies
                                  ? "hits"
                                  : rn2(2) ? "jolts" : "shocks",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            }
            if ((otmp->oprops & ITEM_SHOCK) && spec_dbon_applies)
                otmp->oprops_known |= ITEM_SHOCK;
        }
        if (spec_dbon_applies && otmp->oartifact == ART_MJOLLNIR)
            wake_nearto(mdef->mx, mdef->my, 4 * 4);
        if (!rn2(5))
            (void) destroy_mitem(mdef, RING_CLASS, AD_ELEC);
        if (!rn2(5))
            (void) destroy_mitem(mdef, WAND_CLASS, AD_ELEC);
        msgprinted = TRUE;
        return realizes_damage;
    }
    if (attacks(AD_LOUD, otmp)) {
        if (realizes_damage) {
            if (otmp->oartifact == ART_THUNDERSTRUCK) {
                pline_The("thunderous morning star %s %s%c",
                          !spec_dbon_applies ? "hits" : "blasts", hittee,
                          !spec_dbon_applies ? '.' : '!');
                
                /* Occasionally shoot out a sonic beam */
                if (!rn2(4)) {
                    if (youattack && can_we_zap(u.dx, u.dy, 13)) {
                        dobuzz((int) (20 + (AD_LOUD - 1)), 3, u.ux, u.uy,
                               u.dx, u.dy, TRUE);
                    } else {
                        dobuzz((int) (-20 - (AD_LOUD - 1)), 3, magr->mx,
                               magr->my, mdx, mdy, TRUE);
                    }
                }
            } else if (otmp->oclass == WEAPON_CLASS
                       && (otmp->oprops & ITEM_SCREAM)) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("%s hits %s.",
                                      distant_name(otmp, xname), hittee);
                    } else {
                        pline_The("%s blasts %s%c",
                                  distant_name(otmp, xname), hittee, 
                                  !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("%s %s %s%c",
                              distant_name(otmp, xname),
                              !spec_dbon_applies ? "hits" : "blasts",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
                if ((otmp->oprops & ITEM_SCREAM) && spec_dbon_applies)
                    otmp->oprops_known |= ITEM_SCREAM;
            }
            if (spec_dbon_applies)
                wake_nearto(mdef->mx, mdef->my, 4 * 4);
            if (!rn2(4))
                destroy_mitem(mdef, ARMOR_CLASS, AD_LOUD);
            if (!rn2(4))
                destroy_mitem(mdef, POTION_CLASS, AD_LOUD);
            if (!rn2(7))
                destroy_mitem(mdef, RING_CLASS, AD_LOUD);
            if (!rn2(7))
                destroy_mitem(mdef, TOOL_CLASS, AD_LOUD);
            if (!rn2(7))
                destroy_mitem(mdef, WAND_CLASS, AD_LOUD);
            if (mdef->data == &mons[PM_GLASS_GOLEM]) {
                pline("%s shatters into a million pieces!", Monnam(mdef));
                *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
                return TRUE;
            }
            msgprinted = TRUE;
            return realizes_damage;
        }
    }
    
#if 0 /* The Master Sword is now the "imaginary widget!" */
    if (attacks(AD_MAGM, otmp)) {
        if (realizes_damage)
            pline_The("imaginary widget hits%s %s%c",
                      !spec_dbon_applies
                          ? ""
                          : "!  A hail of magic missiles strikes",
                      hittee, !spec_dbon_applies ? '.' : '!');
        msgprinted = TRUE;
        return realizes_damage;
    }
#endif
    /* if (attacks(AD_MAGM, otmp)) {*/
    if (attacks(AD_MAGM, otmp)) {
        if (realizes_damage) {
            if (!youattack && magr && cansee(magr->mx, magr->my)) {
                if (!spec_dbon_applies) {
                    if (!youdefend)
                        ;
                    else
                        pline("%s hits %s.", artiname(otmp->oartifact), hittee);
                } else if (!rn2(10) && spec_dbon_applies) {
                    pline("A hail of magic missiles strikes!");
                    *dmgptr += rnd(2) * 6;
                }
            } else if (!rn2(10)) {
                pline_The("Master Sword hits%s %s%c",
                          !spec_dbon_applies
                              ? ""
                              : "!  A hail of magic missiles strikes",
                          hittee, !spec_dbon_applies ? '.' : '!');
                *dmgptr += rnd(2) * 6;
            }

            /* Occasionally shoot out a magic missile
             * Must be at full health: 3/4 chance of ray.
             * 2d6 - same as a wand. */
            if (rn2(4)) {
                if (youattack && (u.uhp == u.uhpmax) && can_we_zap(u.dx, u.dy, 13)) {
                    dobuzz((int) (20 + (AD_MAGM - 1)), 2, u.ux, u.uy, u.dx,
                           u.dy, TRUE);
                } else if (magr && magr->mhp == magr->mhpmax) {
                    dobuzz((int) (-20 - (AD_MAGM - 1)), 2, magr->mx, magr->my,
                           mdx, mdy, TRUE);
                }
            }
            msgprinted = TRUE;
            return realizes_damage;
        }
    }
    if (attacks(AD_DETH, otmp)) {
        if (realizes_damage) {
            /* currently the only object that uses this
               is the Wand of Orcus artifact */
            if (!youattack && magr && cansee(magr->mx, magr->my)) {
                if (!spec_dbon_applies) {
                    if (!youdefend)
                        ;
                    else
                        pline_The("ornate mace hits %s.", hittee);
                } else {
                    pline_The("ornate mace %s %s%c",
                              rn2(2) ? "bashes" : "bludgeons",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else {
                pline_The("ornate mace %s %s%c",
                          !spec_dbon_applies
                              ? "hits"
                              : rn2(2) ? "bashes" : "bludgeons",
                          hittee, !spec_dbon_applies ? '.' : '!');
            }
        }
        if (youdefend && !immune_death_magic(mdef->data)) {
            switch (rn2(20)) {
            case 19:
            case 18:
            case 17:
                You_feel("the %s trying to annihilate your soul!",
                         distant_name(otmp, xname));
                if (!Antimagic) {
                    killer.format = KILLED_BY;
                    Strcpy(killer.name, "the Wand of Orcus");
                    ukiller = magr;
                    done(DIED);
                    *dmgptr = 0;
                    break;
                }
                /*FALLTHRU*/
            default: /* case 16: ... case 0: */
                You_feel("your life force draining away...");
                permdmg = 1; /* actual damage done below */
                break;
            }
            if (permdmg) {
                int lowerlimit, *hpmax_p;
                /*
                 * Apply some of the damage to permanent hit points:
                 *  polymorphed         100% against poly'd hpmax
                 *  hpmax > 25*lvl  50..100% against normal hpmax
                 *  hpmax > 10*lvl  25..75%
                 *  hpmax >  5*lvl  20..60%
                 *  otherwise        0..50%
                 * Never reduces hpmax below 1 hit point per level.
                 */
                permdmg = rn2(*dmgptr / 2 + 1);
                if (Upolyd || u.uhpmax > 25 * u.ulevel)
                    permdmg = *dmgptr / 2;
                else if (u.uhpmax > 10 * u.ulevel)
                    permdmg += *dmgptr / 4;
                else if (u.uhpmax > 5 * u.ulevel)
                    permdmg += *dmgptr / 5;

                /* cap maximum permdmg to 15 hit points per hit */
                if (permdmg > 15)
                    permdmg = 15;

                if (Upolyd) {
                    hpmax_p = &u.mhmax;
                    /* [can't use youmonst.m_lev] */
                    lowerlimit = min((int) youmonst.data->mlevel, u.ulevel);
                    if (lowerlimit <= 0)
                        lowerlimit = 1;
                } else {
                    hpmax_p = &u.uhpmax;
                    lowerlimit = u.ulevel;
                }
                if (*hpmax_p - permdmg > lowerlimit)
                    *hpmax_p -= permdmg;
                else if (*hpmax_p > lowerlimit)
                    *hpmax_p = lowerlimit;
                /* else unlikely...
                 * already at or below minimum threshold; do nothing */

                if (!Upolyd) {
                    if (u.uhp > *hpmax_p)
                        u.uhp = *hpmax_p;
                } else {
                    if (u.mh > *hpmax_p)
                        u.mh = *hpmax_p;
                }
                context.botl = 1;
            }
        } else if (!youdefend && !immune_death_magic(mdef->data)) {
            switch (rn2(20)) {
            case 19:
            case 18:
            case 17:
                if (!(resists_magm(mdef) || defended(mdef, AD_MAGM))
                    && !resist(mdef, 0, 0, 0)) {
                    mdef->mhp = 0;
                    monkilled(mdef, "", AD_DETH);
                    if (!DEADMONSTER(mdef))
                        return 0;
                    return (MM_DEF_DIED
                            | (grow_up(magr, mdef) ? 0 : MM_AGR_DIED));
                }
                break;
            default: /* case 16 through case 0 */
                if (vis)
                    pline("%s looks weaker!", Monnam(mdef));
                /* mhp will then still be less than this value */
                mdef->mhpmax -= rn2(*dmgptr / 2 + 1);
                if (mdef->mhpmax <= 0) /* protect against invalid value */
                    mdef->mhpmax = 1;
                break;
            }
        }
        msgprinted = TRUE;
        return realizes_damage;
    }
    /* Fifth basic attack - acid (for the new and improved Dirge... DIRGE) */
    if (attacks(AD_ACID, otmp)) {
        if (realizes_damage) {
            boolean isdirge = otmp->oartifact == ART_DIRGE;
            if (!youattack && magr && cansee(magr->mx, magr->my)) {
                if (!spec_dbon_applies) {
                    if (!youdefend)
                        ;
                    else
                        pline_The("%s hits %s.", isdirge ? "acidic blade"
                                                         : distant_name(otmp, xname),
                                  hittee);
                } else {
                    pline_The("%s %s %s%c", isdirge ? "acidic blade"
                                                              : distant_name(otmp, xname),
                              can_corrode(mdef->data)
                                  ? "eats away part of"
                                  : mon_underwater(mdef) ? "hits" : "burns",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else {
                pline_The("%s %s %s%c", isdirge ? "acidic blade" 
                                                : distant_name(otmp, xname),
                          !spec_dbon_applies
                              ? "hits"
                              : can_corrode(mdef->data)
                                  ? "eats away part of"
                                  : mon_underwater(mdef) ? "hits" : "burns",
                          hittee, !spec_dbon_applies ? '.' : '!');
            }
        }
        if (youdefend ? !Underwater : !mon_underwater(mdef)) {
            if (!rn2(5))
                erode_armor(mdef, ERODE_CORRODE);
        }
        msgprinted = TRUE;
        return realizes_damage;
    }
    /* Sixth basic attack - poison */
    if (attacks(AD_DRST, otmp)) {
	if (realizes_damage) {
            if (otmp->oartifact == ART_SWORD_OF_KAS) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("gigantic blade hits %s.", hittee);
                    } else {
                        pline_The("gigantic blade %s %s%c",
                                  (how_resistant(POISON_RES) >= 50)
                                      ? "hits"
                                      : rn2(2) ? "poisons" : "eviscerates",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
	            pline_The("gigantic blade %s %s%c",
                              (resists_poison(mdef) || defended(mdef, AD_DRST))
                                  ? "hits"
                                  : rn2(2) ? "poisons" : "eviscerates",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            } else if (otmp->oclass == WEAPON_CLASS
                       && (otmp->oprops & ITEM_VENOM)) {
                if (!youattack && magr && cansee(magr->mx, magr->my)) {
                    if (!spec_dbon_applies) {
                        if (!youdefend)
                            ;
                        else
                            pline_The("%s hits %s.",
                                      distant_name(otmp, xname), hittee);
                    } else {
                        pline_The("%s %s %s%c",
                                  distant_name(otmp, xname),
                                  (how_resistant(POISON_RES) >= 50)
                                      ? "hits"
                                      : rn2(2) ? "taints" : "poisons",
                                  hittee, !spec_dbon_applies ? '.' : '!');
                    }
                } else {
                    pline_The("%s %s %s%c",
                              distant_name(otmp, xname),
                              (resists_poison(mdef) || defended(mdef, AD_DRST))
                                  ? "hits"
                                  : rn2(2) ? "taints" : "poisons",
                              hittee, !spec_dbon_applies ? '.' : '!');
                }
            }
            if ((otmp->oprops & ITEM_VENOM) && spec_dbon_applies)
                otmp->oprops_known |= ITEM_VENOM;
            if (youdefend) {
                if (spec_dbon_applies && !rn2(8))
                    poisoned("blade", A_STR, "poisoned blade", 30, FALSE);
            }
        }
        msgprinted = TRUE;
        return realizes_damage;
    }
    /* Seventh basic attack - disease */
    if (attacks(AD_DISE, otmp)) {
        if (Role_if(PM_SAMURAI)) {
            You("dishonorably use a diseased weapon!");
            adjalign(-sgn(u.ualign.type));
        } else if (u.ualign.type == A_LAWFUL && u.ualign.record > -10) {
            You_feel("like an evil coward for using a diseased weapon.");
            adjalign(Role_if(PM_KNIGHT) ? -10 : -1);
        }
        if (realizes_damage) {
            boolean elf = youdefend ? maybe_polyd(is_elf(youmonst.data), Race_if(PM_ELF))
                                    : racial_elf(mdef);
            boolean no_sick = youdefend ? Sick_resistance
                                        : (resists_sick(mdef->data)
                                           || defended(mdef, AD_DISE));

            if (!spec_dbon_applies) {
                if ((youdefend || canseemon(mdef))
                    && (youattack || cansee(magr->mx, magr->my)))
                    pline_The("filthy dagger hits %s.", hittee);
            } else if (!rn2(10) && elf) {
                if (youdefend || canseemon(mdef))
                    pline("%s penetrates %s soft flesh, disembowelling %s!",
                          artiname(otmp->oartifact), 
                          youdefend ? "your" : s_suffix(mon_nam(mdef)),
                          youdefend ? "you" : noit_mhim(mdef));
                if (youdefend) {
                    killer.format = NO_KILLER_PREFIX;
                    Sprintf(killer.name, "disemboweled by %s", the(xname(otmp)));
                    done(DIED);
                    *dmgptr = 1;
                } else { /* you or mon hit monster */
                    if (youattack) {
                        xkilled(mdef, XKILL_NOMSG);
                    } else {
                        monkilled(mdef, 0, AD_DISE);
                    }
                }
                msgprinted = TRUE;
                return realizes_damage;
            } else {
                if (youdefend || canseemon(mdef))
                    pline_The("filthy dagger %s %s%c",
                              no_sick ? "hits"
                                      : rn2(2) ? "contaminates" : "infects",
                              hittee, !spec_dbon_applies ? '.' : '!');
            }

            if (youdefend && !rn2(5)) {
                diseasemu(mdef->data);
            } else if (!youdefend) {
                mdef->mdiseasetime = rnd(10) + 5;
                if (!no_sick && !rn2(5)) {
                    if (canseemon(mdef))
                        pline("%s looks %s.", Monnam(mdef),
                              mdef->mdiseased ? "even worse" : "diseased");
                    mdef->mdiseased = 1;
                    if (wielding_artifact(ART_GRIMTOOTH))
                        mdef->mdiseabyu = TRUE;
                    else
                        mdef->mdiseabyu = FALSE;
                }
            }
        }
        msgprinted = TRUE;
        return realizes_damage;
    }

    /* Drowsing Rod */
    if (attacks(AD_SLEE, otmp) && rn2(20)) {
        if (realizes_damage && (youdefend || mdef->mcanmove)) {
            pline_The("staff sprays a %s %s at %s!", rndcolor(),
            (rn2(2) ? "gas" : "mist"), hittee);
        }
        if (youdefend && 
            (how_resistant(SLEEP_RES) == 100 || Breathless)) {
            pline_The("rod's vapors do not affect you.");
        } else if (youdefend) {
            fall_asleep(-resist_reduce(rnd(10), SLEEP_RES), TRUE);
            if (Blind)
                You("are put to sleep!");
            else
                You("are put to sleep by %s!", artiname(otmp->oartifact));
        } else if (mdef->mcanmove && !breathless(mdef->data) && sleep_monst(mdef, d(2, 4), -1)) {
             if (!Blind)
                pline("%s is put to sleep by %s's vapors!", Monnam(mdef), artiname(otmp->oartifact));
            slept_monst(mdef);
        }
        return realizes_damage;
    }
    /* Imhullu */
    if (attacks(AD_WIND, otmp)) {
        if (realizes_damage) {
            if (rn2(3))
                pline_The("humming glaive buffets %s with a massive blast of wind!", hittee);
            else {
                pline_The("humming glaive strikes %s with a tornado!", hittee);
                if (youdefend)
                    hurtle(u.ux - magr->mx, u.uy - magr->my, 5 + rn2(7), TRUE);
                else
                    mhurtle(mdef, mdef->mx - u.ux, mdef->my - u.uy, 5 + rn2(7));
                *dmgptr += d(3, 4);
            }
        }
        return realizes_damage;
    }
    if (attacks(AD_STUN, otmp) && dieroll <= MB_MAX_DIEROLL) {
        if (dieroll <= MB_MAX_DIEROLL)
            /* Magicbane's special attacks (possibly modifies hittee[]) */
            return Mb_hit(magr, mdef, otmp, dmgptr, dieroll, vis, hittee);
        else
            return msgprinted;
    }

    if (otmp->oartifact != ART_THIEFBANE || !youdefend) {
        if (!spec_dbon_applies) {
            /* since damage bonus didn't apply, nothing more to do;
               no further attacks have side-effects on inventory */
            return FALSE;
        }
    }
    /* Some neat and unique effects from various artifact weapons.
     * Pulled this from SporkHack - by default, the odds of an instakill
     * were 50/50. Because we can dual-wield artifact weapons in HackEM,
     * we need to tone this down. Significantly.
     * I've added a few extra artifact weapons here that could use some
     * love. Just be warned, this can be used against the player depending
     * on the race they choose...
     */
    atmp = &artilist[(unsigned char) otmp->oartifact];

    if (atmp->spfx & (SPFX_DFLAGH | SPFX_DCLAS)) {
	j = !rn2(10); /* 10% chance of instakill for some artifacts */
        k = !rn2(20); /* 5% chance if same weapon is used against the player */
        switch (otmp->oartifact) {
            case ART_WEREBANE:
                if (youattack && is_were(mdef->data) && j) {
                    You("severely burn %s with your silver blade!", mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && is_were(mdef->data) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s severely burns %s with its silver blade!",
                              Monnam(magr), mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && is_were(youmonst.data) && k) {
                    pline_The("silver blade gravely burns you!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_GIANTSLAYER:
                if (youattack && racial_giant(mdef) && j) {
                    You("eviscerate %s with a fatal stab!", mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && racial_giant(mdef) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s eviscerates %s with a fatal stab!",
                              Monnam(magr), mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && maybe_polyd(is_giant(youmonst.data), Race_if(PM_GIANT)) && k) {
                    pline_The("magical spear eviscerates you!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_OGRESMASHER:
                if (youattack && is_ogre(mdef->data) && j) {
                    You("crush %s skull!", s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && is_ogre(mdef->data) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s crushes %s skull!",
                              Monnam(magr), s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && is_ogre(youmonst.data) && k) {
                    pline_The("monstrous hammer crushes your skull!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_TROLLSBANE:
                if (youattack && is_troll(mdef->data) && j) {
                    pline("As you strike %s, it bursts into flame!", mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                    xkilled(mdef, XKILL_NOMSG | XKILL_NOCORPSE);
                } else if (!youattack && !youdefend
                           && magr && is_troll(mdef->data) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("As %s strikes %s, it bursts into flame!",
                              mon_nam(magr), mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                    mongone(mdef);
                } else if (youdefend && is_troll(youmonst.data) && k) {
                    You("burst into flame as you are hit!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                    /* player returns to their original form */
                } else
                    return FALSE;
                return TRUE;
            case ART_ORCRIST:
                if (youattack && racial_orc(mdef) && j) {
                    You("slice open %s throat!", s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && racial_orc(mdef) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s slices open %s throat!",
                              Monnam(magr), s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && maybe_polyd(is_orc(youmonst.data),
                           Race_if(PM_ORC)) && k) {
                    You_feel("Orcrist slice deep across your neck!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_ELFRIST: /* --hackem: Blatant copy-paste of Orcrist */
                if (youattack && racial_elf(mdef) && j) {
                    You("slice open %s throat!", s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && racial_elf(mdef) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s slices open %s throat!",
                              Monnam(magr), s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && maybe_polyd(is_elf(youmonst.data),
                           Race_if(PM_ELF)) && k) {
                    You_feel("%s slice deep across your neck!", artiname(otmp->oartifact));
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_STING:
                if (youattack && racial_orc(mdef) && j) {
                    You("stab deep into %s heart!", s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && racial_orc(mdef) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s stabs deep into %s heart!",
                              Monnam(magr), s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && maybe_polyd(is_orc(youmonst.data),
                           Race_if(PM_ORC)) && k) {
                    You_feel("%s stab deep into your heart!", artiname(otmp->oartifact));
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_GRIMTOOTH:
                if (youattack && racial_elf(mdef) && j) {
                    You("push %s deep into the bowels of %s!", 
                        artiname(otmp->oartifact), mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && racial_elf(mdef) && j) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s pushes %s deep into %s bowels!", Monnam(magr), 
                              artiname(otmp->oartifact), s_suffix(mon_nam(mdef)));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && maybe_polyd(is_elf(youmonst.data),
                           Race_if(PM_ELF)) && k) {
                    pline("%s penetrates your soft flesh, disembowelling you!",
                          artiname(otmp->oartifact));
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_SUNSWORD:
                if (youattack && is_undead(mdef->data) && j) {
                    if (mdef->isvecna) {
                        pline("%s flares brightly, severely wounding %s!",
                              artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr *= 3;
                        return TRUE;
                    } else {
                        pline("%s flares brightly as it incinerates %s!",
                              artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                        xkilled(mdef, XKILL_NOMSG | XKILL_NOCORPSE);
                    }
                } else if (!youattack && !youdefend
                           && magr && is_undead(mdef->data) && j) {
                    if (mdef->isvecna) {
                        if (cansee(magr->mx, magr->my))
                            pline("%s flares brightly, severely wounding %s!",
                              artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr *= 3;
                        return TRUE;
                    } else {
                        if (cansee(magr->mx, magr->my))
                            pline("%s flares brightly as it incinerates %s!",
                                  artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                        mongone(mdef);
                    }
                } else if (youdefend && is_undead(youmonst.data) && k) {
                    pline_The("holy power of %s incinerates your undead flesh!",
                              artiname(otmp->oartifact));
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                    /* player returns to their original form */
                } else
                    return FALSE;
                return TRUE;
            case ART_DRAMBORLEG:
                /* Dramborleg will one-shot kill a balrog every time */
                if (youattack && mdef->data == &mons[PM_BALROG]) {
                    You("obliterate %s with a powerful strike!", mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (!youattack && !youdefend
                           && magr && mdef->data == &mons[PM_BALROG]) {
                    if (cansee(magr->mx, magr->my))
                        pline("%s obliterates %s with a powerful strike!",
                              Monnam(magr), mon_nam(mdef));
                    *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                } else if (youdefend && youmonst.data == &mons[PM_BALROG]) {
                    pline_The("magical axe obliterates you!");
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                } else
                    return FALSE;
                return TRUE;
            case ART_DEMONBANE:
                if (youattack && is_demon(mdef->data) && j) {
                    if (!is_ndemon(mdef->data)) {
                        pline("%s gravely wounds %s!", artiname(otmp->oartifact),
                              mon_nam(mdef));
                        *dmgptr *= 3;
                        return TRUE;
                    } else {
                        pline("%s shines brilliantly, destroying %s!",
                              artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                    }
                } else if (!youattack && !youdefend
                           && magr && is_demon(mdef->data) && j) {
                    if (!is_ndemon(mdef->data)) {
                        if (cansee(magr->mx, magr->my))
                            pline("%s gravely wounds %s!", 
                                  artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr *= 3;
                        return TRUE;
                    } else {
                        if (cansee(magr->mx, magr->my))
                            pline("%s shines brilliantly, destroying %s!",
                                  artiname(otmp->oartifact), mon_nam(mdef));
                        *dmgptr = (2 * mdef->mhp + FATAL_DAMAGE_MODIFIER);
                    }
                } else if (youdefend && k
                           && maybe_polyd(is_demon(youmonst.data), Race_if(PM_DEMON))) {
                    pline("%s shines brilliantly, destroying you!",
                          artiname(otmp->oartifact));
                    *dmgptr = (2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER);
                    /* player returns to their original form */
                } else
                    return FALSE;
                return TRUE;
            default:
                break;
        }
    }
    /* We really want "on a natural 20" but Nethack does it in */
    /* reverse from AD&D. */
    if (spec_ability(otmp, SPFX_BEHEAD)) {
        /* --hackem: Sexy 15% beheading chance (inspired by Slash'EM) */
        if (otmp->oartifact == ART_TSURUGI_OF_MURAMASA && dieroll < 4) {
            wepdesc = "The razor-sharp blade";
            /* not really beheading, but so close, why add another SPFX */
            if (youattack && u.uswallow && mdef == u.ustuck) {
                You("slice %s wide open!", mon_nam(mdef));
                *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
                return TRUE;
            }
            if (!youdefend) {
                /* allow normal cutworm() call to add extra damage */
                if (notonhead)
                    return FALSE;

                if (r_bigmonst(mdef)) {
                    if (youattack)
                        You("slice deeply into %s!", mon_nam(mdef));
                    else if (vis)
                        pline("%s cuts deeply into %s!", Monnam(magr),
                              hittee);
                    *dmgptr *= 2;
                    return TRUE;
                }
                *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
                if (Role_if(PM_PIRATE))
                    pline("%s cleaves %s to the brisket!", wepdesc, mon_nam(mdef));
                else    
                    pline("%s cuts %s in half!", wepdesc, mon_nam(mdef));
                otmp->dknown = TRUE;
                return TRUE;
            } else {
                /* Invulnerable player won't be bisected */
                if (bigmonst(youmonst.data) || Invulnerable) {
                    pline("%s cuts deeply into you!",
                          magr ? Monnam(magr) : wepdesc);
                    *dmgptr *= 2;
                    return TRUE;
                }

                /* Players with negative AC's take less damage instead
                 * of just not getting hit.  We must add a large enough
                 * value to the damage so that this reduction in
                 * damage does not prevent death.
                 */
                *dmgptr = 2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER;
                pline("%s cuts you in half!", wepdesc);
                otmp->dknown = TRUE;
                return TRUE;
            }
        /* --hackem: Sexy 10% beheading chance from Slash'EM */
        } else if (dieroll < 3 || (otmp->oartifact == ART_VORPAL_BLADE &&
                                   is_jabberwock(mdef->data))) {
            
            static const char *const behead_msg[2] = { 
                "%s beheads %s!",                                       
                "%s decapitates %s!" 
            };

            if (youattack && u.uswallow && mdef == u.ustuck)
                return FALSE;
            wepdesc = artilist[otmp->oartifact == ART_VORPAL_BLADE 
                ? ART_VORPAL_BLADE : ART_THIEFBANE].name;
                
            if (!youdefend) {
                if (!has_head(mdef->data) || notonhead || u.uswallow) {
                    if (youattack)
                        pline("Somehow, you miss %s wildly.", mon_nam(mdef));
                    else if (vis)
                        pline("Somehow, %s misses wildly.", mon_nam(magr));
                    *dmgptr = 0;
                    return (boolean) (youattack || vis);
                }
                if (noncorporeal(mdef->data) || amorphous(mdef->data)) {
                    pline("%s slices through %s %s.", wepdesc,
                          s_suffix(mon_nam(mdef)), mbodypart(mdef, NECK));
                    return TRUE;
                }
                if (mdef->data == &mons[PM_CERBERUS]) {
                    pline("%s removes one of %s heads!", wepdesc,
                          s_suffix(mon_nam(mdef)));
                    *dmgptr = rn2(15) + 10;
                    if (!DEADMONSTER(mdef)) {
                        if (canseemon(mdef))
                            You("watch in horror as it quickly grows back.");
                    }
                    return TRUE;
                }
                *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
                pline(behead_msg[rn2(SIZE(behead_msg))], wepdesc,
                      mon_nam(mdef));
                if (Hallucination && !flags.female)
                    pline("Good job Henry, but that wasn't Anne.");
		        if (is_zombie(mdef->data)
                      || (is_troll(mdef->data) && !mlifesaver(mdef))) {
                    mdef->mcan = 1; /* kinda hard to revive if you've lost your head... */
                        otmp->dknown = TRUE;
                        return TRUE;
                }
            } else {
                if (!has_head(youmonst.data)) {
                    pline("Somehow, %s misses you wildly.",
                          magr ? mon_nam(magr) : wepdesc);
                    *dmgptr = 0;
                    return TRUE;
                }
                if (noncorporeal(youmonst.data) || amorphous(youmonst.data)) {
                    pline("%s slices through your %s.", wepdesc,
                          body_part(NECK));
                    return TRUE;
                }
                if (Hidinshell) {
                    pline("%s glances harmlessly off of your protective shell.", wepdesc);
                    return TRUE;
                }
                *dmgptr = 2 * (Upolyd ? u.mh : u.uhp) + FATAL_DAMAGE_MODIFIER;
                if (Invulnerable) {
                    pline("%s slices into your %s.", wepdesc, body_part(NECK));
                    return TRUE;
			    }
                pline(behead_msg[rn2(SIZE(behead_msg))], wepdesc, "you");
                otmp->dknown = TRUE;
                /* Should amulets fall off? */
                return TRUE;
            }
        } 
    }
    
    if (otmp->oartifact == ART_CIRCE_S_WITCHSTAFF && dieroll < 3) {
        wepdesc = "The witch's rod";
        if (youdefend && !Antimagic && !Unchanging) {
            pline("%s sends bizarre energies coursing through you!", wepdesc);
            polyself(5);
            return TRUE;
        } else if (youdefend) {
            shieldeff(u.ux, u.uy);
            pline("%s blasts you with weird energies, but you manage to fight them off!", wepdesc);
            return FALSE;
        }

        if (resists_magm(mdef) || defended(mdef, AD_MAGM)) {
            shieldeff(mdef->mx, mdef->my);
            pline("%s blasts %s with magical energies, but to no great effect.", wepdesc, mon_nam(mdef));
            return FALSE;
        } else if (youattack && u.uswallow && mdef == u.ustuck) {
            You("compact %s around you and burst free from it!", mon_nam(mdef));
            *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
            return TRUE;
        } else {
            pline("%s blasts %s with magical power!", wepdesc, mon_nam(mdef));
            newcham(mdef, rn2(2) ? &mons[PM_PIG] : &mons[PM_FERAL_HOG], FALSE, TRUE);
            return TRUE;
        }
    }

    if (spec_ability(otmp, SPFX_BLIND) && !rn2(3)) {
        if (!youdefend) {
            if (mdef->mcansee) {
                pline("%s blinds %s!", The(distant_name(otmp, xname)),
                      mon_nam(mdef));
                mdef->mcansee = 0;
                mdef->mblinded = 17;
                return TRUE;
            }
        } else if (!Blind) {
            Your("vision is consumed in a flash of blinding light!");
            make_blinded(Blinded + 17, FALSE);
            return TRUE;
        }
        *dmgptr += d(1, 10);
    }
    if (otmp->oartifact == ART_DOOMBLADE && dieroll < 6) {
        if (verysmall(mdef->data)) {
            if (youattack)
                You("violently smash the %s with your Doomblade!", mon_nam(mdef));
            else
                pline("%s violently smashes %s with its Doomblade!",
                      Monnam(magr), hittee);
        } else {
            if (youattack)
                You("plunge the %s deeply into %s!", 
                    artiname(otmp->oartifact), mon_nam(mdef));
            else
                pline("%s plunges the %s deeply into %s!",
                      Monnam(magr), artiname(otmp->oartifact), hittee);
        }
        *dmgptr += rnd(4) * 5;
        return TRUE;
    }
    
    /* Credits to BarclayII for Mouser's Scalpel rename and mechanic */
    if (otmp->oartifact == ART_MOUSER_S_SCALPEL && dieroll < 10) { 
        /* faster than a speeding bullet is the Gray Mouser... */
        There("is a flurry of blows!");
        /* I suppose this could theoretically continue forever... */
        do {
            *dmgptr += rnd(8) + 1 + otmp->spe;
            time++;
            dieroll = rn2(11);
        } while (dieroll < 5);
            
        if (time == 1)
            pline_The("rapier strikes %s!", hittee);
        else if (time == 2)
            pline_The("rapier strikes %s twice!", hittee);
        else
            pline_The("rapier strikes %s %d times in a row!", hittee, time); 
    }
       
    if (otmp->oartifact == ART_DEATHSWORD && dieroll < 4) {
        if (youdefend && !mindless(youmonst.data)) {
            pline_The((!multi) ? "heavy blade strikes you and you give in to pain!" 
                              : "heavy blade hurts you even more!"
            );
            nomovemsg = "You regain consciousness.";
            nomul(multi - d(1, 5));
            return TRUE;
        } else if (!youdefend && !mindless(mdef->data)) {
            int rnd_tmp = d(1, 5);
            pline_The(mdef->mcanmove ? "heavy blade knocks %s unconscious!" 
                                     : "heavy blade hurts %s even more!", 
                      (canseemon(mdef)) ? mon_nam(mdef) : "it");
            mdef->mcanmove = 0;
            mdef->mfrozen = ((int)mdef->mfrozen + rnd_tmp > 127) 
                                ? 127 : mdef->mfrozen + rnd_tmp;
            return TRUE;
        }
   }
       
    if (otmp->oartifact == ART_SERPENT_S_TONGUE) {
        otmp->dknown = TRUE;
        pline_The("twisted blade poisons %s!",
                youdefend ? "you" : mon_nam(mdef));
        if (youdefend ? Poison_resistance : resists_poison(mdef)) {
            if (youdefend)
                You("are not affected by the poison.");
            else
                pline("%s seems unaffected by the poison.", Monnam(mdef));
            return 
                TRUE;
        }
        switch (rnd(10)) {
        case 1:
        case 2:
        case 3:
        case 4:
            *dmgptr += d(1,6) + 2;
            break;
        case 5:
        case 6:
        case 7:
            *dmgptr += d(2,6) + 4;
            break;
        case 8:
        case 9:
            *dmgptr += d(3,6) + 6;
            break;
        case 10:
            pline_The("poison was deadly...");
            *dmgptr = 2 * (youdefend 
                        ? Upolyd 
                        ? u.mh : u.uhp : mdef->mhp) + FATAL_DAMAGE_MODIFIER;
            break;
        }
        return TRUE;
    }
    if (attacks(AD_DRLI, otmp)
        || spec_ability(otmp, SPFX_DRLI)) {
        /* some non-living creatures (golems, vortices) are
           vulnerable to life drain effects */
        const char *life = nonliving(mdef->data) ? "animating force" : "life";

        if (!youdefend) {
            if (vis) {
                if (otmp->oartifact == ART_STORMBRINGER)
                    pline_The("%s blade draws the %s from %s!",
                              hcolor(NH_BLACK), life, mon_nam(mdef));
                else if (otmp->oartifact == ART_LIFESTEALER)
                    pline_The("massive sword draws the %s from %s!",
                              life, mon_nam(mdef));
                /* 'thristy' weapons currently are not allowed
                 * but we'll cover that base here just in case
                 * they're added someday */
                else if (otmp->oclass == WEAPON_CLASS
                         && (otmp->oprops & ITEM_DRLI))
                    pline_The("deadly %s draws the %s from %s!",
                          distant_name(otmp, xname), life,
                          mon_nam(mdef));
                /* The Staff of Aesculapius */
                else
                    pline("%s draws the %s from %s!",
                          The(distant_name(otmp, xname)), life,
                          mon_nam(mdef));
                if (otmp->oprops & ITEM_DRLI)
                    otmp->oprops_known |= ITEM_DRLI;
            }
            if (mdef->m_lev == 0) {
                *dmgptr = 2 * mdef->mhp + FATAL_DAMAGE_MODIFIER;
            } else {
                int drain = monhp_per_lvl(mdef);

                *dmgptr += drain;
                mdef->mhpmax -= drain;
                mdef->m_lev--;
                drain /= 2;
                if (drain) {
                    healup(drain, 0, FALSE, FALSE);
                
                    if (mdef->data == &mons[PM_HYDRA])
                        pline("One of %s heads swells up and explodes!", 
                        s_suffix(mon_nam(mdef)));
                }
            }
            return vis;
        } else { /* youdefend */
            int oldhpmax = u.uhpmax;

            if (Blind)
                You_feel("an %s drain your %s!",
                         (otmp->oartifact == ART_STORMBRINGER
                          || otmp->oartifact == ART_LIFESTEALER)
                            ? "unholy blade"
                            : "object",
                         life);
            else if (otmp->oartifact == ART_STORMBRINGER)
                pline_The("%s blade drains your %s!", hcolor(NH_BLACK), life);
            else if (otmp->oartifact == ART_LIFESTEALER)
                pline_The("massive sword drains your %s!", life);
            else if (otmp->oclass == WEAPON_CLASS
                     && (otmp->oprops & ITEM_DRLI))
                pline_The("deadly %s drains your %s!",
                          distant_name(otmp, xname), life);
            /* The Staff of Aesculapius */
            else
                pline("%s drains your %s!", The(distant_name(otmp, xname)),
                      life);
            if (otmp->oprops & ITEM_DRLI)
                otmp->oprops_known |= ITEM_DRLI;
            losexp("life drainage");
            if (magr && magr->mhp < magr->mhpmax) {
                magr->mhp += (abs(oldhpmax - u.uhpmax) + 1) / 2;
                if (magr->mhp > magr->mhpmax)
                    magr->mhp = magr->mhpmax;
            }
            return TRUE;
        }
    }

    /* Chains of Malcanthet */
    if (attacks(AD_PLYS, otmp) && !rn2(5)) {
        if (realizes_damage) {
            pline_The("slithering chains bind %s!", hittee);
        }
        if (youdefend && Free_action) {
            pline_The("slithering chains cannot seem to keep a hold on you.");
        } else if (youdefend) {
            nomul(-4);
            multi_reason = "bound by the Chains of Malcanthet";
            nomovemsg = You_can_move_again;
        } else if (mdef->mcanmove) {
            paralyze_monst(mdef, rnd(4));
        }
        return realizes_damage;
    }
    /* Bradamante's Fury forces dismounts */
    if (otmp->oartifact == ART_BRADAMANTE_S_FURY) {
        if (youdefend) {
            if (u.usteed) {
                dismount_steed(DISMOUNT_THROWN);
                return TRUE;
            }
            if (!rn2(10))
                make_stunned((HStun & TIMEOUT) + 10L, FALSE);
        } else {
            if (!rn2(10))
                mdef->mstun = 1;
        }
    }
    /* Balmung item destruction */
    if (otmp->oartifact == ART_BALMUNG) {
        register struct obj *obj = some_armor(mdef);
        if (youdefend) {
            destroy_arm(some_armor(&youmonst), FALSE);
        } else if (obj) {
            obj_extract_self(obj);
            if (obj->owornmask) {
                mdef->misc_worn_check &= ~obj->owornmask;
                obj->owornmask = 0L;
                update_mon_intrinsics(mdef, obj, FALSE, FALSE);
                mdef->misc_worn_check |= I_SPECIAL;
            }
            if (vis)
                pline("%s under the brown blade!", An(aobjnam(obj, "shred")));
            obfree(obj, (struct obj *)0);
        }
    }

    /* Reaver item theft */
    if (otmp->oartifact == ART_REAVER){
        if (youattack){
            if (mdef->minvent && (Role_if(PM_PIRATE) || !rn2(10))) {
                struct obj *otmp2;
                long unwornmask;
                
                if ((otmp2 = mdef->minvent) != 0) {
                    /* take the object away from the monster */
                    obj_extract_self(otmp2);
                    if ((unwornmask = otmp2->owornmask) != 0L) {
                        mdef->misc_worn_check &= ~unwornmask;
                        if (otmp2->owornmask & W_WEP) {
                            setmnotwielded(mdef,otmp2);
                            MON_NOWEP(mdef);
                        }
                        otmp2->owornmask = 0L;
                        update_mon_intrinsics(mdef, otmp2, FALSE, FALSE);
                    }
                    if (otmp2->otyp == CORPSE &&
                        touch_petrifies(&mons[otmp2->corpsenm]) && !uarmg) {
                        char kbuf[BUFSZ];

                        /*Sprintf(kbuf, "stolen %s corpse", mons[otmp2->corpsenm].pmnames[NEUTRAL]);*/
                        Sprintf(kbuf, "stolen %s corpse", corpse_xname(otmp2, (const char *) 0, CXN_NO_PFX));
                        
                        instapetrify(kbuf);
                    }
                    /* give the object to the character */
                    otmp2 = Role_if(PM_PIRATE) 
                        ? hold_another_object(otmp2, "Ye snatched but dropped %s.", 
                                                      doname(otmp2), "Ye steal: ") 
                                : hold_another_object(otmp2, "You snatched but dropped %s.", 
                                                      doname(otmp2), "You steal: ");
                    /* more take-away handling, after theft message */
                    if (unwornmask & W_WEP) {		/* stole wielded weapon */
                        possibly_unwield(mdef, FALSE);
                    } else if (unwornmask & W_ARMG) {	/* stole worn gloves */
                        mselftouch(mdef, (const char *)0, TRUE);
                        if (mdef->mhp <= 0)	/* it's now a statue */
                            return 1; /* monster is dead */
                    }
                }
            }
        } else if(youdefend){
            char buf[BUFSZ];
            buf[0] = '\0';
            steal(magr, buf, TRUE);
        } else{
            struct obj *obj;
            /* find an object to steal, non-cursed if magr is tame */
            for (obj = mdef->minvent; obj; obj = obj->nobj)
                if (!magr->mtame || !obj->cursed)
                    break;

            if (obj) {
                char buf[BUFSZ], onambuf[BUFSZ], mdefnambuf[BUFSZ];

                /* make a special x_monnam() call that never omits
                the saddle, and save it for later messages */
                Strcpy(mdefnambuf, x_monnam(mdef, ARTICLE_THE, (char *)0, 0, FALSE));
                if (u.usteed == mdef &&
                    obj == which_armor(mdef, W_SADDLE))
                    /* "You can no longer ride <steed>." */
                    dismount_steed(DISMOUNT_POLY);
                obj_extract_self(obj);
                if (obj->owornmask) {
                    mdef->misc_worn_check &= ~obj->owornmask;
                    if (obj->owornmask & W_WEP)
                        setmnotwielded(mdef,obj);
                    obj->owornmask = 0L;
                    update_mon_intrinsics(mdef, obj, FALSE, FALSE);
                }
                /* add_to_minv() might free obj [if it merges] */
                if (vis)
                    Strcpy(onambuf, doname(obj));
                (void) add_to_minv(magr, obj);
                if (vis) {
                    Strcpy(buf, Monnam(magr));
                    pline("%s steals %s from %s!", buf,
                          onambuf, mdefnambuf);
                }
                possibly_unwield(mdef, FALSE);
                mdef->mstrategy &= ~STRAT_WAITFORU;
                mselftouch(mdef, (const char *)0, FALSE);
                if (mdef->mhp <= 0)
                    return 1;
            }
        }
    }
    
    /* Staff of Rot */
    if (attacks(AD_WTHR, otmp) && !rn2(3)) {
        /* Duplicated from uhitm.c */
        uchar withertime = max(2, *dmgptr);
        boolean lose_maxhp = (withertime >= 8); /* if already withering */
        /* Most monster attacks don't do their normal damange, but the Staff
         * of Rot is an exceptions - it's not a monster attack, and the 
         * quarterstaff needs some help anyway. */
        /* tmp = 0;  doesn't deal immediate damage */
        
        boolean no_effect = (nonliving(youmonst.data) || is_vampshifter(&youmonst));
        if (youdefend && !no_effect) {
            lose_maxhp = (withertime >= 8); /* if already withering */
            if (Withering)
                Your("withering speeds up!");
            else
                You("begin to wither away!");
            incr_itimeout(&HWithering, withertime);
            if (lose_maxhp) {
                if (Upolyd && u.mhmax > 1) {
                    u.mhmax--;
                    u.mh = min(u.mh, u.mhmax);
                } else if (u.uhpmax > 1) {
                    u.uhpmax--;
                    u.uhp = min(u.uhp, u.uhpmax);
                }
            }
        } else {
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
        return realizes_damage;
    }
    chance = 4;
    /* Thiefbane gets a huge bonus for cancelling thiefs or covetous */
    if (otmp->oartifact == ART_THIEFBANE && 
        (is_covetous(mdef->data) 
         || dmgtype(mdef->data, AD_SITM) 
         || dmgtype(mdef->data, AD_SEDU)))
        chance = 8;
    
    /* WAC -- 1/6 chance of cancellation with foobane weapons */
    if (otmp->oartifact == ART_ORCRIST ||
        otmp->oartifact == ART_DEMONBANE ||
        otmp->oartifact == ART_THIEFBANE) {
        if (!mdef->mcan && dieroll < chance) {
            if (realizes_damage) {
                pline("%s %s!", The(distant_name(otmp, xname)), Blind ?
                      "roars deafeningly" : "shines brilliantly");
                pline("It strikes %s!", hittee);
            }
            cancel_monst(mdef, otmp, youattack, TRUE, magr == mdef);
            return TRUE;
        }
    }
    return msgprinted;
}

static NEARDATA const char recharge_type[] = { ALLOW_COUNT, ALL_CLASSES, 0 };
static NEARDATA const char invoke_types[] = { ALL_CLASSES, 0 };
/* #invoke: an "ugly check" filters out most objects */

/* the #invoke command */
int
doinvoke()
{
    struct obj *obj;

    obj = getobj(invoke_types, "invoke");
    if (!obj)
        return 0;
    if (Hidinshell) {
        You_cant("invoke %s while hiding in your shell.",
                 the(distant_name(obj, xname)));
        return 0;
    }
    if (!retouch_object(&obj, FALSE))
        return 1;
    return arti_invoke(obj);
}

STATIC_OVL int
arti_invoke(obj)
struct obj *obj;
{
    register const struct artifact *oart = get_artifact(obj);
    register struct monst *mtmp;
    register struct permonst *pm;
    int unseen;
    int artinum = obj->oartifact;

    if (!obj) {
        impossible("arti_invoke without obj");
        return 0;
    }
    if (!oart || !oart->inv_prop) {
        if (obj->otyp == CRYSTAL_BALL)
            use_crystal_ball(&obj);
        else
            pline1(nothing_happens);
        return 1;
    }

    if (oart->inv_prop > LAST_PROP) {
        if (artinum == ART_FIREWALL && !Role_if(PM_FLAME_MAGE)) {
            You("don't feel that kind of connection with %s.", 
                artiname(obj->oartifact));
            return 1;
        }
        if (artinum == ART_DEEP_FREEZE && !Role_if(PM_ICE_MAGE)) {
            You("don't feel that kind of connection with %s.",
                artiname(obj->oartifact));
            return 1;
        }
        /* It's a special power, not "just" a property */
        if (obj->age > monstermoves) {
            /* the artifact is tired :-) */
            You_feel("that %s %s ignoring you.", the(xname(obj)),
                     otense(obj, "are"));
            /* and just got more so; patience is essential... */
            obj->age += (long) d(3, 10);
            return 1;
        }
        
        obj->age = monstermoves + rnz(100);

        switch (oart->inv_prop) {
        case TAMING: {
            struct obj pseudo;

            pseudo =
                zeroobj; /* neither cursed nor blessed, zero oextra too */
            pseudo.otyp = SCR_TAMING;
            (void) seffects(&pseudo);
            break;
        }
        case HEALING: {
            int healamt = (u.uhpmax + 1 - u.uhp) / 2;
            long creamed = (long) u.ucreamed;

            if (Upolyd)
                healamt = (u.mhmax + 1 - u.mh) / 2;
            if (healamt || Sick || Slimed || Withering || Blinded > creamed)
                You_feel("better.");
            else
                goto nothing_special;
            if (healamt > 0) {
                if (Upolyd)
                    u.mh += healamt;
                else
                    u.uhp += healamt;
            }
            if (Sick)
                make_sick(0L, (char *) 0, FALSE, SICK_ALL);
            if (Slimed)
                make_slimed(0L, (char *) 0);
            if (Withering)
                set_itimeout(&HWithering, (long) 0);
            if (Blinded > creamed)
                make_blinded(creamed, FALSE);
            if (LarvaCarrier) {
                make_carrier(0L, FALSE);
            }
            /* We also forget we were afraid! */
            make_afraid(0L, TRUE);
            context.botl = TRUE;
            break;
        }
        case ENERGY_BOOST: {
            int epboost = (u.uenmax + 1 - u.uen) / 2;

            if (epboost > 120)
                epboost = 120; /* arbitrary */
            else if (epboost < 12)
                epboost = u.uenmax - u.uen;
            if (epboost) {
                u.uen += epboost;
                context.botl = TRUE;
                You_feel("re-energized.");
            } else
                goto nothing_special;
            break;
        }
        case UNTRAP: {
            if (!untrap(TRUE)) {
                obj->age = 0; /* don't charge for changing their mind */
                return 0;
            }
            break;
        }
        case CHARGE_OBJ: {
            struct obj *otmp = getobj(recharge_type, "charge");
            boolean b_effect;

            if (!otmp) {
                obj->age = 0;
                return 0;
            }
            b_effect = (obj->blessed && (oart->role == Role_switch
                                         || oart->role == NON_PM));
            recharge(otmp, b_effect ? 1 : obj->cursed ? -1 : 0, &youmonst);
            update_inventory();
            break;
        }
        case LEV_TELE:
            level_tele();
            break;
        case SELF_TELE:
            tele();
            break;
        case LIGHT_AREA: {
            struct obj *pseudo = mksobj(SCR_LIGHT, FALSE, FALSE);
            bless(pseudo);
            pseudo->ox = u.ux, pseudo->oy = u.uy;
            if (!Blind)
                pline("%s shines brightly for an instant!", The(xname(obj)));
            else
                pline("%s grows warm for a second!", The(xname(obj)));
            litroom(TRUE, pseudo);
            obfree(pseudo, NULL);
            vision_recalc(0);

            if (is_undead(youmonst.data)) {
                You("burn in the radiance!");
                /* This is ground zero.  Not good news ... */
                u.uhp /= 100;
                if (u.uhp < 1) {
                    u.uhp = 0;
                    killer.format = KILLED_BY;
                    Strcpy(killer.name, "the Holy Spear of Light");
                    done(DIED);
                }
            }

            /* Undead and Demonics can't stand the light */
            unseen = 0;
            for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
                if (DEADMONSTER(mtmp))
                    continue;
                if (distu(mtmp->mx, mtmp->my) > 9 * 9)
                    continue;
                if (couldsee(mtmp->mx, mtmp->my)
                    && (is_undead(mtmp->data) || is_demon(mtmp->data))
                    && !resist(mtmp, '\0', 0, TELL)) {
                    if (canseemon(mtmp))
                        pline("%s burns in the radiance!", Monnam(mtmp));
                    else
                        unseen++;

                    /* damage depends on distance, divisor ranges from 10 to 2
                     */
                    mtmp->mhp /= (10 - (distu(mtmp->mx, mtmp->my) / 10));
                    if (mtmp->mhp < 1)
                        mtmp->mhp = 1;
                }
            }
            if (unseen)
                You_hear("%s of intense pain!",
                         unseen > 1 ? "cries" : "a cry");
            break;
        }
        case CREATE_PORTAL: {
            int i, num_ok_dungeons, last_ok_dungeon = 0;
            d_level newlev;
            extern int n_dgns; /* from dungeon.c */
            winid tmpwin = create_nhwindow(NHW_MENU);
            anything any;

            any = zeroany; /* set all bits to zero */
            
            if (Is_blackmarket(&u.uz) && *u.ushops) {
                You_feel("very disoriented for a moment.");
                break;
            }
            start_menu(tmpwin);
            /* use index+1 (cant use 0) as identifier */
            for (i = num_ok_dungeons = 0; i < n_dgns; i++) {
                if (!dungeons[i].dunlev_ureached)
                    continue;
                any.a_int = i + 1;
                add_menu(tmpwin, NO_GLYPH, &any, 0, 0, ATR_NONE,
                         dungeons[i].dname, MENU_UNSELECTED);
                num_ok_dungeons++;
                last_ok_dungeon = i;
            }
            end_menu(tmpwin, "Open a portal to which dungeon?");
            if (num_ok_dungeons > 1) {
                /* more than one entry; display menu for choices */
                menu_item *selected;
                int n;

                n = select_menu(tmpwin, PICK_ONE, &selected);
                if (n <= 0) {
                    destroy_nhwindow(tmpwin);
                    goto nothing_special;
                }
                i = selected[0].item.a_int - 1;
                free((genericptr_t) selected);
            } else
                i = last_ok_dungeon; /* also first & only OK dungeon */
            destroy_nhwindow(tmpwin);

            /*
             * i is now index into dungeon structure for the new dungeon.
             * Find the closest level in the given dungeon, open
             * a use-once portal to that dungeon and go there.
             * The closest level is either the entry or dunlev_ureached.
             */
            newlev.dnum = i;
            if (dungeons[i].depth_start >= depth(&u.uz))
                newlev.dlevel = dungeons[i].entry_lev;
            else
                newlev.dlevel = dungeons[i].dunlev_ureached;

            if (u.uhave.amulet || In_endgame(&u.uz) || In_endgame(&newlev)
                || newlev.dnum == u.uz.dnum || !next_to_u()) {
                You_feel("very disoriented for a moment.");
            } else {
                if (!Blind)
                    You("are surrounded by a shimmering sphere!");
                else
                    You_feel("weightless for a moment.");
                if (u.uswallow) {
                    unstuck(u.ustuck);
                    docrt();
                }
                u.ustuck = 0;
                goto_level(&newlev, FALSE, FALSE, FALSE);
            }
            break;
        }
        case ENLIGHTENING:
            enlightenment(MAGICENLIGHTENMENT, ENL_GAMEINPROGRESS);
            break;
        case SUMMON_FIRE_ELEMENTAL:
            pm = &mons[PM_FIRE_ELEMENTAL];
            You("summon an elemental.");
            mtmp = makemon(pm, u.ux, u.uy, MM_EDOG | MM_IGNOREWATER);
            initedog(mtmp);
            u.uconduct.pets++;
            mtmp->msleeping = 0;
            break;
        case SUMMON_WATER_ELEMENTAL:
            /* --hackem: Added more variety for "stormy" pets here */
            switch (rnd(10)) {
            case 1: pm = &mons[PM_WATER_ELEMENTAL]; break;
            case 2: pm = &mons[PM_AIR_ELEMENTAL]; break;
            case 3: pm = &mons[PM_ICE_VORTEX]; break;
            case 4: pm = &mons[PM_ENERGY_VORTEX]; break;
            case 5: pm = &mons[PM_BABY_WHITE_DRAGON]; break;
            case 6: pm = &mons[PM_BABY_BLUE_DRAGON]; break;
            case 7: pm = &mons[PM_FROST_GIANT]; break;
            case 8: pm = &mons[PM_STORM_GIANT]; break;
            case 9: pm = &mons[PM_FREEZING_SPHERE]; break;
            case 10: pm = &mons[PM_SHOCKING_SPHERE]; break;
            default: pm = &mons[PM_WATER_ELEMENTAL]; break;
            }
            mtmp = makemon(pm, u.ux, u.uy, MM_EDOG | MM_IGNOREWATER);
            You("blow the whistle and a creature appears from a storm cloud!");
            initedog(mtmp);
            u.uconduct.pets++;
            mtmp->msleeping = 0;
            break;
        case LIGHTNING_BOLT: {
            struct obj* pseudo = mksobj(WAN_LIGHTNING, FALSE, FALSE);
            pseudo->blessed = pseudo->cursed = 0;
            /* type is a "spell of lightning bolt" which doesn't actually
            * exist: 10 + AD_ELEC - 1 */
            if(!getdir(NULL) || (!u.dx && !u.dy && !u.dz)) {
                int damage = zapyourself(pseudo, TRUE);
                if (damage > 0) {
                    losehp(damage, "struck by lightning", NO_KILLER_PREFIX);
                }
            }
            else {
                /* don't use weffects - we want higher damage than that */
                buzz(9 + AD_ELEC, 8, u.ux, u.uy, u.dx, u.dy);
            }
            obfree(pseudo, NULL);
            break;
        }
        case SEFFECT: {
            struct obj* pseudo = NULL;
            switch (artinum) {
            case ART_IMHULLU:
                pseudo = mksobj(SCR_AIR, FALSE, FALSE);
                break;
            case ART_DEEP_FREEZE:
                cast_sphere(SPE_FREEZE_SPHERE); /* Casts freeze sphere */
                /* Virtually reads a scroll of ice */
                pseudo = mksobj(SCR_ICE, FALSE, FALSE);
                break;
            case ART_FIREWALL:
                cast_sphere(SPE_FLAME_SPHERE); /* Casts flame sphere */
                break;
            default:
                impossible("bad artifact invocation seffect?");
                break;
            }
            if (pseudo) {
                pseudo->blessed = TRUE;
                pseudo->cursed = FALSE;
                if (!seffects(pseudo))
                    obfree(pseudo, NULL);
            }
            break;
        }
        case WITHER: {
            uchar withertime = max(20, 60);
            boolean lose_maxhp = (withertime >= 8); /* if already withering */
            pline("%s gleams with dark energy!", artiname(obj->oartifact));
            aggravate();
            
            if (Withering)
                Your("withering speeds up!");
            else
                You("begin to wither away!");
            incr_itimeout(&HWithering, withertime);

            if (lose_maxhp) {
                if (Upolyd && u.mhmax > 1) {
                    u.mhmax--;
                    u.mh = min(u.mh, u.mhmax);
                } else if (u.uhpmax > 1) {
                    u.uhpmax--;
                    u.uhp = min(u.uhp, u.uhpmax);
                }
            }
            break;
        }
        case CREATE_AMMO: {
            struct obj *otmp = mksobj(obj->otyp == CROSSBOW ? CROSSBOW_BOLT : ARROW, TRUE, FALSE);

            if (!otmp)
                goto nothing_special;
            otmp->blessed = obj->blessed;
            otmp->cursed = obj->cursed;
            otmp->bknown = obj->bknown;
            otmp->oeroded = otmp->oeroded2 = 0;
            if (obj->blessed) {
                if (otmp->spe < 0)
                    otmp->spe = 0;
                otmp->quan += rnd(10);
            } else if (obj->cursed) {
                if (otmp->spe > 0)
                    otmp->spe = 0;
            } else
                otmp->quan += rnd(5);
            otmp->owt = weight(otmp);
            otmp = hold_another_object(otmp, "Suddenly %s out.",
                                       aobjnam(otmp, "fall"), (char *) 0);
            nhUse(otmp);
            break;
        }
        case PHASING:   /* Walk through walls and stone like a xorn */
            if (Passes_walls)
                goto nothing_special;
            if (!Hallucination) {
                Your("body begins to feel less solid.");
            } else {
                You_feel("one with the spirit world.");
            }
            incr_itimeout(&HPasses_walls, (50 + rnd(100)));
            obj->age += HPasses_walls; /* Time begins after phasing ends */
            break;
        case SMOKE_CLOUD: {
            coord cc;
            cc.x = u.ux;
            cc.y = u.uy;
            /* Cause trouble if cursed or player is wrong role */
            if (!obj->cursed && (Role_switch == oart->role || !oart->role)) {
                You("may summon a stinking cloud.");
                pline("Where do you want to center the cloud?");
                if (getpos(&cc, TRUE, "the desired position") < 0) {
                    pline("%s", Never_mind);
                    obj->age = 0;
                    return 0;
                }
                if (!cansee(cc.x, cc.y) || distu(cc.x, cc.y) >= 64) {
                    You("smell rotten eggs.");
                    return 0;
                }
            }
            pline("A cloud of toxic smoke pours out!");
            (void) create_gas_cloud(cc.x, cc.y, 3 + bcsign(obj),
                                    8 + 4 * bcsign(obj));
            break;
        }
        case CHANNEL:
            /* Should this break atheist conduct?  Currently it doesn't,
             * under the excuse of being necessary to ascend.
             * But still, we're channeling a god's power here... */
            if (IS_ALTAR(levl[u.ux][u.uy].typ)) {
                aligntyp altar_align = Amask2align(levl[u.ux][u.uy].altarmask
                                                   & AM_MASK);
                boolean high_altar = (Is_astralevel(&u.uz)
                                      || Is_sanctum(&u.uz))
                                     && (levl[u.ux][u.uy].altarmask
                                         & AM_SHRINE);
                if (!Blind)
                    pline("Tendrils of %s mist seep out of %s "
                          "and into the altar below...",
                          hcolor("crimson"), the(xname(obj)));
                else
                    You_feel("something flow from %s.", the(xname(obj)));
                if (altar_align == A_NONE) {
                    if (high_altar && Role_if(PM_INFIDEL)
                        && u.uachieve.amulet) {
                        godvoice(A_NONE, (char *) 0);
                        qt_pager(QT_MOLOCH_2);
                        You_feel("strange energies envelop %s.",
                                 the(xname(obj)));
                        u.uhave.amulet = 1;
                        break;
                    }
                    if (!Blind)
                        pline_The("altar glows for a moment.");
                    /* nothing happens */
                    break;
                }
                if (high_altar) {
                    /* messages yoinked from pray.c */
                    You("sense a conflict between %s and %s.",
                        align_gname(A_NONE), a_gname());
                    if (u.uachieve.amulet && altar_align == inf_align(1)) {
                        You_feel("the power of %s increase.",
                                 align_gname(A_NONE));
                    } else {
                        pline("%s feel the power of %s decrease.",
                              u.ualign.type == A_NONE ? "Unluckily, you"
                                                      : "You",
                              align_gname(A_NONE));
                        godvoice(altar_align, "So, mortal!  You dare "
                                              "desecrate my High Temple!");
                        god_zaps_you(altar_align);
                        break;
                    }
                }
                levl[u.ux][u.uy].altarmask &= AM_SHRINE;
                levl[u.ux][u.uy].altarmask |= AM_NONE;
                if (!Blind)
                    pline_The("altar glows %s.", hcolor(NH_RED));
                if (!high_altar) {
                    /* the Idol does all the work for you,
                     * so you don't get a luck increase;
                     * but you don't get a hostile minion, either */
                    struct monst *pri = findpriest(temple_occupied(u.urooms));
                    if (pri && mon_aligntyp(pri) != A_NONE)
                        angry_priest();
                } else {
                    /* At this point, the player must be an Infidel.
                     * Should we still check for opposite alignment?
                     * Currently, Moloch doesn't care. */
                    adjalign(10);
                    u.uachieve.ascended = 1;
                    pline1("A sinister laughter echoes through the temple, "
                           "and you're bathed in darkness...");
                    godvoice(A_NONE, "My pawn, thou hast done well!");
                    display_nhwindow(WIN_MESSAGE, FALSE);
                    verbalize("In return for thy service, "
                              "I grant thee a part of My domain!");
                    You("ascend, becoming the Archfiend of Moloch...");
                    done(ASCENDED);
                }
            } else {
                obj->age = 0; /* will be set below */
                use_figurine(&obj);
            }
            break;
        case DEATH_MAGIC:
            if (u.uluck < -9) {
                /* being immune to death magic doesn't help in this instance */
                pline("%s turns on you!", The(xname(obj)));
                u.uhp = 0;
                killer.format = KILLED_BY;
                if (obj->oartifact == ART_EYE_OF_VECNA) {
                    Strcpy(killer.name, "the Eye of Vecna");
                } else {
                    Strcpy(killer.name, "the Hand of Vecna");
                }
                done(DIED);
            } else {
                if (obj->oartifact == ART_EYE_OF_VECNA) {
                    pline("%s looks around with its icy gaze!",
                          The(xname(obj)));
                /* The Hand of Vecna needs to be 'worn' for it to be invoked */
                } else if (uarmg && uarmg->oartifact == ART_HAND_OF_VECNA) {
                    You("hold %s up high above your %s!",
                        the(xname(obj)), body_part(HEAD));
                } else if (obj->oartifact == ART_HAND_OF_VECNA) {
                    You("hold %s up high above your %s, but nothing happens.",
                        the(xname(obj)), body_part(HEAD));
                    break;
                }

                for (mtmp = fmon; mtmp; mtmp = mtmp->nmon) {
                    if (DEADMONSTER(mtmp))
                        continue;
                    /* The eye is never blind ... */
                    if ((obj->oartifact == ART_EYE_OF_VECNA ? couldsee(mtmp->mx, mtmp->my)
                                                            : canspotmon(mtmp))
                        && !immune_death_magic(mtmp->data)) {
                        int tmp = 12;
                        switch (rn2(20)) {
                        case 19:
                        case 18:
                        case 17:
                            if (!(resists_magm(mtmp) || defended(mtmp, AD_MAGM))
                                && !resist(mtmp, 0, 0, 0)) {
                                mtmp->mhp = 0;
                                if (DEADMONSTER(mtmp)) {
                                    if (context.mon_moving)
                                        monkilled(mtmp, (char *) 0, AD_DETH);
                                    else
                                        killed(mtmp);
                                }
                            }
                            break;
                        default: /* case 16 through case 2 */
                            if (!Deaf)
                                pline("%s %s in %s!", Monnam(mtmp),
                                      makeplural(growl_sound(mtmp)),
                                      rn2(2) ? "agony" : "pain");
                            else if (cansee(mtmp->mx, mtmp->my))
                                pline("%s trembles in %s!", Monnam(mtmp),
                                rn2(2) ? "agony" : "pain");
                            /* mhp will then still be less than this value */
                            mtmp->mhpmax -= rn2(tmp / 2 + 1);
                            if (mtmp->mhpmax <= 0) /* protect against invalid value */
                                mtmp->mhpmax = 1;
                            mtmp->mhp /= 3;
                            if (mtmp->mhp < 1)
                                mtmp->mhp = 1;
                            break;
                        case 1:
                        case 0:
                            if (resists_magm(mtmp) || defended(mtmp, AD_MAGM))
                                shieldeff(mtmp->mx, mtmp->my);
                            if (obj->oartifact == ART_EYE_OF_VECNA) {
                                pline("%s resists %s deadly gaze.",
                                      Monnam(mtmp), the(s_suffix(xname(obj))));
                            } else {
                                pline("%s resists %s deadly aura.",
                                      Monnam(mtmp), the(s_suffix(xname(obj))));
                            }
                            tmp = 0;
                            break;
                        }
                    }
                }
            }
            /* Use at your own risk... */
            if (u.ualign.type != A_NONE) {
                if (u.ualign.type == A_LAWFUL) {
                    You_feel("very guilty.");
                    adjalign(-7);
                } else {
                    You_feel("guilty.");
                    adjalign(-3);
                }
            }
            change_luck(-3);
            exercise(A_WIS, FALSE);
            break;
        }
    } else {
        long eprop = (u.uprops[oart->inv_prop].extrinsic ^= W_ARTI),
             iprop = u.uprops[oart->inv_prop].intrinsic;
        boolean on = (eprop & W_ARTI) != 0; /* true if prop just set */

        if (on && obj->age > monstermoves) {
            /* the artifact is tired :-) */
            u.uprops[oart->inv_prop].extrinsic ^= W_ARTI;
            You_feel("that %s %s ignoring you.", the(xname(obj)),
                     otense(obj, "are"));
            /* can't just keep repeatedly trying */
            obj->age += (long) d(3, 10);
            return 1;
        } else if (!on) {
            /* when turning off property, determine downtime */
            /* arbitrary for now until we can tune this -dlc */
            obj->age = monstermoves + rnz(100);
        }

        if ((eprop & ~W_ARTI) || iprop) {
 nothing_special:
            /* you had the property from some other source too */
            if (carried(obj))
                You_feel("a surge of power, but nothing seems to happen.");
            return 1;
        }
        switch (oart->inv_prop) {
        case CONFLICT:
            if (on)
                You_feel("like a rabble-rouser.");
            else
                You_feel("the tension decrease around you.");
            break;
        case LEVITATION:
            if (on) {
                float_up();
                spoteffects(FALSE);
            } else
                (void) float_down(I_SPECIAL | TIMEOUT, W_ARTI);
            break;
        case INVIS:
            if (BInvis || Blind)
                goto nothing_special;
            newsym(u.ux, u.uy);
            if (on)
                Your("body takes on a %s transparency...",
                     Hallucination ? "normal" : "strange");
            else
                Your("body seems to unfade...");
            break;
        case WWALKING:
            if (on) {
                pline_The("dungeon starts to tremble!");
                do_earthquake(7);
                Your("feet are surrounded by a swirl of foam!");
                if (u.uinwater)
                    spoteffects(TRUE);
            } else {
                You_feel("as if you are no longer at equilibrium.");
                if ((is_pool(u.ux, u.uy) || is_lava(u.ux, u.uy))
                    && !Levitation && !Flying && !is_clinger(youmonst.data)
                    && !context.takeoff.cancelled_don
                    /* avoid recursive call to lava_effects() */
                    && !iflags.in_lava_effects) {
                        spoteffects(TRUE);
                }
            }
            break;
        }
    }

    return 1;
}

/* will freeing this object from inventory cause levitation to end? */
boolean
finesse_ahriman(obj)
struct obj *obj;
{
    const struct artifact *oart;
    struct prop save_Lev;
    boolean result;

    /* if we aren't levitating or this isn't an artifact which confers
       levitation via #invoke then freeinv() won't toggle levitation */
    if (!Levitation || (oart = get_artifact(obj)) == 0
        || oart->inv_prop != LEVITATION || !(ELevitation & W_ARTI))
        return FALSE;

    /* arti_invoke(off) -> float_down() clears I_SPECIAL|TIMEOUT & W_ARTI;
       probe ahead to see whether that actually results in floating down;
       (this assumes that there aren't two simultaneously invoked artifacts
       both conferring levitation--safe, since if there were two of them,
       invoking the 2nd would negate the 1st rather than stack with it) */
    save_Lev = u.uprops[LEVITATION];
    HLevitation &= ~(I_SPECIAL | TIMEOUT);
    ELevitation &= ~W_ARTI;
    result = (boolean) !Levitation;
    u.uprops[LEVITATION] = save_Lev;
    return result;
}


/*
 * Artifact is dipped into water
 * -1 not handled here (not used up here)
 *  0 no effect but used up
 *  else return
 *  AD_FIRE, etc.
 *  Note caller should handle what happens to the medium in these cases.
 *      This only prints messages about the actual artifact.
 */

int
artifact_wet(obj, silent)
struct obj *obj;
boolean silent;
{
	 if (!obj->oartifact) return (-1);
	 switch (artilist[(int) (obj)->oartifact].attk.adtyp) {
		 case AD_FIRE:
			 if (!silent) {
				pline("A cloud of steam rises.");
				pline("%s is untouched.", The(xname(obj)));
			 }
			 return (AD_FIRE);
		 case AD_COLD:
			 if (!silent) {
				pline("Icicles form and fall from the freezing %s.", xname(obj));
			 }
			 return (AD_COLD);
		 case AD_ELEC:
			 if (!silent) {
				pline_The("humid air crackles with electricity from %s.", xname(obj));
			 }
			 return (AD_ELEC);
		 case AD_DRLI:
			 if (!silent) {
				pline("%s absorbs the water!", The(xname(obj)));
			 }
			 return (AD_DRLI);
		 default:
			 break;
	}
	return (-1);
}

/* WAC return TRUE if artifact is always lit */
boolean
artifact_light(obj)
struct obj *obj;
{
    /* not artifacts but treat them as if they were because they emit
       light without burning */
    if (obj && (Is_dragon_armor(obj)
                && (Dragon_armor_to_scales(obj) == GOLD_DRAGON_SCALES
                    || Dragon_armor_to_scales(obj) == CHROMATIC_DRAGON_SCALES))
        && (obj->owornmask & (W_ARM | W_ARMC)) != 0L)
        return TRUE;

    if (obj && obj->otyp == SHIELD_OF_LIGHT
        && (obj->owornmask & W_ARMS) != 0L)
        return TRUE;

    return (boolean) (get_artifact(obj) && 
        (obj->oartifact == ART_SUNSWORD
        || obj->oartifact == ART_HOLY_SPEAR_OF_LIGHT
        || obj->oartifact == ART_CANDLE_OF_ETERNAL_FLAME));
}

/* KMH -- Talking artifacts are finally implemented */
void
arti_speak(obj)
struct obj *obj;
{
    register const struct artifact *oart = get_artifact(obj);
    const char *line;
    char buf[BUFSZ];

    /* Is this a speaking artifact? */
    if (!oart || !(oart->spfx & SPFX_SPEAK))
        return;

    line = getrumor(bcsign(obj), buf, TRUE);
    if (!*line)
        line = "NetHack rumors file closed for renovation.";
    pline("%s:", Tobjnam(obj, "whisper"));
    verbalize1(line);
    maybe_learn_elbereth(line);
    return;
}

boolean
artifact_has_invprop(otmp, inv_prop)
struct obj *otmp;
uchar inv_prop;
{
    const struct artifact *arti = get_artifact(otmp);

    return (boolean) (arti && (arti->inv_prop == inv_prop));
}

/* Return the price sold to the hero of a given artifact or unique item */
long
arti_cost(otmp)
struct obj *otmp;
{
    if (!otmp->oartifact)
        return (long) objects[otmp->otyp].oc_cost;
    else if (artilist[(int) otmp->oartifact].cost)
        return artilist[(int) otmp->oartifact].cost;
    else
        return (100L * (long) objects[otmp->otyp].oc_cost);
}

STATIC_OVL uchar
abil_to_adtyp(abil)
long *abil;
{
    struct abil2adtyp_tag {
        long *abil;
        uchar adtyp;
    } abil2adtyp[] = {
        { &EFire_resistance, AD_FIRE },
        { &ECold_resistance, AD_COLD },
        { &EShock_resistance, AD_ELEC },
        { &EAntimagic, AD_MAGM },
        { &EDisint_resistance, AD_DISN },
        { &EPoison_resistance, AD_DRST },
        { &ESleep_resistance, AD_SLEE },
        { &EDrain_resistance, AD_DRLI },
        { &EStable, AD_CLOB },
        { &EAcid_resistance, AD_ACID },
        { &EStone_resistance, AD_STON },
        { &ESick_resistance, AD_DISE },
        { &ESonic_resistance, AD_LOUD },
        /* AD_WTHR has no Withering Resistance */
    };
    int k;

    for (k = 0; k < SIZE(abil2adtyp); k++) {
        if (abil2adtyp[k].abil == abil)
            return abil2adtyp[k].adtyp;
    }
    return 0;
}

STATIC_OVL unsigned long
abil_to_spfx(abil)
long *abil;
{
    static const struct abil2spfx_tag {
        long *abil;
        unsigned long spfx;
    } abil2spfx[] = {
        { &ESearching, SPFX_SEARCH },
        { &EHalluc_resistance, SPFX_HALRES },
        { &ETelepat, SPFX_ESP },
        { &EStealth, SPFX_STLTH },
        { &ERegeneration, SPFX_REGEN },
        { &ETeleport_control, SPFX_TCTRL },
        { &EWarn_of_mon, SPFX_DFLAGH },
        { &EWarning, SPFX_WARN },
        { &EEnergy_regeneration, SPFX_EREGEN },
        { &EHalf_spell_damage, SPFX_HSPDAM },
        { &EHalf_physical_damage, SPFX_HPHDAM },
        { &EReflecting, SPFX_REFLECT },
        { &EMagical_breathing, SPFX_BREATHE },
    };
    int k;

    for (k = 0; k < SIZE(abil2spfx); k++) {
        if (abil2spfx[k].abil == abil)
            return abil2spfx[k].spfx;
    }
    return 0L;
}

/*
 * Return the first item that is conveying a particular intrinsic.
 */
struct obj *
what_gives(abil)
long *abil;
{
    struct obj *obj;
    uchar dtyp;
    unsigned long spfx;
    long wornbits;
    long wornmask = (W_ARM | W_ARMC | W_ARMH | W_ARMS
                     | W_ARMG | W_ARMF | W_ARMU
                     | W_AMUL | W_RINGL | W_RINGR | W_TOOL
                     | W_ART | W_ARTI);

    if (u.twoweap)
        wornmask |= W_SWAPWEP;
    dtyp = abil_to_adtyp(abil);
    spfx = abil_to_spfx(abil);
    wornbits = (wornmask & *abil);

    for (obj = invent; obj; obj = obj->nobj) {
        if (obj->oartifact
            && (abil != &EWarn_of_mon || context.warntype.obj)) {
            const struct artifact *art = get_artifact(obj);

            if (art) {
                if (dtyp) {
                    if (art->cary.adtyp == dtyp /* carried */
                        || (art->defn.adtyp == dtyp /* defends while worn */
                            && (obj->owornmask & ~(W_ART | W_ARTI))))
                        return obj;
                }
                if (spfx) {
                    /* property conferred when carried */
                    if ((art->cspfx & spfx) == spfx)
                        return obj;
                    /* property conferred when wielded or worn */
                    if ((art->spfx & spfx) == spfx && obj->owornmask)
                        return obj;
                }
            }
        } else {
            if (wornbits && wornbits == (wornmask & obj->owornmask))
                return obj;
        }
    }
    return (struct obj *) 0;
}

const char *
glow_color(arti_indx)
int arti_indx;
{
    int colornum = artilist[arti_indx].acolor;
    const char *colorstr = clr2colorname(colornum);

    return hcolor(colorstr);
}

/* glow verb; [0] holds the value used when blind */
static const char *glow_verbs[] = {
    "quiver", "flicker", "glimmer", "gleam"
};

/* relative strength that Sting is glowing (0..3), to select verb */
STATIC_OVL int
glow_strength(count)
int count;
{
    /* glow strength should also be proportional to proximity and
       probably difficulty, but we don't have that information and
       gathering it is more trouble than this would be worth */
    return (count > 12) ? 3 : (count > 4) ? 2 : (count > 0);
}

const char *
glow_verb(count, ingsfx)
int count; /* 0 means blind rather than no applicable creatures */
boolean ingsfx;
{
    static char resbuf[20];

    Strcpy(resbuf, glow_verbs[glow_strength(count)]);
    /* ing_suffix() will double the last consonant for all the words
       we're using and none of them should have that, so bypass it */
    if (ingsfx)
        Strcat(resbuf, "ing");
    return resbuf;
}

/* Warning glow from in-use race-detecting artifacts like Sting, Demonbane, etc. */
void
glow_warning_effects(glower)
struct obj *glower;
{
    if (glower && glower->oartifact
        && artilist[(int) glower->oartifact].acolor != NO_COLOR) {
        int oldstr = glow_strength(glower->lastwarncnt),
            newstr = glow_strength(glower->newwarncnt);

        if (newstr > 0 && newstr != oldstr) {
            /* 'start' message */
            if (!Blind)
                pline("%s %s %s%c", bare_artifactname(glower),
                      otense(glower, glow_verb(glower->newwarncnt, FALSE)),
                      glow_color(glower->oartifact),
                      (newstr > oldstr) ? '!' : '.');
            else if (oldstr == 0) /* quivers */
                pline("%s %s slightly.", bare_artifactname(glower),
                      otense(glower, glow_verb(0, FALSE)));
        } else if (glower->newwarncnt == 0 && glower->lastwarncnt > 0) {
            /* 'stop' message */
            pline("%s stops %s.", bare_artifactname(glower),
                  glow_verb(Blind ? 0 : glower->lastwarncnt, TRUE));
        }
    }
}

/* Blinding supresses perception of artifacts like Sting that glow to warn of certain monsters. */
void
blind_glow_warnings(VOID_ARGS)
{
    register struct obj *otmp;

    for (otmp = invent; otmp; otmp = otmp->nobj) {
        if (((otmp->owornmask & (W_ARMOR | W_ACCESSORY | W_WEP))
                || (u.twoweap && (otmp->owornmask & W_SWAPWEP)))
            && has_glow_warning(otmp)
            && otmp->lastwarncnt > 0) {
            /* blindness has just been toggled; give a
               'continue' message that eventual 'stop' message will match */
            pline("%s is %s.", bare_artifactname(otmp),
                  glow_verb(Blind ? 0 : otmp->lastwarncnt, TRUE));
        }
    }
}

/* called when hero is wielding/applying/invoking a carried item, or
   after undergoing a transformation (alignment change, lycanthropy,
   polymorph) which might affect item access */
int
retouch_object(objp, loseit)
struct obj **objp; /* might be destroyed or unintentionally dropped */
boolean loseit;    /* whether to drop it if hero can longer touch it */
{
    struct obj *obj = *objp;

    /* allow hero (specifically, a crowned Infidel) in silver-hating form
       to try to perform invocation ritual */
    if (obj->otyp == BELL_OF_OPENING
        && invocation_pos(u.ux, u.uy) && !On_stairs(u.ux, u.uy)) {
        return 1;
    }

    if (touch_artifact(obj, &youmonst)) {
        char buf[BUFSZ];
        int dmg = 0;
        boolean hatemat = Hate_material(obj->material),
                bane = bane_applies(get_artifact(obj), &youmonst);

        /* nothing else to do if hero can successfully handle this object */
        if (!hatemat && !bane)
            return 1;

        /* another case where nothing should happen: hero is wearing gloves
         * which protect them from directly touching a weapon of a material they
         * hate
         * (no other gear slots are considered to completely block touching an
         * outer piece of gear; e.g. wearing body armor doesn't protect from
         * touching a worn cloak) */
        if (!bane && obj == uwep && uarmg)
            return 1;

        /* hero can't handle this object, but didn't get touch_artifact()'s
           "<obj> evades your grasp|control" message; give an alternate one */
#if 0
        if (!bane && !(hatemat && obj->material == SILVER)) {
#else
        if (!bane) {
#endif
            pline_The("%s of %s %s!", materialnm[obj->material],
                  yname(obj), rn2(2) ? "hurts to touch" : "burns your skin");
        } else {
            You_cant("handle %s%s!", yname(obj),
                     obj->owornmask ? " anymore" : "");
        }
        /* also inflict damage unless touch_artifact() already did so */
        if (!touch_blasted) {
            /* damage is somewhat arbitrary: 1d10 magical for <foo>bane,
             * half of the usual damage for materials */
            if (hatemat)
                dmg += rnd(sear_damage(obj->material) / 2);
            if (bane)
                dmg += rnd(10);
            Sprintf(buf, "handling %s that was made of %s",
                    killer_xname(obj), materialnm[obj->material]);
            losehp(dmg, buf, KILLED_BY);
            exercise(A_CON, FALSE);
        }
        /* concession to those wishing to use gear made of an adverse material:
         * don't make them totally unable to use them. In fact, they can touch
         * them just fine as long as they're willing to.
         * In keeping with the flavor of searing vs just pain implemented
         * everywhere else, only silver is actually unbearable -- other
         * hated non-silver materials can be used too. */
#if 0
        if (!bane && !(hatemat && obj->material == SILVER))
#else      
        if (!bane)
#endif
            return 1;
    }

    /* removing a worn item might result in loss of levitation,
       dropping the hero onto a polymorph trap or into water or
       lava and potentially dropping or destroying the item */
    if (obj->owornmask) {
        struct obj *otmp;

        remove_worn_item(obj, FALSE);
        for (otmp = invent; otmp; otmp = otmp->nobj)
            if (otmp == obj)
                break;
        if (!otmp)
            *objp = obj = 0;
    }

    /* if we still have it and caller wants us to drop it, do so now */
    if (loseit && obj) {
        if (Levitation) {
            freeinv(obj);
            hitfloor(obj, TRUE);
        } else {
            /* dropx gives a message iff item lands on an altar */
            if (!IS_ALTAR(levl[u.ux][u.uy].typ))
                pline("%s to the %s.", Tobjnam(obj, "fall"),
                      surface(u.ux, u.uy));
            dropx(obj);
        }
        *objp = obj = 0; /* no longer in inventory */
    }
    return 0;
}

/* an item which is worn/wielded or an artifact which conveys
   something via being carried or which has an #invoke effect
   currently in operation undergoes a touch test; if it fails,
   it will be unworn/unwielded and revoked but not dropped */
STATIC_OVL boolean
untouchable(obj, drop_untouchable)
struct obj *obj;
boolean drop_untouchable;
{
    struct artifact *art;
    boolean beingworn, carryeffect, invoked;
    long wearmask = ~(W_QUIVER | (u.twoweap ? 0L : W_SWAPWEP) | W_BALL);

    beingworn = ((obj->owornmask & wearmask) != 0L
                 /* some items in use don't have any wornmask setting */
                 || (obj->oclass == TOOL_CLASS
                     && (obj->lamplit || (obj->otyp == LEASH && obj->leashmon)
                         || (Is_container(obj) && Has_contents(obj)))));

    if ((art = get_artifact(obj)) != 0) {
        carryeffect = (art->cary.adtyp || art->cspfx);
        invoked = (art->inv_prop > 0 && art->inv_prop <= LAST_PROP
                   && (u.uprops[art->inv_prop].extrinsic & W_ARTI) != 0L);
    } else {
        carryeffect = invoked = FALSE;
    }

    if (beingworn || carryeffect || invoked) {
        if (!retouch_object(&obj, drop_untouchable)) {
            /* "<artifact> is beyond your control" or "you can't handle
               <object>" has been given and it is now unworn/unwielded
               and possibly dropped (depending upon caller); if dropped,
               carried effect was turned off, else we leave that alone;
               we turn off invocation property here if still carried */
            if (invoked && obj)
                arti_invoke(obj); /* reverse #invoke */
            return TRUE;
        }
    }
    return FALSE;
}

/* check all items currently in use (mostly worn) for touchability */
void
retouch_equipment(dropflag)
int dropflag; /* 0==don't drop, 1==drop all, 2==drop weapon */
{
    static int nesting = 0; /* recursion control */
    struct obj *obj;
    boolean dropit, had_gloves = (uarmg != 0);
    int had_rings = (!!uleft + !!uright);

    /*
     * We can potentially be called recursively if losing/unwearing
     * an item causes worn helm of opposite alignment to come off or
     * be destroyed.
     *
     * BUG: if the initial call was due to putting on a helm of
     * opposite alignment and it does come off to trigger recursion,
     * after the inner call executes, the outer call will finish
     * using the non-helm alignment rather than the helm alignment
     * which triggered this in the first place.
     */
    if (!nesting++)
        clear_bypasses(); /* init upon initial entry */

    dropit = (dropflag > 0); /* drop all or drop weapon */
    /* check secondary weapon first, before possibly unwielding primary */
    if (u.twoweap) {
        bypass_obj(uswapwep); /* so loop below won't process it again */
        (void) untouchable(uswapwep, dropit);
    }
    /* check primary weapon next so that they're handled together */
    if (uwep) {
        bypass_obj(uwep); /* so loop below won't process it again */
        (void) untouchable(uwep, dropit);
    }

    /* in case someone is daft enough to add artifact or silver saddle */
    if (u.usteed && (obj = which_armor(u.usteed, W_SADDLE)) != 0) {
        /* untouchable() calls retouch_object() which expects an object in
           hero's inventory, but remove_worn_item() will be harmless for
           saddle and we're suppressing drop, so this works as intended */
        if (untouchable(obj, FALSE))
            dismount_steed(DISMOUNT_THROWN);
    }
    /*
     * TODO?  Force off gloves if either or both rings are going to
     * become unworn; force off cloak [suit] before suit [shirt].
     * The torso handling is hypothetical; the case for gloves is
     * not, due to the possibility of unwearing silver rings.
     */

    dropit = (dropflag == 1); /* all untouchable items */
    /* loss of levitation (silver ring, or Heart of Ahriman invocation)
       might cause hero to lose inventory items (by dropping into lava,
       for instance), so inventory traversal needs to rescan the whole
       invent chain each time it moves on to another object; we use bypass
       handling to keep track of which items have already been processed */
    while ((obj = nxt_unbypassed_obj(invent)) != 0)
        (void) untouchable(obj, dropit);

    if (had_rings != (!!uleft + !!uright) && uarmg && uarmg->cursed)
        uncurse(uarmg); /* temporary? hack for ring removal plausibility */
    if (had_gloves && !uarmg)
        selftouch("After losing your gloves, you");

    if (!--nesting)
        clear_bypasses(); /* reset upon final exit */
}

static int mkot_trap_warn_count = 0;

STATIC_OVL int
count_surround_traps(x, y)
int x, y;
{
    struct rm *levp;
    struct obj *otmp;
    struct trap *ttmp;
    int dx, dy, glyph, ret = 0;

    for (dx = x - 1; dx < x + 2; ++dx)
        for (dy = y - 1; dy < y + 2; ++dy) {
            if (!isok(dx, dy))
                continue;
            /* If a trap is shown here, don't count it; the hero
             * should be expecting it.  But if there is a trap here
             * that's not shown, either undiscovered or covered by
             * something, do count it.
             */
            glyph = glyph_at(dx, dy);
            if (glyph_is_trap(glyph))
                continue;
            if ((ttmp = t_at(dx, dy)) != 0) {
                ++ret;
                continue;
            }
            levp = &levl[dx][dy];
            if (IS_DOOR(levp->typ) && (levp->doormask & D_TRAPPED) != 0) {
                ++ret;
                continue;
            }
            for (otmp = level.objects[dx][dy]; otmp; otmp = otmp->nexthere)
                if (Is_container(otmp) && otmp->otrapped) {
                    ++ret; /* we're counting locations, so just */
                    break; /* count the first one in a pile     */
                }
        }
    /*
     * [Shouldn't we also check inventory for a trapped container?
     * Even if its trap has already been found, there's no 'tknown'
     * flag to help hero remember that so we have nothing comparable
     * to a shown glyph to justify skipping it.]
     */
    return ret;
}

/* sense adjacent traps if wielding MKoT without wearing gloves */
void
mkot_trap_warn()
{
    static const char *const heat[7] = {
        "cool", "slightly warm", "warm", "very warm",
        "hot", "very hot", "like fire"
    };

    if (!uarmg && uwep && uwep->oartifact == ART_MASTER_KEY_OF_THIEVERY) {
        int idx, ntraps = count_surround_traps(u.ux, u.uy);

        if (ntraps != mkot_trap_warn_count) {
            idx = min(ntraps, SIZE(heat) - 1);
            pline_The("Key feels %s%c", heat[idx], (ntraps > 3) ? '!' : '.');
        }
        mkot_trap_warn_count = ntraps;
    } else
        mkot_trap_warn_count = 0;
}

/* Master Key is magic key if its bless/curse state meets our criteria:
   not cursed for rogues or blessed for non-rogues */
boolean
is_magic_key(mon, obj)
struct monst *mon; /* if null, non-rogue is assumed */
struct obj *obj;
{
    if (obj && obj->oartifact == ART_MASTER_KEY_OF_THIEVERY) {
        if ((mon == &youmonst) ? Role_if(PM_ROGUE)
                               : (mon && mon->data == &mons[PM_ROGUE]))
            return !obj->cursed; /* a rogue; non-cursed suffices for magic */
        /* not a rogue; key must be blessed to behave as a magic one */
        return obj->blessed;
    }
    return FALSE;
}

/* figure out whether 'mon' (usually youmonst) is carrying the magic key */
struct obj *
has_magic_key(mon)
struct monst *mon; /* if null, hero assumed */
{
    struct obj *o;
    short key = artilist[ART_MASTER_KEY_OF_THIEVERY].otyp;

    if (!mon)
        mon = &youmonst;
    for (o = ((mon == &youmonst) ? invent : mon->minvent); o;
         o = nxtobj(o, key, FALSE)) {
        if (is_magic_key(mon, o))
            return o;
    }
    return (struct obj *) 0;
}

boolean
wielding_artifact(art)
int art;
{
    if (!art)
        return FALSE;

    return ((uwep && uwep->oartifact == art)
            || (u.twoweap && uswapwep->oartifact == art));
}

boolean
awaiting_guaranteed_gift()
{
    int m;
    struct artifact *a;
    for (m = 1, a = &artilist[m]; a->otyp; a++, m++) {
        if (artiexist[m])
            continue;
        if (a->spfx & SPFX_NOGEN)
            continue;
        /* uses the same criteria as mk_artifact */
        if (Role_if(a->role)
            && (a->alignment == u.ualign.type || a->alignment == A_NONE)
            && (a->race == NON_PM || !race_hostile(&mons[a->race]))
            && (!(Race_if(PM_GIANT) && (a->mtype & MH_GIANT)))
            && (!(Role_if(PM_PRIEST) && (is_slash(a) || is_pierce(a))))) {
            return TRUE;
        }
    }
    return FALSE;
}

struct art_info_t
artifact_info(int anum)
{
    struct art_info_t art_info = { 0 };
    char buf[QBUFSZ];
    
    art_info.name = artiname(anum);
    art_info.alignment = align_str(artilist[anum].alignment);
    art_info.cost = artilist[anum].cost;
    art_info.material = materialnm[artifact_material(anum)];
    art_info.role = (artilist[anum].role == NON_PM) 
                        ? "None" : mons[artilist[anum].role].mname;
    art_info.race = (artilist[anum].race == NON_PM) 
                        ? "None" : mons[artilist[anum].race].mname;
    
    art_info.intelligent = (artilist[anum].spfx & SPFX_INTEL) != 0;
    art_info.restricted = (artilist[anum].spfx & SPFX_RESTR) != 0;
    art_info.nogen = (artilist[anum].spfx & SPFX_NOGEN) != 0;
    art_info.speaks = (artilist[anum].spfx & SPFX_SPEAK) != 0;
    art_info.beheads = (artilist[anum].spfx & SPFX_BEHEAD) != 0;
    art_info.vscross = (artilist[anum].spfx & SPFX_DALIGN) != 0;

    
    /* Hated/Targeted Monster */
    if ((artilist[anum].mtype)) {
        int i;
        for (i = 0; i < 32; i++) {
            if (artilist[anum].mtype & (1 << i)) {
                art_info.hates = makeplural(mon_race_name(i));
                break;
            }
        }
    }
    
    /* Special attacks */
    if (artilist[anum].attk.adtyp
          || artilist[anum].attk.damn || artilist[anum].attk.damd) {
        Sprintf(buf, "%s, +1d%d to-hit +1d%d damage",
                adtyp_str(artilist[anum].attk.adtyp, FALSE),
                artilist[anum].attk.damn, 
                artilist[anum].attk.damd);
        art_info.attack = malloc(100);
        strcpy(art_info.attack, buf);
        
        /* Does this deal double damage? */
        if (artilist[anum].attk.damd == 0) {
            art_info.dbldmg = malloc(100);
            if (art_info.hates) {
                Sprintf(buf, "\t\tdouble damage vs %s", art_info.hates);
                strcpy(art_info.dbldmg, buf);
            } else
                strcpy(art_info.dbldmg, "\t\tdeals double damage");
        }
    } else
        art_info.attack = NULL;
    
    /* Granted while wielded. */
    if (artilist[anum].defn.adtyp) {
        Sprintf(buf, "%s resistance", adtyp_str(artilist[anum].defn.adtyp, TRUE));
        art_info.wield_res = malloc(100);
        strcpy(art_info.wield_res, buf);
    }
    
    if ((artilist[anum].spfx & SPFX_SEARCH) != 0)
        art_info.wielded[0] = "searching";
    if ((artilist[anum].spfx & SPFX_HALRES) != 0)
        art_info.wielded[1] = "hallucination resistance";
    if ((artilist[anum].spfx & SPFX_ESP) != 0)
        art_info.wielded[2] = "telepathy";
    if ((artilist[anum].spfx & SPFX_STLTH) != 0)
        art_info.wielded[3] = "stealth";
    if ((artilist[anum].spfx & SPFX_REGEN) != 0)
        art_info.wielded[4] = "regeneration";
    if ((artilist[anum].spfx & SPFX_EREGEN) != 0)
        art_info.wielded[5] = "energy regeneration";
    if ((artilist[anum].spfx & SPFX_HSPDAM) != 0)
        art_info.wielded[6] = "half spell damage";
    if ((artilist[anum].spfx & SPFX_HPHDAM) != 0)
        art_info.wielded[7] = "half physical damage";
    if ((artilist[anum].spfx & SPFX_TCTRL) != 0)
        art_info.wielded[8] = "teleport control";
    if ((artilist[anum].spfx & SPFX_LUCK) != 0)
        art_info.wielded[9] = "luck";
    if ((artilist[anum].spfx & SPFX_XRAY) != 0)
        art_info.wielded[10] = "astral vision";
    if ((artilist[anum].spfx & SPFX_REFLECT) != 0)
        art_info.wielded[11] = "reflection";
    if ((artilist[anum].spfx & SPFX_PROTECT) != 0)
        art_info.wielded[12] = "protection";
    if ((artilist[anum].spfx & SPFX_BREATHE) != 0)
        art_info.wielded[13] = "magical breathing";
    
    if ((artilist[anum].spfx & SPFX_WARN) != 0) {
        if ((artilist[anum].spfx & SPFX_DFLAGH) != 0) {
            Sprintf(buf, "warning against %s ", art_info.hates);
            art_info.wield_warn = malloc(100);
            strcpy(art_info.wield_warn, buf);
        } else {
            /*art_info.wield_warn = "warning";*/
            strcpy(art_info.wield_warn, "warning");
        }
    }
    /* Granted while carried. */
    if (artilist[anum].cary.adtyp) {
        Sprintf(buf, "%s resistance", adtyp_str(artilist[anum].cary.adtyp, TRUE));
        art_info.carr_res = malloc(100);
        strcpy(art_info.carr_res, buf);
    } 
    if ((artilist[anum].cspfx & SPFX_SEARCH) != 0)
        art_info.carried[0] = "searching";
    if ((artilist[anum].cspfx & SPFX_HALRES) != 0)
        art_info.carried[1] = "hallucination resistance";
    if ((artilist[anum].cspfx & SPFX_ESP) != 0)
        art_info.carried[2] = "telepathy";
    if ((artilist[anum].cspfx & SPFX_STLTH) != 0)
        art_info.carried[3] = "stealth";
    if ((artilist[anum].cspfx & SPFX_REGEN) != 0)
        art_info.carried[4] = "regeneration";
    if ((artilist[anum].cspfx & SPFX_EREGEN) != 0)
        art_info.carried[5] = "energy regeneration";
    if ((artilist[anum].cspfx & SPFX_HSPDAM) != 0)
        art_info.carried[6] = "half spell damage";
    if ((artilist[anum].cspfx & SPFX_HPHDAM) != 0)
        art_info.carried[7] = "half physical damage";
    if ((artilist[anum].cspfx & SPFX_TCTRL) != 0)
        art_info.carried[8] = "teleport control";
    if ((artilist[anum].cspfx & SPFX_LUCK) != 0)
        art_info.carried[9] = "luck";
    if ((artilist[anum].cspfx & SPFX_XRAY) != 0)
        art_info.carried[10] = "astral vision";
    if ((artilist[anum].cspfx & SPFX_REFLECT) != 0)
        art_info.carried[11] = "reflection";
    if ((artilist[anum].cspfx & SPFX_PROTECT) != 0)
        art_info.carried[12] = "protection";
    if ((artilist[anum].cspfx & SPFX_BREATHE) != 0)
        art_info.carried[13] = "magical breathing";
    if ((artilist[anum].cspfx & SPFX_WARN) != 0)
        art_info.carried[14] = "warning";

    switch (artilist[anum].inv_prop) {
    case TAMING: art_info.invoke = "Taming"; break;
    case HEALING: art_info.invoke = "Healing"; break;
    case ENERGY_BOOST: art_info.invoke = "Energy Boost"; break;
    case UNTRAP: art_info.invoke = "Untrap"; break;
    case CHARGE_OBJ: art_info.invoke = "Charge Object"; break;
    case LEV_TELE: art_info.invoke = "Level Teleport"; break;
    case SELF_TELE: art_info.invoke = "Teleport"; break;
    case LIGHT_AREA: art_info.invoke = "Light Area"; break;
    case CREATE_PORTAL: art_info.invoke = "Branchport"; break;
    case ENLIGHTENING: art_info.invoke = "Enlightenment"; break;
    case SUMMON_FIRE_ELEMENTAL: art_info.invoke = "Summon Fire Elemental"; break;
    case SUMMON_WATER_ELEMENTAL: art_info.invoke = "Summon Storm Pet"; break;
    case LIGHTNING_BOLT: art_info.invoke = "Lightning Bolt"; break;
    case WITHER: art_info.invoke = "Wither"; break;
    case CREATE_AMMO: art_info.invoke = "Create Ammo"; break;
    case PHASING: art_info.invoke = "Phasing"; break;
    case CHANNEL: art_info.invoke = "Channel"; break;
    case DEATH_MAGIC: art_info.invoke = "Death Magic"; break;
    case CONFLICT: art_info.invoke = "Conflict"; break;
    case LEVITATION: art_info.invoke = "Levitation"; break;
    case INVIS: art_info.invoke = "Invisibility"; break;
    case FLYING: art_info.invoke = "Flying"; break;
    case WWALKING: art_info.invoke = "Water Walking"; break;
    case OBJECT_DET: art_info.invoke = "Object Detection"; break;
        /*Invoke for water-walking and an earthquake. */
        
    case SEFFECT: 
        switch (anum) {
        case ART_IMHULLU:
            art_info.invoke = "Scroll of Air"; break;
        case ART_DEEP_FREEZE:
            art_info.invoke = "Scroll of Ice + Freeze Sphere"; break;
        case ART_FIREWALL:
            art_info.invoke = "Flame Sphere"; break;
        }
        break;
    default:
        art_info.invoke = "None"; break;
    }
    
    /* Extra hard-coded info (not possible to automate into the lookup) */ 
    switch (anum) {
    case ART_BALMUNG:
        art_info.xattack = "\t\tShreds the armor of opponents.";
        /*Balmung always resists destruction */
        break;
    case ART_DRAMBORLEG:
        art_info.xattack = "\t\tCan destroy any balrog in one hit.";
        break;
    case ART_DEMONBANE:
        art_info.wielded[16] = "Angers demons princes and lords.";
        art_info.wielded[17] = "Blocks demon gating while wielded. ";
        break;
    case ART_EXCALIBUR:
        art_info.wielded[16] = "Angers demons princes and lords.";
        break;
    case ART_HAND_GRENADE_OF_ANTIOCH: 
        art_info.xattack = "\t\tExplodes dealing 6d50 magical damage.";
        /* Once applied, the timer is always 3 turns - unless it hits a 
monster then the grenade will instantly explode.  */
        break;
    case ART_MASTER_SWORD: 
        art_info.xattack = "\t\t10% of bonus magic attack on each successful hit.\n";
        art_info.xinfo = "If at full health, each hit has a 75% chance of shooting a magic missile for 2d6 damage.";
        break;
    case ART_BRADAMANTE_S_FURY: 
        art_info.xattack = "\t\tStuns monsters.";
        break;
    case ART_CIRCE_S_WITCHSTAFF: 
        art_info.xattack = "\t\t1/20 chance of turning target into a pig.";
        break;
    case ART_CLEAVER: 
        art_info.xattack = "\t\tHits in a wide slashing arc; covers the 2 spots 45 degrees from the direction you attack. ";
        break;
    case ART_GUNGNIR: 
        art_info.xinfo = "Can be created via a successful prayer by a lawful or neutral \n"
                         "pious Valkyrie, while wielding a non-artifact spear-like (spear \n"
                         "skill) weapon that is at least +5. Beatitude and erosion do not \n"
                         "matter. The spear will be turned into the atgeir Gungnir with no \n"
                         "other changes made.";
        break;
    case ART_IMHULLU: 
        art_info.xattack = "\t\t1/3 of the time Imhullu releases a tornado which\n"
                         "\t\tdoes 3d4 bonus damage (Does not affect big monsters.)";
        break;
    case ART_MAGICBANE: 
        art_info.wielded[16] = "Absorbs and negate 95% of of curses directed at its wielder.";
        break;
    case ART_MIRRORBRIGHT: 
        art_info.wielded[16] = "Does not impede spellcasting while worn.";
        break;
    case ART_MJOLLNIR: 
        art_info.xinfo = "Mjollnir can be thrown when wielded if hero has 25 Strength\n"
                         "(usually via gauntlets of power but possible with rings of\n"
                         "gain strength).  If the thrower is a Valkyrie, Mjollnir will\n"
                         "usually (99%) return and then usually (separate 99%) be caught\n"
                         "and automatically be re-wielded.  When returning Mjollnir is\n"
                         "not caught, there is a 50:50 chance of hitting hero for damage\n"
                         "and its lightning shock might destroy some wands and/or rings.";
        break;
    case ART_DOOMBLADE: 
        art_info.xattack = "\t\tOn each hit there is a 25% chance Doomblade deals an extra (1d4 * 5) damage.";
        break;
    case ART_ELFRIST:
        art_info.xattack = "\t\t10% chance of instakill vs elves";
        break;
    case ART_GRIMTOOTH: 
        art_info.xattack = "\t\t10% chance to instakill any elf it hits. \n"
                         "Has a 1⁄6 chance to inflict sickness on any target.";
        break;
    case ART_LUCKLESS_FOLLY: 
        art_info.xinfo = "Acts as a cursed luckstone regardless of its BCU status. It gets \n"
                         "bonuses to-hit and damage based on the opposite of your luck value. \n"
                         "Obtaining luck of -13 will give this weapon an automatic +26\n"
                         "to-hit and damage bonus.";
        break;
    case ART_PLAGUE: 
        art_info.xattack = "\t\tAll arrows show from Plague are auto-poisoned.";
        break;
    case ART_ORCRIST:
        art_info.xattack = "\t\t10% chance of instakill vs orcs";
        break;
    case ART_SECESPITA: 
        art_info.wielded[16] = "Increases sacrifice value by ~50%.";
        /* The energy drain only works if the artifact kills its victim. */
        break;
    case ART_SERPENT_S_TONGUE: 
        art_info.xattack = "\t\tVs non-poison-resistant enemies: "
                         "40% chance of 2 + d6 damage;"
                         "30% chance of 4 + 2d6 damage;"
                         "20% chance of 6 + 3d6 damage;"
                         "10% chance of poison instadeath";
        break;
    case ART_STING:
        art_info.xattack = "\t\t10% chance of instakill vs orcs";
        break;
    case ART_ANGELSLAYER:
        art_info.xattack = "\t\tFire damage applies to all monsters.";
        break;
    case ART_CHAINS_OF_MALCANTHET: 
        art_info.xattack = "\t\tHas a 1/5 chance of paralyzing a monster for 1-4 turns on hit.";
        art_info.wielded[16] = "Boosts charisma to 25 when wielded.";
        break;
    case ART_DROWSING_ROD: 
        art_info.xattack = "\t\tOn hit, there is a 19/20 chance of spraying"
                         "a sleep inducing mist at the enemy.";
        break;
    case ART_GLEIPNIR: 
        art_info.xinfo = "Based on the bindings used to trap Fenrir. This hook can \n"
                         "grapple things regardless of size, and has a massive range \n"
                         "(3x a regular grappling hook.)";
        break;
    case ART_MYSTIC_EYES: 
        art_info.wielded[16] = "While worn, confers Death Vision - which makes your attacks (both";
        art_info.wielded[17] = "physical and magical) deal double damage, but also makes you hallucinate.";
        break;
    case ART_MORTALITY_DIAL: 
        art_info.wielded[16] = "Prevents monsters from regenerating.";
        art_info.wielded[17] = "Prevents trolls and zombies from reviving.";
        break;
    case ART_OGRESMASHER: 
        art_info.wielded[16] = "Wielding Ogresmasher grants 25 constitution";
        break;
    case ART_ORIGIN: 
        art_info.wielded[16] = "Greatly increases spellcasting ability (as a robe)";
        break;
    case ART_SUNSPOT: 
        art_info.xattack = "\t\tHas a 1/3 chance of blinding monsters it hits for 17 turns.";
        break;
    case ART_THUNDERSTRUCK: 
        art_info.xattack = "\t\t25% chance of shooting out a sonic beam on each hit.";
        break;
    case ART_TROLLSBANE: 
        art_info.wielded[16] = "Prevents troll corpses from reviving.";
        break;
    case ART_STAFF_OF_ROT: 
        art_info.xattack = "\t\tDouble damage while you are withering.";
        break;
    case ART_CANDLE_OF_ETERNAL_FLAME: 
        art_info.carried[16] = "Deals 2d10 passive fire damage to attackers";
        break;
    case ART_MITRE_OF_HOLINESS:
        art_info.wielded[16] = "1/2 physical damage from undead and demons (Priests only)";
        art_info.wielded[17] = "allows #pray and turn tech to function in Gehennom when worn.";
        art_info.xinfo = "Doesn't block illithids from casting psionic wave when worn.";
        break;
    case ART_MASTER_KEY_OF_THIEVERY: 
        art_info.xinfo = "additional carry property if the Key is not cursed\n"
                         "(for rogues) or blessed (for non-rogues):  #untrap of doors and\n"
                         "chests will always find any traps; disarming those will always \n"
                         "succeed.";
        break;
    case ART_IRON_SPOON_OF_LIBERATION: 
        art_info.xattack = "\t\t+1d5 to-hit bonus and double damage vs all monsters. ";
        break;
    case ART_TREASURY_OF_PROTEUS:
        art_info.xinfo = "Occasionally polymorphs items that have been placed inside.";
        art_info.carried[17] = "Protects items in your inventory from becoming cursed.";
        break;
    default:
        art_info.xinfo = "";
    } 
        
    return art_info;
}



const char *
adtyp_str(adtyp, defend)
int adtyp;
boolean defend;
{
    switch (adtyp) {
    case AD_FIRE:
        return "fire";
    case AD_COLD:
        return "cold";
    case AD_ELEC:
        return "shock";
    case AD_DRST:
        return "poison";
    case AD_MAGM:
        return defend ? "magic" : "magic missile";
    case AD_DRLI:
        return "drain life";
    case AD_CLOB:
        return defend ? "clobber" : "extreme stability";
    case AD_DISE:
        return "disease";
    case AD_SLEE:
        return "sleep";
    case AD_DISN:
        return "disintegration";
    case AD_SLOW:
        return "slow";
    case AD_ACID:
        return "acid";
    case AD_STON:
        return "petrification";
    case AD_PLYS:
        return defend ? "paralyze" : "free action";
    case AD_LOUD:
        return "sonic";
    case AD_PHYS:
        return "physical";
    case AD_WERE:
        return "lycanthropy";
    case AD_BLND:
        return "blinding";
    case AD_STUN:
        return "stuns/magic";
    case AD_DREN:
        return "drain energy";
    case AD_WTHR:
        return "withering";
    case AD_WIND:
        return "wind";
    default:
        impossible("Bad AD_TYPE!");
    }
    return "";
}

int
arti_align(oartifact)
int oartifact;
{
    return artilist[oartifact].alignment;
}

boolean
non_wishable_artifact(otmp)
struct obj *otmp;
{
    int art = otmp->oartifact;
    if (!art)
        return FALSE;
    return ((artilist[art].spfx & SPFX_NOWISH) != 0L);
}

static const char *random_seasound[] = {
    "distant waves",
    "distant surf",
    "the distant sea",
    "the call of the ocean",
    "waves against the shore",
    "flowing water",
    "the sighing of waves",
    "quarrelling gulls",
    "the song of the deep",
    "rumbling in the deeps",
    "the singing of Eidothea",
    "the laughter of the protean nymphs",
    "rushing tides",
    "the elusive sea change",
    "the silence of the briney deep",
    "the passage of the albatross",
    "dancing raindrops",
    "coins rolling on the seabed",
    "treasure galleons crumbling in the depths",
    "waves lapping against a hull"
};

/* Polymorph obj contents */
void
arti_poly_contents(struct obj *obj)
{
    struct obj *dobj = 0;  /* object to be deleted */
    struct obj *otmp;
    You_hear("%s.", random_seasound[rn2(SIZE(random_seasound))]);
    
    for (otmp = obj->cobj; otmp; otmp = otmp->nobj) {
        if (!otmp->unpaid)
            otmp->no_charge = 1;
        if (dobj) {
            delobj(dobj);
            dobj = 0;
        }
        if (!obj_resists(otmp, 5, 95)) {
            /* KMH, conduct */
            u.uconduct.polypiles++;
            /* any saved lock context will be dangerously obsolete */
            if (Is_box(otmp)) 
                (void) boxlock(otmp, obj);

            if (obj_shudders(otmp)) {
                dobj = otmp;
            }
            else 
                otmp = poly_obj(otmp, STRANGE_OBJECT);
        }
    }
    if (dobj) {
        delobj(dobj);
        dobj = 0;
    }
}

/* used so that callers don't need to known about SPFX_ codes */
boolean
arti_digs(obj)
struct obj *obj;
{
    return (obj && obj->oartifact && spec_ability(obj, SPFX_DIG));
}


STATIC_OVL boolean
can_we_zap(dx, dy, maxdist)
int dx, dy, maxdist;
{
    struct monst *targ = 0;
    int curx = u.ux, cury = u.uy;
    int dist = 0;

    /* Walk outwards */
    for ( ; dist < maxdist; ++dist) {
        curx += dx;
        cury += dy;
        if (!isok(curx, cury))
            break;

        /*if (curx == mtmp->mux && cury == mtmp->muy)
            return &youmonst;*/

        if ((targ = m_at(curx, cury)) != 0) {
            /* Is the monster visible? */
            if ((targ->minvis && !See_invisible) || targ->mundetected) {
                continue;
            }
            if (targ->mtame || targ->mpeaceful) {
                return FALSE;
            }
        }
    }
    return TRUE;
}
/*artifact.c*/
