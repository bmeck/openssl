/*
 * Written by Peter Sylvester (peter.sylvester@edelweb.fr) for the EdelKey
 * project and contributed to the OpenSSL project 2004.
 */
/* ====================================================================
 * Copyright (c) 2004 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_SRP
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <openssl/conf.h>
# include <openssl/bio.h>
# include <openssl/err.h>
# include <openssl/txt_db.h>
# include <openssl/buffer.h>
# include <openssl/srp.h>

# include "apps.h"

# define BASE_SECTION    "srp"
# define CONFIG_FILE "openssl.cnf"

# define ENV_RANDFILE            "RANDFILE"

# define ENV_DATABASE            "srpvfile"
# define ENV_DEFAULT_SRP         "default_srp"

static int get_index(CA_DB *db, char *id, char type)
{
    char **pp;
    int i;
    if (id == NULL)
        return -1;
    if (type == DB_SRP_INDEX)
        for (i = 0; i < sk_OPENSSL_PSTRING_num(db->db->data); i++) {
            pp = sk_OPENSSL_PSTRING_value(db->db->data, i);
            if (pp[DB_srptype][0] == DB_SRP_INDEX
                && !strcmp(id, pp[DB_srpid]))
                return i;
    } else
        for (i = 0; i < sk_OPENSSL_PSTRING_num(db->db->data); i++) {
            pp = sk_OPENSSL_PSTRING_value(db->db->data, i);

            if (pp[DB_srptype][0] != DB_SRP_INDEX
                && !strcmp(id, pp[DB_srpid]))
                return i;
        }

    return -1;
}

static void print_entry(CA_DB *db, int indx, int verbose, char *s)
{
    if (indx >= 0 && verbose) {
        int j;
        char **pp = sk_OPENSSL_PSTRING_value(db->db->data, indx);
        BIO_printf(bio_err, "%s \"%s\"\n", s, pp[DB_srpid]);
        for (j = 0; j < DB_NUMBER; j++) {
            BIO_printf(bio_err, "  %d = \"%s\"\n", j, pp[j]);
        }
    }
}

static void print_index(CA_DB *db, int indexindex, int verbose)
{
    print_entry(db, indexindex, verbose, "g N entry");
}

static void print_user(CA_DB *db, int userindex, int verbose)
{
    if (verbose > 0) {
        char **pp = sk_OPENSSL_PSTRING_value(db->db->data, userindex);

        if (pp[DB_srptype][0] != 'I') {
            print_entry(db, userindex, verbose, "User entry");
            print_entry(db, get_index(db, pp[DB_srpgN], 'I'), verbose,
                        "g N entry");
        }

    }
}

static int update_index(CA_DB *db, char **row)
{
    char **irow;
    int i;

    irow = app_malloc(sizeof(char *) * (DB_NUMBER + 1), "row pointers");
    for (i = 0; i < DB_NUMBER; i++) {
        irow[i] = row[i];
        row[i] = NULL;
    }
    irow[DB_NUMBER] = NULL;

    if (!TXT_DB_insert(db->db, irow)) {
        BIO_printf(bio_err, "failed to update srpvfile\n");
        BIO_printf(bio_err, "TXT_DB error number %ld\n", db->db->error);
        OPENSSL_free(irow);
        return 0;
    }
    return 1;
}

static void lookup_fail(const char *name, const char *tag)
{
    BIO_printf(bio_err, "variable lookup failed for %s::%s\n", name, tag);
}

static char *srp_verify_user(const char *user, const char *srp_verifier,
                             char *srp_usersalt, const char *g, const char *N,
                             const char *passin, int verbose)
{
    char password[1024];
    PW_CB_DATA cb_tmp;
    char *verifier = NULL;
    char *gNid = NULL;

    cb_tmp.prompt_info = user;
    cb_tmp.password = passin;

    if (password_callback(password, 1024, 0, &cb_tmp) > 0) {
        if (verbose)
            BIO_printf(bio_err,
                       "Validating\n   user=\"%s\"\n srp_verifier=\"%s\"\n srp_usersalt=\"%s\"\n g=\"%s\"\n N=\"%s\"\n",
                       user, srp_verifier, srp_usersalt, g, N);
        BIO_printf(bio_err, "Pass %s\n", password);

        OPENSSL_assert(srp_usersalt != NULL);
        if (!
            (gNid =
             SRP_create_verifier(user, password, &srp_usersalt, &verifier, N,
                                 g))) {
            BIO_printf(bio_err, "Internal error validating SRP verifier\n");
        } else {
            if (strcmp(verifier, srp_verifier))
                gNid = NULL;
            OPENSSL_free(verifier);
        }
    }
    return gNid;
}

static char *srp_create_user(char *user, char **srp_verifier,
                             char **srp_usersalt, char *g, char *N,
                             char *passout, int verbose)
{
    char password[1024];
    PW_CB_DATA cb_tmp;
    char *gNid = NULL;
    char *salt = NULL;
    cb_tmp.prompt_info = user;
    cb_tmp.password = passout;

    if (password_callback(password, 1024, 1, &cb_tmp) > 0) {
        if (verbose)
            BIO_printf(bio_err, "Creating\n user=\"%s\"\n g=\"%s\"\n N=\"%s\"\n",
                       user, g, N);
        if (!
            (gNid =
             SRP_create_verifier(user, password, &salt, srp_verifier, N,
                                 g))) {
            BIO_printf(bio_err, "Internal error creating SRP verifier\n");
        } else
            *srp_usersalt = salt;
        if (verbose > 1)
            BIO_printf(bio_err, "gNid=%s salt =\"%s\"\n verifier =\"%s\"\n", gNid,
                       salt, *srp_verifier);

    }
    return gNid;
}

typedef enum OPTION_choice {
    OPT_ERR = -1, OPT_EOF = 0, OPT_HELP,
    OPT_VERBOSE, OPT_CONFIG, OPT_NAME, OPT_SRPVFILE, OPT_ADD,
    OPT_DELETE, OPT_MODIFY, OPT_LIST, OPT_GN, OPT_USERINFO,
    OPT_PASSIN, OPT_PASSOUT, OPT_ENGINE
} OPTION_CHOICE;

OPTIONS srp_options[] = {
    {"help", OPT_HELP, '-', "Display this summary"},
    {"verbose", OPT_VERBOSE, '-', "Talk a lot while doing things"},
    {"config", OPT_CONFIG, '<', "A config file"},
    {"name", OPT_NAME, 's', "The particular srp definition to use"},
    {"srpvfile", OPT_SRPVFILE, '<', "The srp verifier file name"},
    {"add", OPT_ADD, '-', "Add a user and srp verifier"},
    {"modify", OPT_MODIFY, '-',
     "Modify the srp verifier of an existing user"},
    {"delete", OPT_DELETE, '-', "Delete user from verifier file"},
    {"list", OPT_LIST, '-', "List users"},
    {"gn", OPT_GN, 's', "Set g and N values to be used for new verifier"},
    {"userinfo", OPT_USERINFO, 's', "Additional info to be set for user"},
    {"passin", OPT_PASSIN, 's', "Input file pass phrase source"},
    {"passout", OPT_PASSOUT, 's', "Output file pass phrase source"},
# ifndef OPENSSL_NO_ENGINE
    {"engine", OPT_ENGINE, 's', "Use engine, possibly a hardware device"},
# endif
    {NULL}
};

int srp_main(int argc, char **argv)
{
    CA_DB *db = NULL;
    DB_ATTR db_attr;
    CONF *conf = NULL;
    int gNindex = -1, maxgN = -1, ret = 1, errors = 0, verbose =
        0, i, doupdatedb = 0;
    int mode = OPT_ERR;
    char *user = NULL, *passinarg = NULL, *passoutarg = NULL;
    char *passin = NULL, *passout = NULL, *gN = NULL, *userinfo = NULL;
    char *randfile = NULL, *tofree = NULL, *section = NULL;
    char **gNrow = NULL, *configfile = NULL, *dbfile = NULL, **pp, *prog;
    long errorline = -1;
    OPTION_CHOICE o;

    prog = opt_init(argc, argv, srp_options);
    while ((o = opt_next()) != OPT_EOF) {
        switch (o) {
        case OPT_EOF:
        case OPT_ERR:
 opthelp:
            BIO_printf(bio_err, "%s: Use -help for summary.\n", prog);
            goto end;
        case OPT_HELP:
            opt_help(srp_options);
            ret = 0;
            goto end;
        case OPT_VERBOSE:
            verbose++;
            break;
        case OPT_CONFIG:
            configfile = opt_arg();
            break;
        case OPT_NAME:
            section = opt_arg();
            break;
        case OPT_SRPVFILE:
            dbfile = opt_arg();
            break;
        case OPT_ADD:
        case OPT_DELETE:
        case OPT_MODIFY:
        case OPT_LIST:
            if (mode != OPT_ERR) {
                BIO_printf(bio_err,
                           "%s: Only one of -add/delete-modify/-list\n",
                           prog);
                goto opthelp;
            }
            mode = o;
            break;
        case OPT_GN:
            gN = opt_arg();
            break;
        case OPT_USERINFO:
            userinfo = opt_arg();
            break;
        case OPT_PASSIN:
            passinarg = opt_arg();
            break;
        case OPT_PASSOUT:
            passoutarg = opt_arg();
            break;
        case OPT_ENGINE:
            (void)setup_engine(opt_arg(), 0);
            break;
        }
    }
    argc = opt_num_rest();
    argv = opt_rest();

    if (dbfile && configfile) {
        BIO_printf(bio_err,
                   "-dbfile and -configfile cannot be specified together.\n");
        goto end;
    }
    if (mode == OPT_ERR) {
        BIO_printf(bio_err,
                   "Exactly one of the options -add, -delete, -modify -list must be specified.\n");
        goto opthelp;
    }
    if ((mode == OPT_DELETE || mode == OPT_MODIFY || mode == OPT_ADD)
        && argc < 1) {
        BIO_printf(bio_err,
                   "Need at least one user for options -add, -delete, -modify. \n");
        goto opthelp;
    }
    if ((passin || passout) && argc != 1) {
        BIO_printf(bio_err,
                   "-passin, -passout arguments only valid with one user.\n");
        goto opthelp;
    }

    if (!app_passwd(passinarg, passoutarg, &passin, &passout)) {
        BIO_printf(bio_err, "Error getting passwords\n");
        goto end;
    }

    if (!dbfile) {

        /*****************************************************************/
        tofree = NULL;
        if (configfile == NULL)
            configfile = getenv("OPENSSL_CONF");
        if (configfile == NULL)
            configfile = getenv("SSLEAY_CONF");
        if (configfile == NULL) {
            const char *s = X509_get_default_cert_area();
            size_t len = strlen(s) + 1 + sizeof(CONFIG_FILE);

            tofree = app_malloc(len, "config filename space");
# ifdef OPENSSL_SYS_VMS
            strcpy(tofree, s);
# else
            BUF_strlcpy(tofree, s, len);
            BUF_strlcat(tofree, "/", len);
# endif
            BUF_strlcat(tofree, CONFIG_FILE, len);
            configfile = tofree;
        }

        if (verbose)
            BIO_printf(bio_err, "Using configuration from %s\n", configfile);
        conf = NCONF_new(NULL);
        if (NCONF_load(conf, configfile, &errorline) <= 0) {
            if (errorline <= 0)
                BIO_printf(bio_err, "error loading the config file '%s'\n",
                           configfile);
            else
                BIO_printf(bio_err, "error on line %ld of config file '%s'\n",
                           errorline, configfile);
            goto end;
        }
        OPENSSL_free(tofree);
        tofree = NULL;

        /* Lets get the config section we are using */
        if (section == NULL) {
            if (verbose)
                BIO_printf(bio_err,
                           "trying to read " ENV_DEFAULT_SRP
                           " in \" BASE_SECTION \"\n");

            section = NCONF_get_string(conf, BASE_SECTION, ENV_DEFAULT_SRP);
            if (section == NULL) {
                lookup_fail(BASE_SECTION, ENV_DEFAULT_SRP);
                goto end;
            }
        }

        if (randfile == NULL && conf)
            randfile = NCONF_get_string(conf, BASE_SECTION, "RANDFILE");

        if (verbose)
            BIO_printf(bio_err,
                       "trying to read " ENV_DATABASE " in section \"%s\"\n",
                       section);

        if ((dbfile = NCONF_get_string(conf, section, ENV_DATABASE)) == NULL) {
            lookup_fail(section, ENV_DATABASE);
            goto end;
        }

    }
    if (randfile == NULL)
        ERR_clear_error();
    else
        app_RAND_load_file(randfile, 0);

    if (verbose)
        BIO_printf(bio_err, "Trying to read SRP verifier file \"%s\"\n",
                   dbfile);

    db = load_index(dbfile, &db_attr);
    if (db == NULL)
        goto end;

    /* Lets check some fields */
    for (i = 0; i < sk_OPENSSL_PSTRING_num(db->db->data); i++) {
        pp = sk_OPENSSL_PSTRING_value(db->db->data, i);

        if (pp[DB_srptype][0] == DB_SRP_INDEX) {
            maxgN = i;
            if (gNindex < 0 && gN != NULL && !strcmp(gN, pp[DB_srpid]))
                gNindex = i;

            print_index(db, i, verbose > 1);
        }
    }

    if (verbose)
        BIO_printf(bio_err, "Database initialised\n");

    if (gNindex >= 0) {
        gNrow = sk_OPENSSL_PSTRING_value(db->db->data, gNindex);
        print_entry(db, gNindex, verbose > 1, "Default g and N");
    } else if (maxgN > 0 && !SRP_get_default_gN(gN)) {
        BIO_printf(bio_err, "No g and N value for index \"%s\"\n", gN);
        goto end;
    } else {
        if (verbose)
            BIO_printf(bio_err, "Database has no g N information.\n");
        gNrow = NULL;
    }

    if (verbose > 1)
        BIO_printf(bio_err, "Starting user processing\n");

    if (argc > 0)
        user = *(argv++);

    while (mode == OPT_LIST || user) {
        int userindex = -1;
        if (user)
            if (verbose > 1)
                BIO_printf(bio_err, "Processing user \"%s\"\n", user);
        if ((userindex = get_index(db, user, 'U')) >= 0) {
            print_user(db, userindex, (verbose > 0)
                       || mode == OPT_LIST);
        }

        if (mode == OPT_LIST) {
            if (user == NULL) {
                BIO_printf(bio_err, "List all users\n");

                for (i = 0; i < sk_OPENSSL_PSTRING_num(db->db->data); i++) {
                    print_user(db, i, 1);
                }
            } else if (userindex < 0) {
                BIO_printf(bio_err,
                           "user \"%s\" does not exist, ignored. t\n", user);
                errors++;
            }
        } else if (mode == OPT_ADD) {
            if (userindex >= 0) {
                /* reactivation of a new user */
                char **row =
                    sk_OPENSSL_PSTRING_value(db->db->data, userindex);
                BIO_printf(bio_err, "user \"%s\" reactivated.\n", user);
                row[DB_srptype][0] = 'V';

                doupdatedb = 1;
            } else {
                char *row[DB_NUMBER];
                char *gNid;
                row[DB_srpverifier] = NULL;
                row[DB_srpsalt] = NULL;
                row[DB_srpinfo] = NULL;
                if (!
                    (gNid =
                     srp_create_user(user, &(row[DB_srpverifier]),
                                     &(row[DB_srpsalt]),
                                     gNrow ? gNrow[DB_srpsalt] : gN,
                                     gNrow ? gNrow[DB_srpverifier] : NULL,
                                     passout, verbose))) {
                    BIO_printf(bio_err,
                               "Cannot create srp verifier for user \"%s\", operation abandoned .\n",
                               user);
                    errors++;
                    goto end;
                }
                row[DB_srpid] = BUF_strdup(user);
                row[DB_srptype] = BUF_strdup("v");
                row[DB_srpgN] = BUF_strdup(gNid);

                if (!row[DB_srpid] || !row[DB_srpgN] || !row[DB_srptype]
                    || !row[DB_srpverifier] || !row[DB_srpsalt]
                    || (userinfo &&
                         (!(row [DB_srpinfo] = BUF_strdup (userinfo))))
                    || !update_index(db, row)) {
                    OPENSSL_free(row[DB_srpid]);
                    OPENSSL_free(row[DB_srpgN]);
                    OPENSSL_free(row[DB_srpinfo]);
                    OPENSSL_free(row[DB_srptype]);
                    OPENSSL_free(row[DB_srpverifier]);
                    OPENSSL_free(row[DB_srpsalt]);
                    goto end;
                }
                doupdatedb = 1;
            }
        } else if (mode == OPT_MODIFY) {
            if (userindex < 0) {
                BIO_printf(bio_err,
                           "user \"%s\" does not exist, operation ignored.\n",
                           user);
                errors++;
            } else {

                char **row =
                    sk_OPENSSL_PSTRING_value(db->db->data, userindex);
                char type = row[DB_srptype][0];
                if (type == 'v') {
                    BIO_printf(bio_err,
                               "user \"%s\" already updated, operation ignored.\n",
                               user);
                    errors++;
                } else {
                    char *gNid;

                    if (row[DB_srptype][0] == 'V') {
                        int user_gN;
                        char **irow = NULL;
                        if (verbose)
                            BIO_printf(bio_err,
                                       "Verifying password for user \"%s\"\n",
                                       user);
                        if ((user_gN =
                             get_index(db, row[DB_srpgN], DB_SRP_INDEX)) >= 0)
                            irow =
                                sk_OPENSSL_PSTRING_value(db->db->data,
                                                         userindex);

                        if (!srp_verify_user
                            (user, row[DB_srpverifier], row[DB_srpsalt],
                             irow ? irow[DB_srpsalt] : row[DB_srpgN],
                             irow ? irow[DB_srpverifier] : NULL, passin,
                             verbose)) {
                            BIO_printf(bio_err,
                                       "Invalid password for user \"%s\", operation abandoned.\n",
                                       user);
                            errors++;
                            goto end;
                        }
                    }
                    if (verbose)
                        BIO_printf(bio_err, "Password for user \"%s\" ok.\n",
                                   user);

                    if (!
                        (gNid =
                         srp_create_user(user, &(row[DB_srpverifier]),
                                         &(row[DB_srpsalt]),
                                         gNrow ? gNrow[DB_srpsalt] : NULL,
                                         gNrow ? gNrow[DB_srpverifier] : NULL,
                                         passout, verbose))) {
                        BIO_printf(bio_err,
                                   "Cannot create srp verifier for user \"%s\", operation abandoned.\n",
                                   user);
                        errors++;
                        goto end;
                    }

                    row[DB_srptype][0] = 'v';
                    row[DB_srpgN] = BUF_strdup(gNid);

                    if (!row[DB_srpid] || !row[DB_srpgN] || !row[DB_srptype]
                        || !row[DB_srpverifier] || !row[DB_srpsalt]
                        || (userinfo
                            && (!(row[DB_srpinfo] = BUF_strdup(userinfo)))))
                        goto end;

                    doupdatedb = 1;
                }
            }
        } else if (mode == OPT_DELETE) {
            if (userindex < 0) {
                BIO_printf(bio_err,
                           "user \"%s\" does not exist, operation ignored. t\n",
                           user);
                errors++;
            } else {
                char **xpp =
                    sk_OPENSSL_PSTRING_value(db->db->data, userindex);
                BIO_printf(bio_err, "user \"%s\" revoked. t\n", user);

                xpp[DB_srptype][0] = 'R';

                doupdatedb = 1;
            }
        }
        if (--argc > 0)
            user = *(argv++);
        else {
            user = NULL;
        }
    }

    if (verbose)
        BIO_printf(bio_err, "User procession done.\n");

    if (doupdatedb) {
        /* Lets check some fields */
        for (i = 0; i < sk_OPENSSL_PSTRING_num(db->db->data); i++) {
            pp = sk_OPENSSL_PSTRING_value(db->db->data, i);

            if (pp[DB_srptype][0] == 'v') {
                pp[DB_srptype][0] = 'V';
                print_user(db, i, verbose);
            }
        }

        if (verbose)
            BIO_printf(bio_err, "Trying to update srpvfile.\n");
        if (!save_index(dbfile, "new", db))
            goto end;

        if (verbose)
            BIO_printf(bio_err, "Temporary srpvfile created.\n");
        if (!rotate_index(dbfile, "new", "old"))
            goto end;

        if (verbose)
            BIO_printf(bio_err, "srpvfile updated.\n");
    }

    ret = (errors != 0);
 end:
    if (errors != 0)
        if (verbose)
            BIO_printf(bio_err, "User errors %d.\n", errors);

    if (verbose)
        BIO_printf(bio_err, "SRP terminating with code %d.\n", ret);
    OPENSSL_free(tofree);
    if (ret)
        ERR_print_errors(bio_err);
    if (randfile)
        app_RAND_write_file(randfile);
    NCONF_free(conf);
    free_index(db);
    OBJ_cleanup();
    return (ret);
}

#endif
