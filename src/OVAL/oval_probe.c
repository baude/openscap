/*
 * Copyright 2009-2010 Red Hat Inc., Durham, North Carolina.
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
 *      "Daniel Kopecek" <dkopecek@redhat.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#if defined(OSCAP_THREAD_SAFE)
# include <pthread.h>
#endif

#include "oval_probe.h"
#include "oval_system_characteristics.h"
#include "common/_error.h"
#include "common/assume.h"

#include "oval_probe_impl.h"
#include "oval_system_characteristics_impl.h"
#include "common/util.h"
#include "common/bfind.h"
#include "common/debug_priv.h"

#include "_oval_probe_session.h"
#include "_oval_probe_handler.h"
#include "oval_probe_meta.h"

oval_probe_meta_t OSCAP_GSYM(__probe_meta)[] = {
        { OVAL_SUBTYPE_SYSINFO, "system_info", &oval_probe_sys_handler, OVAL_PROBEMETA_EXTERNAL, "probe_system_info" },
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_FAMILY, "family"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_FILE_MD5, "filemd5"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_FILE_HASH, "filehash"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_ENVIRONMENT_VARIABLE, "environmentvariable"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_SQL, "sql"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_TEXT_FILE_CONTENT_54, "textfilecontent54"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_TEXT_FILE_CONTENT, "textfilecontent"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_VARIABLE, "variable"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_XML_FILE_CONTENT, "xmlfilecontent"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_SQL57, "sql57"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_ENVIRONMENT_VARIABLE58, "environmentvariable58"),
        OVAL_PROBE_EXTERNAL(OVAL_INDEPENDENT_FILE_HASH58, "filehash58"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_DPKG_INFO, "dpkginfo"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_INET_LISTENING_SERVERS, "inetlisteningservers"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_INET_LISTENING_SERVER, "inetlisteningserver"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_RPM_INFO, "rpminfo"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_PARTITION, "partition"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_IFLISTENERS, "iflisteners"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_RPMVERIFY, "rpmverify"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_RPMVERIFYFILE, "rpmverifyfile"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_SELINUXBOOLEAN, "selinuxboolean"),
        OVAL_PROBE_EXTERNAL(OVAL_LINUX_SELINUXSECURITYCONTEXT, "selinuxsecuritycontext"),
        OVAL_PROBE_EXTERNAL(OVAL_SOLARIS_ISAINFO, "isainfo"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_FILE, "file"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_INTERFACE, "interface"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_PASSWORD, "password"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_PROCESS, "process"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_RUNLEVEL, "runlevel"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_SHADOW, "shadow"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_UNAME, "uname"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_XINETD, "xinetd"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_SYSCTL, "sysctl"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_PROCESS58, "process58"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_FILEEXTENDEDATTRIBUTE, "fileextendedattribute"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_GCONF, "gconf"),
        OVAL_PROBE_EXTERNAL(OVAL_UNIX_ROUTINGTABLE, "routingtable")
};

#define __PROBE_META_COUNT (sizeof OSCAP_GSYM(__probe_meta)/sizeof OSCAP_GSYM(__probe_meta)[0])

size_t OSCAP_GSYM(__probe_meta_count) = __PROBE_META_COUNT;
oval_subtypedsc_t OSCAP_GSYM(__s2n_tbl)[__PROBE_META_COUNT];
oval_subtypedsc_t OSCAP_GSYM(__n2s_tbl)[__PROBE_META_COUNT];

#define __s2n_tbl_count OSCAP_GSYM(__probe_meta_count)
#define __n2s_tbl_count OSCAP_GSYM(__probe_meta_count)

static int __s2n_tbl_cmp(oval_subtype_t *type, oval_subtypedsc_t *dsc)
{
        return (*type - dsc->type);
}

static int __n2s_tbl_cmp(const char *name, oval_subtypedsc_t *dsc)
{
        return strcmp(name, dsc->name);
}

#if defined(ENABLE_PROBES)
/*
 * Library side entity name cache. Initialization needs to be
 * thread-safe and is done by oval_probe_session_new. Freeing
 * of memory used by this cache is done at exit using a hook
 * registered with atexit().
 */
probe_ncache_t  *OSCAP_GSYM(ncache) = NULL;
struct id_desc_t OSCAP_GSYM(id_desc);
#endif

#if defined(OSCAP_THREAD_SAFE)
# include <pthread.h>
static pthread_once_t __oval_probe_init_once = PTHREAD_ONCE_INIT;
#else
static volatile int __oval_probe_init_once = 0;
#endif

#define __ERRBUF_SIZE 128

static int __s2n_tbl_sortcmp(oval_subtypedsc_t *a, oval_subtypedsc_t *b)
{
        return (a->type - b->type);
}

static int __n2s_tbl_sortcmp(oval_subtypedsc_t *a, oval_subtypedsc_t *b)
{
        return strcmp(a->name, b->name);
}

void oval_probe_tblinit(void)
{
        register size_t i;

        for(i = 0; i < OSCAP_GSYM(__probe_meta_count); ++i) {
                OSCAP_GSYM(__s2n_tbl)[i].type = OSCAP_GSYM(__probe_meta)[i].otype;
                OSCAP_GSYM(__n2s_tbl)[i].type = OSCAP_GSYM(__probe_meta)[i].otype;
                OSCAP_GSYM(__s2n_tbl)[i].name = OSCAP_GSYM(__probe_meta)[i].stype;
                OSCAP_GSYM(__n2s_tbl)[i].name = OSCAP_GSYM(__probe_meta)[i].stype;
        }

        qsort(OSCAP_GSYM(__s2n_tbl), OSCAP_GSYM(__probe_meta_count), sizeof (oval_subtypedsc_t),
              (int(*)(const void *, const void *))__s2n_tbl_sortcmp);

        qsort(OSCAP_GSYM(__n2s_tbl), OSCAP_GSYM(__probe_meta_count), sizeof (oval_subtypedsc_t),
              (int(*)(const void *, const void *))__n2s_tbl_sortcmp);
}

static void __init_once(void)
{
#if defined(OSCAP_THREAD_SAFE)
        if (pthread_once(&__oval_probe_init_once, &oval_probe_tblinit) != 0)
                abort();
#else
        if (__oval_probe_init_once == 0) {
                oval_probe_tblinit();
                __oval_probe_init_once = 1;
        }
#endif
        return;
}

const char *oval_subtype_to_str(oval_subtype_t subtype)
{
        oval_subtypedsc_t *d;

        __init_once();

        d = oscap_bfind(OSCAP_GSYM(__s2n_tbl), __s2n_tbl_count, sizeof(oval_subtypedsc_t), &subtype,
                        (int(*)(void *, void *))__s2n_tbl_cmp);

        return (d == NULL ? NULL : d->name);
}

oval_subtype_t oval_str_to_subtype(const char *str)
{
        oval_subtypedsc_t *d;

        __init_once();

        d = oscap_bfind(OSCAP_GSYM(__n2s_tbl), __n2s_tbl_count, sizeof(oval_subtypedsc_t), (void *)str,
                        (int(*)(void *, void *))__n2s_tbl_cmp);

        return (d == NULL ? OVAL_SUBTYPE_UNKNOWN : d->type);
}

static void _obj_collect_var_refs(struct oval_object *obj, struct oval_string_map *vm);
static void _var_collect_var_refs(struct oval_variable *var, struct oval_string_map *vm);

static void _comp_collect_var_refs(struct oval_component *comp, struct oval_string_map *vm)
{
	struct oval_object *obj;
	struct oval_variable *var;
	struct oval_component_iterator *cmp_itr;

	switch (oval_component_get_type(comp)) {
	case OVAL_COMPONENT_OBJECTREF:
		obj = oval_component_get_object(comp);
		_obj_collect_var_refs(obj, vm);
		break;
	case OVAL_COMPONENT_VARREF:
		var = oval_component_get_variable(comp);
		_var_collect_var_refs(var, vm);
		break;
	case OVAL_FUNCTION_ARITHMETIC:
	case OVAL_FUNCTION_BEGIN:
	case OVAL_FUNCTION_CONCAT:
	case OVAL_FUNCTION_END:
	case OVAL_FUNCTION_ESCAPE_REGEX:
	case OVAL_FUNCTION_REGEX_CAPTURE:
	case OVAL_FUNCTION_SPLIT:
	case OVAL_FUNCTION_SUBSTRING:
	case OVAL_FUNCTION_TIMEDIF:
		cmp_itr = oval_component_get_function_components(comp);
		while (oval_component_iterator_has_more(cmp_itr)) {
			struct oval_component *cmp;

			cmp = oval_component_iterator_next(cmp_itr);
			_comp_collect_var_refs(cmp, vm);
		}
		oval_component_iterator_free(cmp_itr);
		break;
	default:
		break;
	}
}

static void _var_collect_var_refs(struct oval_variable *var, struct oval_string_map *vm)
{
	char *var_id;

	var_id = oval_variable_get_id(var);
	oval_string_map_put(vm, var_id, var);

	if (oval_variable_get_type(var) == OVAL_VARIABLE_LOCAL) {
		struct oval_component *comp;

		comp = oval_variable_get_component(var);
		_comp_collect_var_refs(comp, vm);
	}
}

static void _ent_collect_var_refs(struct oval_entity *ent, struct oval_string_map *vm)
{
	if (oval_entity_get_varref_type(ent) == OVAL_ENTITY_VARREF_ATTRIBUTE) {
		struct oval_variable *var;

		var = oval_entity_get_variable(ent);
		_var_collect_var_refs(var, vm);
	}
}

static void _ste_collect_var_refs(struct oval_state *ste, struct oval_string_map *vm)
{
	struct oval_state_content_iterator *cont_itr;

	cont_itr = oval_state_get_contents(ste);
	while (oval_state_content_iterator_has_more(cont_itr)) {
		struct oval_state_content *cont;
		struct oval_entity *ent;

		cont = oval_state_content_iterator_next(cont_itr);
		ent = oval_state_content_get_entity(cont);
		_ent_collect_var_refs(ent, vm);
	}
	oval_state_content_iterator_free(cont_itr);
}

static void _set_collect_var_refs(struct oval_setobject *set, struct oval_string_map *vm)
{
	struct oval_setobject_iterator *subset_itr;
	struct oval_object_iterator *obj_itr;
	struct oval_filter_iterator *fil_itr;

	switch (oval_setobject_get_type(set)) {
	case OVAL_SET_AGGREGATE:
		subset_itr = oval_setobject_get_subsets(set);
		while (oval_setobject_iterator_has_more(subset_itr)) {
			struct oval_setobject *subset;

			subset = oval_setobject_iterator_next(subset_itr);
			_set_collect_var_refs(subset, vm);
		}
		oval_setobject_iterator_free(subset_itr);
		break;
	case OVAL_SET_COLLECTIVE:
		obj_itr = oval_setobject_get_objects(set);
		while (oval_object_iterator_has_more(obj_itr)) {
			struct oval_object *obj;

			obj = oval_object_iterator_next(obj_itr);
			_obj_collect_var_refs(obj, vm);
		}
		oval_object_iterator_free(obj_itr);
		fil_itr = oval_setobject_get_filters(set);
		while (oval_filter_iterator_has_more(fil_itr)) {
			struct oval_filter *fil;
			struct oval_state *ste;

			fil = oval_filter_iterator_next(fil_itr);
			ste = oval_filter_get_state(fil);
			_ste_collect_var_refs(ste, vm);
		}
		oval_filter_iterator_free(fil_itr);
		break;
	default:
		break;
	}
}

static void _obj_collect_var_refs(struct oval_object *obj, struct oval_string_map *vm)
{
	struct oval_object_content_iterator *cont_itr;

	cont_itr = oval_object_get_object_contents(obj);
	while (oval_object_content_iterator_has_more(cont_itr)) {
		struct oval_object_content *cont;
		struct oval_entity *ent;
		struct oval_setobject *set;

		cont = oval_object_content_iterator_next(cont_itr);

		switch (oval_object_content_get_type(cont)) {
		case OVAL_OBJECTCONTENT_ENTITY:
			ent = oval_object_content_get_entity(cont);
			_ent_collect_var_refs(ent, vm);
			break;
		case OVAL_OBJECTCONTENT_SET:
			set = oval_object_content_get_setobject(cont);
			_set_collect_var_refs(set, vm);
			break;
		default:
			break;
		}
	}
	oval_object_content_iterator_free(cont_itr);
}

static void _syschar_add_bindings(struct oval_syschar *sc, struct oval_string_map *vm)
{
	struct oval_iterator *var_itr;

	var_itr = oval_string_map_values(vm);
	while (oval_collection_iterator_has_more(var_itr)) {
		struct oval_variable *var;
		struct oval_value_iterator *val_itr;
		struct oval_variable_binding *binding;

		var = oval_collection_iterator_next(var_itr);
		binding = oval_variable_binding_new(var, NULL);

		val_itr = oval_variable_get_values(var);
		while (oval_value_iterator_has_more(val_itr)) {
			struct oval_value *val;
			char *txt;

			val = oval_value_iterator_next(val_itr);
			txt = oval_value_get_text(val);
			txt = oscap_strdup(txt);
			oval_variable_binding_add_value(binding, txt);
		}
		oval_value_iterator_free(val_itr);

		oval_syschar_add_variable_binding(sc, binding);
	}
	oval_collection_iterator_free(var_itr);
}

int oval_probe_query_object(oval_probe_session_t *psess, struct oval_object *object, int flags, struct oval_syschar **out_syschar)
{
	char *oid;
	struct oval_syschar *sysc;
        oval_subtype_t type;
        oval_ph_t *ph;
	struct oval_string_map *vm;
	struct oval_syschar_model *model;
	int ret;

	oid = oval_object_get_id(object);
	model = psess->sys_model;

	dI("Querying object id: \"%s\", flags: %u.\n", oid, flags);

	sysc = oval_syschar_model_get_syschar(model, oid);
	if (sysc != NULL) {
		oval_syschar_collection_flag_t sc_flg;

		sc_flg = oval_syschar_get_flag(sysc);

		dI("Syschar already exists, flag: %u, '%s'.\n", sc_flg, oval_syschar_collection_flag_get_text(sc_flg));

		if (sc_flg != SYSCHAR_FLAG_UNKNOWN || (flags & OVAL_PDFLAG_NOREPLY)) {
			if (out_syschar)
				*out_syschar = sysc;
			return 0;
		}
	} else
		sysc = oval_syschar_new(model, object);

	if (out_syschar)
		*out_syschar = sysc;

	type = oval_object_get_subtype(object);
	ph = oval_probe_handler_get(psess->ph, type);

        if (ph == NULL) {
                char *msg = "OVAL object not supported.";

		dW(msg);
		oval_syschar_add_new_message(sysc, msg, OVAL_MESSAGE_LEVEL_WARNING);
		oval_syschar_set_flag(sysc, SYSCHAR_FLAG_NOT_COLLECTED);

		return 1;
        }

	if ((ret = ph->func(type, ph->uptr, PROBE_HANDLER_ACT_EVAL, sysc, flags)) != 0) {
		return ret;
	}

	if (!(flags & OVAL_PDFLAG_NOREPLY)) {
		vm = oval_string_map_new();
		_obj_collect_var_refs(object, vm);
		_syschar_add_bindings(sysc, vm);
		oval_string_map_free(vm, NULL);
	}

	return 0;
}

int oval_probe_query_sysinfo(oval_probe_session_t *sess, struct oval_sysinfo **out_sysinfo)
{
	struct oval_sysinfo *sysinf;
        oval_ph_t *ph;
	int ret;

        ph = oval_probe_handler_get(sess->ph, OVAL_SUBTYPE_SYSINFO);

        if (ph == NULL) {
                oscap_seterr (OSCAP_EFAMILY_OVAL, OVAL_EPROBENOTSUPP, "OVAL object not supported");
		return(-1);
        }

        if (ph->func == NULL) {
                oscap_seterr (OSCAP_EFAMILY_OVAL, OVAL_EPROBENOTSUPP, "OVAL object not correctly defined");
		return(-1);
        }

        sysinf = NULL;

	ret = ph->func(OVAL_SUBTYPE_SYSINFO, ph->uptr, PROBE_HANDLER_ACT_EVAL, NULL, &sysinf, 0);
	if (ret != 0)
		return(ret);

	*out_sysinfo = sysinf;
	return(0);
}

static int oval_probe_query_criteria(oval_probe_session_t *sess, struct oval_criteria_node *cnode);

int oval_probe_query_definition(oval_probe_session_t *sess, const char *id) {

	struct oval_syschar_model * syschar_model;
        struct oval_definition_model *definition_model;
	struct oval_definition *definition;
	int ret;

	syschar_model = sess->sys_model;
        definition_model = oval_syschar_model_get_definition_model(syschar_model);
	definition = oval_definition_model_get_definition(definition_model, id);
	if (definition == NULL) {
                char msg[100];
                snprintf(msg, sizeof(msg), "No definition with ID: %s in definition model.", id);
                dE(msg);
                oscap_seterr(OSCAP_EFAMILY_OSCAP, OVAL_EOVALINT, msg);
		return -1;
	}

	struct oval_criteria_node * cnode = oval_definition_get_criteria(definition);
	if (cnode == NULL)
		return -1;

	ret = oval_probe_query_criteria(sess, cnode);

	return ret;
}

/**
 * @returns 0 on success; -1 on error; 1 on warning
 */
static int oval_probe_query_criteria(oval_probe_session_t *sess, struct oval_criteria_node *cnode) {
	int ret;

	switch (oval_criteria_node_get_type(cnode)) {
	/* Criterion node is the final node that has a reference to a test */
	case OVAL_NODETYPE_CRITERION:{
		/* There should be a test .. */
		struct oval_test *test;
		struct oval_object *object;
		struct oval_state_iterator *ste_itr;

		test = oval_criteria_node_get_test(cnode);
		if (test == NULL)
			return 0;
		object = oval_test_get_object(test);
		if (object == NULL)
			return 0;
		/* probe object */
		ret = oval_probe_query_object(sess, object, 0, NULL);
		if (ret == -1)
			return ret;
		/* probe objects referenced like this: test->state->variable->object */
		ste_itr = oval_test_get_states(test);
		while (oval_state_iterator_has_more(ste_itr)) {
			struct oval_state *state = oval_state_iterator_next(ste_itr);
			struct oval_state_content_iterator *contents = oval_state_get_contents(state);
			while (oval_state_content_iterator_has_more(contents)) {
				struct oval_state_content *content = oval_state_content_iterator_next(contents);
				struct oval_entity * entity = oval_state_content_get_entity(content);
				if (oval_entity_get_varref_type(entity) == OVAL_ENTITY_VARREF_ATTRIBUTE) {
					oval_syschar_collection_flag_t flag;
					struct oval_variable *var = oval_entity_get_variable(entity);

					ret = oval_probe_query_variable(sess, var);
					if (ret == -1) {
						oval_state_content_iterator_free(contents);
						oval_state_iterator_free(ste_itr);
						return ret;
					}

					flag = oval_variable_get_collection_flag(var);
					switch (flag) {
					case SYSCHAR_FLAG_COMPLETE:
					case SYSCHAR_FLAG_INCOMPLETE:
						break;
					default:
						oval_state_content_iterator_free(contents);
						oval_state_iterator_free(ste_itr);
						return 0;
					}
				}
			}
			oval_state_content_iterator_free(contents);
		}
		oval_state_iterator_free(ste_itr);

		return 0;

		}
		break;
                /* Criteria node is type of set that contains more criterias. Criteria node
                 * child can be also type of criteria, criterion or extended definition */
        case OVAL_NODETYPE_CRITERIA:{
                        /* group of criterion nodes, get subnodes, continue recursive */
                        struct oval_criteria_node_iterator *cnode_it = oval_criteria_node_get_subnodes(cnode);
                        if (cnode_it == NULL)
                                return 0;
                        /* we have subnotes */
                        struct oval_criteria_node *node;
                        while (oval_criteria_node_iterator_has_more(cnode_it)) {
                                node = oval_criteria_node_iterator_next(cnode_it);
                                ret = oval_probe_query_criteria(sess, node);
                                if (ret != 0) {
                                        oval_criteria_node_iterator_free(cnode_it);
                                        return ret;
                                }
                        }
                        oval_criteria_node_iterator_free(cnode_it);
			return 0;
                }
                break;
                /* Extended definition contains reference to definition, we need criteria of this
                 * definition to be evaluated completely */
        case OVAL_NODETYPE_EXTENDDEF:{
                        struct oval_definition *oval_def = oval_criteria_node_get_definition(cnode);
			struct oval_criteria_node *node =  oval_definition_get_criteria(oval_def);
                        return oval_probe_query_criteria(sess, node);
                }
                break;
        case OVAL_NODETYPE_UNKNOWN:
                break;
        }

	/* we shouldn't get here */
        return -1;
}

#if 0
const oval_probe_meta_t * const oval_probe_meta_get(void)
{
    return (const oval_probe_meta_t * const)OSCAP_GSYM(__probe_meta);
}
#endif

void oval_probe_meta_list(FILE *output, int flags)
{
    register size_t i;
    const char *probe_dir;
    char probe_path[PATH_MAX];
    size_t probe_dirlen;
    oval_probe_meta_t *meta = OSCAP_GSYM(__probe_meta);

    if (output == NULL)
	output = stdout;
#if 0
    fprintf(output, "#\n# %-24s %-30s %s", "object", "probe name", "flags");

    if (flags & OVAL_PROBEMETA_LIST_VERBOSE) {
	fprintf(output, " %-5s %s\n#\n", "type", "path");
    } else
	fprintf(output, "\n#\n");
#endif
    probe_dir = oval_probe_ext_getdir();
    assume_d(probe_dir != NULL, /* void */);
    probe_dirlen = strlen(probe_dir);
    assume_r(probe_dirlen + 1 < sizeof probe_path - 1, /* void */);
    strncpy(probe_path, probe_dir, sizeof probe_path - 1);

    probe_path[probe_dirlen  ] = '/';
    probe_path[probe_dirlen+1] = '\0';

    for (i = 0; i < OSCAP_GSYM(__probe_meta_count); ++i) {
	if (meta[i].flags & OVAL_PROBEMETA_EXTERNAL) {
	    strncpy(probe_path + probe_dirlen + 1,
		    meta[i].pname,
		    sizeof probe_path - probe_dirlen + 1);

	    if (flags & OVAL_PROBEMETA_LIST_DYNAMIC) {
		dI("Checking access to \"%s\"\n", probe_path);
		if (access(probe_path, X_OK) != 0) {
		    dW("access: errno=%d, %s\n", errno, strerror(errno));
		    continue;
		}
	    }
	}

	fprintf(output, "%-32s %-32s %c",
		meta[i].stype, meta[i].pname,
		meta[i].flags & OVAL_PROBEMETA_EXTERNAL ? 'E' : '.');

	if (flags & OVAL_PROBEMETA_LIST_VERBOSE) {
	    fprintf(output, " %-5u %s\n",
		    meta[i].otype,
		    meta[i].flags & OVAL_PROBEMETA_EXTERNAL ? probe_path : "");
	} else
	    fprintf(output, "\n");
    }

    return;
}
