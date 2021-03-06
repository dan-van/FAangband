/*
 * File: target.c
 * Purpose: Targetting code
 *
 * Copyright (c) 1997-2007 Angband contributors
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "game-cmd.h"
#include "monster.h"
#include "squelch.h"
#include "trap.h"

/*
 * Height of the help screen; any higher than 4 will overlap the health
 * bar which we want to keep in targeting mode.
 */
#define HELP_HEIGHT 3

/*** File-wide variables ***/

/* Is the target set? */
bool target_set;

/* Current monster being tracked, or 0 */
u16b target_who;

/* Current object being tracked, or 0 */
u16b target_what;

/* Target location */
static s16b target_x, target_y;

#define TS_INITIAL_SIZE	20

/*** Functions ***/

/**
 * Monster health description
 */
static void look_mon_desc(char *buf, size_t max, int m_idx)
{
    monster_type *m_ptr = &m_list[m_idx];
    monster_race *r_ptr = &r_info[m_ptr->r_idx];

    bool living = TRUE;


    /* Determine if the monster is "living" (vs "undead") */
    if (monster_is_unusual(r_ptr))
	living = FALSE;


    /* Healthy monsters */
    if (m_ptr->hp >= m_ptr->maxhp) {
	/* No damage */
	my_strcpy(buf, (living ? "unhurt" : "undamaged"), max);
    } else {
	/* Calculate a health "percentage" */
	int perc = 100L * m_ptr->hp / m_ptr->maxhp;

	if (perc >= 60)
	    my_strcpy(buf, (living ? "somewhat wounded" : "somewhat damaged"),
		      max);
	else if (perc >= 25)
	    my_strcpy(buf, (living ? "wounded" : "damaged"), max);
	else if (perc >= 10)
	    my_strcpy(buf, (living ? "badly wounded" : "badly damaged"), max);
	else
	    my_strcpy(buf, (living ? "almost dead" : "almost destroyed"), max);
    }

    if (m_ptr->csleep)
	my_strcat(buf, ", asleep", max);
    if (m_ptr->confused)
	my_strcat(buf, ", confused", max);
    if (m_ptr->monfear)
	my_strcat(buf, ", afraid", max);
    if (m_ptr->stunned)
	my_strcat(buf, ", stunned", max);

    /* Hostility */
    if (rf_has(r_ptr->flags, RF_RACIAL)) {
	/* Hostile monsters */
	if (m_ptr->hostile < 0) {
	    my_strcat(buf, ",hostile", max);
	}

	/* Not hostile to the player */
	else {
	    my_strcat(buf, ",neutral", max);
	}
    }
}


/**
 * Determine if a monster makes a reasonable target
 *
 * The concept of "targetting" was stolen from "Morgul" (?)
 *
 * The player can target any location, or any "target-able" monster.
 *
 * Currently, a monster is "target_able" if it is visible, and if
 * the player can hit it with a projection, and the player is not
 * hallucinating.  This allows use of "use closest target" macros.
 *
 * Future versions may restrict the ability to target "trappers"
 * and "mimics", but the semantics is a little bit weird.
 */
bool target_able(int m_idx)
{
    int py = p_ptr->py;
    int px = p_ptr->px;

    monster_type *m_ptr;

    /* No monster */
    if (m_idx <= 0)
	return (FALSE);

    /* Get monster */
    m_ptr = &m_list[m_idx];

    /* Monster must be alive */
    if (!m_ptr->r_idx)
	return (FALSE);

    /* Monster must be visible */
    if (!m_ptr->ml)
	return (FALSE);

    /* Monster must be projectable */
    if (!projectable(py, px, m_ptr->fy, m_ptr->fx, PROJECT_NONE))
	return (FALSE);

    /* Hack -- no targeting hallucinations */
    if (p_ptr->timed[TMD_IMAGE])
	return (FALSE);

    /* Hack -- Never target trappers XXX XXX XXX */
    /* if (CLEAR_ATTR && (CLEAR_CHAR)) return (FALSE); */

    /* Assume okay */
    return (TRUE);
}




/**
 * Determine if an object makes a reasonable target
 *
 * The player can target any location, or any "target-able" object.
 *
 * Currently, an object is "target_able" if the player can hit it with a 
 * projection, and the player is not hallucinating.  This allows use of 
 * "use closest target" macros.
 *
 * This is used for the Telekinesis spell -NRM-
 */
bool target_able_obj(int o_idx)
{
    int py = p_ptr->py;
    int px = p_ptr->px;

    int this_o_idx, next_o_idx;

    bool only_gold = TRUE;

    object_type *o_ptr;

    /* No object */
    if (o_idx <= 0)
	return (FALSE);

    /* Scan all objects in the grid */
    for (this_o_idx = o_idx; this_o_idx; this_o_idx = next_o_idx) {
	/* Acquire object */
	o_ptr = &o_list[this_o_idx];

	/* Acquire next object */
	next_o_idx = o_ptr->next_o_idx;

	/* Found a non-gold object */
	if (o_ptr->tval != TV_GOLD) {
	    only_gold = FALSE;
	    break;
	}
    }

    /* Object must exist */
    if (!o_ptr->k_idx)
	return (FALSE);

    /* There must be a non-gold object */
    if (only_gold)
	return (FALSE);

    /* Object must be projectable */
    if (!projectable(py, px, o_ptr->iy, o_ptr->ix, 0))
	return (FALSE);

    /* Hack -- no targeting hallucinations */
    if (p_ptr->timed[TMD_IMAGE])
	return (FALSE);

    /* Assume okay */
    return (TRUE);
}



/**
 * Update (if necessary) and verify (if possible) the target.
 *
 * We return TRUE if the target is "okay" and FALSE otherwise.
 */
bool target_okay(void)
{
    /* No target */
    if (!target_set)
	return (FALSE);

    /* Accept "location" targets */
    if ((target_who == 0) && (target_what == 0))
	return (TRUE);

    /* Check "monster" targets */
    if (target_who > 0) {
	int m_idx = target_who;

	/* Accept reasonable targets */
	if (target_able(m_idx)) {
	    monster_type *m_ptr = &m_list[m_idx];

	    /* Get the monster location */
	    target_y = m_ptr->fy;
	    target_x = m_ptr->fx;

	    /* Good target */
	    return (TRUE);
	}
    }

    /* Check "object" targets */
    if (target_what > 0) {
	int o_idx = target_what;

	/* Accept reasonable targets */
	if (target_able_obj(o_idx)) {
	    object_type *o_ptr = &o_list[o_idx];

	    /* Acquire object location */
	    target_y = o_ptr->iy;
	    target_x = o_ptr->ix;

	    /* Good target */
	    return (TRUE);
	}
    }

    /* Assume no target */
    return (FALSE);
}


/**
 * Set the target to a monster (or nobody)
 */
void target_set_monster(int m_idx)
{
    /* Acceptable target */
    if ((m_idx > 0) && target_able(m_idx)) {
	monster_type *m_ptr = &m_list[m_idx];

	/* Save target info */
	target_set = TRUE;
	target_who = m_idx;
	target_what = 0;
	target_y = m_ptr->fy;
	target_x = m_ptr->fx;
    }

    /* Clear target */
    else {
	/* Reset target info */
	target_set = FALSE;
	target_who = 0;
	target_what = 0;
	target_y = 0;
	target_x = 0;
    }
}


/**
 * Set the target to an object
 */
void target_set_object(int o_idx)
{
    /* Acceptable target */
    if ((o_idx > 0) && target_able_obj(o_idx)) {
	object_type *o_ptr = &o_list[o_idx];

	/* Save target info */
	target_set = TRUE;
	target_who = 0;
	target_what = o_idx;
	target_y = o_ptr->iy;
	target_x = o_ptr->ix;
    }

    /* Clear target */
    else {
	/* Reset target info */
	target_set = FALSE;
	target_who = 0;
	target_what = 0;
	target_y = 0;
	target_x = 0;
    }
}


/**
 * Set the target to a location
 */
void target_set_location(int y, int x)
{
    /* Legal target */
    if (in_bounds_fully(y, x)) {
	/* Save target info */
	target_set = TRUE;
	target_who = 0;
	target_what = 0;
	target_y = y;
	target_x = x;
    }

    /* Clear target */
    else {
	/* Reset target info */
	target_set = FALSE;
	target_who = 0;
	target_what = 0;
	target_y = 0;
	target_x = 0;
    }
}



/*
 * Sorting hook -- comp function -- by "distance to player"
 *
 * We use "u" and "v" to point to arrays of "x" and "y" positions,
 * and sort the arrays by double-distance to the player.
 */
static int cmp_distance(const void *a, const void *b)
{
    int py = p_ptr->py;
    int px = p_ptr->px;

    const struct loc *pa = a;
    const struct loc *pb = b;

    int da, db, kx, ky;

    /* Absolute distance components */
    kx = pa->x;
    kx -= px;
    kx = ABS(kx);
    ky = pa->y;
    ky -= py;
    ky = ABS(ky);

    /* Approximate Double Distance to the first point */
    da = ((kx > ky) ? (kx + kx + ky) : (ky + ky + kx));

    /* Absolute distance components */
    kx = pb->x;
    kx -= px;
    kx = ABS(kx);
    ky = pb->y;
    ky -= py;
    ky = ABS(ky);

    /* Approximate Double Distance to the first point */
    db = ((kx > ky) ? (kx + kx + ky) : (ky + ky + kx));

    /* Compare the distances */
    if (da < db)
	return -1;
    if (da > db)
	return 1;
    return 0;
}

/*
 * Hack -- help "select" a location (see below)
 */
static s16b target_pick(int y1, int x1, int dy, int dx, struct point_set *targets)
{
    int i, v;

    int x2, y2, x3, y3, x4, y4;

    int b_i = -1, b_v = 9999;


    /* Scan the locations */
    for (i = 0; i < point_set_size(targets); i++)
    {
	/* Point 2 */
	x2 = targets->pts[i].x;
	y2 = targets->pts[i].y;

	/* Directed distance */
	x3 = (x2 - x1);
	y3 = (y2 - y1);

	/* Verify quadrant */
	if (dx && (x3 * dx <= 0))
	    continue;
	if (dy && (y3 * dy <= 0))
	    continue;

	/* Absolute distance */
	x4 = ABS(x3);
	y4 = ABS(y3);

	/* Verify quadrant */
	if (dy && !dx && (x4 > y4))
	    continue;
	if (dx && !dy && (y4 > x4))
	    continue;

	/* Approximate Double Distance */
	v = ((x4 > y4) ? (x4 + x4 + y4) : (y4 + y4 + x4));

	/* Penalize location XXX XXX XXX */

	/* Track best */
	if ((b_i >= 0) && (v >= b_v))
	    continue;

	/* Track best */
	b_i = i;
	b_v = v;
    }

    /* Result */
    return (b_i);
}


/*
 * Hack -- determine if a given location is "interesting"
 */
static bool target_set_interactive_accept(int y, int x)
{
    object_type *o_ptr;


    /* Player grids are always interesting */
    if (cave_m_idx[y][x] < 0)
	return (TRUE);


    /* Handle hallucination */
    if (p_ptr->timed[TMD_IMAGE])
	return (FALSE);


    /* Visible monsters */
    if (cave_m_idx[y][x] > 0) {
	monster_type *m_ptr = &m_list[cave_m_idx[y][x]];

	/* Visible monsters */
	if (m_ptr->ml)
	    return (TRUE);
    }

    /* Traps */
    if (cave_visible_trap(y, x))
	return(TRUE);

    /* Scan all objects in the grid */
    for (o_ptr = get_first_object(y, x); o_ptr; o_ptr = get_next_object(o_ptr)) {
	/* Memorized object */
	if (o_ptr->marked && !squelch_hide_item(o_ptr))
	    return (TRUE);
    }

    /* Interesting memorized features */
    if (cave_has(cave_info[y][x], CAVE_MARK)) {
	feature_type *f_ptr = &f_info[cave_feat[y][x]];

	/* Notice interesting things */
	if (tf_has(f_ptr->flags, TF_INTERESTING))
	    return (TRUE);
    }

    /* Nope */
    return (FALSE);
}

/*
 * Return a target set of target_able monsters.
 */
static struct point_set *target_set_interactive_prepare(int mode)
{
    int y, x;
    struct point_set *targets = point_set_new(TS_INITIAL_SIZE);

    /* Scan the current panel */
    for (y = Term->offset_y; y < Term->offset_y + SCREEN_HGT; y++) {
	for (x = Term->offset_x; x < Term->offset_x + SCREEN_WID; x++) {
	    /* Check bounds */
	    if (!in_bounds_fully(y, x))
		continue;

	    /* Require "interesting" contents */
	    if (!target_set_interactive_accept(y, x))
		continue;

	    /* Monster mode */
	    if (mode & (TARGET_KILL)) {
		/* Must contain a monster */
		if (!(cave_m_idx[y][x] > 0))
		    continue;

		/* Must be a targettable monster */
		if (!target_able(cave_m_idx[y][x]))
		    continue;
	    }

	    /* Object mode */
	    if (mode & (TARGET_OBJ)) {
		/* Must contain an object */
		if (!(cave_o_idx[y][x] > 0))
		    continue;

		/* Must be a targettable object */
		if (!target_able_obj(cave_o_idx[y][x]))
		    continue;
	    }

	    /* Save the location */
	    add_to_point_set(targets, y, x);
	}
    }

    sort(targets->pts, point_set_size(targets), sizeof(*(targets->pts)), cmp_distance);
    return targets;
}

/*
 * Perform the minimum "whole panel" adjustment to ensure that the given
 * location is contained inside the current panel, and return TRUE if any
 * such adjustment was performed. Optionally accounts for the targeting
 * help window.
 */
bool adjust_panel_help(int y, int x, bool help)
{
    bool changed = FALSE;

    int j;

    int screen_hgt_main = help ? (Term->hgt - ROW_MAP - 3) 
	: (Term->hgt - ROW_MAP - 1);

    /* Scan windows */
    for (j = 0; j < ANGBAND_TERM_MAX; j++) {
	int wx, wy;
	int screen_hgt, screen_wid;

	term *t = angband_term[j];

	/* No window */
	if (!t)
	    continue;

	/* No relevant flags */
	if ((j > 0) && !(op_ptr->window_flag[j] & PW_MAP))
	    continue;

	wy = t->offset_y;
	wx = t->offset_x;

	screen_hgt = (j == 0) ? screen_hgt_main : t->hgt;
	screen_wid = (j == 0) ? (Term->wid - COL_MAP - 1) : t->wid;

	/* Bigtile panels need adjustment */
	screen_wid = screen_wid / tile_width;
	screen_hgt = screen_hgt / tile_height;

	/* Adjust as needed */
	while (y >= wy + screen_hgt)
	    wy += screen_hgt / 2;
	while (y < wy)
	    wy -= screen_hgt / 2;

	/* Adjust as needed */
	while (x >= wx + screen_wid)
	    wx += screen_wid / 2;
	while (x < wx)
	    wx -= screen_wid / 2;

	/* Use "modify_panel" */
	if (modify_panel(t, wy, wx))
	    changed = TRUE;
    }

    return (changed);
}


/*
 * Describe a location relative to the player position.
 * e.g. "12 S 35 W" or "0 N, 33 E" or "0 N, 0 E"
 */
void coords_desc(char *buf, int size, int y, int x)
{

    char *east_or_west;
    char *north_or_south;

    int py = p_ptr->py;
    int px = p_ptr->px;

    if (y > py)
	north_or_south = "S";
    else
	north_or_south = "N";

    if (x < px)
	east_or_west = "W";
    else
	east_or_west = "E";

    strnfmt(buf, size, "%d %s, %d %s", ABS(y - py), north_or_south, ABS(x - px),
	    east_or_west);
}

/*
 * Display targeting help at the bottom of the screen.
 */
static void target_display_help(bool monster, bool free)
{
    /* Determine help location */
    int wid, hgt, help_loc;
    Term_get_size(&wid, &hgt);
    help_loc = hgt - HELP_HEIGHT;
	
    /* Clear */
    clear_from(help_loc);

    /* Prepare help hooks */
    text_out_hook = text_out_to_screen;
    text_out_indent = 1;
    Term_gotoxy(1, help_loc);

    /* Display help */
    text_out_c(TERM_L_GREEN, "<dir>");
    text_out(" and ");
    text_out_c(TERM_L_GREEN, "<click>");
    text_out(" look around. '");
    text_out_c(TERM_L_GREEN, "g");
    text_out(" moves to the selection. '");
    text_out_c(TERM_L_GREEN, "p");
    text_out("' selects the player. '");
    text_out_c(TERM_L_GREEN, "q");
    text_out("' exits. '");
    text_out_c(TERM_L_GREEN, "r");
    text_out("' displays details. '");

    if (free) {
	text_out_c(TERM_L_GREEN, "m");
	text_out("' restricts to interesting places. ");
    } else {
	text_out_c(TERM_L_GREEN, "+");
	text_out("' and '");
	text_out_c(TERM_L_GREEN, "-");
	text_out("' cycle through interesting places. '");
	text_out_c(TERM_L_GREEN, "o");
	text_out("' allows free selection. ");
    }
	
    if (monster || free) {
	text_out("'");
	text_out_c(TERM_L_GREEN, "t");
	text_out("' targets the current selection.");
    }

    /* Reset */
    text_out_indent = 0;
}

/*
 * Examine a grid, return a keypress.
 *
 * The "mode" argument contains the "TARGET_LOOK" bit flag, which
 * indicates that the "space" key should scan through the contents
 * of the grid, instead of simply returning immediately.  This lets
 * the "look" command get complete information, without making the
 * "target" command annoying.
 *
 * The "info" argument contains the "commands" which should be shown
 * inside the "[xxx]" text.  This string must never be empty, or grids
 * containing monsters will be displayed with an extra comma.
 *
 * Note that if a monster is in the grid, we update both the monster
 * recall info and the health bar info to track that monster.
 *
 * This function correctly handles multiple objects per grid, and objects
 * and terrain features in the same grid, though the latter never happens.
 *
 * This function must handle blindness/hallucination.
 */
static ui_event target_set_interactive_aux(int y, int x, int mode)
{
    s16b this_o_idx = 0, next_o_idx = 0;

    const char *s1, *s2, *s3, *s4, *s5;

    bool boring;

    int feat;

    int floor_list[MAX_FLOOR_STACK];
    int floor_num;

    ui_event query;

    char out_val[256];

    char coords[20];

    feature_type *f_ptr = &f_info[cave_feat[y][x]];

    /* Describe the square location */
    coords_desc(coords, sizeof(coords), y, x);

    /* Repeat forever */
    while (1) {
	/* Paranoia */
	query.key.code = ' ';

	/* Assume boring */
	boring = TRUE;

	/* Default */
	s1 = "You see ";
	s2 = "";
	s3 = "";


	/* The player */
	if (cave_m_idx[y][x] < 0) {
	    /* Description */
	    s1 = "You are ";

	    /* Preposition */
	    s2 = "on ";
	}

	/* Hack -- hallucination */
	if (p_ptr->timed[TMD_IMAGE]) {
	    const char *name = "something strange";

	    /* Display a message */
	    if (p_ptr->wizard) {
		strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s (%d:%d).", s1,
			s2, s3, name, coords, y, x);
	    } else {
		strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.", s1, s2, s3,
			name, coords);
	    }

	    prt(out_val, 0, 0);
	    move_cursor_relative(y, x);
	    query = inkey_ex();

	    /* Stop on everything but "return" */
	    if (query.key.code != KC_ENTER)
		break;

	    /* Repeat forever */
	    continue;
	}

	/* Actual monsters */
	if (cave_m_idx[y][x] > 0) {
	    monster_type *m_ptr = &m_list[cave_m_idx[y][x]];
	    monster_race *r_ptr = &r_info[m_ptr->r_idx];

	    /* Visible */
	    if (m_ptr->ml) {
		bool recall = FALSE;

		char m_name[80];

		/* Not boring */
		boring = FALSE;

		/* Get the monster name ("a kobold") */
		monster_desc(m_name, sizeof(m_name), m_ptr, MDESC_IND2);

		/* Hack -- track this monster race */
		monster_race_track(m_ptr->r_idx);

		/* Hack -- health bar for this monster */
		health_track(cave_m_idx[y][x]);

		/* Hack -- handle stuff */
		handle_stuff(p_ptr);

		/* Interact */
		while (1) {
		    /* Recall */
		    if (recall) {
			/* Save screen */
			screen_save();

			/* Recall on screen */
			screen_roff(m_ptr->r_idx);

			/* Command */
			query = inkey_ex();

			/* Load screen */
			screen_load();
		    }

		    /* Normal */
		    else {
			char buf[80];

			/* Describe the monster */
			look_mon_desc(buf, sizeof(buf), cave_m_idx[y][x]);

			/* Describe, and prompt for recall */
			if (p_ptr->wizard) {
			    strnfmt(out_val, sizeof(out_val),
				    "%s%s%s%s (%s), %s (%d:%d).", s1, s2, s3,
				    m_name, buf, coords, y, x);
			} else {
			    strnfmt(out_val, sizeof(out_val),
				    "%s%s%s%s (%s), %s.", s1, s2, s3, m_name,
				    buf, coords);
			}

			prt(out_val, 0, 0);

			/* Place cursor */
			move_cursor_relative(y, x);

			/* Command */
			query = inkey_ex();
		    }

		    /* Normal commands */
		    if (query.key.code != 'r')
			break;

		    /* Toggle recall */
		    recall = !recall;
		}

		/* Stop on everything but "return"/"space" */
		if ((query.key.code != KC_ENTER) && (query.key.code != ' '))
		    break;

		/* Sometimes stop at "space" key */
		if ((query.key.code == ' ') && !(mode & (TARGET_LOOK)))
		    break;

		/* Change the intro */
		s1 = "It is ";

		/* Hack -- take account of gender */
		if (rf_has(r_ptr->flags, RF_FEMALE))
		    s1 = "She is ";
		else if (rf_has(r_ptr->flags, RF_MALE))
		    s1 = "He is ";

		/* Use a preposition */
		s2 = "carrying ";

		/* Scan all objects being carried */
		for (this_o_idx = m_ptr->hold_o_idx; this_o_idx;
		     this_o_idx = next_o_idx) {
		    char o_name[80];

		    object_type *o_ptr;

		    /* Get the object */
		    o_ptr = &o_list[this_o_idx];

		    /* Get the next object */
		    next_o_idx = o_ptr->next_o_idx;

		    /* Obtain an object description */
		    object_desc(o_name, sizeof(o_name), o_ptr,
				ODESC_PREFIX | ODESC_FULL);

		    /* Describe the object */
		    if (p_ptr->wizard) {
			strnfmt(out_val, sizeof(out_val),
				"%s%s%s%s, %s (%d:%d).", s1, s2, s3, o_name,
				coords, y, x);
		    } else {
			strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.", s1,
				s2, s3, o_name, coords);
		    }

		    prt(out_val, 0, 0);
		    move_cursor_relative(y, x);
		    query = inkey_ex();

		    /* Stop on everything but "return"/"space" */
		    if ((query.key.code != KC_ENTER) && (query.key.code != ' '))
			break;

		    /* Sometimes stop at "space" key */
		    if ((query.key.code == ' ') && !(mode & (TARGET_LOOK)))
			break;

		    /* Change the intro */
		    s2 = "also carrying ";
		}

		/* Double break */
		if (this_o_idx)
		    break;

		/* Use a preposition */
		s2 = "on ";
	    }
	}

	/* A trap */
	if (cave_visible_trap(y, x)) 
	{
	    trap_type *t_ptr = &trap_list[visible_trap_idx(y, x)];

	    /* Not boring */
	    boring = FALSE;

	    /* Interact */
	    while (1) 
	    {
		/* Change the intro */
		if (cave_m_idx[y][x] < 0) 
		{
		    s1 = "You are ";
		    s2 = "on ";
		}
		else
		{
		    s1 = "You see ";
		    s2 = "";
		}

		/* Pick proper indefinite article */
		s3 = (is_a_vowel(t_ptr->kind->name[0])) ? "an " : "a ";

		/* Describe, and prompt for recall */
		if (p_ptr->wizard) 
		{
		    strnfmt(out_val, sizeof(out_val),
			    "%s%s%s%s, %s (%d:%d).", s1, s2, s3,
			    t_ptr->kind->name, coords, y, x);
		} 
		else 
		{
		    strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.", 
			    s1, s2, s3, t_ptr->kind->name, coords);
		}

		prt(out_val, 0, 0);

		/* Place cursor */
		move_cursor_relative(y, x);

		/* Command */
		query = inkey_ex();
		
		/* Stop on everything but "return"/"space" */
		if ((query.key.code != KC_ENTER) && (query.key.code != ' '))
		    break;
		
		/* Sometimes stop at "space" key */
		if ((query.key.code == ' ') && !(mode & (TARGET_LOOK)))
		    break;
	    }
	}
	
	/* Double break */
	if (cave_visible_trap(y, x))
	    break;
	
	/* Assume not floored */
	floor_num = scan_floor(floor_list, N_ELEMENTS(floor_list), y, x, 0x02);

	/* Scan all marked objects in the grid */
	if ((floor_num > 0)
	    && (!(p_ptr->timed[TMD_BLIND])
		|| (y == p_ptr->py && x == p_ptr->px))) {
	    /* Not boring */
	    boring = FALSE;

	    track_object(-floor_list[0]);
	    handle_stuff(p_ptr);

	    /* If there is more than one item... */
	    if (floor_num > 1)
		while (1) {
		    /* Describe the pile */
		    if (p_ptr->wizard) {
			strnfmt(out_val, sizeof(out_val),
				"%s%s%sa pile of %d objects, %s (%d:%d).", s1,
				s2, s3, floor_num, coords, y, x);
		    } else {
			strnfmt(out_val, sizeof(out_val),
				"%s%s%sa pile of %d objects, %s.", s1, s2, s3,
				floor_num, coords);
		    }

		    prt(out_val, 0, 0);
		    move_cursor_relative(y, x);
		    query = inkey_ex();

		    /* Display objects */
		    if (query.key.code == 'r') {
			int rdone = 0;
			int pos;
			while (!rdone) {
			    /* Save screen */
			    screen_save();

			    /* Display */
			    show_floor(floor_list, floor_num,
				       (OLIST_WEIGHT | OLIST_GOLD));

			    /* Describe the pile */
			    prt(out_val, 0, 0);
			    query = inkey_ex();

			    /* Load screen */
			    screen_load();

			    pos = query.key.code - 'a';
			    if (0 <= pos && pos < floor_num) {
				track_object(-floor_list[pos]);
				handle_stuff(p_ptr);
				continue;
			    }
			    rdone = 1;
			}

			/* Now that the user's done with the display loop,
			 * let's */
			/* the outer loop over again */
			continue;
		    }

		    /* Done */
		    break;
		}
	    /* Only one object to display */
	    else {

		char o_name[80];

		/* Get the single object in the list */
		object_type *o_ptr = &o_list[floor_list[0]];

		/* Not boring */
		boring = FALSE;

		/* Obtain an object description */
		object_desc(o_name, sizeof(o_name), o_ptr,
			    ODESC_PREFIX | ODESC_FULL);

		/* Describe the object */
		if (p_ptr->wizard) {
		    strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s (%d:%d).",
			    s1, s2, s3, o_name, coords, y, x);
		} else {
		    strnfmt(out_val, sizeof(out_val), "%s%s%s%s, %s.", s1, s2,
			    s3, o_name, coords);
		}

		prt(out_val, 0, 0);
		move_cursor_relative(y, x);
		query = inkey_ex();

		/* Stop on everything but "return"/"space" */
		if ((query.key.code != KC_ENTER) && (query.key.code != ' '))
		    break;

		/* Sometimes stop at "space" key */
		if ((query.key.code == ' ') && !(mode & (TARGET_LOOK)))
		    break;

		/* Change the intro */
		s1 = "It is ";

		/* Plurals */
		if (o_ptr->number != 1)
		    s1 = "They are ";

		/* Preposition */
		s2 = "on ";
	    }

	}

	/* Double break */
	if (this_o_idx)
	    break;


	/* Feature (apply "mimic") */
	feat = f_info[cave_feat[y][x]].mimic;

	/* Require knowledge about grid, or ability to see grid */
	if (!cave_has(cave_info[y][x], CAVE_MARK) && 
	    !player_can_see_bold(y, x)) 
	{
	    /* Forget feature */
	    feat = FEAT_NONE;
	}

	/* Terrain feature if needed */
	if (boring || !tf_has(f_ptr->flags, TF_FLOOR))
	{
	    const char *name = f_info[feat].name;

	    /* Hack -- handle unknown grids */
	    if (feat == FEAT_NONE)
		name = "unknown grid";

	    /* Pick a prefix */
	    if (*s2 && (feat != FEAT_FLOOR) && (feat != FEAT_ROAD))
		s2 = "in ";

	    /* Pick proper indefinite article */
	    s3 = (is_a_vowel(name[0])) ? "an " : "a ";

	    /* Hack -- special introduction for store doors */
	    if (tf_has(f_ptr->flags, TF_SHOP))
	    {
		s3 = "the entrance to the ";
	    }

	    /* Hack - destination of surface paths */
	    if (tf_has(f_ptr->flags, TF_PATH))
	    {
		s4 = " to ";
		s5 = locality_name[stage_map[stage_map[p_ptr->stage]
					     [NORTH +
					      (feat - FEAT_LESS_NORTH) / 2]]
				   [LOCALITY]];
	    }
	    else
	    {
		s4 = "";
		s5 = "";
	    }

	    /* Display a message */
	    if (p_ptr->wizard) {
		strnfmt(out_val, sizeof(out_val), "%s%s%s%s%s%s, %s (%d:%d).", 
			s1, s2, s3, name, s4, s5, coords, y, x);
	    } 
	    else 
	    {
		strnfmt(out_val, sizeof(out_val), "%s%s%s%s%s%s, %s.", s1, s2, 
			s3, name, s4, s5, coords);
	    }

	    prt(out_val, 0, 0);
	    move_cursor_relative(y, x);
	    query = inkey_ex();

	    /* Stop on everything but "return"/"space" */
	    if ((query.key.code != KC_ENTER) && (query.key.code != ' '))
		break;
	}

	/* Stop on everything but "return" */
	if (query.key.code != KC_ENTER)
	    break;
    }

    /* Keep going */
    return (query);
}


bool target_set_closest(int mode)
{
    int y, x, m_idx;
    monster_type *m_ptr;
    char m_name[80];
    bool visibility;
    struct point_set *targets;

    /* Cancel old target */
    target_set_monster(0);

    /* Get ready to do targetting */
    targets = target_set_interactive_prepare(mode);

    /* If nothing was prepared, then return */
    if (point_set_size(targets) < 1)
    {
	msg("No Available Target.");
	point_set_dispose(targets);
	return FALSE;
    }

    /* Find the first monster in the queue */
    y = targets->pts[0].y;
    x = targets->pts[0].x;
    m_idx = cave_m_idx[y][x];
	
    /* Target the monster, if possible */
    if ((m_idx <= 0) || !target_able(m_idx))
    {
	msg("No Available Target.");
	point_set_dispose(targets);
	return FALSE;
    }

    /* Target the monster */
    m_ptr = &m_list[m_idx];
    monster_desc(m_name, sizeof(m_name), m_ptr, 0x100);
    if (!(mode & TARGET_QUIET))
	msg("%s is targeted.", m_name);
    Term_fresh();

    /* Set up target information */
    monster_race_track(m_ptr->r_idx);
    health_track(cave_m_idx[y][x]);
    target_set_monster(m_idx);

    /* Visual cue */
    Term_get_cursor(&visibility);
    (void)Term_set_cursor(TRUE);
    move_cursor_relative(y, x);
    Term_redraw_section(x, y, x, y);

    /* TODO: what's an appropriate amount of time to spend highlighting */
    Term_xtra(TERM_XTRA_DELAY, 150);
    (void)Term_set_cursor(visibility);

    point_set_dispose(targets);
    return TRUE;
}


/**
 * Draw a visible path over the squares between (x1,y1) and (x2,y2).
 *
 * The path consists of "*", which are white except where there is a
 * monster, object or feature in the grid.
 *
 * This routine has (at least) three weaknesses:
 * - remembered objects/walls which are no longer present are not shown,
 * - squares which (e.g.) the player has walked through in the dark are
 *   treated as unknown space.
 * - walls which appear strange due to hallucination aren't treated correctly.
 *
 * The first two result from information being lost from the dungeon arrays,
 * which requires changes elsewhere
 *
 * From NPPangband
 */
static int draw_path(u16b path_n, u16b *path_g, wchar_t *c, byte *a, int y1, int x1)
{
    int i;
    bool on_screen;

    /* No path, so do nothing. */
    if (path_n < 1) return 0;

    /* The starting square is never drawn, but notice if it is being
     * displayed. In theory, it could be the last such square.
     */
    on_screen = panel_contains(y1, x1);

    /* Draw the path. */
    for (i = 0; i < path_n; i++) {
	byte colour;

	/* Find the co-ordinates on the level. */
	int y = GRID_Y(path_g[i]);
	int x = GRID_X(path_g[i]);

	/*
	 * As path[] is a straight line and the screen is oblong,
	 * there is only section of path[] on-screen.
	 * If the square being drawn is visible, this is part of it.
	 * If none of it has been drawn, continue until some of it
	 * is found or the last square is reached.
	 * If some of it has been drawn, finish now as there are no
	 * more visible squares to draw.
	 */
	if (panel_contains(y,x)) on_screen = TRUE;
	else if (on_screen) break;
	else continue;

	/* Find the position on-screen */
	move_cursor_relative(y,x);

	/* This square is being overwritten, so save the original. */
	Term_what(Term->scr->cx, Term->scr->cy, a+i, c+i);

	/* Choose a colour. */
	if (cave_m_idx[y][x] && m_list[cave_m_idx[y][x]].ml) {
	    /* Visible monsters are red. */
	    colour = TERM_L_RED;
	}

	else if (cave_o_idx[y][x] && o_list[cave_o_idx[y][x]].marked)
	    /* Known objects are yellow. */
	    colour = TERM_YELLOW;
	
	else if (!cave_project(y, x) &&
		 (cave_has(cave_info[y][x], CAVE_MARK) ||
		  player_can_see_bold(y, x)))
	    /* Known walls are blue. */
	    colour = TERM_BLUE;
	
	else if (!cave_has(cave_info[y][x], CAVE_MARK) &&
		 !player_can_see_bold(y, x)) 
	    /* Unknown squares are grey. */
	    colour = TERM_L_DARK;
	
	else
	    /* Unoccupied squares are white. */
	    colour = TERM_WHITE;
	
	/* Draw the path segment */
	(void)Term_addch(colour, L'*');
    }
    return i;
}

/**
 * Load the attr/char at each point along "path" which is on screen from
 * "a" and "c". This was saved in draw_path().
 */
static void load_path(u16b path_n, u16b *path_g, wchar_t *c, byte *a) {
    int i;
    for (i = 0; i < path_n; i++) {
	int y = GRID_Y(path_g[i]);
	int x = GRID_X(path_g[i]);

	if (!panel_contains(y, x)) continue;
	move_cursor_relative(y, x);
	Term_addch(a[i], c[i]);
    }

    Term_fresh();
}


/*
 * Handle "target" and "look".
 *
 * Note that this code can be called from "get_aim_dir()".
 *
 * Currently, when "flag" is true, that is, when
 * "interesting" grids are being used, and a directional key is used, we
 * only scroll by a single panel, in the direction requested, and check
 * for any interesting grids on that panel.  The "correct" solution would
 * actually involve scanning a larger set of grids, including ones in
 * panels which are adjacent to the one currently scanned, but this is
 * overkill for this function.  XXX XXX
 *
 * Hack -- targetting/observing an "outer border grid" may induce
 * problems, so this is not currently allowed.
 *
 * The player can use the direction keys to move among "interesting"
 * grids in a heuristic manner, or the "space", "+", and "-" keys to
 * move through the "interesting" grids in a sequential manner, or
 * can enter "location" mode, and use the direction keys to move one
 * grid at a time in any direction.  The "t" (set target) command will
 * only target a monster (as opposed to a location) if the monster is
 * target_able and the "interesting" mode is being used.
 *
 * The current grid is described using the "look" method above, and
 * a new command may be entered at any time, but note that if the
 * "TARGET_LOOK" bit flag is set (or if we are in "location" mode,
 * where "space" has no obvious meaning) then "space" will scan
 * through the description of the current grid until done, instead
 * of immediately jumping to the next "interesting" grid.  This
 * allows the "target" command to retain its old semantics.
 *
 * The "*", "+", and "-" keys may always be used to jump immediately
 * to the next (or previous) interesting grid, in the proper mode.
 *
 * The "return" key may always be used to scan through a complete
 * grid description (forever).
 *
 * This command will cancel any old target, even if used from
 * inside the "look" command.
 *
 *
 * 'mode' is one of TARGET_LOOK or TARGET_KILL.
 * 'x' and 'y' are the initial position of the target to be highlighted,
 * or -1 if no location is specified.
 * Returns TRUE if a target has been successfully set, FALSE otherwise.
 */
bool target_set_interactive(int mode, int x, int y)
{
    int py = p_ptr->py;
    int px = p_ptr->px;

    int i, d, m, t, bd;
    int wid, hgt, help_prompt_loc;

    bool done = FALSE;
    bool flag = TRUE;
    bool help = FALSE;

    bool failure_message = FALSE;

    ui_event press;

    /* These are used for displaying the path to the target */
    int path_n;
    u16b path_g[256];
    wchar_t *path_char = malloc(MAX_RANGE * sizeof(*path_char));
    byte *path_attr = malloc(MAX_RANGE * sizeof(*path_attr));
    struct point_set *targets;

    /* If we haven't been given an initial location, start on the player. */
    if (x == -1 || y == -1) {
	x = px;
	y = py;
    }
    /* If we /have/ been given an initial location, make sure we honour it by
     * going into "free targetting" mode. */
    else {
	flag = FALSE;
    }

    /* 
     * Hack -- Start out by selecting any grid by using the TARGET_GRID
     * flag. I do this so dimen_door() is a bit nicer. -TNB-
     */
    if (mode & TARGET_GRID) {
	flag = FALSE;
	mode &= ~TARGET_GRID;
    }

    /* Cancel target */
    target_set_monster(0);

    /* Cancel tracking */
    /* health_track(0); */

    /* Calculate the window location for the help prompt */
    Term_get_size(&wid, &hgt);
    help_prompt_loc = hgt - 1;
	
    /* Display the help prompt */
    prt("Press '?' for help.", help_prompt_loc, 0);

    /* Prepare the "temp" array */
    targets = target_set_interactive_prepare(mode);

    /* Start near the player */
    m = 0;

    /* Interact */
    while (!done) {
	bool path_drawn = FALSE;
		
	/* Interesting grids */
	if (flag && point_set_size(targets))
	{
	    y = targets->pts[m].y;
	    x = targets->pts[m].x;

	    /* Adjust panel if needed */
	    if (adjust_panel_help(y, x, help)) {
		/* Handle stuff */
		handle_stuff(p_ptr);
	    }
		
	    /* Update help */
	    if (help) {
		bool good_target = FALSE;
		if ((mode & TARGET_KILL) && (cave_m_idx[y][x] > 0)
		    && target_able(cave_m_idx[y][x]))
		    good_target = TRUE;
		if ((mode & TARGET_OBJ) && (cave_o_idx[y][x] > 0)
		    && target_able_obj(cave_o_idx[y][x]))
		    good_target = TRUE;
		target_display_help(good_target, !(flag && temp_n));
	    }

	    /* Find the path. */
	    path_n = project_path(path_g, MAX_RANGE, py, px, y, x, PROJECT_THRU);

	    /* Draw the path in "target" mode. If there is one */
	    if (mode & (TARGET_KILL | TARGET_OBJ))
		path_drawn = draw_path(path_n, path_g, path_char, path_attr, py, px);

	    /* Describe and Prompt */
	    press = target_set_interactive_aux(y, x, mode);

	    /* Remove the path */
	    if (path_drawn) load_path(path_n, path_g, path_char, path_attr);

	    /* Cancel tracking */
	    /* health_track(0); */

	    /* Assume no "direction" */
	    d = 0;


	    /* Analyze */
	    if (press.type == EVT_MOUSE) {
		if (press.mouse.button == 3) {
		    /* give the target selection command */
		    press.mouse.button = 2;
		    press.mouse.mods = KC_MOD_CONTROL;
		}
		if (press.mouse.button == 2) {
		    y = KEY_GRID_Y(press);//.mouse.y;
		    x = KEY_GRID_X(press);//.mouse.x;
		    if (press.mouse.mods & KC_MOD_CONTROL) {
			/* same as keyboard target selection command below */
			int m_idx = cave_m_idx[y][x];

			if ((m_idx > 0) && target_able(m_idx)) {
			    monster_type *m_ptr = &m_list[m_idx];
			    /* Set up target information */
			    monster_race_track(m_ptr->r_idx);
			    health_track(m_idx);
			    /*health_track(p_ptr, m_idx);*/
			    target_set_monster(m_idx);
			    done = TRUE;
			} else {
			    bell("Illegal target!");
			}
		    } else
			if (press.mouse.mods & KC_MOD_ALT) {
			    /* go to spot - same as 'g' command below */
			    cmd_insert(CMD_PATHFIND);
			    cmd_set_arg_point(cmd_get_top(), 0, y, x);
			    done = TRUE;
			} else
			{
			    /* cancel look mode */
			    done = TRUE;
			}
		} else
		{
		    y = KEY_GRID_Y(press);//.mouse.y;
		    x = KEY_GRID_X(press);//.mouse.x;
		    if (cave_m_idx[y][x] || cave_o_idx[y][x]){
			/* reset the flag, to make sure we stay in this mode if
			 * something is actually there */
			flag = FALSE;
			/* scan the interesting list and see if there is 
			 * anything here */
			for (i = 0; i < point_set_size(targets); i++) {
			    if ((y == targets->pts[i].y) && (x == targets->pts[i].x)) {
				m = i;
				flag = TRUE;
				break;
			    }
			}
		    } else {
			flag = FALSE;
		    }
		}
	    } else
		switch (press.key.code) {
		case ESCAPE:
		case 'q':
		{
		    done = TRUE;
		    break;
		}

		case ' ':
		case '*':
		case '+':
		{
		    if (++m == point_set_size(targets))
			m = 0;

		    break;
		}

		case '-':
		{
		    if (m-- == 0)
			m = point_set_size(targets) - 1;

		    break;
		}

		case 'p':
		{
		    /* Recenter around player */
		    verify_panel();
		    
		    /* Handle stuff */
		    handle_stuff(p_ptr);
		    
		    y = py;
		    x = px;
		}

		case 'o':
		{
		    flag = FALSE;
		    break;
		}
		
		case 'm':
		{
		    break;
		}

		case 't':
		case '5':
		case '0':
		case '.':
		{
		    if (mode & TARGET_KILL) {
			int m_idx = cave_m_idx[y][x];
			
			if ((m_idx > 0) && target_able(m_idx)) {
			    health_track(m_idx);
			    target_set_monster(m_idx);
			    done = TRUE;
			} else {
			    bell("Illegal target!");
			}
		    } else if (mode & TARGET_OBJ) {
			int o_idx = cave_o_idx[y][x];
			
			if ((o_idx > 0) && target_able_obj(o_idx)) {
			    target_set_object(o_idx);
			    done = TRUE;
			} else {
			    bell("Illegal target!");
			}
		    }
		    break;
		}

		case 'g':
		{
		    cmd_insert(CMD_PATHFIND);
		    cmd_set_arg_point(cmd_get_top(), 0, y, x);
		    done = TRUE;
		    break;
		}
				
		case '?':
		{
		    help = !help;
		    
		    /* Redraw main window */
		    p_ptr->redraw |=
			(PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
		    Term_clear();
		    handle_stuff(p_ptr);
		    if (!help)
			prt("Press '?' for help.", help_prompt_loc, 0);
		    
		    break;
		}
		
		default:
		{
		    /* Extract direction */
		    d = target_dir(press.key);

		    /* Oops */
		    if (!d)
			bell("Illegal command for target mode!");
		    
		    break;
		}
		
		}

	    /* Hack -- move around */
	    if (d) {
		int old_y = targets->pts[m].y;
		int old_x = targets->pts[m].x;
		
		/* Find a new monster */
		i = target_pick(old_y, old_x, ddy[d], ddx[d], targets);
		
		/* Scroll to find interesting grid */
		if (i < 0) {
		    int old_wy = Term->offset_y;
		    int old_wx = Term->offset_x;
		    
		    /* Change if legal */
		    
		    if (change_panel(d)) {
			/* Recalculate interesting grids */
			point_set_dispose(targets);
			targets = target_set_interactive_prepare(mode);
			
			/* Find a new monster */
			i = target_pick(old_y, old_x, ddy[d], ddx[d], targets);
			
			/* Restore panel if needed */
			if ((i < 0) && modify_panel(Term, old_wy, old_wx)) {
			    /* Recalculate interesting grids */
			    point_set_dispose(targets);
			    targets = target_set_interactive_prepare(mode);
			}
			
			/* Handle stuff */
			handle_stuff(p_ptr);
		    }
		}
		
		/* Use interesting grid if found */
		if (i >= 0)
		    m = i;
	    }
	}
	
	/* Objects need a specific target */
	else if (mode & TARGET_OBJ) {
	    done = TRUE;
	    failure_message = TRUE;
	}
	
	/* Arbitrary grids */
	else 
	{
	    /* Update help */
	    if (help) {
		bool good_target = ((cave_m_idx[y][x] > 0)
				    && target_able(cave_m_idx[y][x]));
		target_display_help(good_target, !(flag && point_set_size(targets)));
	    }
	
	    /* Find the path. */
	    path_n = project_path(path_g, MAX_RANGE, py, px, y, x, PROJECT_THRU);

	    /* Draw the path in "target" mode. If there is one */
	    if (mode & (TARGET_KILL))
		path_drawn = draw_path (path_n, path_g, path_char, path_attr, py, px);

	    /* Describe and Prompt (enable "TARGET_LOOK") */
	    press = target_set_interactive_aux(y, x, mode | TARGET_LOOK);

	    /* Remove the path */
	    if (path_drawn)  load_path(path_n, path_g, path_char, path_attr);

	    /* Cancel tracking */
	    /* health_track(0); */

	    /* Assume no direction */
	    d = 0;


	    /* Analyze the keypress */
	    if (press.type == EVT_MOUSE) {
		if (press.mouse.button == 3) {
		    /* give the target selection command */
		    press.mouse.button = 2;
		    press.mouse.mods = KC_MOD_CONTROL;
		}
		if (press.mouse.button == 2) {
		    if (mode & (TARGET_KILL)) {
			if ((y == KEY_GRID_Y(press)) 
			    && (x == KEY_GRID_X(press))) {
			    d = -1;
			}
		    }
		    y = KEY_GRID_Y(press);//.mouse.y;
		    x = KEY_GRID_X(press);//.mouse.x;
		    if (press.mouse.mods & KC_MOD_CONTROL) {
			/* same as keyboard target selection command below */
			target_set_location(y, x);
			done = TRUE;
		    } else
			if (press.mouse.mods & KC_MOD_ALT) {
			    /* go to spot - same as 'g' command below */
			    cmd_insert(CMD_PATHFIND);
			    cmd_set_arg_point(cmd_get_top(), 0, y, x);
			    done = TRUE;
			} else
			{
			    /* cancel look mode */
			    done = TRUE;
			    if (d == -1) {
				target_set_location(y, x);
				d = 0;
			    }
			}
		} else
		    /*if (press.mouse.button == 3) {
		      } else*/
		{
		    int dungeon_hgt = DUNGEON_HGT;
		    int dungeon_wid = DUNGEON_WID;
		
		    /* Adjust for town */
		    if (p_ptr->depth == 0) 
			town_adjust(&dungeon_hgt, &dungeon_wid);

		    y = KEY_GRID_Y(press);//.mouse.y;
		    x = KEY_GRID_X(press);//.mouse.x;
		
		    if (Term) {
			if (press.mouse.y <= 1) {
			    /* move the screen north */
			    y--;
			} else
			    if (press.mouse.y >= (Term->hgt - 2)) {
				/* move the screen south */
				y++;
			    } else
				if (press.mouse.x <= COL_MAP) {
				    /* move the screen in west */
				    x--;
				} else
				    if (press.mouse.x >= (Term->wid - 2)) {
					/* move the screen east */
					x++;
				    }
		    }
		
		    if (y < 0) y = 0;
		    if (x < 0) x = 0;
		    if (y >= dungeon_hgt-1) y = dungeon_hgt-1;
		    if (x >= dungeon_wid-1) x = dungeon_wid-1;
		
		    /* Adjust panel if needed */
		    if (adjust_panel_help(y, x, help))
		    {
			/* Handle stuff */
			handle_stuff(p_ptr);
		    
			/* Recalculate interesting grids */
			point_set_dispose(targets);
			targets = target_set_interactive_prepare(mode);
		    }
		
		    if (cave_m_idx[y][x] || cave_o_idx[y][x]) {
			/* scan the interesting list and see if there's anything here */
			for (i = 0; i < point_set_size(targets); i++) {
			    if ((y == targets->pts[i].y) && (x == targets->pts[i].x)) {
				m = i;
				flag = TRUE;
				break;
			    }
			}
		    } else {
			flag = FALSE;
		    }
		}
	    } else
		switch (press.key.code) 
		{
		case ESCAPE:
		case 'q':
		{
		    done = TRUE;
		    break;
		}
	    
		case ' ':
		case '*':
		case '+':
		case '-':
		{
		    break;
		}
	
		case 'p':
		{
		    /* Recenter around player */
		    verify_panel();
	    
		    /* Handle stuff */
		    handle_stuff(p_ptr);
	    
		    y = p_ptr->py;
		    x = p_ptr->px;
		}

		case 'o':
		{
		    break;
		}
	
		case 'm':
		{
		    flag = TRUE;
	    
		    m = 0;
		    bd = 999;
	    
		    /* Pick a nearby monster */
		    for (i = 0; i < point_set_size(targets); i++)
		    {
			t = distance(y, x, targets->pts[i].y, targets->pts[i].x);
		
			/* Pick closest */
			if (t < bd) {
			    m = i;
			    bd = t;
			}
		    }
	    
		    /* Nothing interesting */
		    if (bd == 999)
			flag = FALSE;
	    
		    break;
		}

		case 't':
		case '5':
		case '0':
		case '.':
		{
		    target_set_location(y, x);
		    done = TRUE;
		    break;
		}
	
		case 'g':
		{
		    cmd_insert(CMD_PATHFIND);
		    cmd_set_arg_point(cmd_get_top(), 0, y, x);
		    done = TRUE;
		    break;
		}
	
		case '?':
		{
		    help = !help;
	    
		    /* Redraw main window */
		    p_ptr->redraw |=
			(PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
		    Term_clear();
		    handle_stuff(p_ptr);
		    if (!help)
			prt("Press '?' for help.", help_prompt_loc, 0);
	    
		    break;
		}
	
		default:
		{
		    /* Extract a direction */
		    d = target_dir(press.key);
	    
		    /* Oops */
		    if (!d)
			bell("Illegal command for target mode!");
	    
		    break;
		}
	    
		}
	
	    /* Handle "direction" */
	    if (d) {
		int dungeon_hgt = DUNGEON_HGT;
		int dungeon_wid = DUNGEON_WID;
		int dy = ddy[d];
		int dx = ddx[d];
	    
		/* Adjust for town */
		if (p_ptr->depth == 0) town_adjust(&dungeon_hgt, &dungeon_wid);

		/* Hack to stop looking outside town walls */
		if (!p_ptr->depth) {
		    if (cave_feat[y + dy][x] == FEAT_PERM_SOLID)
			dy = 0;
		    if (cave_feat[y][x + dx] == FEAT_PERM_SOLID)
			dx = 0;
		}

		/* Move */
		x += dx;
		y += dy;

		/* Slide into legality */
		if (x >= dungeon_wid - 1)
		    x--;
		else if (x <= 0)
		    x++;
	    
		/* Slide into legality */
		if (y >= dungeon_hgt - 1)
		    y--;
		else if (y <= 0)
		    y++;
	    
		/* Adjust panel if needed */
		if (adjust_panel_help(y, x, help)) {
		    /* Handle stuff */
		    handle_stuff(p_ptr);
		
		    /* Recalculate interesting grids */
		    point_set_dispose(targets);
		    targets = target_set_interactive_prepare(mode);
		}
	    }
	}
    }

    /* Forget */
    point_set_dispose(targets);

    /* Redraw as necessary */
    if (help) {
	p_ptr->redraw |= (PR_BASIC | PR_EXTRA | PR_MAP | PR_EQUIP);
	Term_clear();
    } else {
	prt("", 0, 0);
	prt("", help_prompt_loc, 0);
	p_ptr->redraw |= (PR_DEPTH | PR_STATUS);
    }

    /* Recenter around player */
    verify_panel();
    
    /* Handle stuff */
    handle_stuff(p_ptr);

    free(path_char);
    free(path_attr);

    /* Failure to set target */
    if (!target_set) {
	if (failure_message)
	    msg("There is nothing within reach.");
	return (FALSE);
    }

    /* Success */
    return (TRUE);
}


/**
 * Obtains the location the player currently targets.
 *
 * Both `col` and `row` must point somewhere, and on function termination,
 * contain the X and Y locations respectively.
 */
void target_get(s16b * col, s16b * row)
{
	assert(col);
	assert(row);

	*col = target_x;
	*row = target_y;
}


/**
 * Returns the currently targeted monster index.
 */
s16b target_get_monster(void)
{
	return target_who;
}

/**
 * Returns whether there is a current target set.
 */
bool target_is_set(void)
{
    return target_set;
}
