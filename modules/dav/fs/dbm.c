/* ====================================================================
 * The Apache Software License, Version 1.1
 *
 * Copyright (c) 2000 The Apache Software Foundation.  All rights
 * reserved.
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
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Apache Software Foundation (http://www.apache.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Apache" and "Apache Software Foundation" must
 *    not be used to endorse or promote products derived from this
 *    software without prior written permission. For written
 *    permission, please contact apache@apache.org.
 *
 * 5. Products derived from this software may not be called "Apache",
 *    nor may "Apache" appear in their name, without prior written
 *    permission of the Apache Software Foundation.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE APACHE SOFTWARE FOUNDATION OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Apache Software Foundation.  For more
 * information on the Apache Software Foundation, please see
 * <http://www.apache.org/>.
 */

/*
** DAV extension module for Apache 2.0.*
**  - Database support using DBM-style databases,
**    part of the filesystem repository implementation
*/

/*
** This implementation uses a SDBM or GDBM database per file and directory to
** record the properties. These databases are kept in a subdirectory (of
** the directory in question or the directory that holds the file in
** question) named by the macro DAV_FS_STATE_DIR (.DAV). The filename of the
** database is equivalent to the target filename, and is
** DAV_FS_STATE_FILE_FOR_DIR (.state_for_dir) for the directory itself.
*/

#ifdef DAV_USE_GDBM
#include <gdbm.h>
#else

/* ### need to APR-ize */
#include <fcntl.h>		/* for O_RDONLY, O_WRONLY */
#include <sys/stat.h>           /* for S_IRUSR, etc */

#include "sdbm.h"

/* ### this is still needed for sdbm_open()...
 * sdbm should be APR-ized really. */
#ifndef WIN32

#define DAV_FS_MODE_FILE	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)

#else /* WIN32 */

#define DAV_FS_MODE_FILE	(_S_IREAD | _S_IWRITE)

#endif /* WIN32 */

#endif

#include "mod_dav.h"
#include "repos.h"


#ifdef DAV_USE_GDBM

typedef GDBM_FILE dav_dbm_file;

#define DAV_DBM_CLOSE(f)	gdbm_close(f)
#define DAV_DBM_FETCH(f, k)	gdbm_fetch((f), (k))
#define DAV_DBM_STORE(f, k, v)	gdbm_store((f), (k), (v), GDBM_REPLACE)
#define DAV_DBM_DELETE(f, k)	gdbm_delete((f), (k))
#define DAV_DBM_FIRSTKEY(f)	gdbm_firstkey(f)
#define DAV_DBM_NEXTKEY(f, k)	gdbm_nextkey((f), (k))
#define DAV_DBM_CLEARERR(f)	if (0) ; else	/* stop "no effect" warning */
#define DAV_DBM_FREEDATUM(f, d)	((d).dptr ? free((d).dptr) : 0)

#else

typedef DBM *dav_dbm_file;

#define DAV_DBM_CLOSE(f)	sdbm_close(f)
#define DAV_DBM_FETCH(f, k)	sdbm_fetch((f), (k))
#define DAV_DBM_STORE(f, k, v)	sdbm_store((f), (k), (v), DBM_REPLACE)
#define DAV_DBM_DELETE(f, k)	sdbm_delete((f), (k))
#define DAV_DBM_FIRSTKEY(f)	sdbm_firstkey(f)
#define DAV_DBM_NEXTKEY(f, k)	sdbm_nextkey(f)
#define DAV_DBM_CLEARERR(f)	sdbm_clearerr(f)
#define DAV_DBM_FREEDATUM(f, d)	if (0) ; else	/* stop "no effect" warning */

#endif

struct dav_db {
    ap_pool_t *pool;
    dav_dbm_file file;
};

#define D2G(d)	(*(datum*)&(d))


void dav_dbm_get_statefiles(ap_pool_t *p, const char *fname,
			    const char **state1, const char **state2)
{
    char *work;

    if (fname == NULL)
	fname = DAV_FS_STATE_FILE_FOR_DIR;

#ifndef DAV_USE_GDBM
    fname = ap_pstrcat(p, fname, DIRFEXT, NULL);
#endif

    *state1 = fname;

#ifdef DAV_USE_GDBM
    *state2 = NULL;
#else
    {
	int extension;

	work = ap_pstrdup(p, fname);

	/* we know the extension is 4 characters -- len(DIRFEXT) */
	extension = strlen(work) - 4;
	memcpy(&work[extension], PAGFEXT, 4);
	*state2 = work;
    }
#endif
}

static dav_error * dav_fs_dbm_error(dav_db *db, ap_pool_t *p)
{
    int save_errno = errno;
    int errcode;
    const char *errstr;
    dav_error *err;

    p = db ? db->pool : p;

#ifdef DAV_USE_GDBM
    errcode = gdbm_errno;
    errstr = gdbm_strerror(gdbm_errno);
#else
    /* There might not be a <db> if we had problems creating it. */
    errcode = !db || sdbm_error(db->file);
    if (errcode)
	errstr = "I/O error occurred.";
    else
	errstr = "No error.";
#endif

    err = dav_new_error(p, HTTP_INTERNAL_SERVER_ERROR, errcode, errstr);
    err->save_errno = save_errno;
    return err;
}

/* ensure that our state subdirectory is present */
/* ### does this belong here or in dav_fs_repos.c ?? */
void dav_fs_ensure_state_dir(ap_pool_t * p, const char *dirname)
{
    const char *pathname = ap_pstrcat(p, dirname, "/" DAV_FS_STATE_DIR, NULL);

    /* ### do we need to deal with the umask? */

    /* just try to make it, ignoring any resulting errors */
    (void) ap_make_dir(pathname, APR_OS_DEFAULT, p);
}

/* dav_dbm_open_direct:  Opens a *dbm database specified by path.
 *    ro = boolean read-only flag.
 */
dav_error * dav_dbm_open_direct(ap_pool_t *p, const char *pathname, int ro,
				dav_db **pdb)
{
    dav_dbm_file file;

    *pdb = NULL;

    /* NOTE: stupid cast to get rid of "const" on the pathname */
#ifdef DAV_USE_GDBM
    file = gdbm_open((char *) pathname,
		     0,
		     ro ? GDBM_READER : GDBM_WRCREAT,
		     DAV_FS_MODE_FILE,
		     NULL);
#else
    file = sdbm_open((char *) pathname,
		     ro ? O_RDONLY : (O_RDWR | O_CREAT),
		     DAV_FS_MODE_FILE);
#endif

    /* we can't continue if we couldn't open the file and we need to write */
    if (file == NULL && !ro) {
	return dav_fs_dbm_error(NULL, p);
    }

    /* may be NULL if we tried to open a non-existent db as read-only */
    if (file != NULL) {
	/* we have an open database... return it */
	*pdb = ap_pcalloc(p, sizeof(**pdb));
	(*pdb)->pool = p;
	(*pdb)->file = file;
    }

    return NULL;
}

static dav_error * dav_dbm_open(ap_pool_t * p, const dav_resource *resource,
                                int ro, dav_db **pdb)
{
    const char *dirpath;
    const char *fname;
    const char *pathname;

    /* Get directory and filename for resource */
    dav_fs_dir_file_name(resource, &dirpath, &fname);

    /* If not opening read-only, ensure the state dir exists */
    if (!ro) {
	/* ### what are the perf implications of always checking this? */
        dav_fs_ensure_state_dir(p, dirpath);
    }

    pathname = ap_pstrcat(p,
			  dirpath,
			  "/" DAV_FS_STATE_DIR "/",
			  fname ? fname : DAV_FS_STATE_FILE_FOR_DIR,
			  NULL);

    /* ### readers cannot open while a writer has this open; we should
       ### perform a few retries with random pauses. */

    /* ### do we need to deal with the umask? */

    return dav_dbm_open_direct(p, pathname, ro, pdb);
}

static void dav_dbm_close(dav_db *db)
{
    DAV_DBM_CLOSE(db->file);
}

static dav_error * dav_dbm_fetch(dav_db *db, dav_datum key, dav_datum *pvalue)
{
    *(datum *) pvalue = DAV_DBM_FETCH(db->file, D2G(key));

    /* we don't need the error; we have *pvalue to tell */
    DAV_DBM_CLEARERR(db->file);

    return NULL;
}

static dav_error * dav_dbm_store(dav_db *db, dav_datum key, dav_datum value)
{
    int rv;

    rv = DAV_DBM_STORE(db->file, D2G(key), D2G(value));

    /* ### fetch more specific error information? */

    /* we don't need the error; we have rv to tell */
    DAV_DBM_CLEARERR(db->file);

    if (rv == -1) {
	return dav_fs_dbm_error(db, NULL);
    }
    return NULL;
}

static dav_error * dav_dbm_delete(dav_db *db, dav_datum key)
{
    int rv;

    rv = DAV_DBM_DELETE(db->file, D2G(key));

    /* ### fetch more specific error information? */

    /* we don't need the error; we have rv to tell */
    DAV_DBM_CLEARERR(db->file);

    if (rv == -1) {
	return dav_fs_dbm_error(db, NULL);
    }
    return NULL;
}

static int dav_dbm_exists(dav_db *db, dav_datum key)
{
    int exists;

#ifdef DAV_USE_GDBM
    exists = gdbm_exists(db->file, D2G(key)) != 0;
#else
    {
	datum value = sdbm_fetch(db->file, D2G(key));
	sdbm_clearerr(db->file);	/* unneeded */
	exists = value.dptr != NULL;
    }
#endif
    return exists;
}

static dav_error * dav_dbm_firstkey(dav_db *db, dav_datum *pkey)
{
    *(datum *) pkey = DAV_DBM_FIRSTKEY(db->file);

    /* we don't need the error; we have *pkey to tell */
    DAV_DBM_CLEARERR(db->file);

    return NULL;
}

static dav_error * dav_dbm_nextkey(dav_db *db, dav_datum *pkey)
{
    *(datum *) pkey = DAV_DBM_NEXTKEY(db->file, D2G(*pkey));

    /* we don't need the error; we have *pkey to tell */
    DAV_DBM_CLEARERR(db->file);

    return NULL;
}

static void dav_dbm_freedatum(dav_db *db, dav_datum data)
{
    DAV_DBM_FREEDATUM(db, data);
}

const dav_hooks_db dav_hooks_db_dbm =
{
    dav_dbm_open,
    dav_dbm_close,
    dav_dbm_fetch,
    dav_dbm_store,
    dav_dbm_delete,
    dav_dbm_exists,
    dav_dbm_firstkey,
    dav_dbm_nextkey,
    dav_dbm_freedatum,
};
