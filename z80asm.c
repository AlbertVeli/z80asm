/* Z80 assembler by shevek

   Copyright (C) 2002-2004 Bas Wijnen <b.wijnen@phys.rug.nl>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define VERSION "0.2"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <getopt.h>

/* defines which are not function-specific */
#ifndef BUFLEN
#define BUFLEN 300       /* size of readbuffer for file i/o */
#endif

#ifndef MAX_INCLUDE
#define MAX_INCLUDE 20  /* stack size for include command */
#endif

/* types */
/* mnemonics. THESE MUST BE IN THE SAME ORDER AS const char *mnemonic[]! */
enum mnemonic {
  CALL,CPDR,CPIR,DJNZ,HALT,INDR,INIR,LDDR,LDIR,OTDR,OTIR,OUTD,OUTI,PUSH,
  RETI,RETN,RLCA,RRCA,DEFB,DEFW,DEFS,DEFM,
  ADC,ADD,AND,BIT,CCF,CPD,CPI,CPL,DAA,DEC,EQU,EXX,INC,IND,INI,LDD,LDI,NEG,NOP,
  OUT,POP,RES,RET,RLA,RLC,RLD,RRA,RRC,RRD,RST,SBC,SCF,SET,SLA,SLL,SLI,SRA,SRL,
  SUB,XOR,ORG,
  CP,DI,EI,EX,IM,IN,JP,JR,LD,OR,RL,RR,DB,DW,DS,DM,
  INCLUDE,IF,ELSE,ENDIF
};

/* types of reference */
enum reftype
{
  TYPE_BSR,  /* bit value (0-7) for bit, set and res mnemonics */
  TYPE_DS,   /* ds reference (byte count and value) */
  TYPE_RST,  /* rst reference: 0-0x38, with val & 0x38 == val */
  TYPE_ABSW, /* absolute word (2 bytes) */
  TYPE_ABSB, /* absolute byte */
  TYPE_RELB  /* relative byte */
};

/* filetypes that can appear on the input. object files are on the todo list */
enum filetype
{
  FILETYPE_ASM
};

/* these structs will be malloced for each reference */
struct reference
{
  struct reference *next, *prev;
  enum reftype type;		/* type of reference */
  long oseekpos;		/* position in outfile for data */
  long lseekpos;		/* position in listfile for data */
  char delimiter;		/* delimiter for parser */
  int addr, line;		/* address and line of reference */
  int comma;			/* comma when reference was set */
  int count;	        	/* only for ds: number of items */
  int infile;                   /* index in infile[], current infile */
  char input[1];		/* variable size buffer containing formula */
};

/* labels (will be malloced) */
struct label
{
  struct label *next, *prev;    /* linked list */
  int value;			/* value */
  int valid;                    /* if it is valid, or not yet computed */
  int busy;                     /* if it is currently being computed */
  char *addr;                   /* mallocced memory to value for computation */
  char name[1];			/* space with name in it */
};

/* files that were given on the commandline */
struct infile
{
  const char *name;
  enum filetype type;
};

/* elements on the file stack */
struct stack
{
  const char *name;   /* filename (for errors). may be malloced */
  FILE *file;   /* the handle */
  int line;     /* the current line number (for errors) */
};

/* filenames must be remembered for references */
struct name
{
  struct name *next, *prev;
  char name[1];
};

/* global variables */
/* mnemonics, used as argument to indx() in assemble */
const char *mnemonics[]={
  "call","cpdr","cpir","djnz","halt","indr","inir","lddr","ldir","otdr","otir",
  "outd","outi","push","reti","retn","rlca","rrca","defb","defw","defs","defm",
  "adc","add","and","bit","ccf","cpd","cpi","cpl","daa","dec","equ","exx",
  "inc","ind","ini","ldd","ldi","neg","nop","out","pop","res","ret","rla",
  "rlc","rld","rra","rrc","rrd","rst","sbc","scf","set","sla","sll","sli",
  "sra","srl","sub","xor","org","cp","di","ei","ex","im","in","jp","jr","ld",
  "or","rl","rr","db","dw","ds","dm","include","if","else","endif",NULL
};

/* linked lists */
static struct reference *firstreference = NULL;
static struct label *firstlabel = NULL, *lastlabel = NULL;
static struct name *firstname = NULL;

/* files */
static FILE *realoutputfile, *outfile, *listfile, *labelfile;
static struct infile *infile;
/* prefix for labels in labelfile */
static const char *labelprefix = "";
/* bools to see if files are opened */
static int havelist = 0, label = 0;
/* number of infiles in array */
static int infilecount;

/* number of errors seen so far */
static int errors = 0;

/* current line, address and file */
static int line = 0, addr = 0, file;
/* current number of characters in list file, for indentation */
static int listdepth;

/* use readbyte instead of (hl) if writebyte is true */
static int writebyte;
static const char *readbyte;
/* variables which are filled by rd_* functions and used later,
 * like readbyte */
static const char *readword, *indexjmp, *bitsetres;

/* 0, 0xdd or 0xfd depening on which index prefix should be given */
static int indexed;

/* increased for every -v option on the command line */
static int verbose = 0;

/* read commas after indx() if comma > 1. increase for every call */
static int comma;

/* address at start of line (for references) */
static int baseaddr;

/* set by readword and readbyte, used for new_reference */
static char mem_delimiter;

/* line currently being parsed */
static char buffer[BUFLEN];

/* print an error message, including current line and file */
static void
printerr (const char *fmt, ...)
{
  va_list l;
  va_start (l, fmt);
  fprintf (stderr, "%s:%5d (%04X): ", infile[file].name, line, addr);
  vfprintf (stderr, fmt, l);
  va_end (l);
}

/* skip over spaces in string */
static const char *
delspc (const char *ptr)
{
  while (*ptr && isspace (*ptr))
    ptr++;
  if (*ptr ==';') ptr = "";
  return ptr;
}

/* read away a comma, error if there is none */
static void
rd_comma (const char **p)
{
  *p = delspc (*p);
  if (**p != ',')
    {
      printerr ("`,' expected. Remainder of line: %s.\n", *p);
      errors++;
      return;
    }
  *p = delspc ((*p) + 1);
}

/* During assembly, most literals are not parsed.  Instead, they are saved
 * until all labels are read.  After that, they are parsed.  This function
 * is used during assembly, to find the place where the command continues. */
static void
skipword (const char **pos, char delimiter)
{
  int depth = 0;
  char c;
  while (1)
    {
      switch (c = (*((*pos)++)))
	{
	case '\0':
	  if (depth > 0)
	    {
	      printerr ("Unexpected end of line.\n");
	      errors++;
	    }
	  (*pos)--;
	  return;
	case '(':
	  depth++;
	  break;
	case ')':
	  if (--depth < 0)
	    {
	      if (delimiter == ')')
		return;
	      printerr ("Unexpected `)'.\n");
	      errors++;
	    }
	  break;
	default:
	  if (delimiter == c && depth == 0)
	    return;
	}
    }
}

/* callback function for argument parser, used to open output files. */
static FILE *
openfile (int *done, /* flag to check that a file is opened only once. */
	  const char *type, /* name of filetype for error message */
	  FILE *def,         /* default value, in case "-" is specified */
	  const char *name, /* filename to open */
	  const char *flags) /* open flags */
{
  FILE *retval;
  if (*done)
    {
      fprintf (stderr, "Error: more than one %s specified\n", type);
      exit (1);
    }
  *done = 1;
  if (def && name && name[0] == '-' && name[1] == 0)
    {
      return def;
    }
  if (!name || !name[0])
    {
      fprintf (stderr, "Error: no %s specified\n", type);
      exit (1);
    }
  if (!(retval = fopen (name, flags)))
    {
      fprintf (stderr, "Unable to open %s %s: %s\n",
	       type, name, strerror (errno));
      exit (1);
    }
  return retval;
}

/* queue a file to be opened for reading */
static void
open_infile (const char *name)
{
  if (!(infile = realloc (infile, sizeof (struct infile) * (infilecount + 1))))
    {
      fprintf (stderr, "Error: insufficient memory\n");
      exit (1);
    }
  /* only asm is currently supported */
  infile[infilecount].type = FILETYPE_ASM;
  infile[infilecount].name = name;
  if (verbose > 2)
    fprintf (stderr, "queued inputfile %s\n", infile[infilecount].name);
  infilecount++;
}

/* parse commandline arguments */
static void
parse_commandline (int argc, char **argv)
{
  const struct option opts[] = {
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, 'V'},
    {"verbose", no_argument, NULL, 'v'},
    {"list", required_argument, NULL, 'l'},
    {"label", required_argument, NULL, 'L'},
    {"input", required_argument, NULL, 'i'},
    {"output", required_argument, NULL, 'o'},
    {"label-prefix", required_argument, NULL, 'p'}
  };
  const char *short_opts = "hVvl:L:i:o:p:";
  int done = 0, i, out = 0;
  infile = NULL;
  while (!done)
    {
      switch (getopt_long (argc, argv, short_opts, opts, NULL) )
	{
	case 'h':	
	  printf ("Usage: %s [options] [input files]\n"
		  "\n"
		  "Possible options are:\n"
		  "-h\t--help\t\tDisplay this help text and exit.\n"
		  "-V\t--version\tDisplay version information and exit.\n"
		  "-v\t--verbose\tBe verbose.  "
		  "Specify again to be more verbose.\n"
		  "-l\t--list\t\tWrite a list file.\n"
		  "-L\t--label\t\tWrite a label file.\n"
		  "-p\t--label-prefix\tprefix all labels with this prefix.\n"
		  "-i\t--input\t\tSpecify an input file (-i may be omitted).\n"
		  "-o\t--output\tSpecify the output file.\n"
		  "Please send bug reports and feature requests to "
		  "<b.wijnen@phys.rug.nl>\n", argv[0]);
	  exit (0);
	case 'V':
	  printf ("Z80 assembler version " VERSION "\n"
		  "Copyright (C) 2002-2003 Bas Wijnen "
		  "<b.wijnen@phys.rug.nl>.\n"
		  "This program comes with ABSOLUTELY NO WARRANTY.\n"
		  "You may distribute copies of the program under the terms\n"
		  "of the GNU General Public License as published by\n"
		  "the Free Software Foundation; either version 2 of the\n"
		  "License, or (at your option) any later version.\n\n"
		  "For more information about these matters, see the file\n"
		  "named COPYING.\n");
	  exit (0);
	case 'v':
	  verbose++;
	  if (verbose > 2)
	    fprintf (stderr, "Verbosity increased to level %d\n", verbose);
	  break;
	case 'o':
	  realoutputfile =
	    openfile (&out, "output file", stdout, optarg, "wb");
	  if (verbose > 2) fprintf (stderr, "Opened outputfile\n");
	  break;
	case 'i':
	  open_infile (optarg);
	  break;
	case 'l':
	  listfile = openfile (&havelist, "list file", NULL, optarg, "w");
	  if (verbose > 2) fprintf (stderr, "Opened list file\n");
	  break;
	case 'L':
	  labelfile = openfile (&label, "label file", NULL, optarg, "w");
	  if (verbose > 2) fprintf (stderr, "Opened label file\n");
	  break;
	case 'p':
	  labelprefix = optarg;
	  break;
	case -1:
	  done = 1;
	  break;
	default:
	  /* errors are handled by getopt_long */
	  break;
	}
    }
  for (i = optind; i < argc; ++i) open_infile (argv[i]);
  if (!infilecount) open_infile ("-");
  if (!out)
    realoutputfile = stdout;
  if (!(outfile = tmpfile ()))
    {
      fprintf (stderr, "Error: Unable to open temporary file: %s\n",
	       strerror (errno));
      exit (1);
    }
}

/* return first differing character from two strings, or 0 if they are equal */
static int
compare (const char *a, const char *b, size_t size)
{
  size_t i;
  int j, z;
  for (i = 0; i < size; ++i)
    if ( (j = tolower (z = *a++) - tolower (*b++) ) || !z ) return j;
  return 0;
}

/* find any of the list[] entries as the start of ptr and return index */
static int
indx (const char **ptr, const char **list, int error)
{
  int i, l;
  *ptr = delspc (*ptr);
  if (!**ptr)
    {
      if (error)
	{
	  printerr ("Unexpected end of line.\n");
	  errors++;
	  return 0;
	}
      else
	return 0;
    }
  if (comma > 1)
    rd_comma (ptr);
  for (i = 0; list[i]; i++)
    {
      l = strlen (list[i]);
      if (list[i][0] && !compare (*ptr, list[i], l)
	  && (!isalnum ((*ptr)[l]) || !isalnum (list[i][l - 1])))
	{
	  (*ptr) += l;
	  if (verbose >= 2)
	    fprintf (stderr, "%5d (%04x): Piece of code found:%s\n", line,
		     addr, list[i]);
	  if (verbose >= 4)
	    fprintf (stderr, "%5d (%04x): Remainder of line=%s.\n", line, addr,
		     *ptr);
	  comma++;
	  return i + 1;
	}
    }
  if (error)
    {
      printerr ("Parse error. Remainder of line=%s\n", *ptr);
      if (verbose >= 1)
	{
	  fprintf (stderr, "When looking for any of:\n");
	  for (i = 0; list[i]; i++)
	    fprintf (stderr, "%s\t", list[i]);
	  fprintf (stderr, "\n");
	}
      errors++;
    }
  return 0;
}

/* read a mnemonic */
static int
readcommand (const char **p)
{
  return indx (p, mnemonics, 0);
}

/* try to read a label and store it in the list */
static void
readlabel (const char **p)
{
  const char *c, *pos;
  int i;
  struct label *buf, *tmp, *prv;
  for (c = *p; *c && !strchr (" \r\n\t", *c); ++c) {}
  pos = strchr (*p, ':');
  if (!pos || pos > c) return;
  if (pos == *p)
    {
      printerr ("Error: `:' found without a label.");
      errors++;
      return;
    }
  c = pos + 1;
  for (prv = NULL, tmp = firstlabel; tmp; prv = tmp, tmp = tmp->next)
    {
      i = compare (tmp->name, *p, c - *p);
      if (i == 0)
	{
	  printerr ( "Duplicate definition of label %s.\n", *p);
	  errors++;
	  *p = c + 1;
	  return;
	}
      if (i > 0)
	break;
    }
  if (NULL == (buf = malloc (sizeof (struct label) + c - *p)))
    {
      printerr ("Not enough memory to store label %s.\n", *p);
      *p = c + 1;
      return;
    }
  strncpy (buf->name, *p, c - *p - 1);
  buf->name[c - *p - 1] = 0;
  if (verbose >= 1)
    fprintf (stderr, "%5d (%04x): Label found: %s\n", line, addr, buf->name);
  *p = c + 1;
  buf->value = addr;
  lastlabel = buf;
  buf->next = tmp;
  buf->prev = prv;
  buf->valid = 1;
  buf->busy = 0;
  if (buf->prev)
    buf->prev->next = buf;
  else
    firstlabel = buf;
  if (buf->next)
    buf->next->prev = buf;
}

static void new_reference (const char *data, int type, char delimiter,
			   int ds_count);

/* write one byte to the outfile, and add it to the list file as well */
static void write_one_byte (int b, int list)
{
  b &= 0xff;
  putc (b, outfile);
  if (list && havelist)
    {
      fprintf (listfile, " %02x", b);
      listdepth += 3;
    }
  addr++;
}

/* write byte to outfile and possibly some index things as well */
static void
wrtb (int b)
{
  if (verbose >= 2)
    fprintf (stderr, "%5d (%04x): wrtb called with argument %2x\n", line, addr,
	     b);
  if (indexed)
    {
      if (verbose >= 3)
	fprintf (stderr, "%5d (%04x): writing indexed byte %2x\n", line, addr,
		indexed);
      write_one_byte (indexed, 1);
      indexed = 0;
    }
  if (writebyte)
    {
      if (verbose >= 3)
	fprintf (stderr, "%5d (%04x): using a xor on byte because there is a "
		 "writebyte.\n", line, addr);
      b ^= 0x40;
    }
  if (verbose >= 3)
    fprintf (stderr, "%5d (%04x): writing byte %2x\n", line, addr, b);
  if (bitsetres && b != 0xCB)
    {
      new_reference (bitsetres, TYPE_BSR, ',', b);
      bitsetres = NULL;
    }
  else
    {
      write_one_byte (b, 1);
    }
  if (indexjmp)
    {
      if (verbose >= 3)
	fprintf (stderr, "%5d (%04x): Making reference for index/jump %s\n",
		 line, addr, indexjmp);
      new_reference (indexjmp, TYPE_ABSB, ')', 1);
      indexjmp = NULL;
    }
  if (writebyte)
    {
      if (verbose >= 3)
	fprintf (stderr, "%5d (%04x): writing argument byte for padding\n",
		 line, addr);
      writebyte = 0;
      new_reference (readbyte, TYPE_ABSB, mem_delimiter, 1);
    }
}

/* reading expressions. The following operators are supported
 * in order of precedence, with function name:
 * expr?expr:expr rd_expr
 * |              rd_expr_or
 * ^              rd_expr_xor
 * &              rd_expr_and
 * == !=          rd_expr_equal
 * >= <= > <      rd_expr_unequal
 * << >>          rd_expr_shift
 * + - (binary)   rd_term
 * * / %          rd_factor
 * ~ + - (unary)  rd_factor
 */

static int
rd_number (const char **p, const char **endp, int base)
{
  int result = 0, i;
  char *c, num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read number of base %d"
	     "(string=%s).\n", line, addr, base, *p);
  num[base] = '\0';
  *p = delspc (*p);
  while (**p && (c = strchr (num, tolower (**p))))
    {
      i = c - num;
      if (verbose >= 5)
	fprintf (stderr, "%5d (%04x): Digit found:%1x.\n", line, addr, i);
      result = result * base + i;
      (*p)++;
    }
  if ( endp )
    *endp = *p;
  *p = delspc (*p);
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_number returned %d (%04X).\n",
	    line, addr, result, result);
  return result;
}

static int
rd_otherbasenumber (const char **p)
{
  char c;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read basenumber (string=%s).\n",
	    line, addr, *p);
  (*p)++;
  if (!**p)
    {
      printerr ("Unexpected end of line after `@'.\n");
      errors++;
      return 0;
    }
  if (**p == '0' || !isalnum (**p))
    {
      printerr ("Base must be between 1 and z.\n");
      errors++;
      return 0;
    }
  c = **p;
  (*p)++;
  if (isalpha (**p))
    return rd_number (p, NULL, tolower (c) - 'a' + 1);
  return rd_number (p, NULL, c - '0' + 1);
}

static int
rd_character (const char **p)
{
  int i;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read character (string=%s).\n",
	    line, addr, *p);
  (*p)++;
  i = **p;
  if (!i)
    {
      printerr ("Unexpected end of line after opening quote.\n");
      errors++;
      return 0;
    }
  if (i == '\\')
    {
      (*p)++;
      switch (**p)
	{
	case 'n':
	  i = 10;
	  break;
	case 'r':
	  i = 13;
	  break;
	case 't':
	  i = 9;
	  break;
	case 'a':
	  i = 7;
	  break;
	case '\'':
	  printerr ("Empty literal character.\n");
	  errors++;
	  return 0;
	case 0:
	  printerr ("Unexpected end of line after "
		    "opening opening quote and backslash.\n");
	  errors++;
	  return 0;
	default:
	  i = **p;
	}
    }
  (*p)++;
  if (**p != '\'')
    {
      printerr ("Missing closing quote.\n");
      errors++;
      return 0;
    }
  (*p)++;
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_character returned %d (%c).\n",
	     line, addr, i, i);
  return i;
}

static int rd_expr (const char **p, char delimiter, int *valid);

static void compute_label (struct label *l)
{
  int value, valid;
  const char *c;
  if (l->busy) return; /* don't loop */
  l->busy = 1;
  c = l->addr;
  if (verbose >= 4)
    fprintf (stderr, "Trying to compute label %s.\n", l->name);
  value = rd_expr (&c, '\0', &valid);
  if (valid)
    {
      l->value = value;
      l->valid = 1;
      free (l->addr);
    }
  l->busy = 0;
}

static int
rd_label (const char **p, int *exists)
{
  int found = 0;
  const char *where = NULL;
  struct label *l;
  if (exists) *exists = 0;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read label (string=%s).\n", line,
	     addr, *p);
  for (l = firstlabel; l; l = l->next)
    {
      if (!strncmp (l->name, *p, strlen (l->name)))
	{
	  const char *c;
	  c = *p + strlen (l->name);
	  if (!(isalnum (*c) || *c == '_'))
	    {
	      found = 1;
	      where = *p;
	      *p = delspc (c);
	      /* if label is not valid, compute it */
	      if (!l->valid) compute_label (l);
	      if (!l->valid)
		{
		  /* label was not valid, and isn't computable.  tell the
		   * caller that it doesn't exist, so it will try again later.
		   * Just in case some other label matches the same input,
		   * continue trying all the other labels. */
		  if (verbose >= 4)
		    fprintf (stderr,
			     "%5d (%04x): returning invalid label %s.\n",
			     line, addr, l->name);
		  continue;
		}
	      if (exists) *exists = 1;
	      if (verbose >= 5)
		fprintf (stderr, "%5d (%04x): rd_label returned %d (%04X).\n",
			 line, addr, l->value, l->value);
	      return l->value;
	    }
	}
    }
  if (exists)
    {
      /* this was only a check for existance, skip the label */
      if (verbose >= 4)
	fprintf (stderr, "%5d (%04x): Returning invalid for unknown label.\n",
		 line, addr);
      while (isalnum (**p) || **p == '_') ++*p;
    }
  else
    {
      if (found)
	{
	  printerr ("Uncomputable label found: %.*s.\n", *p, where - *p);
	}
      else
	{
	  printerr ("Syntax error (label or number expected): %s.\n", *p);
	}
      errors++;
      *p = "";
    }
  return 0;
}

static int
rd_value (const char **p, int *valid)
{
  int sign = 1, not = 0, base, v;
  const char *p0, *p1, *p2;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read value (string=%s).\n", line,
	     addr, *p);
  *p = delspc (*p);
  while (**p && strchr ("+-~", **p))
    {
      if (**p == '-')
	sign = -sign;
      else if (**p == '~')
	not = ~not;
      (*p)++;
      *p = delspc (*p);
    }
  base = 10;			/* Default base for suffixless numbers */
  switch (**p)
    {
      int exist;
    case '(':
      (*p)++;
      return not ^ (sign * rd_expr (p, ')', valid));
    case '0':
      if ((*p)[1] == 'x')
	{
	  (*p)+=2;
	  return not ^ (sign * rd_number (p, NULL, 0x10));
	}
      base = 8;			/* If first digit it 0, assume octal unless suffix */
      /* fall through */
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      p0 = *p;
      rd_number(p, &p1, 36);	/* Advance to end of numeric string */
      p1--;			/* Last character in numeric string */
      switch ( *p1 )
	{
	case 'h':
	case 'H':
	  base = 16;
	  break;
	case 'b':
	case 'B':
	  base = 2;
	  break;
	case 'o':
	case 'O':
	case 'q':
	case 'Q':
	  base = 8;
	  break;
	case 'd':
	case 'D':
	  base = 10;
	  break;
	default:		/* No suffix */
	  p1++;
	  break;
	}
      v = rd_number(&p0, &p2, base);
      if ( p1 != p2 ) {
	printerr("invalid character in number: \'%c\'\n", *p2);
	errors++;
      }
      return not ^ (sign * v);
    case '$':
      (*p)++;
      return not ^ (sign * addr);
    case '%':
      (*p)++;
      return not ^ (sign * rd_number (p, NULL, 2) );
    case '\'':
      return not ^ (sign * rd_character (p) );
    case '@':
      return not ^ (sign * rd_otherbasenumber (p) );
    case '!':
      rd_label (p, &exist);
      return not ^ (sign * exist);
    default:
      return not ^ (sign * rd_label (p, valid) );
    }
}

static int
rd_factor (const char **p, int *valid)
{
  /* read a factor of an expression */
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read factor (string=%s).\n",
	     line, addr, *p);
  result = rd_value (p, valid);
  *p = delspc (*p);
  while (**p == '*' || **p == '/')
    {
      if (**p == '*')
	{
	  (*p)++;
	  result *= rd_value (p, valid);
	}
      else if (**p == '/')
	{
	  (*p)++;
	  result /= rd_value (p, valid);
	}
      *p = delspc (*p);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_factor returned %d (%04X).\n",
	    line, addr, result, result);
  return result;
}

static int
rd_term (const char **p, int *valid)
{
  /* read a term of an expression */
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read term (string=%s).\n", line,
	     addr, *p);
  result = rd_factor (p, valid);
  *p = delspc (*p);
  while (**p == '+' || **p == '-')
    {
      if (**p == '+')
	{
	  (*p)++;
	  result += rd_factor (p, valid);
	}
      else if (**p == '-')
	{
	  (*p)++;
	  result -= rd_factor (p, valid);
	}
      *p = delspc (*p);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_term returned %d (%04X).\n",
	    line, addr, result, result);
  return result;
}

static int
rd_expr_shift (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read shift expression "
	     "(string=%s).\n", line, addr, *p);
  result = rd_term (p, valid);
  *p = delspc (*p);
  while ( (**p == '<' || **p == '>') && (*p)[1] == **p)
    {
      if (**p == '<')
	{
	  (*p) += 2;
	  result <<= rd_term (p, valid);
	}
      else if (**p == '>')
	{
	  (*p) += 2;
	  result >>= rd_term (p, valid);
	}
      *p = delspc (*p);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_shift returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr_unequal (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read "
	     "unequality expression (string=%s).\n", line, addr, *p);
  result = rd_expr_shift (p, valid);
  *p = delspc (*p);
  if (**p == '<' && (*p)[1] == '=')
    {
      (*p) += 2;
      return result <= rd_expr_unequal (p, valid);
    }
  else if (**p == '>' && (*p)[1] == '=')
    {
      (*p) += 2;
      return result >= rd_expr_unequal (p, valid);
    }
  if (**p == '<' && (*p)[1] != '<')
    {
      (*p)++;
      return result < rd_expr_unequal (p, valid);
    }
  else if (**p == '>' && (*p)[1] != '>')
    {
      (*p)++;
      return result > rd_expr_unequal (p, valid);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_shift returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr_equal (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read equality epression "
	     "(string=%s).\n", line, addr, *p);
  result = rd_expr_unequal (p, valid);
  *p = delspc (*p);
  if (**p == '=' && (*p)[1] == '=')
    {
      (*p) += 2;
      return result == rd_expr_equal (p, valid);
    }
  else if (**p == '!' && (*p)[1] == '=')
    {
      (*p) += 2;
      return result != rd_expr_equal (p, valid);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_equal returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr_and (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read and expression "
	     "(string=%s).\n", line, addr, *p);
  result = rd_expr_equal (p, valid);
  *p = delspc (*p);
  if (**p == '&')
    {
      (*p)++;
      result &= rd_expr_and (p, valid);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_expr_and returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr_xor (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read xor expression "
	     "(string=%s).\n", line, addr, *p);
  result = rd_expr_and (p, valid);
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_expr_xor: rd_expr_and returned %d "
	     "(%04X).\n", line, addr, result, result);
  *p = delspc (*p);
  if (**p == '^')
    {
      (*p)++;
      result ^= rd_expr_xor (p, valid);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_expr_xor returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr_or (const char **p, int *valid)
{
  int result;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read or expression "
	     "(string=%s).\n", line, addr, *p);
  result = rd_expr_xor (p, valid);
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_expr_or: rd_expr_xor returned %d "
	     "(%04X).\n", line, addr, result, result);
  *p = delspc (*p);
  if (**p == '|')
    {
      (*p)++;
      result |= rd_expr_or (p, valid);
    }
  if (verbose >= 5)
    fprintf (stderr, "%5d (%04x): rd_expr_or returned %d (%04X).\n",
	     line, addr, result, result);
  return result;
}

static int
rd_expr (const char **p, char delimiter, int *valid)
{
  /* read an expression. delimiter can _not_ be '?' */
  int result = 0;
  if (verbose >= 4)
    fprintf (stderr, "%5d (%04x): Starting to read expression (string=%s).\n",
	     line, addr, *p);
  if (valid) *valid = 1;
  *p = delspc (*p);
  if (!**p || **p == delimiter)
    {
      printerr ("Error: Expression expected (not %s)\n", *p);
      errors++;
      return 0;
    }
  result = rd_expr_or (p, valid);
  *p = delspc (*p);
  if (**p == '?')
    {
      (*p)++;
      if (result)
	{
	  result = rd_expr (p, ':', valid);
	  if (**p) (*p)++;
	  rd_expr (p, delimiter, valid);
	}
      else
	{
	  rd_expr (p, ':', valid);
	  if (**p) (*p)++;
	  result = rd_expr (p, delimiter, valid);
	}
    }
  if (verbose >= 5)
    {
      fprintf (stderr, "%5d (%04x): rd_expr returned %d (%04X).\n",
	       line, addr, result, result);
      if (valid && !*valid)
	fprintf (stderr, "%5d (%04x): Returning invalid result.\n",
		 line, addr);
    }
  return result;
}

static void wrt_ref (int val, int type, int count);

/* Create a new reference, to be resolved after assembling (so all labels are
 * known.) */
void
new_reference (const char *p, int type, char delimiter, int ds_count)
{
  struct reference *tmp;
  long opos, lpos;
  int valid, value;
  const char *c;
  c = p;
  value = rd_expr (&c, delimiter, &valid);
  if (valid)
    {
      if (verbose >= 3)
	{
	  fprintf (stderr,
		   "%5d (%04x): Using calculated value %d (%x) immediately.\n",
		   line, addr, value, value);
	}
    }
  else
    {
      /* the expression is not valid (yet), we need to make a real reference.
       */
      if (NULL == (tmp = malloc (sizeof (struct reference) + strlen (p))))
	{
	  printerr ("Warning: unable to allocate memory for reference %s.\n",
		    p);
	  return;
	}
      opos = ftell (outfile);
      lpos = havelist ? ftell (listfile) : 0;
      if (verbose >= 1)
	fprintf (stderr, "%5d (%04x): reference set to %s (delimiter=%c)\n",
		 line, addr, p, delimiter);
      strcpy (tmp->input, p);
      tmp->addr = baseaddr;
      tmp->count = ds_count;
      tmp->line = line;
      tmp->infile = file;
      tmp->comma = comma;
      tmp->oseekpos = opos;
      tmp->lseekpos = lpos;
      tmp->delimiter = delimiter;
      tmp->type = type;
      tmp->next = firstreference;
      if (firstreference)
	firstreference->prev = tmp;
      tmp->prev = NULL;
      firstreference = tmp;
      /* Dummy value which should not give warnings */
      value = (type == TYPE_RELB) ? ds_count : 0;
    }
  wrt_ref (value, type, ds_count);
}

/* write the last read word to file */
static void
write_word (void)
{
  new_reference (readword, TYPE_ABSW, mem_delimiter, 1);
}

/* write the last read byte to file (relative) */
static void
write_rel (void)
{
  new_reference (readbyte, TYPE_RELB, mem_delimiter, (addr + 1) & 0xffff);
  writebyte = 0;
}

/* read a word from input and store it in readword. return 1 on success */
static int
rd_word (const char **p, char delimiter)
{
  *p = delspc (*p);
  if (**p == 0) return 0;
  readword = *p;
  mem_delimiter = delimiter;
  skipword (p, delimiter);
  return 1;
}

/* read a byte from input and store it in readbyte. return 1 on success */
static int
rd_byte (const char **p, char delimiter)
{
  *p = delspc (*p);
  if (**p == 0) return 0;
  readbyte = *p;
  writebyte = 1;
  mem_delimiter = delimiter;
  skipword (p, delimiter);
  return 1;
}

/* read an indexjump byte (for (ix + nn) commands) */
static void
rd_index (const char **p)
{
  rd_byte (p, ')');
  indexjmp = readbyte;
  writebyte = 0;
  return;
}

/* read an address from infile and put it in reference table.
 * so that it will be written here afterwards */
static void
rd_wrt_addr (const char **p, char delimiter)
{
  if (!rd_word (p, delimiter))
    return;
  write_word ();
}

/* like rd_wrt_addr, but for a relative jump */
static void
rd_wrt_jr (const char **p, char delimiter)
{
  if (!rd_byte (p, delimiter))
    return;
  write_rel ();
}

/* read (SP), DE, or AF */
static int
rd_ex1 (const char **p)
{
#define DE 2
#define AF 3
  const char *list[] = {"(sp)", "de", "af", NULL};
  return indx (p, list, 1);
}

/* read first argument of IN */
static int
rd_in (const char **p)
{
#define A 8
  const char *list[] = {"b", "c", "d", "e", "h", "l", "f", "a", NULL};
  return indx (p, list, 1);
}

/* read second argument of out (c),x */
static int
rd_out (const char **p)
{
  const char *list[] = {"b", "c", "d", "e", "h", "l", "0", "a", NULL};
  return indx (p, list, 1);
}

/* read (c) or (nn) */
static int
rd_nnc (const char **p)
{
#define C 1
  int i;
  const char *list[] = {"(", NULL};
  i = indx (p, list, 1);
  if (!i) return 0;
  *p = delspc (*p);
  if (tolower (**p) == 'c')
    {
      *p = delspc ((*p) + 1);
      if (**p != ')')
	{
	  printerr ("Missing closing parenthesis\n");
	  errors++;
	  return 0;
	}
      (*p)++;
      return C;
    }
  if (tolower (**p) == 'a')
    {
      *p = delspc ((*p) + 1);
      if (**p != ',')
	{
	  printerr ("Missing ','\n");
	  errors++;
	  return 0;
	}
      *p = delspc ((*p) + 1);
    }
  rd_byte (p, ')');
  writebyte = 0;
  return 2;
}

/* read (C) */
static int
rd_c (const char **p)
{
  const char *list[] = {"(c)", "(bc)", NULL};
  return indx (p, list, 1);
}

/* read a or hl */
static int
rd_a_hl (const char **p)
{
#define HL 2
  const char *list[] = {"a", "hl", NULL};
  return indx (p, list, 1);
}

/* read first argument of ld */
static int
rd_ld (const char **p)
{
#define ldBC	1
#define ldDE	2
#define ldHL	3
#define ldSP	4
#define ldIX	5
#define ldIY	6
#define ldB	7
#define ldC	8
#define ldD	9
#define ldE	10
#define ldH	11
#define ldL	12
#define ld_HL	13
#define ldA	14
#define ldI	15
#define ldR	16
#define ld_BC	17
#define ld_DE	18
#define ld_IX	19
#define ld_IY	20
#define ld_NN	21
  int i;
  const char *list[] = {
    "ixh", "ixl", "iyh", "iyl", "bc", "de", "hl", "sp", "ix",
    "iy", "b", "c", "d", "e", "h", "l", "(hl)", "a", "i",
    "r", "(bc)", "(de)", "(ix", "(iy", "(", NULL};
  i = indx (p, list, 1);
  if (!i)
    return 0;
  if (i <= 2)
    {
      indexed = 0xdd;
      return ldH + (i == 2);
    }
  if (i <= 4)
    {
      indexed = 0xfd;
      return ldH + (i == 4);
    }
  i -= 4;
  if (i == ldIX || i == ldIY)
    {
      indexed = i == ldIX ? 0xDD : 0xFD;
      return ldHL;
    }
  if (i == ld_IX || i == ld_IY)
    {
      rd_index (p);
      indexed = i == ld_IX ? 0xDD : 0xFD;
      return ld_HL;
    }
  if (i == ld_NN)
    rd_word (p, ')');
  return i;
}

/* read first argument of JP */
static int
rd_jp (const char **p)
{
  int i;
  const char *list[] = {
    "nz", "z", "nc", "c", "po", "pe", "p", "m", "(ix)", "(iy)",
    "(hl)", NULL};
  i = indx (p, list, 0);
  if (i < 9)
    return i;
  if (i == 11)
    return -1;
  indexed = 0xDD + 0x20 * (i - 9);
  return -1;
}

/* read first argument of JR */
static int
rd_jr (const char **p)
{
  const char *list[] = {"nz", "z", "nc", "c", NULL};
  return indx (p, list, 0);
}

/* read A */
static int
rd_a (const char **p)
{
  const char *list[] = {"a", NULL};
  return indx (p, list, 1);
}

/* read bc,de,hl,af */
static int
rd_stack (const char **p)
{
  int i;
  const char *list[] = {"bc", "de", "hl", "af", "ix", "iy", NULL};
  i = indx (p, list, 1);
  if (i < 5)
    return i;
  indexed = 0xDD + 0x20 * (i - 5);
  return 3;
}

/* read a or hl(2) or i[xy](2) with variables set */
static int
rd_a_hlx (const char **p)
{
  int i;
  const char *list[] = {"a", "hl", "ix", "iy", NULL};
  i = indx (p, list, 1);
  if (i < 2)
    return i;
  if (i == 2)
    return 2;
  indexed = 0xDD + 0x20 * (i - 3);
  return 2;
}

/* read bc,de,hl, or sp */
static int
rd_rr_ (const char **p)
{
  const char *list[] = {"bc", "de", "hl", "sp", NULL};
  return indx (p, list, 1);
}

/* read bc,de,hl|ix|iy,sp. hl|ix|iy only if it is already indexed. */
static int
rd_rrxx (const char **p)
{
  const char *listx[] = {"bc", "de", "ix", "sp", NULL};
  const char *listy[] = {"bc", "de", "iy", "sp", NULL};
  const char *list[] = {"bc", "de", "hl", "sp", NULL};
  switch (indexed)
    {
    case 0xDD:
      return indx (p, listx, 1);
    case 0xFD:
      return indx (p, listy, 1);
    default:
      return indx (p, list, 1);
    }
}

/* read b,c,d,e,h,l,(hl),a,(ix+nn),(iy+nn),nn
 * and set variables accordingly */
static int
rd_r (const char **p)
{
  int i;
  const char *list[] = {
    "ixl", "ixh", "iyl", "iyh", "b", "c", "d", "e", "h", "l", "(hl)",
    "a", "(ix", "(iy", NULL};
  i = indx (p, list, 0);
  if (!i)
    {
      rd_byte (p, '\0');
      return 7;
    }
  if (i <= 4)
    {
      indexed = 0xdd + 0x20 * i > 2;
      return 6 - (i & 1);
    }
  i -= 4;
  if (i < 9)
    return i;
  indexed = 0xDD + 0x20 * (i - 9);
  rd_index (p);
  return 7;
}

/* like rd_r(), but without nn */
static int
rd_r_ (const char **p)
{
  int i;
  const char *list[] = {
    "b", "c", "d", "e", "h", "l", "(hl)", "a", "(ix", "(iy", NULL};
  i = indx (p, list, 1);
  if (i < 9)
    return i;
  indexed = 0xDD + 0x20 * (i - 9);
  rd_index (p);
  return 7;
}

/* read a number from 0 to 7, for bit, set or res */
static int
rd_0_7 (const char **p)
{
  *p = delspc (*p);
  if (**p == 0 || **p == ';') return 0;
  bitsetres = *p;
  skipword (p, ',');
  return 1;
}

/* read long condition. do not error if not found. */
static int
rd_cc (const char **p)
{
  const char *list[] = {"nz", "z", "nc", "c", "po", "pe", "p", "m", NULL};
  return indx (p, list, 0);
}

/* read long or short register,  */
static int
rd_r_rr (const char **p)
{
  int i;
  const char *list[] = {
	    "iy", "ix", "sp", "hl", "de", "bc", "", "b", "c", "d", "e", "h",
	    "l", "(hl)", "a", "(ix", "(iy", NULL};
  i = indx (p, list, 1);
  if (!i)
    return 0;
  if (i < 16 && i > 2)
    return 7 - i;
  if (i > 15)
    {
      rd_index (p);
      indexed = 0xDD + (i - 16) * 0x20;
      return -7;
    }
  indexed = 0xDD + (2 - i) * 0x20;
  return 3;
}

/* read hl */
static int
rd_hl (const char **p)
{
  const char *list[] = {"hl", NULL};
  return indx (p, list, 1);
}

/* read hl, ix, or iy */
static int
rd_hlx (const char **p)
{
  int i;
  const char *list[] = {"hl", "ix", "iy", NULL};
  i = indx (p, list, 1);
  switch (i)
    {
    case 0:
      return 0;
    case 1:
      return 1;
    default:
      indexed = 0xDD + 0x20 * (i - 2);
      return 1;
    }
}

/* read af' */
static int
rd_af_ (const char **p)
{
  const char *list[] = {"af", NULL};
  return indx (p, list, 1);
}

/* read 0(1), 1(3), or 2(4) */
static int
rd_0_2 (const char **p)
{
  const char *list[] = {"0", "", "1", "2", NULL};
  return indx (p, list, 1);
}

/* read argument of ld (hl), */
static int
rd_ld_hl (const char **p)
{
  int i;
  const char *list[] = {"b", "c", "d", "e", "h", "l", "", "a", NULL};
  i = indx (p, list, 0);
  if (i)
    return i;
  rd_byte (p, '\0');
  return 7;
}

/* read argument of ld (nnnn), */
static int
rd_ld_nn (const char **p)
{
#define ld_nnHL 5
#define ld_nnA 6
  int i;
  const char *list[] = {"bc", "de", "", "sp", "hl", "a", "ix", "iy", NULL};
  i = indx (p, list, 1);
  if (i < 7)
    return i;
  indexed = 0xdd + 0x20 * (i == 8);
  return ld_nnHL;
}

/* read argument of ld a, */
static int
rd_lda (const char **p)
{
#define A_I 9
#define A_R 10
#define A_NN 11
  int i;
  const char *list[] = {
    "(sp)", "(iy", "(de)", "(bc)", "(ix", "b", "c", "d", "e", "h",
    "l", "(hl)", "a", "i", "r", "(", NULL};
  i = indx (p, list, 0);
  if (i == 2 || i == 5)
    {
      indexed = (i == 2) ? 0xFD : 0xDD;
      rd_index (p);
      return 7;
    }
  if (!i)
    {
      rd_byte (p, '\0');
      return 7;
    }
  if (i == 16)
    {
      rd_word (p, ')');
    }
  return i - 5;
}

/* read argument of b|c|d|e|h|l */
static int
rd_ldbcdehla (const char **p)
{
  int i;
  const char *list[] = {
    "b", "c", "d", "e", "h", "l", "(hl)", "a", "(ix", "(iy", "ixh",
    "ixl", "iyh", "iyl", NULL};
  i = indx (p, list, 0);
  if (i > 10)
    {
      int x;
      x = 0xdd + 0x20 * (i > 12);
      if (indexed && indexed != x)
	{
	  printerr ("Syntax error: illegal use of index registers.\n");
	  errors++;
	  return 0;
	}
      indexed = x;
      return 6 - (i & 1);
    }
  if (i > 8)
    {
      if (indexed)
	{
	  printerr ("Syntax error: illegal use of index registers.\n");
	  errors++;
	  return 0;
	}
      indexed = 0xDD + 0x20 * (i == 10);
      rd_index (p);
      return 7;
    }
  if (i)
    return i;
  rd_byte (p, '\0');
  return 7;
}

/* read nnnn, or (nnnn) */
static int
rd_nn_nn (const char **p)
{
#define _NN 1
  int i;
  const char *list[] = {"(", NULL};
  i = indx (p, list, 0);
  if (i)
    {
      rd_word (p, ')');
      return 1;
    }
  rd_word (p, '\0');
  return 0;
}

/* read {HL|IX|IY},nnnn, or (nnnn) */
static int
rd_sp (const char **p)
{
#define SPNN 0
#define SPHL 1
  int i;
  const char *list[] = {"(", "ix", "iy", "hl", NULL};
  i = indx (p, list, 0);
  switch (i)
    {
    case 0:
      rd_word (p, '\0');
      return 0;
    case 1:
      rd_word (p, ')');
      return 2;
    }
  if (i == 4)
    return 1;
  indexed = 0xDD + 0x20 * (i - 2);
  return 1;
}

/* write a reference after it has been computed */
static void
wrt_ref (int val, int type, int count)
{
  switch (type)
    {
    case TYPE_RST:
      if ((val & 0x38) != val)
	{
	  printerr ("Error: incorrect RST value %d (%02x).\n", val, val);
	  errors++;
	  return;
	}
      write_one_byte (val + 0xC7, 1);
      return;
    case TYPE_ABSW:
      write_one_byte (val & 0xff, 1);
      write_one_byte ( (val >> 8) & 0xff, 1);
      return;
    case TYPE_ABSB:
      write_one_byte (val & 0xff, 1);
      return;
    case TYPE_DS:
      if (havelist)
	{
	  fprintf (listfile, " %02x...", val & 0xff);
	  listdepth += 6;
	}
      while (count--)
	{
	  write_one_byte (val & 0xff, 0);
	}
      return;
    case TYPE_BSR:
      if (val & ~7)
	{
	  printerr ("Error: incorrect BIT/SET/RES value %d.\n", val);
	  errors++;
	  return;
	}
      write_one_byte (0x08 * val + count, 1);
      return;
    case TYPE_RELB:
      val -= count;
      if (val < -128 || val > 127)
	{
	  printerr ("Warning: relative jump out of range (%d).\n", val);
	}
      write_one_byte (val & 0xff, 1);
    }
}

/* do the actual work */
static void
assemble (void)
{
  int ifcount = 0, noifcount = 0;
  const char *ptr;
  char *bufptr;
  struct reference *tmp;
  int r, s, shouldclose, sp; /* r,s for registers, sp is stack pointer */
  struct stack stack[MAX_INCLUDE]; /* maximum level of includes */
  /* continue assembling until the last input file is done */
  for (file = 0; file < infilecount; ++file)
    {
      sp = 0; /* clear stack */
      stack[sp].line = 0;
      shouldclose = 0;
      stack[sp].name = infile[file].name;
      if (infile[file].name[0] == '-' && infile[file].name[1] == 0)
	{
	  stack[sp].file = stdin;
	}
      else
	{
	  stack[sp].file = fopen (infile[file].name, "r");
	  if (!stack[sp].file)
	    {
	      printerr ("Error: unable to open %s. skipping\n",
			infile[file].name);
	      errors++;
	      continue;
	    }
	  shouldclose = 1;
	}
      if (havelist) fprintf (listfile, "# File %s\n", stack[sp].name);
      buffer[0] = 0;
      /* loop until this source file is done */
      while (1)
	{
	  int cmd, cont = 1;
	  if (havelist)
	    {
	      if (buffer[0] != 0)
		{
		  int i, tabs;
		  if (listdepth <= 8) tabs = 3;
		  else if (listdepth <= 16) tabs = 2;
		  else tabs = 1;
		  for (i = 0; i < tabs; ++i)
		    fputc ('\t', listfile);
		  fprintf (listfile, "%s\n", buffer);
		}
	      listdepth = 4;
	    }
	  while (NULL == fgets (buffer, BUFLEN, stack[sp].file))
	    {
	      if (verbose > 3) fprintf (stderr, "finished reading file %s\n",
					stack[sp].name);
	      if (havelist) fprintf (listfile, "# End of file %s\n",
				     stack[sp].name);
	      if (shouldclose) fclose (stack[sp].file);
	      if (!sp--)
		{
		  cont = 0;
		  break;
		}
	    }
	  if (!cont) break; /* break to next source file */
	  if (havelist) fprintf (listfile, "%04X", addr);
	  for (bufptr = buffer; (bufptr = strchr (bufptr, '\n'));)
	    *bufptr = ' ';
	  for (bufptr = buffer; (bufptr = strchr (bufptr, '\r'));)
	    *bufptr = ' ';
	  ptr = buffer;
	  lastlabel = NULL;
	  baseaddr = addr;
	  line++;
	  ptr = delspc (ptr);
	  if (!*ptr) continue;
	  if (!noifcount) readlabel (&ptr);
	  ptr = delspc (ptr);
	  if (!*ptr) continue;
	  comma = 0;
	  indexed = 0;
	  indexjmp = 0;
	  writebyte = 0;
	  readbyte = 0;
	  readword = 0;
	  cmd = readcommand (&ptr) - 1;
	  if (noifcount)
	    {
	      switch (cmd)
		{
		case IF:
		  noifcount++;
		  break;
		case ELSE:
		  if (noifcount == 1)
		    {
		      noifcount = 0;
		      ifcount++;
		    }
		case ENDIF:
		  noifcount--;
		}
	      continue;
	    }
	  switch (cmd)
	    {
	      int i, valid;
	      const char *c;
	    case ADC:
	      if (!(r = rd_a_hl (&ptr)))
		break;
	      if (r == HL)
		{
		  if (!(r = rd_rr_ (&ptr)))
		    break;
		  wrtb (0xED);
		  wrtb (0x4A + 0x10 * --r);
		  break;
		}
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0x88 + --r);
	      break;
	    case ADD:
	      if (!(r = rd_a_hlx (&ptr)))
		break;
	      if (r == HL)
		{
		  if (!(r = rd_rrxx (&ptr)))
		    break;
		  wrtb (0x09 + 0x10 * --r);
		  break;
		}
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0x80 + --r);
	      break;
	    case AND:
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0xA0 + --r);
	      break;
	    case BIT:
	      if (!rd_0_7 (&ptr))
		break;
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x40 + (r - 1));
	      break;
	    case CALL:
	      if (!(r = rd_cc (&ptr)))
		{
		  wrtb (0xCD);
		}
	      else
		{
		  wrtb (0xC4 + 8 * --r);
		  rd_comma (&ptr);
		}
	      rd_wrt_addr (&ptr, '\0');
	      break;
	    case CCF:
	      wrtb (0x3F);
	      break;
	    case CP:
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0xB8 + --r);
	      break;
	    case CPD:
	      wrtb (0xED);
	      wrtb (0xA9);
	      break;
	    case CPDR:
	      wrtb (0xED);
	      wrtb (0xB9);
	      break;
	    case CPI:
	      wrtb (0xED);
	      wrtb (0xA1);
	      break;
	    case CPIR:
	      wrtb (0xED);
	      wrtb (0xB1);
	      break;
	    case CPL:
	      wrtb (0x2F);
	      break;
	    case DAA:
	      wrtb (0x27);
	      break;
	    case DEC:
	      if (!(r = rd_r_rr (&ptr)))
		break;
	      if (r < 0)
		{
		  wrtb (0x05 - 8 * ++r);
		  break;
		}
	      wrtb (0x0B + 0x10 * --r);
	      break;
	    case DI:
	      wrtb (0xF3);
	      break;
	    case DJNZ:
	      wrtb (0x10);
	      rd_wrt_jr (&ptr, '0');
	      break;
	    case EI:
	      wrtb (0xFB);
	      break;
	    case EQU:
	      if (!lastlabel)
		{
		  printerr ("EQU without label.\n");
		  errors++;
		  break;
		}
	      c = ptr;
	      lastlabel->value = rd_expr (&ptr, '\0', &valid);
	      lastlabel->valid = 1;
	      if (!valid)
		{
		  lastlabel->addr = malloc (ptr - c + 1);
		  if (lastlabel->addr == NULL)
		    {
		      printerr ("No memory to store label reference.\n");
		      errors++;
		      break;
		    }
		  strncpy (lastlabel->addr, c, ptr - c);
		  lastlabel->addr[ptr - c] = 0;
		  lastlabel->valid = 0;
		  lastlabel->busy = 0;
		}
	      if (verbose >= 2)
		{
		  if (lastlabel->valid)
		    fprintf (stderr, "Assigned value %d to label %s.\n",
			     lastlabel->value, lastlabel->name);
		  else
		    fprintf (stderr,
			     "Scheduled label %s for later computation.\n",
			     lastlabel->name);
		}
	      break;
	    case EX:
	      if (!(r = rd_ex1 (&ptr)))
		break;
	      switch (r)
		{
		case DE:
		  if (!rd_hl (&ptr))
		    break;
		  wrtb (0xEB);
		  break;
		case AF:
		  if (!rd_af_ (&ptr))
		    break;
		  wrtb (0x08);
		  break;
		default:
		  if (!rd_hlx (&ptr))
		    break;
		  wrtb (0xE3);
		}
	      break;
	    case EXX:
	      wrtb (0xD9);
	      break;
	    case HALT:
	      wrtb (0x76);
	      break;
	    case IM:
	      if (!(r = rd_0_2 (&ptr)))
		break;
	      wrtb (0xED);
	      wrtb (0x46 + 8 * r--);
	      break;
	    case IN:
	      if (!(r = rd_in (&ptr)))
		break;
	      if (r == A)
		{
		  const char *tmp2;
		  if (!(r = rd_nnc (&ptr)))
		    break;
		  if (r == C)
		    {
		      wrtb (0xED);
		      wrtb (0x40 + 8 * --r);
		      break;
		    }
		  tmp2 = readbyte;
		  wrtb (0xDB);
		  new_reference (tmp2, TYPE_ABSB, ')', 1);
		  break;
		}
	      if (!rd_c (&ptr))
		break;
	      wrtb (0xED);
	      wrtb (0x40 + 8 * --r);
	      break;
	    case INC:
	      if (!(r = rd_r_rr (&ptr)))
		break;
	      if (r < 0)
		{
		  wrtb (0x04 - 8 * ++r);
		  break;
		}
	      wrtb (0x03 + 0x10 * --r);
	      break;
	    case IND:
	      wrtb (0xED);
	      wrtb (0xAA);
	      break;
	    case INDR:
	      wrtb (0xED);
	      wrtb (0xBA);
	      break;
	    case INI:
	      wrtb (0xED);
	      wrtb (0xA2);
	      break;
	    case INIR:
	      wrtb (0xED);
	      wrtb (0xB2);
	      break;
	    case JP:
	      r = rd_jp (&ptr);
	      if (r < 0)
		{
		  wrtb (0xE9);
		  break;
		}
	      if (r == 0)
		{
		  wrtb (0xC3);
		}
	      else
		{
		  wrtb (0xC2 + 8 * --r);
		  rd_comma (&ptr);
		}
	      rd_wrt_addr (&ptr, '\0');
	      break;
	    case JR:
	      r = rd_jr (&ptr);
	      if (r)
		rd_comma (&ptr);
	      wrtb (0x18 + 8 * r);
	      rd_wrt_jr (&ptr, '\0');
	      break;
	    case LD:
	      if (!(r = rd_ld (&ptr)))
		break;
	      switch (r)
		{
		case ld_BC:
		case ld_DE:
		  if (!rd_a (&ptr))
		    break;
		  wrtb (0x02 + 0x10 * (r == ld_DE));
		  break;
		case ld_HL:
		  r = rd_ld_hl (&ptr);
		  wrtb (0x70 + --r);
		  break;
		case ld_NN:
		  if (!(r = rd_ld_nn (&ptr)))
		    break;
		  if (r == ld_nnA || r == ld_nnHL)
		    {
		      wrtb (0x22 + 0x10 * (r == ld_nnA));
		      write_word ();
		      break;
		    }
		  wrtb (0xED);
		  wrtb (0x43 + 0x10 * --r);
		  write_word ();
		  break;
		case ldA:
		  if (!(r = rd_lda (&ptr)))
		    break;
		  if (r == A_NN)
		    {
		      wrtb (0x3A);
		      write_word ();
		      break;
		    }
		  if (r == A_I || r == A_R)
		    {
		      wrtb (0xED);
		      wrtb (0x57 + 8 * (r == A_R));
		      break;
		    }
		  if (r < 0)
		    {
		      wrtb (0x0A - 0x10 * ++r);
		      break;
		    }
		  wrtb (0x78 + --r);
		  break;
		case ldB:
		case ldC:
		case ldD:
		case ldE:
		case ldH:
		case ldL:
		  if (!(s = rd_ldbcdehla (&ptr)))
		    break;
		  wrtb (0x40 + 0x08 * (r - 7) + (s - 1));
		  break;
		case ldBC:
		case ldDE:
		  s = rd_nn_nn (&ptr);
		  if (s == _NN)
		    {
		      wrtb (0xED);
		      wrtb (0x4B + 0x10 * (r == ldDE));
		      write_word ();
		      break;
		    }
		  wrtb (0x01 + (r == ldDE) * 0x10);
		  write_word ();
		  break;
		case ldHL:
		  r = rd_nn_nn (&ptr);
		  wrtb (0x21 + (r == _NN) * 9);
		  write_word ();
		  break;
		case ldI:
		case ldR:
		  if (!rd_a (&ptr))
		    break;
		  wrtb (0xED);
		  wrtb (0x47 + 0x08 * (r == ldR));
		  break;
		case ldSP:
		  r = rd_sp (&ptr);
		  if (r == SPHL)
		    {
		      wrtb (0xF9);
		      break;
		    }
		  if (r == SPNN)
		    {
		      wrtb (0x31);
		      write_word ();
		      break;
		    }
		  wrtb (0xED);
		  wrtb (0x7B);
		  write_word ();
		  break;
		}
	      break;
	    case LDD:
	      wrtb (0xED);
	      wrtb (0xA8);
	      break;
	    case LDDR:
	      wrtb (0xED);
	      wrtb (0xB8);
	      break;
	    case LDI:
	      wrtb (0xED);
	      wrtb (0xA0);
	      break;
	    case LDIR:
	      wrtb (0xED);
	      wrtb (0xB0);
	      break;
	    case NEG:
	      wrtb (0xED);
	      wrtb (0x44);
	      break;
	    case NOP:
	      wrtb (0x00);
	      break;
	    case OR:
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0xB0 + --r);
	      break;
	    case OTDR:
	      wrtb (0xED);
	      wrtb (0xBB);
	      break;
	    case OTIR:
	      wrtb (0xED);
	      wrtb (0xB3);
	      break;
	    case OUT:
	      if (!(r = rd_nnc (&ptr)))
		break;
	      if (r == C)
		{
		  if (!(r = rd_out (&ptr)))
		    break;
		  wrtb (0xED);
		  wrtb (0x41 + 8 * --r);
		  break;
		}
	      if (!rd_a (&ptr))
		break;
	      {
		const char *tmp2 = readbyte;
		wrtb (0xD3);
		new_reference (tmp2, TYPE_ABSB, ')', 1);
	      }
	      break;
	    case OUTD:
	      wrtb (0xED);
	      wrtb (0xAB);
	      break;
	    case OUTI:
	      wrtb (0xED);
	      wrtb (0xA3);
	      break;
	    case POP:
	      if (!(r = rd_stack (&ptr)))
		break;
	      wrtb (0xC1 + 0x10 * --r);
	      break;
	    case PUSH:
	      if (!(r = rd_stack (&ptr)))
		break;
	      wrtb (0xC5 + 0x10 * --r);
	      break;
	    case RES:
	      if (!rd_0_7 (&ptr))
		break;
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x80 + --r);
	      break;
	    case RET:
	      if (!(r = rd_cc (&ptr)))
		{
		  wrtb (0xC9);
		  break;
		}
	      wrtb (0xC0 + 8 * --r);
	      break;
	    case RETI:
	      wrtb (0xED);
	      wrtb (0x4D);
	      break;
	    case RETN:
	      wrtb (0xED);
	      wrtb (0x45);
	      break;
	    case RL:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x10 + --r);
	      break;
	    case RLA:
	      wrtb (0x17);
	      break;
	    case RLC:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x00 + --r);
	      break;
	    case RLCA:
	      wrtb (0x07);
	      break;
	    case RLD:
	      wrtb (0xED);
	      wrtb (0x6F);
	      break;
	    case RR:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x18 + --r);
	      break;
	    case RRA:
	      wrtb (0x1F);
	      break;
	    case RRC:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x08 + --r);
	      break;
	    case RRCA:
	      wrtb (0x0F);
	      break;
	    case RRD:
	      wrtb (0xED);
	      wrtb (0x67);
	      break;
	    case RST:
	      new_reference (ptr, TYPE_RST, '\0', 1);
	      break;
	    case SBC:
	      if (!(r = rd_a_hl (&ptr)))
		break;
	      if (r == HL)
		{
		  if (!(r = rd_rr_ (&ptr)))
		    break;
		  wrtb (0xED);
		  wrtb (0x42 + 0x10 * --r);
		  break;
		}
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0x98 + --r);
	      break;
	    case SCF:
	      wrtb (0x37);
	      break;
	    case SET:
	      if (!rd_0_7 (&ptr))
		break;
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0xC0 + --r);
	      break;
	    case SLA:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x20 + --r);
	      break;
	    case SLI:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x30 + --r);
	      break;
	    case SRA:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x28 + --r);
	      break;
	    case SRL:
	      if (!(r = rd_r_ (&ptr)))
		break;
	      wrtb (0xCB);
	      wrtb (0x38 + --r);
	      break;
	    case SUB:
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0x90 + --r);
	      break;
	    case XOR:
	      if (!(r = rd_r (&ptr)))
		break;
	      wrtb (0xA8 + --r);
	      break;
	    case DEFB:
	    case DB:
	    case DEFM:
	    case DM:
	      ptr = delspc (ptr);
	      if (*ptr != '"' && !(r = rd_byte (&ptr, ',')))
		break;
	      do
		{
		  if (*ptr == '"')
		    {
		      while (*++ptr != '"')
			{
			  if (*ptr == 0)
			    {
			      printerr ("Error: end of line in quoted "
					"string\n");
			      errors++;
			      break;
			    }
			  wrtb (*ptr);
			}
		      if (!*ptr) break;
		      ++ptr;
		      ptr = delspc (ptr);
		      if (!*ptr) break;
		      if (*ptr++ != ',')
			{
			  printerr ("Error: expected end of line or ','");
			  errors++;
			  break;
			}
		      continue;
		    }
		  new_reference (readbyte, TYPE_ABSB, ',', 1);
		}
	      while ((r = rd_byte (&ptr, ',')));
	      writebyte = 0;
	      break;
	    case DEFW:
	    case DW:
	      if (!(r = rd_word (&ptr, ',')))
		break;
	      do
		{
		  new_reference (readword, TYPE_ABSW, ',', 1);
		}
	      while ((r = rd_word (&ptr, ',')));
	      break;
	    case DEFS:
	    case DS:
	      r = rd_expr (&ptr, ',', NULL);
	      if (r < 0)
		{
		  printerr ("ds should have its first argument >=0"
			    " (not %d).\n", r);
		  errors++;
		  break;
		}
	      ptr = delspc (ptr);
	      if (*ptr)
		{
		  rd_comma (&ptr);
		  rd_byte (&ptr, '\0');
		  writebyte = 0;
		  new_reference (readbyte, TYPE_DS, '\0', r);
		  break;
		}
	      if (havelist)
		{
		  fprintf (listfile, " 00...");
		  listdepth += 6;
		}
	      for (i = 0; i < r; i++)
		{
		  write_one_byte (0, 0);
		}
	      break;
	    case ORG:
	      addr = rd_expr (&ptr, '\0', NULL);
	      break;
	    case INCLUDE:
	      if (sp + 1 >= MAX_INCLUDE)
		{
		  printerr ("stack overflow (circular include?)");
		  if (verbose > 2)
		    {
		      int x;
		      fprintf (stderr, "Stack dump:\nframe  line file\n");
		      for (x = 0; x < MAX_INCLUDE; ++x)
			fprintf (stderr, "%5d %5d %s\n", x, stack[x].line,
				stack[x].name);
		    }
		  break;
		}
	      ptr = delspc (ptr);
	      {
		int pos = 0;
		char quote;
		struct name *nm,
		  *name = malloc (sizeof (struct name) + strlen (ptr));
		if (!name)
		  {
		    printerr ("Unable to allocate memory for filename %s\n",
			      name);
		    errors++;
		    break;
		  }
		if (*ptr == ';' || !*ptr)
		  {
		    printerr ("include without filename\n");
		    free (name);
		    errors++;
		    break;
		  }
		quote = *ptr++;
		while (*ptr != quote)
		  {
		    if (!*ptr)
		      {
			printerr ("filename without closing quote (%c)\n",
				  quote);
			free (name);
			errors++;
			break;
		      }
		    name->name[pos++] = *ptr++;
		  }
		name->name[pos] = 0;
		nm =
		  realloc (name, sizeof (struct name) + strlen (name->name));
		if (nm) name = nm;
		sp++;
		stack[sp].name = name->name;
		stack[sp].line = 0;
		stack[sp].file = fopen (name->name, "r");
		if (!stack[sp].file)
		  {
		    printerr ("Unable to open file %s: %s\n",
			      name->name, strerror (errno));
		    errors++;
		    free (name);
		    sp--;
		    break;
		  }
		name->next = firstname;
		name->prev = NULL;
		if (name->next) name->next->prev = name;
		firstname = name;
		if (verbose > 1)
		  fprintf (stderr, "Reading file %s\n", name->name);
	      }
	      break;
	    case IF:
	      if (rd_expr (&ptr, '\0', NULL)) ifcount++; else noifcount++;
	      break;
	    case ELSE:
	      if (ifcount == 0)
		{
		  printerr ("Error: else without if.\n");
		  errors++;
		  break;
		}
	      noifcount = 1;
	      ifcount--;
	      break;
	    case ENDIF:
	      if (noifcount == 0 && ifcount == 0)
		{
		  printerr ("Error: endif without if.\n");
		  errors++;
		  break;
		}
	      if (noifcount) noifcount--; else ifcount--;
	      break;
	    default:
	      printerr ("Syntax error: command or comment "
			"expected (was %s).\n", ptr);
	      errors++;
	    }
	}
    }
  if (ifcount || noifcount) printerr ("Reached EOF at IF level %d\n",
				      ifcount + noifcount);
  if (havelist)
    {
      fprintf (listfile, "%04X\n", addr);
    }
  {
    struct reference *next;
    for (tmp = firstreference; tmp; tmp = next)
      {
	int ref;
	char *c;
	next = tmp->next;
	fseek (outfile, tmp->oseekpos, SEEK_SET);
	if (havelist) fseek (listfile, tmp->lseekpos, SEEK_SET);
	addr = tmp->addr;
	line = tmp->line;
	comma = tmp->comma;
	file = tmp->infile;
	if ((c = strchr (tmp->input, '\n')))
	  *c = 0;
	if (verbose >= 1)
	  fprintf (stderr, "%5d (%04x): Making reference to %s.\n", line, addr,
		   tmp->input);
	ptr = tmp->input;
	ref = rd_expr (&ptr, tmp->delimiter, NULL);
	if (verbose >= 2)
	  fprintf (stderr, "%5d (%04x): Reference is %d (%04x).\n", line, addr,
		   ref, ref);
	wrt_ref (ref, tmp->type, tmp->count);
	free (tmp);
      }
  }
  if (havelist) fclose (listfile);
  if (label)
    {
      /* write all labels */
      struct label *l;
      for (l = firstlabel; l; l = l->next)
	{
	  if (!l->valid) compute_label (l);
	  if (!l->valid)
	    {
	      printerr ("Label %s at end of code still uncomputable.\n",
			l->name);
	      errors++;
	      continue;
	    }
	  fprintf (labelfile, "%s%s:\tequ %04xh\n", labelprefix, l->name,
		   l->value);
	}
      fclose (labelfile);
    }
  while (firstlabel)
    {
      struct label *l;
      l = firstlabel->next;
      free (firstlabel);
      firstlabel = l;
    }
  {
    int l, size, len = 0;
    rewind (outfile);
    while (1)
      {
	clearerr (outfile);
	errno = 0;
	len = fread (buffer, 1, BUFLEN, outfile);
	if (len == 0 && feof (outfile)) break;
	if (len <= 0)
	  {
	    fprintf (stderr, "error reading temp file: %s\n",
		     strerror (errno));
	    exit (1);
	  }
	l = 0;
	while (l < len)
	  {
	    clearerr (realoutputfile);
	    size = fwrite (&buffer[l], 1, len - l, realoutputfile);
	    if (size <= 0)
	      {
		fprintf (stderr, "error writing output file: %s\n",
			 strerror (errno));
		exit (1);
	      }
	    l += size;
	  }
      }
  }
  fclose (outfile);
  fclose (realoutputfile);
  free (infile);
}

int
main (int argc, char **argv)
{
  parse_commandline (argc, argv);
  assemble ();
  if (errors)
    {
      if (errors == 1)
	fprintf (stderr, "*** 1 error found ***\n");
      else
	fprintf (stderr, "*** %d error found ***\n", errors);
      return 1;
    }
  else return 0;
}
