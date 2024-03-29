/* #ifdef-format output routines for GNU DIFF.

   Copyright (C) 1989, 1991-1994, 2001-2002, 2004, 2006, 2009-2013, 2015-2024
   Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   GNU DIFF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.  No author or distributor
   accepts responsibility to anyone for the consequences of using it
   or for whether it serves any particular purpose or works at all,
   unless he says so in writing.  Refer to the GNU General Public
   License for full details.

   Everyone is granted permission to copy, modify and redistribute
   GNU DIFF, but only under the conditions described in the
   GNU General Public License.   A copy of this license is
   supposed to have been given to you along with GNU DIFF so you
   can know your rights and responsibilities.  It should be in a
   file named COPYING.  Among other things, the copyright notice
   and this notice must be preserved on all copies.  */

#include "diff.h"

#include <c-ctype.h>
#include <xmalloca.h>

struct group
{
  struct file_data const *file;
  lin from, upto; /* start and limit lines for this group of lines */
};

static char const *format_group (FILE *, char const *, char,
                                 struct group const *);
static char const *do_printf_spec (FILE *, char const *,
                                   struct file_data const *, lin,
                                   struct group const *);
static char const *scan_char_literal (char const *, char *);
static lin groups_letter_value (struct group const *, char);
static void format_ifdef (char const *, lin, lin, lin, lin);
static void print_ifdef_hunk (struct change *);
static void print_ifdef_lines (FILE *, char const *, struct group const *);

static lin next_line0;
static lin next_line1;

/* Print the edit-script SCRIPT as a merged #ifdef file.  */

void
print_ifdef_script (struct change *script)
{
  next_line0 = next_line1 = - curr.file[0].prefix_lines;
  print_script (script, find_change, print_ifdef_hunk);
  if (next_line0 < curr.file[0].valid_lines
      || next_line1 < curr.file[1].valid_lines)
    {
      begin_output ();
      format_ifdef (group_format[UNCHANGED],
		    next_line0, curr.file[0].valid_lines,
		    next_line1, curr.file[1].valid_lines);
    }
}

/* Print a hunk of an ifdef diff.
   This is a contiguous portion of a complete edit script,
   describing changes in consecutive lines.  */

static void
print_ifdef_hunk (struct change *hunk)
{
  /* Determine range of line numbers involved in each file.  */
  lin first0, last0, first1, last1;
  enum changes changes = analyze_hunk (hunk, &first0, &last0, &first1, &last1);
  if (!changes)
    return;

  begin_output ();

  /* Print lines up to this change.  */
  if (next_line0 < first0 || next_line1 < first1)
    format_ifdef (group_format[UNCHANGED],
                  next_line0, first0,
                  next_line1, first1);

  /* Print this change.  */
  next_line0 = last0 + 1;
  next_line1 = last1 + 1;
  format_ifdef (group_format[changes],
                first0, next_line0,
                first1, next_line1);
}

/* Print a set of lines according to FORMAT.
   Lines BEG0 up to END0 are from the first file;
   lines BEG1 up to END1 are from the second file.  */

static void
format_ifdef (char const *format, lin beg0, lin end0, lin beg1, lin end1)
{
  format_group (outfile, format, '\0',
		((struct group const[])
		 {{.file = &curr.file[0], .from = beg0, .upto = end0},
		  {.file = &curr.file[1], .from = beg1, .upto = end1}}));
}

/* Print to file OUT a set of lines according to FORMAT.
   The format ends at the first free instance of ENDCHAR.
   Yield the address of the terminating character.
   GROUPS specifies which lines to print.
   If OUT is zero, do not actually print anything; just scan the format.  */

static char const *
format_group (FILE *out, char const *format, char endchar,
              struct group const *groups)
{
  char const *f = format;

  for (char c; (c = *f) != endchar && c; )
    {
      char const *f1 = ++f;
      if (c == '%')
       {
	c = *f++;
	switch (c)
          {
          case '%':
            break;

          case '(':
            /* Print if-then-else format e.g. '%(n=1?thenpart:elsepart)'.  */
            {
              intmax_t value[2];

              for (int i = 0; i < 2; i++)
                {
                  if (c_isdigit (*f))
                    {
                      char *fend;
                      errno = 0;
                      value[i] = strtoimax (f, &fend, 10);
                      if (errno)
                        goto bad_format;
                      f = fend;
                    }
                  else
                    {
                      value[i] = groups_letter_value (groups, *f);
                      if (value[i] < 0)
                        goto bad_format;
                      f++;
                    }
                  if (*f++ != "=?"[i])
                    goto bad_format;
                }

	      bool equal_values = value[0] == value[1];
              FILE *thenout = equal_values ? out : nullptr;
	      FILE *elseout = equal_values ? nullptr : out;
              f = format_group (thenout, f, ':', groups);
              if (*f)
                {
                  f = format_group (elseout, f + 1, ')', groups);
                  if (*f)
                    f++;
                }
            }
            continue;

          case '<':
            /* Print lines deleted from first file.  */
            print_ifdef_lines (out, line_format[OLD], &groups[0]);
            continue;

          case '=':
            /* Print common lines.  */
            print_ifdef_lines (out, line_format[UNCHANGED], &groups[0]);
            continue;

          case '>':
            /* Print lines inserted from second file.  */
            print_ifdef_lines (out, line_format[NEW], &groups[1]);
            continue;

          default:
            f = do_printf_spec (out, f - 2, nullptr, 0, groups);
            if (f)
              continue;
            /* Fall through. */
          bad_format:
            c = '%';
            f = f1;
            break;
          }
       }

      if (out)
        putc (c, out);
    }

  return f;
}

/* For the line group pair G, return the number corresponding to LETTER.
   Return -1 if LETTER is not a group format letter.  */
static lin
groups_letter_value (struct group const *g, char letter)
{
  switch (letter)
    {
    case 'E': letter = 'e'; g++; break;
    case 'F': letter = 'f'; g++; break;
    case 'L': letter = 'l'; g++; break;
    case 'M': letter = 'm'; g++; break;
    case 'N': letter = 'n'; g++; break;
    }

  switch (letter)
    {
      case 'e': return translate_line_number (g->file, g->from) - 1;
      case 'f': return translate_line_number (g->file, g->from);
      case 'l': return translate_line_number (g->file, g->upto) - 1;
      case 'm': return translate_line_number (g->file, g->upto);
      case 'n': return g->upto - g->from;
      default: return -1;
    }
}

/* Print to file OUT, using FORMAT to print the line group GROUP.
   But do nothing if OUT is zero.  */
static void
print_ifdef_lines (FILE *out, char const *format,
                   struct group const *group)
{
  if (!out)
    return;

  struct file_data const *file = group->file;
  char const *const *linbuf = file->linbuf;
  lin from = group->from, upto = group->upto;

  /* If possible, use a single fwrite; it's faster.  */
  if (!expand_tabs && format[0] == '%')
    {
      if (format[1] == 'l' && format[2] == '\n' && !format[3] && from < upto)
        {
          fwrite (linbuf[from], sizeof (char),
                  linbuf[upto] + (linbuf[upto][-1] != '\n') -  linbuf[from],
                  out);
          return;
        }
      if (format[1] == 'L' && !format[2])
        {
          fwrite (linbuf[from], sizeof (char),
                  linbuf[upto] -  linbuf[from], out);
          return;
        }
    }

  for (;  from < upto;  from++)
    {
      char c;
      char const *f = format;

      while ((c = *f++))
        {
          char const *f1 = f;
          if (c == '%')
	   {
	    c = *f++;
	    switch (c)
              {
              case '%':
                break;

              case 'l':
                output_1_line (linbuf[from],
                               (linbuf[from + 1]
                                - (linbuf[from + 1][-1] == '\n')),
                               nullptr, nullptr);
                continue;

              case 'L':
                output_1_line (linbuf[from], linbuf[from + 1],
			       nullptr, nullptr);
                continue;

              default:
                f = do_printf_spec (out, f - 2, file, from, nullptr);
                if (f)
                  continue;
                c = '%';
                f = f1;
                break;
              }
	   }

          putc (c, out);
        }
    }
}

static char const *
do_printf_spec (FILE *out, char const *spec,
                struct file_data const *file, lin n,
                struct group const *groups)
{
  char const *f = spec;
  char c;

  /* Scan printf-style SPEC of the form %[-'0]*[0-9]*(.[0-9]*)?[cdoxX].  */
  dassert (*f == '%');
  f++;
  while ((c = *f++) == '-' || c == '\'' || c == '0')
    continue;
  while (c_isdigit (c))
    c = *f++;
  if (c == '.')
    while (c_isdigit (c = *f++))
      continue;
  char c1 = *f++;

  switch (c)
    {
    case 'c':
      if (c1 != '\'')
        return nullptr;
      else
        {
          char value;
          f = scan_char_literal (f, &value);
          if (!f)
            return nullptr;
          if (out)
            putc (value, out);
        }
      break;

    case 'd': case 'o': case 'x': case 'X':
      {
        lin value;

        if (file)
          {
            if (c1 != 'n')
              return nullptr;
            value = translate_line_number (file, n);
          }
        else
          {
            value = groups_letter_value (groups, c1);
            if (value < 0)
              return nullptr;
          }

        if (out)
          {
            /* For example, if the spec is "%3xn" and pI is "l", use the printf
               format spec "%3lx".  Here the spec prefix is "%3".  */
            idx_t spec_prefix_len = f - spec - 2;
            idx_t pI_len = sizeof pI - 1;
            char *format = xmalloca (spec_prefix_len + pI_len + 2);
            char *p = mempcpy (format, spec, spec_prefix_len);
            p = stpcpy (p, pI);
            *p++ = c;
            *p = '\0';
            fprintf (out, format, value);
            freea (format);
          }
      }
      break;

    default:
      return nullptr;
    }

  return f;
}

/* Scan the character literal represented in the string LIT; LIT points just
   after the initial apostrophe.  Put the literal's value into *VALPTR.
   Yield the address of the first character after the closing apostrophe,
   or a null pointer if the literal is ill-formed.  */
static char const *
scan_char_literal (char const *lit, char *valptr)
{
  char const *p = lit;
  char value;
  char c = *p++;

  switch (c)
    {
      case 0:
      case '\'':
        return nullptr;

      case '\\':
        value = '\0';
        while ((c = *p++) != '\'')
          {
            unsigned int digit = c - '0';
            if (8 <= digit)
              return nullptr;
            value = 8 * value + digit;
          }
        ptrdiff_t digits = p - lit - 2;
        if (! (1 <= digits && digits <= 3))
          return nullptr;
        break;

      default:
        value = c;
        if (*p++ != '\'')
          return nullptr;
        break;
    }

  *valptr = value;
  return p;
}
