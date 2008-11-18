/****************************************************************************

NAME
    yuk.c -- C-INTERCAL debugger and profiler code, linked into programs.

DESCRIPTION
    File linked into programs as a debugger or profiler.

LICENSE TERMS
    Copyright (C) 2006 Alex Smith

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

****************************************************************************/
#include "config.h" /* AIS: Generated by autoconf */
#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
# ifdef TIME_WITH_SYS_TIME
#  include <time.h>
# endif
#else
# include <time.h>
#endif
#include <signal.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif
#include <setjmp.h>
#include <string.h>

#define YUKDEBUG 1

#include "yuk.h"
#include "ick_lose.h"
#include "sizes.h"
#include "abcess.h"
#include "uncommon.h"

#if YPTIMERTYPE == 4
#include <sys/times.h>
#endif

extern signed char onewatch[];
extern signed char twowatch[];
extern ick_type16      oneold[];
extern ick_type32      twoold[];

char** globalargv;
int globalargc;
int yuklines = 0;
int yukloop = 0;
int yukcommands = 0; /* these 5 lines because externs must be defined
			somewhere */
/* Global variable storage types:
   static: limited to the file
   unadorned: defining an extern
   extern: defined elsewhere (unless initialised) */

static char buf[21];

static sig_atomic_t singlestep = 1; /* if 0, run until a breakpoint */
static sig_atomic_t writelines = 1; /* whether to display executed lines onscreen */
static int breakpoints[80]; /* initialised to all 0s. Breakpoint locations */
static int nbreakpoints = 1; /* how many breakpoints we have */
static int monitors[80]; /* monitors give a message when program flow passes them */
static int nmonitors = 0;
static int untilnext = -1; /* NEXTING level to break the program at */
static int firstrun = 1; /* ick_first time an interactive command point is reached */
static int yukerrcmdg = -1; /* the aboff that indicates an error in the 'g' command */

static yptimer tickcount; /* yptimer of last run */
static int lastaboff = 0; /* last value of aboff */

static void handlesigint(int i)
{ /* this is a signal handler, so can't do much */
  singlestep = 1;
  writelines = 1;
  /*@-noeffect@*/ (void) i; /*@=noeffect@*/
}

#if YPTIMERTYPE==1 || YPTIMERTYPE==2
static yptimer yukgettimeofday()
{
  static struct timeval tp;
  yptimer temp;
  /* gettimeofday is POSIX; config.sh has checked that it's
     available, so turn off the unrecog warning */
  /*@-unrecog@*/
  gettimeofday(&tp,0);
  /*@=unrecog@*/
  temp=(yptimer)tp.tv_usec +
    (yptimer)tp.tv_sec * (yptimer)1000000LU;
  /* here we make use of unsigned wraparound. In the case
     YPTIMERTYPE == 1, it seems quite likely that we're going
     to wraparound, but because everything is cast to the
     unsigned integral type yptimer, we get a value that will
     wraparound in such a way that - will give us the correct
     time interval. */
  return temp;
}
#elif YPTIMERTYPE == 4
yptimer yuktimes()
{
  static struct tms tp;
  times(&tp);
  return tp.tms_utime + tp.tms_stime;
}
#elif YPTIMERTYPE == 5
static yptimer yukclock_gettime()
{
  static struct timespec ts;
  yptimer temp;
  /* We've checked that this function is available; -lrt will be linked in. */
  /*@-unrecog@*/
#if defined(_POSIX_CPUTIME) && _POSIX_CPUTIME > 0
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);
#else
# if defined(_POSIX_THREAD_CPUTIME) && _POSIX_THREAD_CPUTIME > 0
  clock_gettime(CLOCK_THREAD_CPUTIME_ID,&ts);
# else
#  if defined(_POSIX_MONOTONIC_CLOCK) && _POSIX_MONOTONIC_CLOCK > 0
  clock_gettime(CLOCK_MONOTONIC,&ts);
#  else
#   ifndef CLOCK_REALTIME
#    error clock_gettime is defined, but no clocks seem to be; try changing YPTIMERTYPE in src/yuk.h
#   endif
  clock_gettime(CLOCK_REALTIME,&ts);
#  endif
# endif
#endif
  /*@=unrecog@*/
  temp=(yptimer)ts.tv_nsec +
    (yptimer)ts.tv_sec * (yptimer)1000000LU;
  /* using wraparound as with gettimeofday */
  return temp;
}
#endif /* YPTIMERTYPE */

void yukterm(void)
{
  int i,lastline,thisline,inrow=0;
  yptimer avgtime,avgtime2;
  if(yukopts==2) (void) puts("Program ended without error.");
  if(!(yukopts&1)) return;
  /* Print profiling information */
  (void) puts("Profiling information saved to \"yuk.out\".");
  (void) freopen("yuk.out","w",stdout);
  i=-1;
  lastline=-1;
  while(++i<yukcommands)
  {
    thisline=lineofaboff[i];
    if(thisline==lastline) inrow++; else
    {
      inrow=1;
      printf("%5d:\t%s\n",thisline,textlines[thisline]);
    }
    lastline=thisline;
    avgtime=ypexecount[i]+ypabscount[i];
    if(avgtime) avgtime=ypexectime[i]/avgtime;
    avgtime2=0;
    if(ypexecount[i]) avgtime2=ypexectime[i]/ypexecount[i];
    /*@-formatcode@*/ /* Splint doesn't understand string interpolation */
    printf("C%d: Time%4" YPTIMERTFORMAT ".%0" YPTIMERTFORMATD
	   ", Avg%2" YPTIMERTFORMAT ".%0" YPTIMERTFORMATD ", "
	   "Avg Exec%2" YPTIMERTFORMAT ".%0" YPTIMERTFORMATD
	   ", Exec%8" YPCOUNTERTFORMAT ", Abs%8" YPCOUNTERTFORMAT "\n",
	   inrow,ypexectime[i]/YPTIMERSCALE, ypexectime[i]%YPTIMERSCALE,
	   avgtime/YPTIMERSCALE, avgtime%YPTIMERSCALE,
	   avgtime2/YPTIMERSCALE, avgtime2%YPTIMERSCALE,
	   ypexecount[i],ypabscount[i]);
    /*@=formatcode@*/
  }
}

void yukline(int aboff,int emitlineno)
{ /* this runs every time a source line is encountered */
  int i;
  int broken; /* hit a breakpoint, monitorpoint, viewbreak, until */
  int keeplooping;
  int templine;
  int tempcmd;
  int temp;
  yptimer temptick;
  char* text=textlines[lineofaboff[aboff]];
  char copyloc[BUFSIZ+9];
  const char* tempcharp;
  if(!yukopts) return;
  if(globalargc!=3)
    ick_lose(IE778,emitlineno,(const char*)NULL);
  if(yukopts & 1)
  { /* profile */
    temptick=YPGETTIME;
    if(lastaboff) ypexectime[lastaboff]+=temptick-tickcount;
    tickcount=temptick;
    if(ick_abstained[aboff]) ypabscount[aboff]++;
    else ypexecount[aboff]++;
    lastaboff=aboff;
  }
  if(yukopts & 2)
  { /* debug */
    if(firstrun)
    {
      /* GNU GPL requires a copyright notice at this point */
      (void) puts("yuk debugger Copyright (C) 2006 Alex Smith.");
      (void) puts("The yuk debugger is covered by the GNU GPL and can");
      (void) puts("be redistributed freely, but comes with ABSOLUTELY");
      (void) puts("NO WARRANTY. For more information, type *<RET>.\n");
      (void) puts("For help on yuk, type ?<RET>.\n");
    }
    i=nbreakpoints;
    broken=0;
    while(i--) broken|=breakpoints[i]==lineofaboff[aboff];
    if(yukloop&&broken&&*breakpoints!=lineofaboff[aboff]) broken=0;
    if(broken)
    {
      if(*breakpoints!=lineofaboff[aboff])
	printf("Breakpoint hit at line %d:\n",lineofaboff[aboff]);
      singlestep=1;
    }
    else
    {
      i=nmonitors;
      while(i--) broken|=monitors[i]==lineofaboff[aboff];
      if(yukloop) broken=0;
      if(broken) printf("Command flowed past line %d:\n",lineofaboff[aboff]);
    }
    if(ick_nextindex <= untilnext)
    {
      broken = 1;
      singlestep = 1;
    }
    if(!broken&&yukerrcmdg==aboff)
    {
      singlestep = 1;
      (void) puts("There are no commands on that line.");
      /* To the user, nothing will have happened but the error message! */
    }
    i=-1;
    while(++i,1)
    {
      if(yukvars[i].vartype==YUKEND) break;
      if(yukvars[i].vartype==ick_ONESPOT)
      {
	if(onewatch[yukvars[i].intername] != (char)0)
	{
	  if(ick_onespots[yukvars[i].intername]!=oneold[yukvars[i].intername]&&
	     onewatch[yukvars[i].intername]>(char)1)
	  {
	    oneold[yukvars[i].intername]=ick_onespots[yukvars[i].intername];
	    if(onewatch[yukvars[i].intername]==(char)2||!ick_onespots[yukvars[i].intername])
	    {
	      /*@-formatconst@*/ /* it's safe, I checked it */
	      printf(onewatch[yukvars[i].intername]==(char)2?
		     "Variable .%d changed.\n":"Variable .%d became 0.\n",
		     yukvars[i].extername);
	      /*@=formatconst@*/
	      broken=1; singlestep=1;
	    }
	  }
	  if(writelines||broken)
	  {
	    printf(".%d is:\n",yukvars[i].extername);
	    ick_pout(ick_onespots[yukvars[i].intername]);
	  }
	}
      }
      if(yukvars[i].vartype==ick_TWOSPOT)
      {
	if(twowatch[yukvars[i].intername] != (char)0)
	{
	  if(ick_twospots[yukvars[i].intername]!=twoold[yukvars[i].intername]&&
	     twowatch[yukvars[i].intername] > (char)1)
	  {
	    twoold[yukvars[i].intername]=ick_twospots[yukvars[i].intername];
	    if(twowatch[yukvars[i].intername]==(char)2||!ick_twospots[yukvars[i].intername])
	    {
	      /*@-formatconst@*/ /* there's just the one %d each way round */
	      printf(twowatch[yukvars[i].intername]==(char)2?
		     "Variable :%d changed.\n":"Variable :%d became 0.\n",
		     yukvars[i].extername);
	      /*@=formatconst@*/
	      broken=1; singlestep=1;
	    }
	  }
	  if(writelines||broken)
	  {
	    printf(":%d is:\n",yukvars[i].extername);
	    ick_pout(ick_twospots[yukvars[i].intername]);
	  }
	}
      }
    }    
    if(writelines||broken)
    {
      /* write line that we're on */
      printf("%5d:\t%s\n",lineofaboff[aboff],text);
      /* write command within line that we're on */
      tempcmd=aboff;
      while(tempcmd&&lineofaboff[--tempcmd]==lineofaboff[aboff]);
      printf("On C%d: Abstained %d time%s\n",aboff?aboff-tempcmd:1,
	     ick_abstained[aboff]-yukloop,ick_abstained[aboff]-yukloop==1?".":"s.");
    }
    if(singlestep)
    {
      (void)signal(SIGINT,handlesigint); /* placing this line here means that a rapid
					    ^C^C will terminate the program, if it's
					    stuck in a loop somewhere */
      keeplooping = 1;
      breakpoints[0] = 0; /* breakpoints[0] goes whenever a breakpoint is hit */
      untilnext = -1;
      if(yukloop)
      { /* reverse the abstentions that g caused */
	i = -1;
	while(++i<yukcommands) ick_abstained[i]--;
      }
      yukloop = 0;
      yukerrcmdg = -1;
      do
      {
	printf("yuk007 "); /* this is our prompt, a sort of reverse
			      INTERCAL version of % */
	(void) fgets(buf,20,stdin);
	if(!strchr(buf,'\n'))
	{
	  ick_lose(IE810,emitlineno,(const char*)NULL);
	}
	templine=0;
	switch(*buf)
	{
	case '?':
	case '@':
	  (void) puts("a<line>\tabstain once from all non-ick_abstained commands on <line>");
	  (void) puts("b<line>\tset breakpoint at <line>");
	  (void) puts("c\tcontinue execution until a breakpoint is reached");
	  (void) puts("d<line>\tdelete breakpoint at <line>");
	  (void) puts("e<line>\texplain the ick_main expression on <line>");
	  (void) puts("f<line>\tstop producing messages when commands on <line> are run");
	  (void) puts("g<line>\tchange currently executing command to the ick_first command");
	  (void) puts("\ton <line> or the ick_next command if already on <line>");
	  (void) puts("h\tlist 10 lines either side of the current line");
	  (void) puts("i<var>\tignore a variable");
	  (void) puts("j<var>\tremember a variable");
	  (void) puts("k\tcontinue until we RESUME back to the current nexting level");
	  (void) puts("\t(that is, step unless we are on a NEXT, in which case execute");
	  (void) puts("\tuntil a RESUME or FORGET back to the same or smaller NEXT stack)");
	  (void) puts("l<line>\tlist 10 lines either side of <line>");
	  (void) puts("m<line>\tproduce a message every time a command on <line> is run");
	  (void) puts("n\tshow the NEXT stack");
	  (void) puts("o\tcontinue until we RESUME/FORGET below the current nexting level");
	  (void) puts("\tie until the NEXT stack becomes smaller than it is at present");
	  (void) puts("p\tdisplay the values of all onespot and twospot variables");
	  (void) puts("q\tabort execution");
	  (void) puts("r<line>\treinstate once all ick_abstained commands on <line>");
	  (void) puts("s\texecute one command");
	  (void) puts("t\tcontinue until a breakpoint, displaying all lines executed");
	  (void) puts("u<line>\texecute until just before <line> is reached");
	  (void) puts("v<var>\tshow value of variable every time a command is printed");
	  (void) puts("w\tshow the current line and current command");
	  (void) puts("x<var>\tremove a variable view, breakchange, or breakzero");
	  (void) puts("y<var>\tview variable every displayed line, and break on change");
	  (void) puts("z<var>\tview variable every displayed line, and break on zero");
	  (void) puts("<var>\tview the value of a onespot or twospot variable");
	  (void) puts("<<var>\tset the value of a onespot or twospot variable");
	  (void) puts("*\tview the GNU General Public License");
	  (void) puts("?\tview this help screen");
	  (void) puts("@\tview this help screen");
	  (void) puts("Line numbers refer to lines of source code, not line labels.");
	  (void) puts("Listings have (Axxxxxx) at the start of each line: this shows");
	  (void) puts("the abstention status of each command on that line.");
	  (void) puts("The values of variables must be input in proper INTERCAL");
	  (void) puts("notation (i.e. ONE TWO THREE), and are output as butchered");
	  (void) puts("Roman ick_numerals.");
#ifdef __DJGPP__
	  (void) puts("You can press <CTRL>-<BREAK> to interrupt an executing program.");
#else
	  (void) puts("You can press <CTRL>-C to interrupt an executing program.");
#endif
	  break;
	case 'q':
	  exit(0);
	  /*@-unreachable@*/ break; /*@=unreachable@*/
	case 'n':
	  i=ick_nextindex;
	  if(!i)
	  {
	    (void) puts("The NEXT stack is empty.");
	  }
	  else
	  {
	    (void) puts("Commands NEXTED from:");
	    while(i--)
	    {
	      /* write NEXT line */
	      printf("%5d:\t%s\n",lineofaboff[ick_next[i]-1],
		     textlines[lineofaboff[ick_next[i]-1]]);
	      /* write NEXT command within line */
	      tempcmd=(int)ick_next[i];
	      while(tempcmd&&lineofaboff[--tempcmd]==lineofaboff[ick_next[i]-1]);
	      printf("NEXTED from command C%u: Abstained %d time%s\n",ick_next[i]-1?
		     ick_next[i]-1-tempcmd:1,ick_abstained[ick_next[i]-1],
		     ick_abstained[ick_next[i]-1]==1?".":"s.");
    	    }
	  }
	  break;
	case 'w':
	  /* write line that we're on */
	  printf("%5d:\t%s\n",lineofaboff[aboff],text);
	  /* write command within line that we're on */
	  tempcmd=aboff;
	  while(tempcmd&&lineofaboff[--tempcmd]==lineofaboff[aboff]);
	  printf("On C%d: Abstained %d time%s\n",aboff?aboff-tempcmd:1,ick_abstained[aboff],
	     ick_abstained[aboff]==1?".":"s.");
	  break;
	case 'p':
	  i=-1;
	  while(++i,1)
	  {
	    if(yukvars[i].vartype==YUKEND) break;
	    if(yukvars[i].vartype==ick_ONESPOT)
	    {
	      printf("Variable .%d is:\n",yukvars[i].extername);
	      ick_pout(ick_onespots[yukvars[i].intername]);
	    }
	    if(yukvars[i].vartype==ick_TWOSPOT)
	    {
	      printf("Variable :%d is:\n",yukvars[i].extername);
	      ick_pout(ick_twospots[yukvars[i].intername]);
	    }
	  }
	  break;
	case '.':
	case ':':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine<1 || temp != 1)
	  {
	    (void) puts("Don't know which variable you mean.");
	    break;
	  }
	  i=-1;
	  temp=0;
	  while(++i,!temp)
	  {
	    if(yukvars[i].vartype==YUKEND) break;
	    if((*buf=='.'&&yukvars[i].vartype==ick_ONESPOT)||
	       (*buf==':'&&yukvars[i].vartype==ick_TWOSPOT))
	    {
	      if(yukvars[i].extername==templine)
	      {
		temp=1;
		if(yukvars[i].vartype==ick_ONESPOT)
		{
		  ick_pout(ick_onespots[yukvars[i].intername]);
		  (void) puts(ick_oneforget[yukvars[i].intername]?
		       "This variable is currently ignored.":
		       "This variable is currently remembered.");
		}
		if(yukvars[i].vartype==ick_TWOSPOT)
		{
		  ick_pout(ick_twospots[yukvars[i].intername]);
		  (void) puts(ick_twoforget[yukvars[i].intername]?
		       "This variable is currently ignored.":
		       "This variable is currently remembered.");
		}
	      }
	    }
	  }
	  if(temp) break;
	  (void) puts("That variable is not in the program.");
	  break;
	case 'v':
	case 'x':
	case 'y':
	case 'z':
	  if(buf[1]!='.'&&buf[1]!=':')
	  {
	    (void) puts("This command only works on onespot and twospot variables.");
	    break;
	  }
	  temp = sscanf(buf+2,"%d",&templine);
	  if(templine<1 || temp != 1)
	  {
	    (void) puts("Don't know which variable you mean.");
	    break;
	  }
	  i=-1;
	  temp=0;
	  while(++i,!temp)
	  {
	    if(yukvars[i].vartype==YUKEND) break;
	    if(yukvars[i].extername==templine)
	    {
	      if(buf[1]=='.'&&yukvars[i].vartype==ick_ONESPOT)
	      {
		if(*buf=='v') onewatch[yukvars[i].intername]=(char)1;
		if(*buf=='x') onewatch[yukvars[i].intername]=(char)0;
		if(*buf=='y') onewatch[yukvars[i].intername]=(char)2;
		if(*buf=='z') onewatch[yukvars[i].intername]=(char)3;
		oneold[yukvars[i].intername]=ick_onespots[yukvars[i].intername];
		temp=1;
	      }
	      if(buf[1]==':'&&yukvars[i].vartype==ick_TWOSPOT)
	      {
		if(*buf=='v') twowatch[yukvars[i].intername]=(char)1;
		if(*buf=='x') twowatch[yukvars[i].intername]=(char)0;
		if(*buf=='y') twowatch[yukvars[i].intername]=(char)2;
		if(*buf=='z') twowatch[yukvars[i].intername]=(char)3;
		twoold[yukvars[i].intername]=ick_twospots[yukvars[i].intername];
		temp=1;
	      }
	    }
	  }
	  if(!temp)
	  {
	    (void) puts("That variable is not in the program.");
	    break;
	  }
	  if(*buf=='v') (void) puts("Set a normal variable view.");
	  if(*buf=='x') (void) puts("Removed all views from that variable.");
	  if(*buf=='y') (void) puts("Set a breakchange variable view.");
	  if(*buf=='z') (void) puts("Set a breakzero variable view.");
	  break;
	case 'i':
	case 'j':
	  if(buf[1]!='.'&&buf[1]!=':'&&buf[1]!=','&&buf[1]!=';')
	  {
	    (void) puts("That isn't a real sort of variable.");
	    break;
	  }
	  temp = sscanf(buf+2,"%d",&templine);
	  if(templine<1 || temp != 1)
	  {
	    (void) puts("Don't know which variable you mean.");
	    break;
	  }
	  i=-1;
	  temp=0;
	  while(++i,!temp)
	  {
	    if(yukvars[i].vartype==YUKEND) break;
	    if((buf[1]=='.'&&yukvars[i].vartype==ick_ONESPOT)||
	       (buf[1]==':'&&yukvars[i].vartype==ick_TWOSPOT)||
	       (buf[1]==','&&yukvars[i].vartype==ick_TAIL)||
	       (buf[1]==';'&&yukvars[i].vartype==ick_HYBRID))
	    {
	      if(yukvars[i].extername==templine)
	      {
		temp=1;
		if(yukvars[i].vartype==ick_ONESPOT)
		  ick_oneforget[yukvars[i].intername]=*buf=='i';
		if(yukvars[i].vartype==ick_TWOSPOT)
		  ick_twoforget[yukvars[i].intername]=*buf=='i';
		if(yukvars[i].vartype==ick_TAIL)
		  ick_tailforget[yukvars[i].intername]=*buf=='i';
		if(yukvars[i].vartype==ick_HYBRID)
		  ick_hyforget[yukvars[i].intername]=*buf=='i';		
	      }
	    }
	  }
	  if(temp)
	  {
	    if(*buf=='i')
	      (void) puts("Variable ignored.");
	    else
	      (void) puts("Variable remembered.");
	    break;
	  }
	  (void) puts("That variable is not in the program.");
	  break;
	  	case '<':
	  if(buf[1]!='.'&&buf[1]!=':')
	  {
	    (void) puts("You cannot set that sort of variable (if it exists at all).");
	    break;
	  }
	  temp = sscanf(buf+2,"%d",&templine);
	  if(templine<1 || temp != 1)
	  {
	    (void) puts("Don't know which variable you mean.");
	    break;
	  }
	  i=-1;
	  temp=0;
	  while(++i,!temp)
	  {
	    if(yukvars[i].vartype==YUKEND) break;
	    if((buf[1]=='.'&&yukvars[i].vartype==ick_ONESPOT)||
	       (buf[1]==':'&&yukvars[i].vartype==ick_TWOSPOT))
	    {
	      if(yukvars[i].extername==templine)
	      {
		temp=1;
		if(yukvars[i].vartype==ick_ONESPOT)
		  ick_onespots[yukvars[i].intername]=(ick_type16)ick_pin();
		if(yukvars[i].vartype==ick_TWOSPOT)
		  ick_twospots[yukvars[i].intername]=(ick_type32)ick_pin();
		/* note that when debugging, you can set an
		   ignored variable */
	      }
	    }
	  }
	  if(temp) break;
	  (void) puts("That variable is not in the program.");
	  break;
	case 'g':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(!templine || temp != 1)
	  {
	    (void) puts("Don't know which line you mean.");
	    break;
	  }
	  breakpoints[0] = templine;
	  yukloop = 1;
	  /* This is implemented by incrementing all ABSTAIN counts,
	     even the normally immutable ones on GIVE UP lines, setting
	     a temporary breakpoint ([0]) on the line given, and running the
	     program. When the breakpoint is hit singlestep will see that
	     yukloop is set (its purpose is to cause the program to go back
	     to the start when it reaches the end) and decrement all ABSTAIN
	     counts, putting the commands back the way they were. We set an error
	     breakpoint on this line in case the user is trying to jump to a
	     line with no commands (although this debugger command is called
	     'g', would I dare to describe this as a GOTO?) */
	  i = -1;
	  while(++i<yukcommands) ick_abstained[i]++;
	  yukerrcmdg = aboff;
	  singlestep = 0;
	  writelines = 0;
	  keeplooping = 0; /* to break out of the loop in the debugger! */
	  break;
	case 'a':
	case 'r':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(!templine || temp != 1)
	  {
	    (void) puts("Don't know which line you mean.");
	    break;
	  }
	  i=-1;
	  tempcmd=1;
	  while(++i<yukcommands)
	  {
	    if(lineofaboff[i]==templine)
	    {
	      temp=ick_abstained[i];
	      if(*buf=='a') if(!temp) ick_abstained[i]=1;
	      if(*buf=='r') if(temp) ick_abstained[i]--;
	      printf("Command %d on line %d was ick_abstained %d time%s, "
		     "now ick_abstained %d time%s.\n",tempcmd,templine,
		     temp,temp==1?"":"s",ick_abstained[i],
		     ick_abstained[i]==1?"":"s");
	      tempcmd++;
	    }
	  }
	  if(tempcmd==1) (void) puts("No commands start on this line.");
	  break;
	case 'k':
	case 'o':
	  untilnext=ick_nextindex-(*buf=='o');
	  /*@fallthrough@*/
	case 'c':
	  singlestep=0;
	  writelines=0;
	  keeplooping=0;
	  break;
	case 'b':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine && temp == 1)
	  {
	    printf("Breakpoint set at line %d.\n",templine);
	    breakpoints[nbreakpoints++]=templine;
	    if(nbreakpoints>=80) ick_lose(IE811,emitlineno,(const char*)NULL);
	  }
	  else
	    (void) puts("Don't know which line you mean.");
	  break;
	case 'd':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine && temp == 1)
	  {
	    printf("All breakpoints removed from line %d.\n",templine);
	    i=nbreakpoints;
	    while(i--) if(templine==breakpoints[i])
	    {
	      memmove(breakpoints+i,breakpoints+i+1,sizeof(int)*(nbreakpoints-i));
	      nbreakpoints--;
	    }
	  }
	  else
	    (void) puts("Don't know which line you mean.");
	  break;
	case 'm':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine && temp == 1)
	  {
	    printf("Monitor set at line %d.\n",templine);
	    monitors[nmonitors++]=templine;
	    if(nmonitors>=80) ick_lose(IE811,emitlineno,(const char*)NULL);
	  }
	  else
	    (void) puts("Don't know which line you mean.");
	  break;
	case 'f':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine && temp == 1)
	  {
	    printf("All monitors removed from line %d.\n",templine);
	    i=nmonitors;
	    while(i--) if(templine==monitors[i])
	    {
	      memmove(monitors+i,monitors+i+1,sizeof(int)*(nmonitors-i));
	      nmonitors--;
	    }
	  }
	  else
	    (void) puts("Don't know which line you mean.");
	  break;	  
	case 's':
	  singlestep=1;
	  writelines=1;
	  keeplooping=0;
	  break;
	case 't':
	  singlestep=0;
	  writelines=1;
	  keeplooping=0;
	  break;
	case 'u':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(templine && temp == 1)
	  {
	    breakpoints[0]=templine;
	    singlestep=0;
	    writelines=0;
	    keeplooping=0;
	  }
	  else
	    (void) puts("Don't know which line you mean.");
	  break;
	case 'e':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(!templine || temp != 1)
	  {
	    (void) puts("Don't know which line you mean.");
	    break;
	  }
	  tempcmd=-1;
	  temp=0;
	  i=0;
	  while(++tempcmd<yukcommands)
	  {
	    if(lineofaboff[tempcmd]==templine)
	    {
	      if(!yukexplain[tempcmd]) ++i;
	      else
	      {
		printf("C%d: Expression is %s\n",++i,yukexplain[tempcmd]);
		temp++;
	      }
	    }
	  }
	  if(!temp) (void) puts("No expressions on that line.");
	  break;
	case 'l':
	  temp = sscanf(buf+1,"%d",&templine);
	  if(!templine || temp != 1)
	  {
	    (void) puts("Don't know which line you mean.");
	    break;
	  }
	  /*@fallthrough@*/
	case 'h':
	  if(!templine) templine=lineofaboff[aboff];
	  templine-=10;
	  if(templine+21>=yuklines) templine=yuklines-22;
	  if(templine<1) templine=1;
	  i=templine;
	  while(i<templine+21&&i<yuklines)
	  {
	    buf[6]='\0';
	    buf[0]=buf[1]=buf[2]=buf[3]=buf[4]=buf[5]=' ';
	    tempcmd=-1;
	    temp=0;
	    while(++tempcmd<yukcommands)
	    {
	      if(lineofaboff[tempcmd]==i)
	      {
		if(ick_abstained[tempcmd]>9) buf[temp++]='!';
		else buf[temp++]='0'+(char)ick_abstained[tempcmd];
		if(temp==6) break;
	      }
	    }
	    printf("(A%s)%5d:\t%s\n",buf,i,textlines[i]);
	    i++;
	  }
	  break;
	case '*':
	  tempcharp=ick_findandtestopen("COPYING.txt",globalargv[1],
				    "r",globalargv[2]);
	  if(tempcharp != NULL)
	  {
#ifndef HAVE_SNPRINTF
	    (void) sprintf(copyloc,"more < %s",tempcharp);
#else
	    (void) snprintf(copyloc,sizeof copyloc,"more < %s",tempcharp);
#endif
	    (void) system(copyloc); /* display the GNU GPL copyright */
	  }
	  else
	    (void) puts("Couldn't find license file. See the file COPYING.txt that\n"
			"came with your C-INTERCAL distribution.");
	  break;
	default:
	  (void) puts("Not sure what you mean. Try typing ?<RET>.");
	  break;
	}
      } while(keeplooping);
    }
  }
  firstrun=0;
}
