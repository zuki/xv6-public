/* UNIX V7 source code: see /COPYRIGHT or www.tuhs.org for details. */

/*
 * Print working (current) directory
 */
#include	<stdio.h>
#include	<stdlib.h>
#include 	"stat.h"
#include	"user.h"
#include	"fs.h"

char	dot[]	= ".";
char	dotdot[] = "..";
char	name[512];
int	file;
int	off	= -1;
struct	stat	d, dd;
struct	dirent	dir;

/* func definision */
void prname(void);
void cat(void);

int
main(void)
{
	int rdev;
	uint rino;

	xv6_stat("/", &d);
	rdev = d.dev;
	rino = d.ino;
	for (;;) {
		xv6_stat(dot, &d);
		if (d.ino==rino && d.dev==rdev)
			prname();
		if ((file = open(dotdot,0)) < 0) {
			fprintf(stderr,"pwd: cannot open ..\n");
			exit(1);
		}
		fstat(file, &dd);
		chdir(dotdot);
		if(d.dev == dd.dev) {
			if(d.ino == dd.ino)
				prname();
			do
				if (read(file, (char *)&dir, sizeof(dir)) < sizeof(dir)) {
					fprintf(stderr,"read error in ..\n");
					exit(1);
				}
			while (dir.inum != d.ino);
		}
		else do {
				if(read(file, (char *)&dir, sizeof(dir)) < sizeof(dir)) {
					fprintf(stderr,"read error in ..\n");
					exit(1);
				}
				xv6_stat(dir.name, &dd);
			} while(dd.ino != d.ino || dd.dev != d.dev);
		close(file);
		cat();
	}
}

void
prname(void)
{
	write(1, "/", 1);
	if (off<0)
		off = 0;
	name[off] = '\n';
	write(1, name, off+1);
	exit(0);
}

void
cat(void)
{
	register int i, j;

	i = -1;
	while (dir.name[++i] != 0);
	if ((off+i+2) > 511)
		prname();
	for(j=off+1; j>=0; --j)
		name[j+i+1] = name[j];
	off=i+off+1;
	name[i] = '/';
	for(--i; i>=0; --i)
		name[i] = dir.name[i];
}
