#!/usr/bin/env bash

# Author:
#      Peter Vrabec, <pvrabec@redhat.com>


. ${srcdir}/../test_common.sh

# Test Cases.

MITRE_FILES="/tmp/ValidationSupportFiles"
EXTVARFILE="${MITRE_FILES}/External Variables/linux-external-variables.xml"

function test_mitre {

    require "egrep" || return 255
    require "unzip" || return 255

    if [ ! -d "$MITRE_FILES" ]; then	
        /usr/bin/unzip -u ${srcdir}/ValidationSupportFiles.zip -d /tmp
        # workaround file access time issue
        find "$MITRE_FILES"
    fi

    local ret_val=0;
    local DEFFILE=${srcdir}/$1
    local RESFILE=$1.results

    [ -f $RESFILE ] && rm -f $RESFILE
    ../../utils/.libs/oscap oval eval --results "$RESFILE" --variables "$EXTVARFILE"  "$DEFFILE"
    # catch error from oscap tool
    ret_val=$?
    if [ $ret_val -eq 1 ]; then
	return 1
    fi

    if [ -n "$3" ]; then
        # only check the individual results specified in the arg list
        shift 1
        while [ -n "$1" -a -n "$2" ]; do
            def_id="$1"
            def_res="$2"
            grep -q "definition_id=\"$def_id\".*result=\"$def_res\"" "$RESFILE" || return 1
            shift 2
        done
        return 0
    fi

    # assume all definitions should have the same result

    LINES=`grep definition_id "$RESFILE"`
    # catch error from grep
    ret_val=$?
    if [ $ret_val -eq 2 ]; then
	return 1
    fi

    # calculate return code
    echo "$LINES" | grep -q -v "result=\"$2\""
    ret_val=$?
    if [ $ret_val -eq 1 ]; then
        return 0;
    elif [ $ret_val -eq 0 ]; then
	return 1;
    else
	return "$ret_val"
    fi
}

function cleanup {
    rm -rf "$MITRE_FILES"
}


# Testing.
test_init "test_mitre.log"

test_run "ind-def_unknown_test.xml" test_mitre ind-def_unknown_test.xml "unknown"
test_run "ind-def_variable_test.xml" test_mitre ind-def_variable_test.xml "true"
test_run "ind-def_environmentvariable_test.xml" test_mitre ind-def_environmentvariable_test.xml "true"
test_run "ind-def_family_test.xml" test_mitre ind-def_family_test.xml "true"
test_run "ind-def_textfilecontent54_test.xml" test_mitre ind-def_textfilecontent54_test.xml "true"
test_run "ind-def_textfilecontent_test.xml" test_mitre ind-def_textfilecontent_test.xml "true"
test_run "ind-def_xmlfilecontent_test.xml" test_mitre ind-def_xmlfilecontent_test.xml "true"
test_run "ind-def_filehash58_test.xml" test_mitre ind-def_filehash58_test.xml "true"

# does not work because of symplink `/etc/rc' -> `/etc/rc.d/rc' (oval:org.mitre.oval.test:tst:102)
#test_run "ind-def_filehash_test.xml" test_mitre ind-def_filehash_test.xml "true"
#test_run "unix-def_file_test.xml" test_mitre unix-def_file_test.xml "true"

test_run "linux-def_partition_test.xml" test_mitre linux-def_partition_test.xml "true"
test_run "linux-def_rpminfo_test.xml" test_mitre linux-def_rpminfo_test.xml "true"
test_run "linux-def_rpmverify_test.xml" test_mitre linux-def_rpmverify_test.xml "true"
test_run "linux-def_selinuxboolean_test.xml" test_mitre linux-def_selinuxboolean_test.xml "true"
test_run "linux-def_inetlisteningservers_test.xml" test_mitre linux-def_inetlisteningservers_test.xml "true"

test_run "oval_binary_datatype.xml" test_mitre oval_binary_datatype.xml "true"
test_run "oval_boolean_datatype.xml" test_mitre oval_boolean_datatype.xml "true"
test_run "oval_check_enumeration_entity.xml" test_mitre oval_check_enumeration_entity.xml "true"
test_run "oval_check_enumeration_object_state.xml" test_mitre oval_check_enumeration_object_state.xml "true"
test_run "oval-def_arithmetic_function.xml" test_mitre oval-def_arithmetic_function.xml "true"
test_run "oval-def_begin_function.xml" test_mitre oval-def_begin_function.xml "true"
test_run "oval-def_concat_function.xml" test_mitre oval-def_concat_function.xml "true"
test_run "oval-def_constant_variable.xml" test_mitre oval-def_constant_variable.xml "true"
test_run "oval-def_criteria.xml" test_mitre oval-def_criteria.xml "true"
test_run "oval-def_criterion.xml" test_mitre oval-def_criterion.xml "true"
test_run "oval-def_end_function.xml" test_mitre oval-def_end_function.xml "true"
test_run "oval-def_escape_regex_function.xml" test_mitre oval-def_escape_regex_function.xml "true"
test_run "oval-def_extend_definition.xml" test_mitre oval-def_extend_definition.xml "oval:org.mitre.oval.test:def:117" "true" "oval:org.mitre.oval.test:def:97" "true"
test_run "oval-def_external_variable.xml" test_mitre oval-def_external_variable.xml "true"
test_run "oval-def_literal_component.xml" test_mitre oval-def_literal_component.xml "true"
test_run "oval-def_local_variable.xml" test_mitre oval-def_local_variable.xml "true"
test_run "oval-def_object_component.xml" test_mitre oval-def_object_component.xml "true"
test_run "oval-def_regex_capture_function.xml" test_mitre oval-def_regex_capture_function.xml "true"
test_run "oval-def_set.xml" test_mitre oval-def_set.xml "true"
test_run "oval-def_split_function.xml" test_mitre oval-def_split_function.xml "true"

# win_filetime format is currently unsupported, there are possibly some wrong values in the document
#test_run "oval-def_time_difference_function.xml" test_mitre oval-def_time_difference_function.xml "true"

test_run "oval-def_substring_function.xml" test_mitre oval-def_substring_function.xml "true"
test_run "oval-def_variable_component.xml" test_mitre oval-def_variable_component.xml "true"
test_run "oval_evr_string_datatype.xml" test_mitre oval_evr_string_datatype.xml "true"
test_run "oval_existence_enumeration.xml" test_mitre oval_existence_enumeration.xml "true"
test_run "oval_float_datatype.xml" test_mitre oval_float_datatype.xml "true"
test_run "oval_int_datatype.xml" test_mitre oval_int_datatype.xml "true"
test_run "oval_string_datatype.xml" test_mitre oval_string_datatype.xml "true"
test_run "oval_version_datatype.xml" test_mitre oval_version_datatype.xml "true"

test_run "unix-def_password_test.xml" test_mitre unix-def_password_test.xml "true"
test_run "unix-def_process58_test.xml" test_mitre unix-def_process58_test.xml "true"
test_run "unix-def_process_test.xml" test_mitre unix-def_process_test.xml "true"
test_run "unix-def_runlevel_test.xml" test_mitre unix-def_runlevel_test.xml "true"
test_run "unix-def_uname_test.xml" test_mitre unix-def_uname_test.xml "true"

# needs only two interfaces - lo, eth0 - without ipv6 addresses
#test_run "unix-def_interface_test.xml" test_mitre unix-def_interface_test.xml "true"

# root needed
#test_run "unix-def_shadow_test.xml" test_mitre unix-def_shadow_test.xml "true"

# install xinetd, telnet-server and tftp-server in order to test xinetd probe
#test_run "unix-def_xinetd_test.xml" test_mitre unix-def_xinetd_test.xml "true"

# Unsupported objects on Fedora
#test_run "ind-def_ldap_test.xml" test_mitre ind-def_ldap_test.xml "unknown"
#test_run "ind-def_sql_test.xml" test_mitre ind-def_sql_test.xml "unknown"
#test_run "linux-def_slackwarepkginfo_test.xml" test_mitre linux-def_slackwarepkginfo_test.xml "unknown"
#test_run "unix-def_inetd_test.xml" test_mitre unix-def_inetd_test.xml "unknown"
#test_run "linux-def_dpkginfo_test.xml" test_mitre linux-def_dpkginfo_test.xml

test_exit cleanup
