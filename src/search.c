/*
 *  PWMan - password management application
 *
 *  Copyright (C) 2002  Ivan Kelly <ivan@ivankelly.net>
 *  Copyright (c) 2014	Felicity Tarnell.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include	<stdlib.h>
#include	<string.h>

#include	"pwman.h"
#include	"ui.h"

static search_result_t *_search_add_if_matches(search_result_t *, password_t *, folder_t *);
static void	_search_free(void);
static int	search_active(search_t *srch);
static int	search_apply(void);

search_t *
search_new()
{
search_t       *new;

	new = xcalloc(1, sizeof(*new));
	return new;
}

/*
 * String checking only case insensitive using gnu glibc
 */
static char *
search_strcasestr(haystack, needle)
	char const     *haystack, *needle;
{
	/* Never matches if null/empty string given */
	if (haystack == NULL)
		return 0;

	if (strlen(haystack) == 0)
		return 0;

#ifdef HAVE_STRCASESTR
	return strcasestr(haystack, needle);
#else
	return strstr(haystack, needle);
#endif
}


static search_result_t *
_search_add_if_matches(search_result_t *current, password_t *entry, folder_t *list)
{
search_result_t *next = NULL;

	/* Did we get an entry of a list? */
	if (entry != NULL) {
		if (search_strcasestr(entry->name, options->search->search_term)
		    || search_strcasestr(entry->host, options->search->search_term)
		    || search_strcasestr(entry->user, options->search->search_term)
		    || search_strcasestr(entry->passwd, options->search->search_term)
		    || search_strcasestr(entry->launch, options->search->search_term)
			) {
			next = xcalloc(1, sizeof(*next));
			next->entry = entry;
			next->sublist = list;
			debug("Matched entry on host '%s'", entry->host);
		}
	} else {
		if (search_strcasestr(list->name, options->search->search_term)) {
			next = xcalloc(1, sizeof(*next));
			next->sublist = list;
			next->entry = NULL;
			debug("Matched sublist '%s'", list->name);
		}
	}

	/* If we matched, append */
	if (next == NULL)
		return current;

	if (current == NULL) {
		/* First hit */
		search_results = next;
	} else {
		/* Additional hit, append */
		current->next = next;
	}

	/* For now, nothing follows us */
	next->next = NULL;
	/* We are the new current entry */
	return next;
}

static int
search_apply()
{
folder_t       *stack[MAX_SEARCH_DEPTH];
folder_t       *tmpList = NULL;
password_t     *tmp = NULL;
int		depth;
int		stepping_back;

search_result_t *cur = NULL;

	/* Tidy up any existing search results */
	if (search_results != NULL)
		_search_free();

	/* If no search term, then nothing to do! */
	if (search_active(options->search) == 0)
		return 1;

	/* Make sure we have a clean search stack so we won't get confused */
	for (depth = 0; depth < MAX_SEARCH_DEPTH; depth++)
		stack[depth] = NULL;

	/* Setup for start */
	depth = 0;
	tmpList = folder;
	stepping_back = 0;

	/* Find anything we like the look of */
	while (depth >= 0) {
		/* Any sublists? */
		if (!stepping_back && tmpList->sublists != NULL && depth < MAX_SEARCH_DEPTH) {
			/* Prepare to descend */
			stack[depth] = tmpList;
			depth++;
			tmpList = tmpList->sublists;

			/* Test first child */
			cur = _search_add_if_matches(cur, NULL, tmpList);

			/* Descend into first child */
			continue;
		}
		stepping_back = 0;

		/* Any entries? */
		PWLIST_FOREACH(tmp, &tmpList->list)
			cur = _search_add_if_matches(cur, tmp, tmpList);

		/* Next sibling if there is one */
		if (tmpList->next != NULL) {
			tmpList = tmpList->next;
			/* Test sibling */
			cur = _search_add_if_matches(cur, NULL, tmpList);
			/* Process sibling */
			continue;
		}
		/* Otherwise step up */
		depth--;
		stepping_back = 1;
		if (depth >= 0) {
			tmpList = stack[depth];
			stack[depth] = NULL;
		}
	}

	/* All done */
	return 1;
}

void
search_remove()
{
	/* Put things back how they should have been */
	current_pw_sublist = folder;
	current_pw_sublist->current_item = -1;

	/* Free the memory held by the search results */
	_search_free();

	/* Clear the search term too */
	options->search->search_term[0] = 0;

	/* Back to the old screen */
	uilist_refresh();
}

void
_search_free()
{
search_result_t *cur;
search_result_t *next;

	/* Free the memory held by the search results */
	cur = search_results;
	while (cur != NULL) {
		next = cur->next;
		xfree(cur);
		cur = next;
	}
	search_results = NULL;
}

void
search_get()
{
	if (options->search == NULL) {
		debug("No options->search");
	} else {
		if (options->search->search_term == NULL) {
			debug("No options->search->search_term");
		} else {
			debug("Len was %d", strlen(options->search->search_term));
		}
	}

	xfree(options->search->search_term);
	options->search->search_term = ui_ask_str("String to search for:", NULL);

	search_apply();

	current_pw_sublist->current_item = -1;
	uilist_refresh();
}

void
search_alert(search_t *srch)
{
char		alert[80];

	if (search_active(srch) == 0)
		return;

	if (search_results == NULL)
		sprintf(alert, " (No results found for '%s')", srch->search_term);
	else
		sprintf(alert, " (Search results for '%s')", srch->search_term);

	ui_statusline_clear();
	ui_statusline_msg(alert);
}


int
search_active(search_t *srch)
{
	if ((srch == NULL) || (srch->search_term == NULL))
		/* no search object */
		return 0;

	if (strlen(srch->search_term) == 0)
		/* no search */
		return 0;

	return 1;
}
