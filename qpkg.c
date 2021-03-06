/*
 * Copyright 2005-2014 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 *
 * Copyright 2005-2010 Ned Ludd	- <solar@gentoo.org>
 * Copyright 2005-2014 Mike Frysinger  - <vapier@gentoo.org>
 */

#ifdef APPLET_qpkg

#include <fnmatch.h>

#define QPKG_FLAGS "cEpP:" COMMON_FLAGS
static struct option const qpkg_long_opts[] = {
	{"clean",    no_argument, NULL, 'c'},
	{"eclean",   no_argument, NULL, 'E'},
	{"pretend",  no_argument, NULL, 'p'},
	{"pkgdir",    a_argument, NULL, 'P'},
	COMMON_LONG_OPTS
};
static const char * const qpkg_opts_help[] = {
	"clean pkgdir of unused binary files",
	"clean pkgdir of files not in the tree anymore (slow)",
	"pretend only",
	"alternate package directory",
	COMMON_OPTS_HELP
};
#define qpkg_usage(ret) usage(ret, QPKG_FLAGS, qpkg_long_opts, qpkg_opts_help, NULL, lookup_applet_idx("qpkg"))

extern char pretend;

static char *qpkg_bindir = NULL;
static int eclean = 0;

/* global functions */
int filter_tbz2(const struct dirent *);
uint64_t qpkg_clean_dir(char *, queue *);
int qpkg_clean(char *);
const char *qpkg_get_bindir(void);
int qpkg_make(depend_atom *);

/* checks to make sure this is a .tbz2 file. used by scandir() */
int filter_tbz2(const struct dirent *dentry)
{
	if (dentry->d_name[0] == '.')
		return 0;
	if (strlen(dentry->d_name) < 6)
		return 0;
	return !strcmp(".tbz2", dentry->d_name + strlen(dentry->d_name) - 5);
}

/* process a single dir for cleaning. dir can be a $PKGDIR, $PKGDIR/All/, $PKGDIR/$CAT */
uint64_t qpkg_clean_dir(char *dirp, queue *vdb)
{
	queue *ll;
	struct dirent **fnames;
	int i, count;
	char buf[_Q_PATH_MAX];
	struct stat st;
	uint64_t num_all_bytes = 0;
	size_t disp_units = 0;

	if (dirp == NULL)
		return 0;
	if (chdir(dirp) != 0)
		return 0;
	if ((count = scandir(".", &fnames, filter_tbz2, alphasort)) < 0)
		return 0;

	for (i = 0; i < count; i++) {
		int del = 1;
		fnames[i]->d_name[strlen(fnames[i]->d_name)-5] = 0;
		for (ll = vdb; ll != NULL; ll = ll->next) {
			if (1) {
				if (strcmp(fnames[i]->d_name, basename(ll->name)) == 0) {
					del = 0;
					break;
				}
			}
		}
		if (!del)
			continue;
		snprintf(buf, sizeof(buf), "%s.tbz2", fnames[i]->d_name);
		if (lstat(buf, &st) != -1) {
			if (S_ISREG(st.st_mode)) {
				disp_units = KILOBYTE;
				if ((st.st_size / KILOBYTE) > 1000)
					disp_units = MEGABYTE;
				num_all_bytes += st.st_size;
				qprintf(" %s[%s%s %3s %s %s%s]%s %s%s/%s%s\n", DKBLUE, NORM, GREEN, make_human_readable_str(st.st_size, 1, disp_units),
					disp_units == MEGABYTE ? "M" : "K", NORM, DKBLUE, NORM, CYAN, basename(dirp), fnames[i]->d_name, NORM);
			}
			if (!pretend)
				unlink(buf);
		}
	}

	scandir_free(fnames, count);

	return num_all_bytes;
}

/* figure out what dirs we want to process for cleaning and display results. */
int qpkg_clean(char *dirp)
{
	FILE *fp;
	int i, count;
	size_t disp_units = 0;
	uint64_t num_all_bytes;
	struct dirent **dnames;
	queue *vdb;

	vdb = get_vdb_atoms(1);

	if (chdir(dirp) != 0) {
		free_sets(vdb);
		return 1;
	}
	if ((count = scandir(".", &dnames, filter_hidden, alphasort)) < 0) {
		free_sets(vdb);
		return 1;
	}

	if (eclean) {
		size_t n;
		const char *overlay;

		array_for_each(overlays, n, overlay) {
			fp = fopen(initialize_flat(overlay, CACHE_EBUILD, false), "re");
			if (fp == NULL)
				continue;

			size_t buflen;
			char *buf;

			buf = NULL;
			while (getline(&buf, &buflen, fp) != -1) {
				char *name, *p;
				if ((p = strrchr(buf, '.')) == NULL)
					continue;
				*p = 0;
				if ((p = strrchr(buf, '/')) == NULL)
					continue;
				*p = 0;
				name = p + 1;
				if ((p = strrchr(buf, '/')) == NULL)
					continue;
				*p = 0;
				/* these strcat() are safe. the name is extracted from buf already. */
				strcat(buf, "/");
				strcat(buf, name);

				/* num_all_bytes will be off when pretend and eclean are enabled together */
				/* vdb = del_set(buf, vdb, &i); */
				vdb = add_set(buf, vdb);
			}

			free(buf);
			fclose(fp);
		}
	}

	num_all_bytes = qpkg_clean_dir(dirp, vdb);

	for (i = 0; i < count; i++) {
		char buf[_Q_PATH_MAX];
		snprintf(buf, sizeof(buf), "%s/%s", dirp, dnames[i]->d_name);
		num_all_bytes += qpkg_clean_dir(buf, vdb);
	}
	scandir_free(dnames, count);

	free_sets(vdb);

	disp_units = KILOBYTE;
	if ((num_all_bytes / KILOBYTE) > 1000)
		disp_units = MEGABYTE;
	qprintf(" %s*%s Total space that would be freed in packages directory: %s%s %c%s\n", GREEN, NORM, RED,
		make_human_readable_str(num_all_bytes, 1, disp_units), disp_units == MEGABYTE ? 'M' : 'K', NORM);

	return 0;
}

const char *qpkg_get_bindir(void)
{
	if (qpkg_bindir != NULL)
		return qpkg_bindir;
	if (getuid() == 0)
		return "/var/tmp/binpkgs";
	if (getenv("HOME") == NULL)
		errp("Your $HOME env var isn't set, aborting");
	xasprintf(&qpkg_bindir, "%s/binpkgs", getenv("HOME"));

	return qpkg_bindir;
}

int check_pkg_install_mask(char *name);
int check_pkg_install_mask(char *name)
{
	int i, iargc, ret;
	char **iargv;

	i = iargc = ret = 0;

	if (*name != '/')
		return ret;

	makeargv(pkg_install_mask, &iargc, &iargv);

	for (i = 1; i < iargc; i++) {
		if (fnmatch(iargv[i], name, 0) != 0)
			continue;
		ret = 1;
		break;
	}
	freeargv(iargc, iargv);
	return ret;
}

int qpkg_make(depend_atom *atom)
{
	FILE *fp, *out;
	char tmpdir[BUFSIZE], filelist[BUFSIZE], xpak[BUFSIZE], tbz2[BUFSIZE];
	size_t buflen;
	char *buf;
	int i;
	char *xpak_argv[2];
	struct stat st;

	if (pretend) {
		printf(" %s-%s %s/%s:\n", GREEN, NORM, atom->CATEGORY, atom_to_pvr(atom));
		return 0;
	}

	buflen = _Q_PATH_MAX;
	buf = xmalloc(buflen);

	snprintf(buf, buflen, "%s/%s/%s/CONTENTS", portvdb, atom->CATEGORY, atom_to_pvr(atom));
	if ((fp = fopen(buf, "r")) == NULL)
		return -1;

	snprintf(tmpdir, sizeof(tmpdir), "%s/qpkg.XXXXXX", qpkg_get_bindir());
	if ((i = mkstemp(tmpdir)) == -1)
		return -2;
	close(i);
	unlink(tmpdir);
	if (mkdir(tmpdir, 0750))
		return -3;

	snprintf(filelist, sizeof(filelist), "%s/filelist", tmpdir);
	if ((out = fopen(filelist, "w")) == NULL)
		return -4;

	while (getline(&buf, &buflen, fp) != -1) {
		contents_entry *e;
		e = contents_parse_line(buf);
		if (!e || e->type == CONTENTS_DIR)
			continue;
		if (check_pkg_install_mask(e->name) != 0)
			continue;
		fprintf(out, "%s\n", e->name+1); /* dont output leading / */
		if (e->type == CONTENTS_OBJ && verbose) {
			char *hash = (char *)hash_file(e->name, HASH_MD5);
			if (hash != NULL) {
				if (strcmp(e->digest, hash) != 0)
					warn("MD5: mismatch expected %s got %s for %s", e->digest, hash, e->name);
				free(hash);
			}
		}
	}

	fclose(out);
	fclose(fp);

	printf(" %s-%s %s/%s: ", GREEN, NORM, atom->CATEGORY, atom_to_pvr(atom));
	fflush(stdout);

	snprintf(tbz2, sizeof(tbz2), "%s/bin.tar.bz2", tmpdir);
	snprintf(buf, buflen, "tar jcf '%s' --files-from='%s' --no-recursion >/dev/null 2>&1", tbz2, filelist);
	if ((fp = popen(buf, "r")) == NULL)
		return 2;
	pclose(fp);

	snprintf(xpak, sizeof(xpak), "%s/inf.xpak", tmpdir);
	snprintf(buf, buflen, "%s/%s/%s", portvdb, atom->CATEGORY, atom_to_pvr(atom));
	xpak_argv[0] = buf;
	xpak_argv[1] = NULL;
	xpak_create(AT_FDCWD, xpak, 1, xpak_argv);

	snprintf(buf, buflen, "%s/binpkg.tbz2", tmpdir);
	tbz2_compose(AT_FDCWD, tbz2, xpak, buf);

	unlink(filelist);
	unlink(xpak);
	unlink(tbz2);

	snprintf(tbz2, sizeof(tbz2), "%s/%s.tbz2", qpkg_get_bindir(), atom_to_pvr(atom));
	if (rename(buf, tbz2)) {
		warnp("could not move '%s' to '%s'", buf, tbz2);
		return 1;
	}

	rmdir(tmpdir);

	stat(tbz2, &st);
	printf("%s%s%s kB\n", RED, make_human_readable_str(st.st_size, 1, KILOBYTE), NORM);

	return 0;
}

int qpkg_main(int argc, char **argv)
{
	q_vdb_ctx *ctx;
	q_vdb_cat_ctx *cat_ctx;
	q_vdb_pkg_ctx *pkg_ctx;
	size_t s, pkgs_made;
	int i;
	struct stat st;
	char buf[BUFSIZE];
	const char *bindir;
	depend_atom *atom;
	int restrict_chmod = 0;
	int qclean = 0;

	while ((i = GETOPT_LONG(QPKG, qpkg, "")) != -1) {
		switch (i) {
		case 'E': eclean = qclean = 1; break;
		case 'c': qclean = 1; break;
		case 'p': pretend = 1; break;
		case 'P':
			restrict_chmod = 1;
			qpkg_bindir = xstrdup(optarg);
			if (access(qpkg_bindir, W_OK) != 0)
				errp("%s", qpkg_bindir);
			break;
		COMMON_GETOPTS_CASES(qpkg)
		}
	}
	if (qclean)
		return qpkg_clean(qpkg_bindir == NULL ? pkgdir : qpkg_bindir);

	if (argc == optind)
		qpkg_usage(EXIT_FAILURE);

	/* setup temp dirs */
	i = 0;
	bindir = qpkg_get_bindir();
	if (*bindir != '/')
		err("'%s' is not a valid package destination", bindir);
retry_mkdir:
	if (mkdir(bindir, 0750) == -1) {
		lstat(bindir, &st);
		if (!S_ISDIR(st.st_mode)) {
			unlink(bindir);
			if (!i++) goto retry_mkdir;
			errp("could not create temp bindir '%s'", bindir);
		}
		if (!restrict_chmod)
			if (chmod(bindir, 0750))
				errp("could not chmod(0750) temp bindir '%s'", bindir);
	}

	/* we have to change to the root so that we can feed the full paths
	 * to tar when we create the binary package. */
	xchdir(portroot);

	/* first process any arguments which point to /var/db/pkg */
	pkgs_made = 0;
	s = strlen(portvdb);
	for (i = optind; i < argc; ++i) {
		size_t asize = strlen(argv[i]);
		if (asize == 0) {
			argv[i] = NULL;
			continue;
		}
		if (argv[i][asize-1] == '/')
			argv[i][asize-1] = '\0';
		if (!strncmp(portvdb, argv[i], s))
			memmove(argv[i], argv[i]+s+1, asize-s);
		else if (argv[i][0] == '/' && !strncmp(portvdb, argv[i]+1, s))
			memmove(argv[i], argv[i]+s+2, asize-s-1);
		else
			continue;

		atom = atom_explode(argv[i]);
		if (atom) {
			if (!qpkg_make(atom)) ++pkgs_made;
			atom_implode(atom);
		} else
			warn("could not explode '%s'", argv[i]);
		argv[i] = NULL;
	}

	/* now try to run through vdb and locate matches for user inputs */
	ctx = q_vdb_open();
	if (!ctx)
		return EXIT_FAILURE;

	/* scan all the categories */
	while ((cat_ctx = q_vdb_next_cat(ctx))) {
		/* scan all the packages in this category */
		const char *catname = cat_ctx->name;
		while ((pkg_ctx = q_vdb_next_pkg(cat_ctx))) {
			const char *pkgname = pkg_ctx->name;

			/* see if user wants any of these packages */
			snprintf(buf, sizeof(buf), "%s/%s", catname, pkgname);
			atom = atom_explode(buf);
			if (!atom) {
				warn("could not explode '%s'", buf);
				goto next_pkg;
			}
			snprintf(buf, sizeof(buf), "%s/%s", atom->CATEGORY, atom->PN);
			for (i = optind; i < argc; ++i) {
				if (!argv[i]) continue;

				if (!strcmp(argv[i], atom->PN) || !strcmp(argv[i], atom->P) || !strcmp(argv[i], buf) || !strcmp(argv[i], "world"))
					if (!qpkg_make(atom)) ++pkgs_made;
			}
			atom_implode(atom);

 next_pkg:
			q_vdb_close_pkg(pkg_ctx);
		}
	}

	s = (argc - optind) - pkgs_made;
	if (s && !pretend)
		printf(" %s*%s %i package%s could not be matched :/\n", RED, NORM, (int)s, (s > 1 ? "s" : ""));
	if (pkgs_made)
		qprintf(" %s*%s Packages can be found in %s\n", GREEN, NORM, bindir);

	return (pkgs_made ? EXIT_SUCCESS : EXIT_FAILURE);
}

#else
DEFINE_APPLET_STUB(qpkg)
#endif
