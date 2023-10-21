#include <u.h>
#include <libc.h>

#include <ctype.h>

#include <fcall.h>
#include <thread.h>
#include <9p.h>


/* 9p filesystem functions */
typedef struct Devfile Devfile;

void	initfs(char *dirname);

void	fsstart(Srv *);
void	fsread(Req *r);
void	fswrite(Req *r);
void	fsend(Srv *);


/* I2C device functions */
void	openi2cdev(void);
void	initi2cdev(void);
void	deiniti2cdev(void);
void	closei2cdev(void);

void	openi2cdevmain(void);
void	closei2cdevmain(void);


/* program logic functions, called from fs functions using I2C functions */
char*	fsreadctl(Req *r);
char*	fswritectl(Req *r);

char*	fsreadco2e(Req *r);
char*	fswriteco2e(Req *r);

char*	fsreadtvoc(Req *r);
char*	fswritetvoc(Req *r);

char*	fsreadiaq(Req *r);
char*	fswriteiaq(Req *r);

char*	fsreadrawh2(Req *r);
char*	fswriterawh2(Req *r);

char*	fsreadrawethanol(Req *r);
char*	fswriterawethanol(Req *r);

char*	fsreadraw(Req *r);
char*	fswriteraw(Req *r);

char*	fsreadall(Req *r);
char*	fswriteall(Req *r);


/* I2C device functions, implemented calls and processing */
int 	readCommand(uchar *cmdArr, ushort *resArr, uint resLen, uint timeout, int i2cfd);
int 	writeCommand(uchar *cmdArr, int cmdLen);


typedef struct iaqMeasurments iaqMeasurments;
typedef struct rawMeasurments rawMeasurments;


/* additional logic finctions */
int		sgp30getserial(void);
int		sgp30checkfeatureset(void);
int		sgp30softreset(void);
int		sgp30measuretest(void);
int		sgp30iaqinit(void);
int		sgp30measureiaq(void);
int		sgp30measureraw(void);
int		sgp30measureall(int i2cfd);
uchar	sgp30crc(uchar *data, int len);


static void	timerproc(void *c);


Srv fs = {
	.start = fsstart,
	.read = fsread,
	.write = fswrite,
	.end = fsend,
};

struct Devfile {
	char	*name;
	char*	(*fsread)(Req*);
	char*	(*fswrite)(Req*);
	int	mode;
};

Devfile files[] = {
	{ "ctl", fsreadctl, fswritectl, DMEXCL|0666 },
	{ "co2e", fsreadco2e, fswriteco2e, DMEXCL|0444 },
    { "tvoc", fsreadtvoc, fswritetvoc, DMEXCL|0444 },
	{ "iaq", fsreadiaq, fswriteiaq, DMEXCL|0444 },
	{ "raw_h2", fsreadrawh2, fswriterawh2, DMEXCL|0444 },
	{ "raw_ethanol", fsreadrawethanol, fswriterawethanol, DMEXCL|0444 },
	{ "raw", fsreadraw, fswriteraw, DMEXCL|0444 },
	{ "all", fsreadall, fswriteall, DMEXCL|0444 },
};


struct iaqMeasurments {
	ushort co2e;
	ushort tvoc;
	// long time;
};

struct rawMeasurments {
	ushort raw_h2;
	ushort raw_ethanol;
};


enum
{
	reset
};

static char *cmds[] = {
	[reset]		= "reset",
	nil
};



static int i2cfd;
static int i2cfdmain;

static long long sgp30serial;

static iaqMeasurments iaqdat;
static rawMeasurments rawdat;

static int std;		/* threaded deamon - sample continiously for internal IAQ conpensation to work as documented */


void
initfs(char *dirname)
{
	File *devdir;
	int i;

	fs.tree = alloctree(nil, nil, 0555, nil);
	if (fs.tree == nil) {
		sysfatal("initfs: alloctree: %r");
	}

	if ((devdir = createfile(fs.tree->root, dirname, nil, DMDIR|0555, nil)) == nil) {
		sysfatal("initfs: createfile: %s: %r", dirname);
	}

	for (i = 0; i < nelem(files); i++) {
		if (createfile(devdir, files[i].name, nil, files[i].mode, files + i) == nil) {
			sysfatal("initfs: createfile: %s: %r", files[i].name);
		}
	}
}


void
fsstart(Srv *)
{
	openi2cdev();
	initi2cdev();
}

void
fsread(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->fsread(r));
}

void
fswrite(Req *r)
{
	Devfile *f;

	r->ofcall.count = 0;
	f = r->fid->file->aux;
	respond(r, f->fswrite(r));
}

void
fsend(Srv *)
{
	deiniti2cdev();
	closei2cdev();
}


char*
fsreadctl(Req *r)
{
	char out[128];

	snprint(out, sizeof(out), "serial(hex): 0x%llx\n", sgp30serial);

	readstr(r, out);
	return nil;
}

char*
fswritectl(Req *r)
{
	int si, i, cmd;
	char *s;

	cmd = -1;

	si = 0;
	s = r->ifcall.data;

	/* clear whitespace before command */
	for(;(si<r->ifcall.count)&&(isspace(*s));){
		si++;
		s++;
	}

	/* search for command string */
	for(i=0; cmds[i]!=nil; i++){
		if(strncmp(cmds[i], s, strlen(cmds[i])) == 0){
			s = s + strlen(cmds[i]);
			cmd = i;
			break;
		}
	}

	/* clear whitespace before parameter */
	for(;(si<r->ifcall.count)&&(isspace(*s));){
		si++;
		s++;
	}

	switch (cmd)
	{
	case reset:
		/* no parameters here */
		/* perform soft reset - might reset other devices if they are listening for General Call */
		/* ignore reset status - if we're here it was able to reset on init */
		sgp30softreset();
		sgp30iaqinit();
		break;

	default:
		break;
	}

	return nil;
}

char*
fsreadco2e(Req *r)
{
	char out[16];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureiaq();
	}

	snprint(out, sizeof(out), "%d ppm\n", iaqdat.co2e);

	readstr(r, out);
	return nil;
}

char*
fswriteco2e(Req *r)
{
	/* no writing to co2e */
	USED(r);
	return nil;
}

char*
fsreadtvoc(Req *r)
{
	char out[16];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureiaq();
	}

	snprint(out, sizeof(out), "%d ppm\n", iaqdat.tvoc);

	readstr(r, out);
	return nil;
}

char*
fswritetvoc(Req *r)
{
	/* no writing to tvoc */
	USED(r);
	return nil;
}

char*
fsreadiaq(Req *r)
{
	char out[32];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureiaq();
	}
	
	snprint(out, sizeof(out), "co2e(ppm):\t%d\ttvoc(ppm):\t%d\n", iaqdat.co2e, iaqdat.tvoc);

	readstr(r, out);
	return nil;
}

char*
fswriteiaq(Req *r)
{
	/* no writing to iaq */
	USED(r);
	return nil;
}

char*
fsreadrawh2(Req *r)
{
	char out[16];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureraw();
	}

	snprint(out, sizeof(out), "%d units\n", rawdat.raw_h2);

	readstr(r, out);
	return nil;
}

char*
fswriterawh2(Req *r)
{
	/* no writing to raw_h2 */
	USED(r);
	return nil;
}

char*
fsreadrawethanol(Req *r)
{
	char out[16];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureraw();
	}

	snprint(out, sizeof(out), "%d units\n", rawdat.raw_ethanol);

	readstr(r, out);
	return nil;
}

char*
fswriterawethanol(Req *r)
{
	/* no writing to raw_ethanol */
	USED(r);
	return nil;
}

char*
fsreadraw(Req *r)
{
	char out[64];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureraw();
	}

	snprint(out, sizeof(out), "raw_h2(units):\t%d\traw_ethanol(units):\t%d\n", rawdat.raw_h2, rawdat.raw_ethanol);

	readstr(r, out);
	return nil;
}

char*
fswriteraw(Req *r)
{
	/* no writing to iaq */
	USED(r);
	return nil;
}

char*
fsreadall(Req *r)
{
	char out[128];

	if (0 == std) {
		/* on demand - no continious sampling */
		sgp30measureiaq();
		sgp30measureraw();
	}

	snprint(out, sizeof(out), "co2e(ppm):\t%d\ttvoc(ppm):\t%d\traw_h2(units):\t%d\traw_ethanol(units):\t%d\n", iaqdat.co2e, iaqdat.tvoc, rawdat.raw_h2, rawdat.raw_ethanol);

	readstr(r, out);
	return nil;
}

char*
fswriteall(Req *r)
{
	/* no writing to all */
	USED(r);
	return nil;
}


int
readCommand(uchar *cmdArr, ushort *resArr, uint resLen, uint timeout, int i2cfdp)
{
	/* for reading the command is ALWAYS 2 bytes */

	int i;
	int ret = 0;
	int rawResLen = resLen * 3;		/* sgp30 word length (2 bytes) + crc (1 byte) */

	if (pwrite(i2cfdp, cmdArr, 2, 0) != 2) {
		/* didn't write in all the data - can't trust the transaction */
		ret = -1;
	}

	sleep(timeout);

	if (0 == rawResLen) {
		return ret;
	}

	/* only read if we expect something - avoids malloc(0) */

	uchar *rawResArr = malloc(rawResLen*sizeof(uchar));

	memset(rawResArr, 0, rawResLen);

	if (pread(i2cfdp, rawResArr, rawResLen, 0) != rawResLen) {
		/* didn't read in all the data - can't trust the transaction */
		ret = -1;
	}

	for (i = 0; i < resLen; i++) {
		if(sgp30crc(&rawResArr[i*3], 2) != rawResArr[i*3+2]){
			/* CRC doesn't match - can't trust the data */
			ret = -1;
			break;
		}

		resArr[i] = (ushort)(rawResArr[i*3] << 8) | (ushort)(rawResArr[i*3+1]);
	}

	free(rawResArr);

	return ret;
}

int
writeCommand(uchar *cmdArr, int cmdLen)
{
	USED(cmdArr);
	USED(cmdLen);
	return 0;
}


int
sgp30getserial(void)
{
	#define RES_SER_LEN 3

	uchar cmd[2] = {0x36, 0x82};
	ushort res[RES_SER_LEN];
	int i;

	sgp30serial = 0;

	if (0 != readCommand(cmd, res, RES_SER_LEN, 10, i2cfd)) {
		return -1;
	}

	for(i = 0; i < RES_SER_LEN; i++){
		sgp30serial = sgp30serial << 16 | (long long)(res[i]);
	}

	return 0;
}

int
sgp30checkfeatureset(void)
{
	#define RES_FEAT_LEN 1

	uchar cmd[2] = {0x20, 0x2f};
	ushort res[RES_FEAT_LEN];

	if (0 != readCommand(cmd, res, RES_FEAT_LEN, 10, i2cfd)) {
		return -1;
	}

	/* expected 0x0020 + reserved for version - tested with hardware reporting 0x0022 */
	if (0 == (0x0020 & res[0])) {
		return -1;
	}

	return 0;
}

int
sgp30softreset(void)
{
	uchar cmd[2] = {0x20, 0x2f};

	
	iaqdat.co2e = 0;
	iaqdat.tvoc = 0;

	rawdat.raw_h2 = 0;
	rawdat.raw_ethanol = 0;

	if (0 != readCommand(cmd, nil, 0, 100, i2cfd)) {
		return -1;
	}

	return 0;
}

int
sgp30measuretest(void)
{
	#define RES_TEST_LEN 1

	uchar cmd[2] = {0x20, 0x32};
	ushort res[RES_TEST_LEN];

	if (0 != readCommand(cmd, res, RES_TEST_LEN, 220, i2cfd)) {
		return -1;
	}

	/* expected 0xD400 */
	if (0xD400 != res[0]) {
		return -1;
	}

	return 0;
}

int
sgp30iaqinit(void)
{
	uchar cmd[2] = {0x20, 0x03};

	if (0 != readCommand(cmd, nil, 0, 10, i2cfd)) {
		return -1;
	}

	return 0;
}

int
sgp30measureiaq(void)
{
	#define RES_IAQ_LEN 2

	uchar cmd[2] = {0x20, 0x08};
	ushort res[RES_IAQ_LEN];

	if (0 != readCommand(cmd, res, RES_IAQ_LEN, 12, i2cfd)) {
		return -1;
	}

	iaqdat.co2e = res[0];
	iaqdat.tvoc = res[1];

	return 0;
}

int
sgp30measureraw(void)
{
	#define RES_RAW_LEN 2

	uchar cmd[2] = {0x20, 0x50};
	ushort res[RES_RAW_LEN];

	if (0 != readCommand(cmd, res, RES_RAW_LEN, 25, i2cfd)) {
		return -1;
	}

	rawdat.raw_h2 = res[0];
	rawdat.raw_ethanol = res[1];

	return 0;
}

int
sgp30measureall(int i2cfdp)
{
	#define RES_ALL_LEN 2

	uchar cmd[2];
	ushort res[RES_ALL_LEN];


	/* get iaq data */
	cmd[0] = 0x20;
	cmd[1] = 0x08;

	if (0 != readCommand(cmd, res, RES_ALL_LEN, 12, i2cfdp)) {
		return -1;
	}

	iaqdat.co2e = res[0];
	iaqdat.tvoc = res[1];

	
	/* get raw data */
	cmd[0] = 0x20;
	cmd[1] = 0x50;

	if (0 != readCommand(cmd, res, RES_ALL_LEN, 25, i2cfdp)) {
		return -1;
	}

	rawdat.raw_h2 = res[0];
	rawdat.raw_ethanol = res[1];

	return 0;
}

uchar
sgp30crc(uchar *data, int len)
{
	/* calculate CRC-8 based on parameters from datasheet*/

	int i, b;

	uchar crc = 0xFF;	/* provided start value*/

	for (i = 0; i < len; i++) {
		crc ^= data[i];

		for (b = 0; b < 8; b++) {
			if ((crc & 0x80) != 0) {
				crc <<= 1;
                crc = crc ^ 0x31;	/* provided polynomial seed */
            }
			else {
                crc <<= 1;
            }
		}
	}

	return crc;
}


void
openi2cdev(void)
{
	i2cfd = -1;

	/* default location of sgp30 device is 0x58*/
	if (access("/dev/i2c.58.data", 0) != 0) {
		if (bind("#J58", "/dev", MBEFORE) < 0) {
		    sysfatal("no J58 device");
        }
    }
	i2cfd = open("/dev/i2c.58.data", ORDWR);
	if (i2cfd < 0) {
		sysfatal("cannot open i2c.58.data file");
    }
}

void
initi2cdev(void)
{
	if (0 != sgp30getserial()) {
		/* could not get serial number from device - assume failed communication */
		sysfatal("could not communicate with sgp30 device");
	}

	if (0 != sgp30checkfeatureset()) {
		/* could not validate sgp30 device feature set - assume wrong device */
		sysfatal("could not validate sgp30 device feature set");
	}


	/* unnecessary reset to known sate(?) - documentation is sparce - may reset other devices(!) */
	if (0 != sgp30softreset()) {
		/* should not happen if I2C is working - has no return */
		sysfatal("could not reset sgp30 device");
	}

	/* unnecessary (production line testing) - but since we're already here */
	if (0 != sgp30measuretest()) {
		/* self test failed - assume defective device */
		sysfatal("sgp30 device failed self test");
	}


	/* init the sensor */
	if (0 != sgp30iaqinit()) {
		/* should not happen if I2C is working - has no return */
		sysfatal("could not init sgp30 device");
	}
}

void
deiniti2cdev(void)
{
	/* nothing really to do wuth this device */
	return;
}

void
closei2cdev(void)
{
	close(i2cfd);

	unmount("#J58", "/dev");
}


void
openi2cdevmain(void)
{
	i2cfdmain = -1;

	/* default location of sgp30 device is 0x58*/
	if (access("/dev/i2c.58.data", 0) != 0) {
		if (bind("#J58", "/dev", MBEFORE) < 0) {
		    sysfatal("no J58 device in main thread");
        }
    }
	i2cfdmain = open("/dev/i2c.58.data", ORDWR);
	if (i2cfdmain < 0) {
		sysfatal("cannot open i2c.58.data file in main thread");
    }
}

void
closei2cdevmain(void)
{
	close(i2cfdmain);

	unmount("#J58", "/dev");
}


static void
timerproc(void *c)
{
	threadsetname("timer");

	for (;;) {
		sleep(1000);	/* real all every 1 second - according to documentation */
		sendul(c, 0);
	}
}


void
usage(void)
{
	fprint(2, "usage: %s [-d] [-m mntpt] [-s srvname]\n", argv0);
	threadexitsall("usage");
}


void
threadmain(int argc, char *argv[])
{
	char *srvname, *mntpt;

	srvname = "sgp30";
	mntpt = "/mnt";

	std = 0;	/* state tracking initialization */


	enum {
		Etimer,
		Eend,
	};

	Alt a[] = {
		[Etimer] = { nil, nil, CHANRCV },
		[Eend] = { nil, nil, CHANEND },
	};

	a[Etimer].c = chancreate(sizeof(ulong), 0);


	ARGBEGIN {
	case 'd':
		std = 1;
		break;
	case 'm':
		mntpt = ARGF();
		break;
	case 's':
		srvname = ARGF();
		break;
	default:
		usage();
	} ARGEND


	initfs(srvname);

	threadpostmountsrv(&fs, srvname, mntpt, MBEFORE);


	if (std == 1) {
		openi2cdevmain();	/* hacky hack to get access to I2C device in main thread (threading and namespaces are interesting) */

		proccreate(timerproc, a[Etimer].c, 4096);

		for(;;){
			switch(alt(a)){

			case Etimer:
				sgp30measureall(i2cfdmain);

			/* hacky hack to quiet the compiler about unreacheble code */
			// default:
			// 	continue;
			}

			/* hacky hack to quiet the compiler about unreacheble code */
			if (0) {
				break;
			}
		}

		closei2cdevmain();
	}


	threadexits(nil);
}
