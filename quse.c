/*
 * Copyright 2005 Gentoo Foundation
 * Distributed under the terms of the GNU General Public License v2
 * $Header: /var/cvsroot/gentoo-projects/portage-utils/quse.c,v 1.1 2005/06/07 02:17:24 solar Exp $
 *
 * 2005 Ned Ludd        - <solar@gentoo.org>
 * 2005 Mike Frysinger  - <vapier@gentoo.org>
 *
 ********************************************************************
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 *
 */


int quse_main(int argc, char **argv)
{
	FILE *fp;
	char *p;
	char buf[_POSIX_PATH_MAX];
	char ebuild[_POSIX_PATH_MAX];

	DBG("argc=%d argv[0]=%s argv[1]=%s",
	    argc, argv[0], argc > 1 ? argv[1] : "NULL?");

	initialize_ebuild_flat();	/* sets our pwd to $PORTDIR */
	fp = fopen(".ebuild.x", "r");
	if (!fp)
		return 1;
	while ((fgets(ebuild, sizeof(ebuild), fp)) != NULL) {
		FILE *newfp;
		if ((p = strchr(ebuild, '\n')) != NULL)
			*p = 0;
		if ((newfp = fopen(ebuild, "r")) != NULL) {
			while ((fgets(buf, sizeof(buf), newfp)) != NULL) {
				if ((strncmp(buf, "IUSE=", 5)) == 0) {
					if ((p = strrchr(&buf[6], '"')) != NULL)
						*p = 0;
					if (argc > 1) {
						int i, ok = 0;
						for (i = 1; i < argc; i++) {
							if ((rematch(argv[i], &buf[6], REG_NOSUB)) != 0) {
								ok = 1;
								break;
							}
						}
						if (ok == 0)
							fprintf(stdout, "%s %s\n", ebuild, &buf[6]);
						} else {
							fprintf(stdout, "%s %s\n", ebuild, &buf[6]);
						}
						break;
				}
			}
			fclose(newfp);
		} else {
			if (!reinitialize)
				warnf("(cache update pending) %s : %s", ebuild, strerror(errno));
			reinitialize = 1;
		}
	}
	fclose(fp);
	return 0;
}