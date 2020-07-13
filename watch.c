#include <u.h>
#include <libc.h>
#include <regexp.h>

typedef struct List List;
struct List {
	List *next;
	union {
		Dir;
		Reprog *re;
	};
};

char pwd[1024];
int period = 1000;
int noregroup = 0;
int once = 0;
List *expl;
List *filel;

void
usage(void)
{
	fprint(2, "usage: %s [-G] [[-e expr] ...] [-t sec] [cmd]\n", argv0);
	exits("usage");
}

void*
emalloc(ulong sz)
{
	void *v = malloc(sz);
	if(v == nil)
		sysfatal("malloc: %r");
	setmalloctag(v, getcallerpc(&sz));
	memset(v, 0, sz);
	return v;
}
char*
estrdup(char *s)
{
	if((s = strdup(s)) == nil)
		sysfatal("strdup: %r");
	setmalloctag(s, getcallerpc(&s));
	return s;
}

char*
join(char **arg)
{
	int i;
	char *s, *p;
	
	s = estrdup("");
	for(i = 0; arg[i]; i++){
		p = s;
		if((s = smprint("%s %s", s, arg[i])) == nil)
			sysfatal("smprint: %r");
		free(p);
	}
	return s;
}

void
eadd(char *exp)
{
	List *r;
	
	r = emalloc(sizeof(*r));
	r->re = regcomp(exp);
	r->next = expl;
	expl = r;
}

void
fadd(Dir *d)
{
	List *f;
	
	f = emalloc(sizeof(*f));
	f->Dir = *d;
	f->next = filel;
	filel = f;
}

int
tracked(char *name)
{
	List *r;

	for(r = expl; r; r = r->next)
		if(regexec(r->re, name, nil, 0))
			return 1;
	return 0;
}

int
changed(Dir *d)
{
	List *f;

	for(f = filel; f; f = f->next)
		if(f->type == d->type)
		if(f->dev == d->dev)
		if(f->qid.path == d->qid.path)
		if(f->qid.type == d->qid.type)
			if(f->qid.vers == d->qid.vers)
				return 0;
			else{
				f->Dir = *d;
				return 1;
			}
	fadd(d);
	return 1;
}

void
watch(void)
{
	static int first = 1;
	long fd, n;
	Dir *d, *p;
	
	for(;;){
		sleep(period);
		if((fd = open(pwd, OREAD)) < 0)
			sysfatal("open: %r");
		if((n = dirreadall(fd, &d)) < 0)
			sysfatal("dirreadall: %r");
		close(fd);
		for(p = d; n--; p++){
			if(tracked(p->name)){
				if(first){
					fadd(p);
					continue;
				}
				if(changed(p)){
					free(d);
					return;
				}
			}
		}
		first = 0;
		free(d);
	}
}

void
rc(char *cmd)
{
	Waitmsg *m;

	switch(fork()){
	case -1: sysfatal("fork: %r");
	case 0:
		execl("/bin/rc", "rc", "-c", cmd, nil);
		sysfatal("execl: %r");
	}
	if((m = wait()) && m->msg[0])
		fprint(2, "watch: %s\n", m->msg);
	free(m);
}

void
regroup(void)
{
	char *cmd;
	
	cmd = smprint("cat /proc/%d/noteid >/proc/%d/noteid",
		getppid(), getpid());
	rc(cmd);
	free(cmd);
}

void
main(int argc, char *argv[])
{
	int t;
	char *unit;
	char *cmd;

	cmd = "mk";
	ARGBEGIN{
	case 'e':
		eadd(EARGF(usage())); break;
	case 't':
		t = strtol(EARGF(usage()), &unit, 10);
		if(t < 0)
			t = -t;
		if(unit != nil && strncmp(unit, "ms", 2) == 0)
			period = t;
		else
			period = t*1000;
		break;
	case 'G':
		noregroup = 1; break;
	case '1':
		once = 1; break;
	default: usage();
	}ARGEND;
	if(expl == nil)
		eadd("\.[chsy]$");
	if(argc > 0)
		cmd = join(argv);
	if(getwd(pwd, sizeof pwd) == nil)
		sysfatal("getwd: %r");

	if(noregroup == 0) regroup();
	for(;;){
		watch();
		rc(cmd);
		if(once) exits(nil);
	}
}
