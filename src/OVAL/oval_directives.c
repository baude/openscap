/**
 * @file oval_resultDirectives.c
 * \brief Open Vulnerability and Assessment Language
 *
 * See more details at http://oval.mitre.org/
 * @cond
 */

/*
 * Copyright 2009 Red Hat Inc., Durham, North Carolina.
 * All Rights Reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Authors:
 *      "David Niemoller" <David.Niemoller@g2-inc.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "oval_directives_impl.h"
#include "oval_collection_impl.h"

#include "common/assume.h"
#include "common/util.h"
#include "common/debug_priv.h"


#define NUMBER_OF_RESULTS 6

struct _oval_result_directive {
	bool reported;
	oval_result_directive_content_t content;
};

typedef struct oval_result_directives {
	struct _oval_result_directive directive[NUMBER_OF_RESULTS];
} oval_result_directives_t;

struct oval_result_directives *oval_result_directives_new(void)
{
	oval_result_directives_t *directives = (oval_result_directives_t *)
	    oscap_alloc(sizeof(oval_result_directives_t));
	if (directives == NULL)
		return NULL;

	int i;
	for (i = 0; i < NUMBER_OF_RESULTS; i++) {
		directives->directive[i].reported = false;
		directives->directive[i].content = OVAL_DIRECTIVE_CONTENT_UNKNOWN;
	}
	return directives;
}

void oval_result_directives_free(struct oval_result_directives *directives)
{
	oscap_free(directives);
}

bool oval_result_directives_get_reported(struct oval_result_directives *directives, oval_result_t type) {
	__attribute__nonnull__(directives);

	int i=-1;
	while (type) {
		i = i + 1;
		type = type >> 1;
	}

        assume_r(i >= 0 && i < NUMBER_OF_RESULTS,
                 /* return */ false);

	return directives->directive[i].reported;
}

oval_result_directive_content_t oval_result_directives_get_content
    (struct oval_result_directives * directives, oval_result_t type) {
	__attribute__nonnull__(directives);

	int i=-1;
	while (type) {
		i = i + 1;
		type = type >> 1;
	}

        assume_r(i >= 0 && i < NUMBER_OF_RESULTS,
                 /* return */ OVAL_DIRECTIVE_CONTENT_UNKNOWN);

	return directives->directive[i].content;
}

void oval_result_directives_set_reported(struct oval_result_directives *directives, int flag, bool val) {
	__attribute__nonnull__(directives);

	int i=0;
	while (flag>>i) {
		if( flag & (1 << i) )
			directives->directive[i].reported = val;
		i = i + 1;
	}
}

void oval_result_directives_set_content(struct oval_result_directives *directives, int flag, oval_result_directive_content_t content) 
{
	__attribute__nonnull__(directives);
	int i=0;
	while (flag>>i) {
		if( flag & (1 << i) )
			directives->directive[i].content = content;
		i = i + 1;
	}
}

static const struct oscap_string_map OVAL_DIRECTIVE_MAP[] = {
	{OVAL_RESULT_TRUE, "definition_true"},
	{OVAL_RESULT_FALSE, "definition_false"},
	{OVAL_RESULT_UNKNOWN, "definition_unknown"},
	{OVAL_RESULT_ERROR, "definition_error"},
	{OVAL_RESULT_NOT_EVALUATED, "definition_not_evaluated"},
	{OVAL_RESULT_NOT_APPLICABLE, "definition_not_applicable"},
	{OVAL_ENUMERATION_INVALID, NULL}
};

int oval_result_directives_parse_tag(xmlTextReaderPtr reader, struct oval_parser_context *context, void *usr) {
	struct oval_result_directives *directives = (struct oval_result_directives *)usr;
	int retcode = 0;

	xmlChar *name = xmlTextReaderLocalName(reader);
	oval_result_t type = oscap_string_to_enum(OVAL_DIRECTIVE_MAP, (const char *)name);

	if ( (int) type != OVAL_ENUMERATION_INVALID) {
		/*reported */
		xmlChar *boolstr = xmlTextReaderGetAttribute(reader, BAD_CAST "reported");
		bool reported = (strcmp((const char *)boolstr, "1") == 0) || (strcmp((const char *)boolstr, "true") == 0);
		oscap_free(boolstr);
		oval_result_directives_set_reported(directives, type, reported);

		/*content */
		xmlChar *contentstr = xmlTextReaderGetAttribute(reader, BAD_CAST "content");
		oval_result_directive_content_t content = OVAL_DIRECTIVE_CONTENT_UNKNOWN;
		if (contentstr) {
			if (strcmp("thin", (const char *)contentstr) == 0)
				content = OVAL_DIRECTIVE_CONTENT_THIN;
			else if (strcmp("full", (const char *)contentstr) == 0)
				content = OVAL_DIRECTIVE_CONTENT_FULL;

			if (content != OVAL_DIRECTIVE_CONTENT_UNKNOWN) {
				oval_result_directives_set_content(directives, type, content);
			} else {
				oscap_dlprintf(DBG_W, "Cannot resolve @content: \"%s\".\n", contentstr);
				retcode = 1;
			}
			oscap_free(contentstr);
		} else {
			content = OVAL_DIRECTIVE_CONTENT_FULL;
		}
	} else {
		oscap_dlprintf(DBG_W, "Cannot resolve <%s>.\n", name);
		retcode = 1;
	}
	oscap_free(name);
	return retcode;
}

int oval_result_directives_to_dom(struct oval_result_directives *directives, xmlDoc * doc, xmlNode * parent) {
	int retcode = 1;
	xmlNs *ns_results = xmlSearchNsByHref(doc, parent, OVAL_RESULTS_NAMESPACE);
	xmlNode *directives_node = xmlNewTextChild(parent, ns_results, BAD_CAST "directives", NULL);

	const struct oscap_string_map *map;
	for (map = OVAL_DIRECTIVE_MAP; map->string; map++) {
		oval_result_t directive = (oval_result_t)
		    map->value;
		bool reported = oval_result_directives_get_reported(directives, directive);
		oval_result_directive_content_t content = oval_result_directives_get_content(directives, directive);
		xmlNode *directive_node = xmlNewTextChild(directives_node, ns_results, BAD_CAST(map->string), NULL);
		char *val_reported = (reported) ? "true" : "false";
		char *val_content = (content == OVAL_DIRECTIVE_CONTENT_FULL)
		    ? "full" : "thin";
		xmlNewProp(directive_node, BAD_CAST "reported", BAD_CAST val_reported);
		xmlNewProp(directive_node, BAD_CAST "content", BAD_CAST val_content);
	}
	return retcode;
}

/**
 * @endcond
 */