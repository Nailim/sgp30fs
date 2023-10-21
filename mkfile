</$objtype/mkfile

ALL=sgp30fs

all:V:	$ALL

sgp30fs:		sgp30fs.$O
	$LD $LDFLAGS -o sgp30fs sgp30fs.$O

sgp30fs.$O:	sgp30fs.c
	$CC $CFLAGS sgp30fs.c

clean:V:
	rm -f *.$O sgp30fs
