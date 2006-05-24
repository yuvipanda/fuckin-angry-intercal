/* ick.h -- compilation types for intercal parser */

/* Comment this out if your version of lex automatically supplies yylineno. */
/*#define YYLINENO_BY_HAND*/

/* Comment this out if your version of lex doesn't use yyrestart(). */
/*#define USE_YYRESTART*/

#define YY_NO_UNPUT

typedef int	bool;
#define TRUE	1
#define FALSE	0

#define ALLOC_CHUNK	256

/*
 * We choose this value for maximum number of possible variables on
 * the theory that no human mind could grok a more complex INTERCAL
 * program than this and still retain any vestige of sanity.
#define MAXVARS		100
 */

/*
 * Maximum supported statement types; should be equal to (FROM - GETS + 1)
 * AIS: Changed this when I added new statements.
 */
#define MAXTYPES	23

/* AIS: Maximum supported spark/ears nesting, divided by 32. The value given
   allows for 256 nested spark/ears groupings, which should be enough. */
#define SENESTMAX       8

enum onceagain {onceagain_NORMAL, onceagain_ONCE, onceagain_AGAIN}; /* AIS */

typedef struct node_t
{
    int			opcode;		/* operator or type code */
    unsigned long	constant;	/* constant data attached */
    unsigned long       optdata;        /* AIS: Temp used by the optimizer */
    int			width;		/* is this 32-bit data? */
    struct node_t	*lval, *rval;	/* attached expression nodes */
} node;

typedef struct tuple_t
{
    unsigned int	label;		/* label # of this statement */
    unsigned int	ncomefrom;	/* AIS: How many noncomputed COME FROMS
					   have this line as a suck-point */
    int         	exechance;	/* chance of execution, initial abstain,
					   (AIS) MAYBE details */
    bool                maybe;          /* AIS: Where MAYBE details go when
					   exechance has been parsed */
    bool                abstainable;    /* AIS: Is it possible for this line to
					   be abstained from? */
    bool                initabstain;    /* AIS: Is this line initially
					   abstained from? */
    bool                nextable;       /* AIS: Can this line be a NEXT
					   target? */
    bool                optversion;     /* AIS: Use an optimized version? (Only
					   set if the optimizer thinks that
					   it's safe.) */
    bool                warn112:1;      /* AIS: Should this line produce warning
					   112 during degeneration? */
    bool warn128:1, warn534:1, warn018:1, warn016:1, warn276:1, warn239:1,
      warn622:1; /* AIS: As warn112. The warnings are a bitfield to save space. */
    unsigned int	type;		/* statement type */
    struct
    { /* AIS: Struct, not union needed because ABSTAIN expr FROM (line) has both */
	unsigned int	target;		/* for NEXT statements */
	node		*node;		/* attached expression node(s) */
    } u;
    unsigned int        nexttarget;     /* AIS: The target tuple of a NEXT must
					   also be recorded for optimizef */
    int lineno; 			/* source line for error messages */
    bool sharedline;			/* if NZ, two statements on a line */
    enum onceagain onceagainflag;       /* AIS: ONCE / AGAIN */
} tuple;

/* this maps the `external' name of a variable to an internal array index */
typedef struct
{
    int	type;
    int extindex;
    int intindex;
    int ignorable; /* AIS: Can this variable be IGNOREd? */
}
atom;

typedef struct
{
    int	value;
    char *name;
}
assoc;

extern atom *oblist, *obdex;
extern int obcount, nonespots, ntwospots, ntails, nhybrids;

extern tuple *tuples;
extern int tuplecount;

extern tuple *optuple; /* AIS: The tuple currently being optimized */

extern char *enablers[MAXTYPES];
extern assoc vartypes[];

/* the lexical analyzer keeps copies of the source logical lines */
extern char **textlines;
extern int textlinecount;
extern int yylineno;

/* AIS: These are needed to sort out a grammar near-ambiguity */
extern unsigned long sparkearsstack[SENESTMAX];
extern int sparkearslev;

/* compilation options */
extern bool compile_only;  /* just compile into C, don't run the linker */
extern bool nocompilerbug; /* set possibility of E774 to zero */
extern int traditional;    /* compile as INTERCAL-72 */
extern int yydebug;        /* print debugging information while parsing */

extern int politesse;


/* AIS: I added these */
extern int yukdebug;       /* debug the code with yuk */
extern int yukprofile;     /* profile the code with yuk */
extern int compucomecount; /* number of computed COME FROMs */
extern int compucomesused; /* are computed COME FROMs used? */
extern int multithread;    /* is the program multithreaded? */
extern int coreonerr;      /* dump core on E778? */
extern int optdebug;       /* debug the optimizer */
extern int flowoptimize;   /* optimize program flow */
extern int checkforbugs;   /* check for bugs */
extern int coopt;          /* constant-output optimizer */

/* ick.h ends here */
