/*

B-Free Project ������ʪ�� GNU Generic PUBLIC LICENSE �˽����ޤ���

GNU GENERAL PUBLIC LICENSE
Version 2, June 1991

(C) B-Free Project.

*/
/* misc.cj ---
 *
 */

#include "itron.h"

/*************************************************************************
 * halfword_swap
 *
 * ������
 *
 * ���͡�
 *
 * ������
 *
 */
UH
halfword_swap (UH w)
{
  w = ((w & 0xff) << 8) | ((w & 0xff00) >> 8);
  return (w);
}

/**************************************************************************
 *
 *
 */
VP
bcopy (VP src, VP dest, W count)
{
  UB *from, *to;

  from = (UB *)src;
  to = (UB *)dest;
  while (count-- > 0)
    {
      *to++ = *from++;
    }
  return (dest);
}

/**************************************************************************
 *
 *
 */
void
bzero (VP src, W count)
{
  UB *from;

  from = (UB *)src;
  while (count-- > 0)
    {
      *from = 0;
      from++;
    }
}  

/**************************************************************************
 *
 *
 */
W
strlen (B *buf)
{
  int	i;

  for (i = 0; buf[i] != '\0'; i++)
    ;
  return (i);
}


/**************************************************************************
 *
 *
 */
W
strcpy (B *s1, B *s2)
{
  int	i;

  for (i = 0; *s2 != NULL; i++)
    {
      *s1++ = *s2++;
    }
  *s1 = '\0';
  return (i);
}

/**************************************************************************
 *
 *
 */
W
strncmp (B *s1, B *s2, W size)
{
  W	i;

  for (i = 0; i < size; i++)
    {
      if ((s1[i] - s2[i] != 0) || (s1[i] == '\0') || (s2[i] == '\0'))
	return (s1[i] - s2[i]);
    }
  return (s1[i] - s2[i]);
}

/**************************************************************************
 *
 *
 */
W
strcmp (B *s1, B *s2)
{
  W	i;

  for (i = 0; ; i++)
    {
      if ((s1[i] - s2[i] != 0) || (!s1[i] || !s2[i]))
	return (s1[i] - s2[i]);
    }
  return (s1[i] - s2[i]);
}

W
strncpy (B *dest, B *src, W size)
{
  W count;

  if (size <= 0)
    {
      return 0;
    }

  for (count = 0; count < (size - 1) && src[count] != '\0'; count++)
    {
      dest[count] = src[count];
    }

  dest[count] = '\0';
  return count;
}

W
atoi (B *s)
{
  W	result;
  BOOL	minus = FALSE;

  result = 0;
  if (*s == '-')
    {
      s++;
      minus = TRUE;
    }

  while (*s != '\0')
    {
      result = (result * 10) + (*s - '0');
      s++;
    }
  return (minus ? -result: result);
}


char
tolower (char c)
{
  if(c>='A'){
    if(c<='Z'){
      return c-('A'-'a');
    }
  }
  return c;
}
char
toupper(char c)
{
  if(c>='a'){
    if(c<='z'){
      return c-('a'-'A');
    } 
  }
  return c;
}

