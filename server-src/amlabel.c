/*
 * Amanda, The Advanced Maryland Automatic Network Disk Archiver
 * Copyright (c) 1991-1998, 2000 University of Maryland at College Park
 * All Rights Reserved.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of U.M. not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  U.M. makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * U.M. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING ALL
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL U.M.
 * BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors: the Amanda Development Team.  Its members are listed in a
 * file named AUTHORS, in the root directory of this distribution.
 */
/*
 * $Id: amlabel.c,v 1.53 2006/07/25 18:27:57 martinea Exp $
 *
 * write an Amanda label on a tape
 */
#include "amanda.h"
#include "conffile.h"
#include "tapefile.h"
#include "changer.h"
#include <device.h>
#include <timestamp.h>
#include <taperscan.h>

/* local functions */

int main(int argc, char **argv);

static void usage(void) {
    fprintf(stderr, _("Usage: %s [-f] <conf> <label> [slot <slot-number>] [-o configoption]*\n"),
	    get_pname());
    exit(1);
}

static void print_read_label_status_error(ReadLabelStatusFlags status) {
    char ** status_strv;

    if (status == READ_LABEL_STATUS_SUCCESS)
        return;

    status_strv = g_flags_nick_to_strv(status,
                                       READ_LABEL_STATUS_FLAGS_TYPE);
    g_assert(g_strv_length(status_strv) > 0);
    if (g_strv_length(status_strv) == 1) {
        printf("Error was %s.\n", *status_strv);
    } else {
        char * status_list = g_english_strjoinv(status_strv, "or");
        printf("Error was one of %s.\n", status_list);
        amfree(status_list);
    }
    g_strfreev(status_strv);
}

int
main(
    int		argc,
    char **	argv)
{
    char *conffile;
    char *conf_tapelist;
    char *outslot = NULL;
    char *label, *tapename = NULL;
    char *labelstr, *slotstr;
    char *conf_tapelist_old;
    unsigned long malloc_hist_1, malloc_size_1;
    unsigned long malloc_hist_2, malloc_size_2;
    int have_changer;
    int force, tape_ok;
    tapetype_t *tape;
    size_t tt_blocksize_kb;
    int slotcommand;
    int    new_argc;
    char **new_argv;
    Device * device;
    ReadLabelStatusFlags label_status;

    /*
     * Configure program for internationalization:
     *   1) Only set the message locale for now.
     *   2) Set textdomain for all amanda related programs to "amanda"
     *      We don't want to be forced to support dozens of message catalogs.
     */  
    setlocale(LC_MESSAGES, "C");
    textdomain("amanda"); 

    safe_fd(-1, 0);
    safe_cd();

    set_pname("amlabel");

    dbopen(DBG_SUBDIR_SERVER);
    device_api_init();

    /* Don't die when child closes pipe */
    signal(SIGPIPE, SIG_IGN);

    malloc_size_1 = malloc_inuse(&malloc_hist_1);

    erroutput_type = ERR_INTERACTIVE;

    parse_conf(argc, argv, &new_argc, &new_argv);

    if(new_argc > 1 && strcmp(new_argv[1],"-f") == 0)
	 force=1;
    else force=0;

    if(new_argc != 3+force && new_argc != 5+force)
	usage();

    config_name = new_argv[1+force];
    label = new_argv[2+force];

    if(new_argc == 5+force) {
	if(strcmp(new_argv[3+force], "slot"))
	    usage();
	slotstr = new_argv[4+force];
	slotcommand = 1;
    } else {
	slotstr = "current";
	slotcommand = 0;
    }

    config_dir = vstralloc(CONFIG_DIR, "/", config_name, "/", NULL);
    conffile = stralloc2(config_dir, CONFFILE_NAME);
    if (read_conffile(conffile)) {
	error(_("errors processing config file \"%s\""), conffile);
	/*NOTREACHED*/
    }

    check_running_as(RUNNING_AS_DUMPUSER | RUNNING_WITHOUT_SETUID);

    dbrename(config_name, DBG_SUBDIR_SERVER);

    report_bad_conf_arg();

    conf_tapelist = getconf_str(CNF_TAPELIST);
    if (*conf_tapelist == '/') {
	conf_tapelist = stralloc(conf_tapelist);
    } else {
	conf_tapelist = stralloc2(config_dir, conf_tapelist);
    }
    if (read_tapelist(conf_tapelist)) {
	error(_("could not load tapelist \"%s\""), conf_tapelist);
	/*NOTREACHED*/
    }

    labelstr = getconf_str(CNF_LABELSTR);

    if(!match(labelstr, label)) {
	error(_("label %s doesn't match labelstr \"%s\""), label, labelstr);
	/*NOTREACHED*/
    }

    if((lookup_tapelabel(label))!=NULL) {
	if(!force) {
	    error(_("label %s already on a tape\n"),label);
	    /*NOTREACHED*/
    	}
    }
    tape = lookup_tapetype(getconf_str(CNF_TAPETYPE));
    tt_blocksize_kb = (size_t)tapetype_get_blocksize(tape);

    if((have_changer = changer_init()) == 0) {
	if(slotcommand) {
	    fprintf(stderr,
	     _("%s: no tpchanger specified in \"%s\", so slot command invalid\n"),
		    new_argv[0], conffile);
	    usage();
	}
	tapename = getconf_str(CNF_TAPEDEV);
	if (tapename == NULL) {
	    error(_("No tapedev specified"));
	}
    } else if(have_changer != 1) {
	error(_("changer initialization failed: %s"), strerror(errno));
	/*NOTREACHED*/
    } else {
	if(changer_loadslot(slotstr, &outslot, &tapename)) {
	    error(_("could not load slot \"%s\": %s"), slotstr, changer_resultstr);
	    /*NOTREACHED*/
	}

	printf(_("labeling tape in slot %s (%s):\n"), outslot, tapename);
    }

    tape_ok=1;
    printf("Reading label...\n");fflush(stdout);
    device = device_open(tapename);
    if (device == NULL) {
        error("Could not open device %s.\n", tapename);
    }
    
    device_set_startup_properties_from_config(device);
    label_status = device_read_label(device);

    if (label_status & READ_LABEL_STATUS_VOLUME_UNLABELED) {
        printf("Found an unlabeled tape.\n");
    } else if (label_status != READ_LABEL_STATUS_SUCCESS) {
        printf("Reading the tape label failed: \n  ");
        print_read_label_status_error(label_status);
        tape_ok = 0;
    } else {
	/* got an amanda tape */
	printf(_("Found Amanda tape %s"),device->volume_label);
	if(match(labelstr, device->volume_label) == 0) {
	    printf(_(", but it is not from configuration %s."), config_name);
	    if(!force)
		tape_ok=0;
	} else {
	    if((lookup_tapelabel(device->volume_label)) != NULL) {
		printf(_(", tape is active"));
		if(!force)
		    tape_ok=0;
	    }
	}
	printf("\n");
    }

    if(tape_ok) {
	char *timestamp = NULL;

	printf(_("Writing label %s..\n"), label); fflush(stdout);
        
	timestamp = get_undef_timestamp();
        if (!device_start(device, ACCESS_WRITE, label, timestamp)) {
	    error(_("Error writing label.\n"));
            g_assert_not_reached();
	} else if (!device_finish(device)) {
            error(_("Error closing device.\n"));
            g_assert_not_reached();
        }
	amfree(timestamp);

        printf(_("Checking label...\n")); fflush(stdout);

        label_status = device_read_label(device);
        if (label_status != READ_LABEL_STATUS_SUCCESS) {
            printf("Checking the tape label failed: \n  ");
            print_read_label_status_error(label_status);
            exit(EXIT_FAILURE);
        } else if (device->volume_label == NULL) {
            error(_("no label found.\n"));
            g_assert_not_reached();
        } else if (strcmp(device->volume_label, label) != 0) {
            error(_("Read back a different label: Got %s, but expected %s\n"),
                  device->volume_label, label);
            g_assert_not_reached();
        } else if (get_timestamp_state(device->volume_time) !=
                   TIME_STATE_UNDEF) {
            error(_("Read the right label, but the wrong timestamp: "
                    "Got %s, expected X.\n"), device->volume_time);
            g_assert_not_reached();
        }
        
        /* write tape list */
        
        /* make a copy */
        conf_tapelist_old = stralloc2(conf_tapelist, ".amlabel");
        if(write_tapelist(conf_tapelist_old)) {
            error(_("couldn't write tapelist: %s"), strerror(errno));
            /*NOTREACHED*/
        }
        amfree(conf_tapelist_old);
        
        /* XXX add cur_tape number to tape list structure */
        remove_tapelabel(label);
        add_tapelabel("0", label);
        if(write_tapelist(conf_tapelist)) {
            error(_("couldn't write tapelist: %s"), strerror(errno));
            /*NOTREACHED*/
        }
        
        printf(_("Success!\n"));
    } else {
	printf(_("\ntape not labeled\n"));
    }
    
    g_object_unref(device);
    device = NULL;

    clear_tapelist();
    free_new_argv(new_argc, new_argv);
    free_server_config();
    amfree(outslot);
    amfree(conffile);
    amfree(conf_tapelist);
    amfree(config_dir);
    config_name=NULL;
    dbclose();

    malloc_size_2 = malloc_inuse(&malloc_hist_2);

    if(malloc_size_1 != malloc_size_2) {
	malloc_list(fileno(stderr), malloc_hist_1, malloc_hist_2);
    }

    return 0;
}
