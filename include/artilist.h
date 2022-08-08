/* NetHack 3.6  artilist.h      $NHDT-Date: 1564351548 2019/07/28 22:05:48 $  $NHDT-Branch: NetHack-3.6 $:$NHDT-Revision: 1.20 $ */
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/*-Copyright (c) Robert Patrick Rankin, 2017. */
/* NetHack may be freely redistributed.  See license for details. */

#ifdef MAKEDEFS_C
/* in makedefs.c, all we care about is the list of names */

#define A(nam, typ, s1, s2, mt, atk, dfn, cry, inv, al, cl, rac, cost, clr) nam

static const char *artifact_names[] = {
#else
/* in artifact.c, set up the actual artifact list structure */

#define A(nam, typ, s1, s2, mt, atk, dfn, cry, inv, al, cl, rac, cost, clr) \
    {                                                                       \
        typ, nam, s1, s2, mt, atk, dfn, cry, inv, al, cl, rac, cost, clr    \
    }

/* clang-format off */
#define     NO_ATTK     {0,0,0,0}               /* no attack */
#define     NO_DFNS     {0,0,0,0}               /* no defense */
#define     NO_CARY     {0,0,0,0}               /* no carry effects */
#define     DFNS(c)     {0,c,0,0}
#define     CARY(c)     {0,c,0,0}
#define     PHYS(a,b)   {0,AD_PHYS,a,b}         /* physical */
#define     DRLI(a,b)   {0,AD_DRLI,a,b}         /* life drain */
#define     COLD(a,b)   {0,AD_COLD,a,b}
#define     FIRE(a,b)   {0,AD_FIRE,a,b}
#define     ELEC(a,b)   {0,AD_ELEC,a,b}         /* electrical shock */
#define     STUN(a,b)   {0,AD_STUN,a,b}         /* magical attack */
#define     DRST(a,b)   {0,AD_DRST,a,b}         /* poison attack */
#define     ACID(a,b)   {0,AD_ACID,a,b}         /* acid attack */
#define     DISE(a,b)   {0,AD_DISE,a,b}         /* disease attack */
#define     DREN(a,b)   {0,AD_DREN,a,b}         /* drains energy */
#define     STON(a,b)   {0,AD_STON,a,b}         /* petrification */
#define     DETH(a,b)   {0,AD_DETH,a,b}         /* special death attack */
/* clang-format on */

STATIC_OVL NEARDATA struct artifact artilist[] = {
#endif /* MAKEDEFS_C */

    /* Artifact cost rationale:
     * 1.  The more useful the artifact, the better its cost.
     * 2.  Quest artifacts are highly valued.
     * 3.  Chaotic artifacts are inflated due to scarcity (and balance).
     */

    /*  dummy element #0, so that all interesting indices are non-zero */
    A("", STRANGE_OBJECT, 0, 0, 0, NO_ATTK, NO_DFNS, NO_CARY, 0, A_NONE,
      NON_PM, NON_PM, 0L, NO_COLOR),

        /*** Lawful artifacts ***/

    /* From SporkHack. Now a silver mace with an extra property.
       First sacrifice gift for a priest. */
    A("Demonbane", HEAVY_MACE, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH), 0, MH_DEMON,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_LAWFUL, PM_PRIEST, NON_PM, 3000L,
      NO_COLOR),

    /* From LotR. Provides magic resistance and one level of MC when wielded.
     * Can destroy any balrog in one hit. Warns of any demons nearby.*/
    A("Dramborleg", DWARVISH_BEARDED_AXE,
      (SPFX_RESTR | SPFX_INTEL | SPFX_PROTECT | SPFX_WARN | SPFX_DFLAGH), 0,
      MH_DEMON, PHYS(8, 8), DFNS(AD_MAGM), NO_CARY, 0, A_LAWFUL, NON_PM, PM_DWARF,
      9000L, CLR_RED),

    A("Excalibur", LONG_SWORD, (SPFX_NOGEN | SPFX_RESTR | SPFX_SEEK
                                | SPFX_DEFN | SPFX_INTEL | SPFX_SEARCH),
      0, 0, PHYS(5, 10), DFNS(AD_DRLI), NO_CARY, 0, A_LAWFUL, PM_KNIGHT, NON_PM,
      4000L, NO_COLOR),

    A("Firewall", ATHAME, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      FIRE(4,4), DFNS(AD_FIRE), NO_CARY, 0, A_LAWFUL, PM_FLAME_MAGE, NON_PM, 400L, CLR_RED),

    A("Grayswandir", SABER, (SPFX_RESTR | SPFX_HALRES), 0, 0,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_LAWFUL, NON_PM, NON_PM, 8000L,
      NO_COLOR),

    A("Reaper", HALBERD, SPFX_RESTR, 0, 0,
      PHYS(5,20), NO_DFNS, NO_CARY, 0, A_LAWFUL, NON_PM, NON_PM, 1000L, NO_COLOR ),

    A("Skullcrusher", CLUB, SPFX_RESTR, 0, 0,
      PHYS(3, 10), NO_DFNS, NO_CARY, 0, A_LAWFUL, PM_CAVEMAN, NON_PM, 300L, NO_COLOR),

    A("Snickersnee", KATANA, SPFX_RESTR, 0, 0, PHYS(5, 8), NO_DFNS, NO_CARY,
      0, A_LAWFUL, PM_SAMURAI, NON_PM, 1200L, NO_COLOR),

    A("Spear of Light", SPEAR,
      (SPFX_RESTR | SPFX_INTEL | SPFX_DFLAGH), 0, MH_UNDEAD,
      PHYS(5,10), NO_DFNS, NO_CARY, LIGHT_AREA, A_LAWFUL, NON_PM, NON_PM, 4000L, NO_COLOR),

    /* Silver: Warns of undead. */
    A("Sunsword", LONG_SWORD, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH), 0, MH_UNDEAD,
      PHYS(5, 0), DFNS(AD_BLND), NO_CARY, 0, A_LAWFUL, NON_PM, NON_PM, 2500L,
      NO_COLOR),

    A("Sword of Justice", LONG_SWORD, (SPFX_RESTR | SPFX_DALIGN), 0, 0,
      PHYS(5, 12), NO_DFNS, NO_CARY, 0, A_LAWFUL, PM_YEOMAN, NON_PM, 1500L, NO_COLOR),

    A("Quick Blade", ELVEN_SHORT_SWORD,
      SPFX_RESTR, 0, 0, PHYS(9,2), NO_DFNS, NO_CARY, 0, A_LAWFUL, NON_PM, NON_PM, 1000L, NO_COLOR),


        /*** Neutral artifacts ***/

    A("Cleaver", BATTLE_AXE, SPFX_RESTR, 0, 0, PHYS(3, 6), NO_DFNS, NO_CARY,
      0, A_NEUTRAL, PM_BARBARIAN, NON_PM, 1500L, NO_COLOR),

    A("Deluder", CLOAK_OF_DISPLACEMENT, (SPFX_RESTR | SPFX_STLTH | SPFX_LUCK), 0, 0,
      NO_ATTK, NO_DFNS, NO_CARY, 0, A_NEUTRAL, PM_WIZARD, NON_PM, 5000L, NO_COLOR),

    A("Disrupter", MACE, (SPFX_RESTR | MH_UNDEAD), 0, MH_UNDEAD,
      PHYS(5,30), NO_DFNS, NO_CARY, 0, A_NEUTRAL, PM_PRIEST, NON_PM, 500L, NO_COLOR),

    A("Gauntlets of Defense", GAUNTLETS_OF_DEXTERITY, SPFX_RESTR, SPFX_HPHDAM, 0,
      NO_ATTK, NO_DFNS, NO_CARY, INVIS, A_NEUTRAL, PM_MONK, NON_PM, 5000L, NO_COLOR),

    A("Giantslayer", LONG_SWORD, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH), 0, MH_GIANT,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_NEUTRAL, NON_PM, NON_PM, 500L,
      NO_COLOR),

    /* From SporkHack - a Hawaiian war club. */
    A("Keolewa", CLUB, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      ELEC(5, 8), DFNS(AD_ELEC), NO_CARY, 0, A_NEUTRAL, PM_CAVEMAN, NON_PM,
      2000L, NO_COLOR),

    /* Evilhack change: Magic fanfare unbalances victims in addition
     * to doing some damage. */
    A("Magicbane", QUARTERSTAFF, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      STUN(3, 4), DFNS(AD_MAGM), NO_CARY, 0, A_NEUTRAL, PM_WIZARD, NON_PM,
      3500L, NO_COLOR),

    A("Mirrorbright", SHIELD_OF_REFLECTION, (SPFX_RESTR | SPFX_HALRES | SPFX_REFLECT), 0, 0,
      NO_ATTK, NO_DFNS, NO_CARY, 0, A_NEUTRAL, PM_HEALER, NON_PM, 5000L, NO_COLOR),

    /*
     *      Mjollnir can be thrown when wielded if hero has 25 Strength
     *      (usually via gauntlets of power but possible with rings of
     *      gain strength).  If the thrower is a Valkyrie, Mjollnir will
     *      usually (99%) return and then usually (separate 99%) be caught
     *      and automatically be re-wielded.  When returning Mjollnir is
     *      not caught, there is a 50:50 chance of hitting hero for damage
     *      and its lightning shock might destroy some wands and/or rings.
     *
     *      Monsters don't throw Mjollnir regardless of strength (not even
     *      fake-player valkyries).
     */
    A("Mjollnir", HEAVY_WAR_HAMMER, /* Mjo:llnir */
      (SPFX_RESTR | SPFX_ATTK), 0, 0, ELEC(5, 24), DFNS(AD_ELEC), NO_CARY, 0,
      A_NEUTRAL, PM_VALKYRIE, NON_PM, 5000L, NO_COLOR),

    A("Sword of Balance", SHORT_SWORD, (SPFX_RESTR | SPFX_DALIGN), 0, 0,
      PHYS(2, 5), NO_DFNS, NO_CARY, 0, A_NEUTRAL, NON_PM, NON_PM, 5000L, NO_COLOR),

    A("Vorpal Blade", LONG_SWORD, (SPFX_RESTR | SPFX_BEHEAD | SPFX_WARN | SPFX_DFLAGH),
      0, MH_JABBERWOCK, PHYS(5, 6), NO_DFNS, NO_CARY, 0, A_NEUTRAL, NON_PM, NON_PM, 5000L,
      NO_COLOR),

    A("Whisperfeet", SPEED_BOOTS, (SPFX_RESTR | SPFX_STLTH | SPFX_LUCK), 0, 0,
      NO_ATTK, NO_DFNS, NO_CARY, 0, A_NEUTRAL, PM_TOURIST, NON_PM, 5000L, NO_COLOR),


        /*** Chaotic artifacts ***/

    A("Bat from Hell", BASEBALL_BAT,
      (SPFX_RESTR), 0, 0,
      PHYS(3, 20), NO_DFNS, NO_CARY, 0, A_CHAOTIC, PM_ROGUE, NON_PM, 5000L, CLR_RED),

    /* Yeenoghu's infamous triple-headed flail. A massive weapon reputed to have been created
     * from the thighbone and torn flesh of an ancient god he slew. An extremely lethal artifact */
    A("Butcher", TRIPLE_HEADED_FLAIL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_INTEL),
      SPFX_EXCLUDE, 0, STUN(5, 8), NO_DFNS, NO_CARY, 0, A_CHAOTIC,
      NON_PM, NON_PM, 4000L, NO_COLOR),

    A("Deathsword", TWO_HANDED_SWORD,
      (SPFX_RESTR | SPFX_DFLAGH), 0, MH_HUMAN,
      PHYS(5,14), NO_DFNS, NO_CARY, 0, A_CHAOTIC, PM_BARBARIAN, NON_PM, 5000L, NO_COLOR),

    A("Deep Freeze", ATHAME, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      COLD(5,5), DFNS(AD_COLD), NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM, 400L, CLR_BLUE),

    /* From SporkHack, but with a twist. This is the anti-Excalibur. */
    A("Dirge", LONG_SWORD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_DEFN | SPFX_INTEL), 0, 0,
      ACID(5, 10), DFNS(AD_ACID), NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM,
      4000L, NO_COLOR),

    A("Doomblade", ORCISH_SHORT_SWORD, SPFX_RESTR, 0, 0, PHYS(0, 10), NO_DFNS, NO_CARY,
      0, A_CHAOTIC, NON_PM, NON_PM, 1000L, NO_COLOR),

    A("Elfrist", ORCISH_SPEAR, (SPFX_WARN | SPFX_DFLAGH), 0, MH_ELF,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_CHAOTIC, NON_PM, PM_ORC, 300L, NO_COLOR),

  /* Warns when elves are present, but its damage bonus applies to all targets.
   *      (handled as special case in spec_dbon()). */
    A("Grimtooth", ORCISH_DAGGER, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH),
      0, MH_ELF, DISE(5, 6), NO_DFNS,
      NO_CARY, 0, A_CHAOTIC, NON_PM, PM_ORC, 1500L, CLR_RED),

   /* from SporkHack - many of the same properties as Stormbringer
    *      Meant to be wielded by Vlad. */
    A("Lifestealer", TWO_HANDED_SWORD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_DEFN | SPFX_INTEL | SPFX_DRLI),
      SPFX_EXCLUDE, 0, DRLI(5, 2), DFNS(AD_DRLI), NO_CARY, 0, A_CHAOTIC, NON_PM,
      NON_PM, 10000L, NO_COLOR),

    /* Convict first artifact weapon. Acts like a luckstone. */
    A("Luck Blade", BROADSWORD, (SPFX_RESTR | SPFX_LUCK), 0, 0,
      PHYS(5, 6), NO_DFNS, NO_CARY, 0, A_CHAOTIC, PM_CONVICT, NON_PM, 3000L,
      NO_COLOR),

    A("Plague", DARK_ELVEN_BOW, (SPFX_RESTR | SPFX_DEFN), 0, 0,
      PHYS(5,7), DFNS(AD_DRST), NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM, 6000L, NO_COLOR),
        /* Auto-poison code in dothrow.c */

    /*
     *      Orcrist and Sting have same alignment as elves.
     *
     *      The combination of SPFX_WARN+SPFX_DFLAGH+MH_value will trigger
     *      EWarn_of_mon for all monsters that have the MH_value flag.
     *      Sting and Orcrist will warn of MH_ORC monsters.
     */
    A("Orcrist", ELVEN_BROADSWORD, (SPFX_WARN | SPFX_DFLAGH), 0, MH_ORC,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_CHAOTIC, NON_PM, PM_ELF, 2000L,
      CLR_BRIGHT_BLUE), /* bright blue is actually light blue */

      /* The energy drain only works if the artifact kills its victim.
       * Also increases sacrifice value while wielded. */
    A("Secespita", KNIFE, (SPFX_RESTR | SPFX_ATTK), 0, 0,
      DREN(5, 6), NO_DFNS, NO_CARY, 0, A_CHAOTIC, PM_INFIDEL, NON_PM,
      1000L, NO_COLOR),

    A("Serpent's Tongue", DAGGER, SPFX_RESTR, 0, 0,
      PHYS(2, 0), NO_DFNS, NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM, 400L, NO_COLOR),
        /* See artifact.c for special poison damage */

    A("Sting", ELVEN_DAGGER, (SPFX_WARN | SPFX_DFLAGH), 0, MH_ORC, PHYS(5, 0),
      NO_DFNS, NO_CARY, 0, A_CHAOTIC, NON_PM, PM_ELF, 800L, CLR_BRIGHT_BLUE),

    /* Stormbringer only has a 2 because it can drain a level, providing 8 more. */
    A("Stormbringer", RUNESWORD,
      (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN | SPFX_INTEL | SPFX_DRLI), 0, 0,
      DRLI(5, 2), DFNS(AD_DRLI), NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM, 8000L,
      NO_COLOR),

  /*
   * The Sword of Kas - the sword forged by Vecna and given to his top
   * lieutenant, Kas. */
    A("The Sword of Kas", TWO_HANDED_SWORD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_DEFN | SPFX_INTEL
       | SPFX_DALIGN), SPFX_EXCLUDE, 0,
      DRST(10, 0), DFNS(AD_STON), NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM,
      15000L, NO_COLOR),

 A("Thiefbane", TWO_HANDED_SWORD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_BEHEAD | SPFX_DCLAS | SPFX_DRLI),
      0, S_HUMAN, DRLI(5, 1), SPDF_NONE, NO_CARY, 0, A_CHAOTIC, NON_PM, NON_PM,
      1500L, NO_COLOR),
  /* Orcus' true 'Wand of Death', a truly terrifying weapon that can kill
   * those it strikes with one blow. In the form of an ornate mace/rod, the Wand
   * of Orcus is 'a rod of obsidian topped by a skull. This instrument causes
   * death (or annihilation) to any creature, save those of like status
   * merely by touching their flesh'. Can only be wielded by Orcus or others
   * of his ilk */
    A("Wand of Orcus", ROD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_INTEL),
      SPFX_EXCLUDE, 0, DETH(5, 6), NO_DFNS, NO_CARY, 0, A_CHAOTIC,
      NON_PM, NON_PM, 10000L, CLR_BLACK),


      /*** Unaligned artifacts ***/

    /* The quasi-evil twin of Demonbane, Angelslayer is an unholy trident
     * geared towards the destruction of all angelic beings */
    A("Angelslayer", TRIDENT,
      (SPFX_RESTR | SPFX_ATTK | SPFX_SEARCH | SPFX_HSPDAM | SPFX_WARN | SPFX_DFLAGH),
      0, MH_ANGEL, FIRE(5, 10), NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM,
      5000L, NO_COLOR),

    /* Bag of the Hesperides - this is the magical bag obtained by Perseus
   * from the Hesperides (nymphs) to contain and transport Medusa's head.
   * The bag naturally repels water, and it has greater weight reduction
   * than a regular bag of holding. Found at the end of the Ice Queen branch
   * with the captive pegasus.
   */
    A("Bag of the Hesperides", BAG_OF_HOLDING,
      (SPFX_NOGEN | SPFX_RESTR), (SPFX_EXCLUDE | SPFX_PROTECT), 0,
      NO_ATTK, NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM,
      8000L, NO_COLOR),

    /* Dragonbane is no longer a weapon, but a pair of magical gloves made from
     * the scales of a long dead ancient dragon. The gloves afford much of the
     * same special abilities as the weapon did, but swaps fire resistance for
     * resistance. Other dragons will be able to sense the power of these gloves
     * and will be affected accordingly.
     */
    A("Dragonbane", GLOVES,
      (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH | SPFX_REFLECT), 0, MH_DRAGON,
      NO_ATTK, DFNS(AD_ACID), NO_CARY, 0, A_NONE, NON_PM, NON_PM, 3000L,
      NO_COLOR),

    A("Fire Brand", SHORT_SWORD, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      FIRE(5, 0), DFNS(AD_FIRE), NO_CARY, 0, A_NONE, NON_PM, NON_PM, 3000L,
      NO_COLOR),

    A("Frost Brand", SHORT_SWORD, (SPFX_RESTR | SPFX_ATTK | SPFX_DEFN), 0, 0,
      COLD(5, 0), DFNS(AD_COLD), NO_CARY, 0, A_NONE, NON_PM, NON_PM, 3000L,
      NO_COLOR),

  /* Thought the Oracle just knew everything on her own? Guess again. Should
   * anyone ever be foolhardy enough to take on the Oracle and succeed,
   * they might discover the true source of her knowledge.
   */
    A("Magic 8-Ball", EIGHT_BALL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_SPEAK), (SPFX_WARN | SPFX_EXCLUDE),
      0, NO_ATTK, NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM, 5000L,
      NO_COLOR),

    A("Ogresmasher", HEAVY_WAR_HAMMER, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH), 0, MH_OGRE,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM, 500L,
      NO_COLOR),

    /* The Eye of Vecna, which Vecna will sometimes death drop
       before the rest of his body crumbles to dust */
    A("The Eye of Vecna", EYEBALL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL),
      (SPFX_XRAY | SPFX_ESP | SPFX_HSPDAM | SPFX_EXCLUDE),
      0, NO_ATTK, NO_DFNS, CARY(AD_COLD), DEATH_MAGIC, A_NONE,
      NON_PM, NON_PM, 50000L, NO_COLOR),

    /* The Hand of Vecna, another possible artifact that Vecna
       might drop once destroyed */
    A("The Hand of Vecna", MUMMIFIED_HAND,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_REGEN | SPFX_HPHDAM),
      SPFX_EXCLUDE, 0, NO_ATTK, DFNS(AD_DISE), NO_CARY, DEATH_MAGIC, A_NONE,
      NON_PM, NON_PM, 50000L, CLR_BLACK),

    A("Trollsbane", MORNING_STAR, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH | SPFX_REGEN), 0, MH_TROLL,
      PHYS(5, 0), NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM, 800L,
      NO_COLOR),

    A("Werebane", SABER, (SPFX_RESTR | SPFX_WARN | SPFX_DFLAGH), 0, MH_WERE,
      PHYS(5, 0), DFNS(AD_WERE), NO_CARY, 0, A_NONE, NON_PM, NON_PM, 1500L,
      NO_COLOR),


    /*
     *      The artifacts for the quest dungeon, all self-willed.
     */

    /*
     *      At first I was going to add the artifact shield that was made for the
     *      Archeologist quest in UnNetHack, but then decided to do something unique.
     *      Behold Xiuhcoatl, a dark wooden spear-thrower that does fire damage much like
     *      Mjollnir does electrical damage. From Aztec lore, Xiuhcoatl is an atlatl,
     *      which is an ancient device that was used to throw spears with great force
     *      and even greater distances. Xiuhcoatl will return to the throwers hand much like
     *      Mjollnir, but requires high dexterity instead of strength to handle properly.
     */
    A("Xiuhcoatl", ATLATL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_ATTK | SPFX_DEFN), SPFX_ESP, 0,
      FIRE(5, 24), DFNS(AD_FIRE), NO_CARY, LEVITATION, A_LAWFUL, PM_ARCHEOLOGIST,
      NON_PM, 3500L, NO_COLOR),

#if 0 /* Replaced by Xiuhcoatl */
    A("The Orb of Detection", CRYSTAL_BALL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL), (SPFX_ESP | SPFX_HSPDAM), 0,
      NO_ATTK, NO_DFNS, CARY(AD_MAGM), INVIS, A_LAWFUL, PM_ARCHEOLOGIST,
      NON_PM, 2500L, NO_COLOR),
#endif

#if 0 /* Replaced by The Ring of P'hul */
    A("The Heart of Ahriman", LUCKSTONE,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL), SPFX_STLTH, 0,
      /* this stone does double damage if used as a projectile weapon */
      PHYS(5, 0), NO_DFNS, NO_CARY, LEVITATION, A_NEUTRAL, PM_BARBARIAN,
      NON_PM, 2500L, NO_COLOR),
#endif

    A("The Sceptre of Might", ROD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_DALIGN), 0, 0, PHYS(5, 0),
      DFNS(AD_MAGM), NO_CARY, CONFLICT, A_LAWFUL, PM_CAVEMAN, NON_PM, 2500L,
      NO_COLOR),

#if 0 /* OBSOLETE */
    A("The Palantir of Westernesse", CRYSTAL_BALL,
      (SPFX_NOGEN|SPFX_RESTR|SPFX_INTEL),
      (SPFX_ESP|SPFX_REGEN|SPFX_HSPDAM), 0,
      NO_ATTK, NO_DFNS, NO_CARY,
      TAMING, A_CHAOTIC, NON_PM , PM_ELF, 8000L, NO_COLOR ),
#endif

    A("The Staff of Aesculapius", QUARTERSTAFF,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_ATTK | SPFX_INTEL | SPFX_DRLI
       | SPFX_REGEN),
      0, 0, DRLI(5, 0), DFNS(AD_DRLI), NO_CARY, HEALING, A_NEUTRAL, PM_HEALER,
      NON_PM, 5000L, NO_COLOR),

    A("The Magic Mirror of Merlin", MIRROR,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_SPEAK | SPFX_REFLECT),
      (SPFX_REFLECT | SPFX_ESP | SPFX_HSPDAM), 0,
      NO_ATTK, NO_DFNS, NO_CARY, 0, A_LAWFUL, PM_KNIGHT, NON_PM, 1500L,
      NO_COLOR),

    A("The Eyes of the Overworld", LENSES,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_XRAY), 0, 0, NO_ATTK,
      DFNS(AD_MAGM), NO_CARY, ENLIGHTENING, A_NEUTRAL, PM_MONK, NON_PM,
      2500L, NO_COLOR),

    A("The Mitre of Holiness", HELM_OF_BRILLIANCE,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_DFLAGH | SPFX_INTEL | SPFX_PROTECT), 0,
      MH_UNDEAD, NO_ATTK, NO_DFNS, CARY(AD_FIRE), ENERGY_BOOST, A_LAWFUL,
      PM_PRIEST, NON_PM, 2000L, NO_COLOR),

    /* If playing a gnomish ranger, the player receives the 'Crossbow of Carl',
       otherwise rangers will receive the Longbow of Diana. Exact same properties
       between the two artifacts */
    A("The Longbow of Diana", BOW,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_REFLECT), SPFX_ESP, 0,
      PHYS(5, 6), NO_DFNS, NO_CARY, CREATE_AMMO, A_CHAOTIC, PM_RANGER, NON_PM,
      4000L, NO_COLOR),

    A("The Crossbow of Carl", CROSSBOW,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_REFLECT), SPFX_ESP, 0,
      PHYS(5, 6), NO_DFNS, NO_CARY, CREATE_AMMO, A_CHAOTIC, PM_RANGER, NON_PM,
      4000L, NO_COLOR),

    /* MKoT has an additional carry property if the Key is not cursed (for
       rogues) or blessed (for non-rogues):  #untrap of doors and chests
       will always find any traps and disarming those will always succeed */
    A("The Master Key of Thievery", SKELETON_KEY,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_SPEAK),
      (SPFX_WARN | SPFX_TCTRL | SPFX_HPHDAM), 0, NO_ATTK, NO_DFNS, NO_CARY,
      UNTRAP, A_CHAOTIC, PM_ROGUE, NON_PM, 3500L, NO_COLOR),

    A("The Tsurugi of Muramasa", TSURUGI,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_BEHEAD | SPFX_LUCK
       | SPFX_PROTECT | SPFX_HPHDAM), 0, 0,
      PHYS(3, 8), NO_DFNS, NO_CARY, 0, A_LAWFUL, PM_SAMURAI, NON_PM,
      6000L, NO_COLOR),

    A("The Platinum Yendorian Express Card", CREDIT_CARD,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_DEFN),
      (SPFX_ESP | SPFX_HSPDAM), 0, NO_ATTK, NO_DFNS, CARY(AD_MAGM),
      CHARGE_OBJ, A_NEUTRAL, PM_TOURIST, NON_PM, 7000L, NO_COLOR),

    /* KMH -- More effective against normal monsters
    * Was +10 to-hit, +d20 damage only versus vampires
    */
    A("The Stake of Van Helsing", WOODEN_STAKE,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL), 0, 0,
      PHYS(5,12),    NO_DFNS,        CARY(AD_MAGM),
      0, A_LAWFUL, NON_PM, NON_PM, 5000L, NO_COLOR),

#if 0 /* Replaced by Gjallar */
    A("The Orb of Fate", CRYSTAL_BALL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_LUCK),
      (SPFX_WARN | SPFX_HPHDAM), 0, NO_ATTK, NO_DFNS, NO_CARY,
      LEV_TELE, A_NEUTRAL, PM_VALKYRIE, NON_PM, 3500L, NO_COLOR),
#endif

    A("Gjallar", TOOLED_HORN,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_LUCK),
      (SPFX_WARN | SPFX_HPHDAM), 0, NO_ATTK, NO_DFNS, NO_CARY,
      LEV_TELE, A_NEUTRAL, PM_VALKYRIE, NON_PM, 5000L, NO_COLOR),

    A("The Eye of the Aethiopica", AMULET_OF_ESP,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL), (SPFX_EREGEN | SPFX_HSPDAM), 0,
      NO_ATTK, DFNS(AD_MAGM), NO_CARY, CREATE_PORTAL, A_NEUTRAL, PM_WIZARD,
      NON_PM, 4000L, NO_COLOR),
    /*
     *      Based loosely off of the Ring of P'hul - from 'The Lords of Dus' series
     *      by Lawrence Watt-Evans. This is another one of those artifacts that would
     *      just be ridiculous if its full power were realized in-game. In the series,
     *      it deals out death and disease. Here it will protect the wearer from a
     *      good portion of that. Making this the quest artifact for the Barbarian role.
     *      This artifact also corrects an oversight from vanilla, that no chaotic-based
     *      artiafcts conferred magic resistance, a problem that was compounded if our
     *      hero is in a form that can't wear body armor or cloaks. So, we make the
     *      Barbarian artifact chaotic (why it was neutral before is a bit confusing
     *      to me as most vanilla Barbarian race/role combinations are chaotic).
     */
    A("The Ring of P\'hul", RIN_FREE_ACTION,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL | SPFX_DEFN), 0, 0,
      NO_ATTK, DFNS(AD_MAGM), CARY(AD_DISE), 0, A_CHAOTIC, PM_BARBARIAN,
      NON_PM, 5000L, NO_COLOR),

    /* Convict role quest artifact. Provides magic resistance when carried,
     * invoke to phase through walls like a xorn.
     */
    A("The Iron Ball of Liberation", HEAVY_IRON_BALL,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL),
      (SPFX_STLTH | SPFX_SEARCH | SPFX_WARN), 0,
      NO_ATTK, NO_DFNS, CARY(AD_MAGM), PHASING,
      A_CHAOTIC, PM_CONVICT, NON_PM, 10000L, NO_COLOR),

    /* Infidel role quest artifact. Confers energy regeneration,
     * but only to those in good standing with Moloch. */
    A("The Idol of Moloch", FIGURINE,
      (SPFX_NOGEN | SPFX_RESTR | SPFX_INTEL), SPFX_HSPDAM, 0,
      NO_ATTK, NO_DFNS, CARY(AD_MAGM), CHANNEL, A_CHAOTIC, PM_INFIDEL, NON_PM,
      4000L, NO_COLOR),

    /*
     *  terminator; otyp must be zero
     */
    A(0, 0, 0, 0, 0, NO_ATTK, NO_DFNS, NO_CARY, 0, A_NONE, NON_PM, NON_PM, 0L,
      0) /* 0 is CLR_BLACK rather than NO_COLOR but it doesn't matter here */

}; /* artilist[] (or artifact_names[]) */

#undef A

#ifndef MAKEDEFS_C
#undef NO_ATTK
#undef NO_DFNS
#undef DFNS
#undef PHYS
#undef DRLI
#undef COLD
#undef FIRE
#undef ELEC
#undef STUN
#undef DRST
#undef ACID
#undef DISE
#undef DREN
#undef STON
#undef DETH
#endif

/*artilist.h*/
