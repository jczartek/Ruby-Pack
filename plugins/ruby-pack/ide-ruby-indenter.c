/* ide-ruby-indenter.c
 *
 * Copyright (C) 2017 Jakub Czartek <kuba@linux.pl>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define G_LOG_DOMAIN "ide-ruby-indenter"

#include <libpeas/peas.h>
#include <gtksourceview/gtksource.h>

#include "ide-ruby-indenter.h"

struct _IdeRubyIndenter
{
  IdeObject parent_instance;

  guint tab_width;
  guint indent_width;
  guint use_tabs : 1;
};


static void indenter_iface_init (IdeIndenterInterface *iface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (IdeRubyIndenter, ide_ruby_indenter, IDE_TYPE_OBJECT, 0,
                                G_IMPLEMENT_INTERFACE (IDE_TYPE_INDENTER, indenter_iface_init))

struct Keywords
{
  gchar *keyword;
  gboolean pre_scope;
  gboolean matches_end;
} keywords[] = {
    {"begin", TRUE, TRUE},
    {"class", TRUE, TRUE},
    {"def", TRUE, TRUE},
    {"else", TRUE, FALSE},
    {"elsif", TRUE, FALSE},
    {"ensure", TRUE, FALSE},
    {"for", TRUE, FALSE},
    {"if", TRUE, TRUE},
    {"module", TRUE, TRUE},
    {"rescue", TRUE, FALSE},
    {"unless", TRUE, TRUE},
    {"until", TRUE, TRUE},
    {"when", TRUE, FALSE},
    {"while", TRUE, TRUE},
    {"case", TRUE, TRUE},
    {"do", TRUE, TRUE} /* it must be always last */
};

static gchar *
get_line_indentation (const GtkTextIter *iter)
{
  GtkTextIter start;
  GtkTextIter end;

  start = *iter;
  end = *iter;

  gtk_text_iter_set_line_offset (&start, 0);
  gtk_text_iter_set_line_offset (&end, 0);

  while (!gtk_text_iter_ends_line (&end))
    {
      gunichar ch = gtk_text_iter_get_char (&end);

      if (!g_unichar_isspace (ch))
        break;

      gtk_text_iter_forward_char (&end);
    }

  return gtk_text_iter_get_slice (&start, &end);
}

static gboolean
is_special (const GtkTextIter *iter)
{
  GtkSourceBuffer *buffer;

  buffer = GTK_SOURCE_BUFFER (gtk_text_iter_get_buffer (iter));
  return (gtk_source_buffer_iter_has_context_class (buffer, iter, "string") ||
          gtk_source_buffer_iter_has_context_class (buffer, iter, "comment"));
}

static gint
lookup_keyword_opening_scope (const GtkTextIter *line)
{
  GtkTextIter begin = *line;
  GtkTextIter end = *line;
  gchar **key = NULL;
  gchar *s = NULL;
  gint i, j;

  if (is_special (line))
    return FALSE;

  while (!gtk_text_iter_starts_line (&begin))
    if (!gtk_text_iter_backward_char (&begin))
      break;

  while (!gtk_text_iter_ends_line (&end))
    if (!gtk_text_iter_forward_char (&end))
      break;

  s = gtk_text_iter_get_slice (&begin, &end);
  g_strstrip (s);
  key = g_strsplit (s, " ", -1);

  if(key[0] == NULL)
    goto failure;

  for (i = 0; i < G_N_ELEMENTS (keywords); i++)
    {
      if (g_strcmp0 (key[0], keywords[i].keyword) == 0)
        {
          g_free (s);
          g_strfreev (key);
          return i;
        }
    }

  /* Look up the keyword 'do' */
  for (j = 0; key[j]; j++)
    {
      if (g_strcmp0 (keywords[i-1].keyword, key[j]) == 0)
        {
          g_free (s);
          g_strfreev (key);
          return i-1;
        }
    }

failure:
  g_free (s);
  g_strfreev (key);
  return -1;
}

static gboolean
line_starts_with (const GtkTextIter *line,
                  const gchar       *prefix)
{
  GtkTextIter begin = *line;
  GtkTextIter end = *line;
  gboolean ret;
  gchar *text;

  while (!gtk_text_iter_starts_line (&begin))
    if (!gtk_text_iter_backward_char (&begin))
      break;

  while (!gtk_text_iter_ends_line (&end))
    if (!gtk_text_iter_forward_char (&end))
      break;

  text = gtk_text_iter_get_slice (&begin, &end);
  g_strstrip (text);
  ret = g_str_has_prefix (text, prefix);
  g_free (text);

  return ret;
}

static gboolean
move_first_nonspace_char (GtkTextIter *iter)
{
  g_assert (iter != NULL);

  gtk_text_iter_set_line_offset (iter, 0);

  while (TRUE)
    {
      gunichar ch;

      ch = gtk_text_iter_get_char (iter);
      if (!g_unichar_isspace (ch))
        break;

      if (gtk_text_iter_ends_line (iter))
        break;

      if (!gtk_text_iter_forward_char (iter))
        break;
    }

  return g_unichar_isspace (gtk_text_iter_get_char (iter));
}

static gboolean
move_to_visual_column (GtkSourceView *sv,
                       GtkTextIter   *iter,
                       guint          line_offset)
{
  gtk_text_iter_set_line_offset (iter, 0);

  while (line_offset > gtk_source_view_get_visual_column (sv, iter))
    {
      if (gtk_text_iter_ends_line (iter))
        break;
      gtk_text_iter_forward_char (iter);
    }

  return TRUE;
}

static gboolean
move_previous_line (GtkSourceView *sv,
                    GtkTextIter   *iter,
                    guint          line_offset)
{
  guint line;

  line = gtk_text_iter_get_line (iter);
  if (line == 0)
    return FALSE;

  gtk_text_iter_set_line (iter, line - 1);

  return move_to_visual_column (sv, iter, line_offset);
}

static gchar *
adjust_keyword_add (IdeRubyIndenter *rindenter,
                    GtkTextView     *tv,
                    GtkTextIter     *begin,
                    GtkTextIter     *end)
{
  GtkTextIter copy = *begin;
  GtkSourceView *sv;
  gboolean matches;
  gchar *slice;
  gint index;

  IDE_ENTRY;

  sv = GTK_SOURCE_VIEW (tv);

  gtk_text_iter_backward_chars (&copy, 3);
  slice = gtk_text_iter_get_slice (&copy, begin);
  matches = g_str_equal (slice, "end");

  /* only continue if this is the first word on the line */
  if (matches)
    {
      guint line_offset;

      line_offset = gtk_text_iter_get_line_offset (&copy);
      move_first_nonspace_char (&copy);
      if (line_offset != (guint)gtk_text_iter_get_line_offset (&copy))
        IDE_GOTO (failure);
    }

  if (matches)
    {
      guint line_offset;
      guint ends = 0;

      line_offset = gtk_source_view_get_visual_column (sv, &copy);

      while (TRUE)
        {
          if (!move_previous_line (sv, &copy, line_offset))
            IDE_GOTO (failure);

          move_first_nonspace_char (&copy);

          if (gtk_source_view_get_visual_column (sv, &copy) > line_offset)
            continue;

          if (line_starts_with (&copy, "end"))
            {
              ends++;
              continue;
            }

          if (((index = lookup_keyword_opening_scope (&copy)) != -1) && (keywords[index].matches_end))
            {
              if (ends == 0)
                {
                  move_first_nonspace_char (&copy);
                  line_offset = gtk_source_view_get_visual_column (sv, &copy);
                  move_to_visual_column (sv, begin, line_offset);
                  IDE_RETURN (slice);
                }
              else
                {
                  ends--;
                  continue;
                }
            }
        }
    }

failure:
  g_free (slice);

  IDE_RETURN (NULL);
}

static guint
count_indent (const GtkTextIter *iter)
{
  GtkTextIter copy = *iter;
  guint n = 0;
  gunichar ch;

  gtk_text_iter_set_line_offset (&copy, 0);
  ch = gtk_text_iter_get_char (&copy);
  while (g_unichar_isspace (ch) && gtk_text_iter_forward_char (&copy) && !gtk_text_iter_ends_line(&copy))
    {
      ch = gtk_text_iter_get_char (&copy);
      n++;
    }

  return n;
}

static gchar *
adjust_statement_keywords (IdeRubyIndenter *rindenter,
                           GtkTextView     *tv,
                           GtkTextIter     *begin,
                           GtkTextIter     *end)
{
  GtkTextIter copy = *begin;
  GtkSourceView *sv;
  gchar *s = NULL;

  gboolean matches = FALSE;

  sv = GTK_SOURCE_VIEW (tv);

  gtk_text_iter_backward_word_start (&copy);

  s = gtk_text_iter_get_slice (&copy, begin);

  matches = g_str_equal (s, "rescue") ||
            g_str_equal (s, "ensure") ||
            g_str_equal (s, "when")   ||
            g_str_equal (s, "elsif")  ||
            g_str_equal (s, "else");

    if (matches)
    {
      guint line_offset;

      line_offset = gtk_text_iter_get_line_offset (&copy);
      move_first_nonspace_char (&copy);
      if (line_offset != (guint)gtk_text_iter_get_line_offset (&copy))
        IDE_GOTO (failure);
    }

    if (matches)
    {
      guint line_offset;

      line_offset = gtk_source_view_get_visual_column (sv, &copy);

      while (TRUE)
        {
          if (!move_previous_line (sv, &copy, line_offset))
            IDE_GOTO (failure);

          move_first_nonspace_char (&copy);

          if (gtk_source_view_get_visual_column (sv, &copy) > line_offset)
            continue;

          if (line_starts_with (&copy, "if") ||
              line_starts_with (&copy, "case") ||
              line_starts_with (&copy, "begin") ||
              line_starts_with (&copy, "unless") ||
              line_starts_with (&copy, "def"))
            {
              if (count_indent (&copy) > count_indent (begin))
                {
                  continue;
                }
              else
                {
                  move_first_nonspace_char (&copy);
                  line_offset = gtk_source_view_get_visual_column (sv, &copy);
                  move_to_visual_column (sv, begin, line_offset);
                  IDE_RETURN (s);
                }
            }
        }
    }


failure:
  g_free (s);

  IDE_RETURN (NULL);
}

static gboolean
is_newline_in_braces (const GtkTextIter *iter)
{
  GtkTextIter prev = *iter;
  GtkTextIter next = *iter;
  gboolean square  = FALSE;
  gboolean curly   = FALSE;

  gtk_text_iter_backward_char (&prev);
  gtk_text_iter_forward_char (&next);

  curly  = (gtk_text_iter_get_char (&prev) == '{') && (gtk_text_iter_get_char (iter) == '\n') && (gtk_text_iter_get_char (&next) == '}');
  square = (gtk_text_iter_get_char (&prev) == '[') && (gtk_text_iter_get_char (iter) == '\n') && (gtk_text_iter_get_char (&next) == ']');

  return curly || square;
}

static gchar *
ide_ruby_indenter_indent (IdeRubyIndenter *rindenter,
                          GtkTextView     *view,
                          GtkTextBuffer   *buffer,
                          GtkTextIter     *iter,
                          gint            *cursor_offset)
{
  GtkTextIter cur = *iter;
  gchar *s = NULL;
  gint i = -1;

  gtk_text_iter_backward_char (&cur);
  s = get_line_indentation (&cur);

  if (((i = lookup_keyword_opening_scope (&cur)) != -1) && keywords[i].pre_scope)
    {
      gchar *r = NULL;
      gchar *t = NULL;

      r = g_strnfill (rindenter->indent_width, rindenter->use_tabs ? '\t' : ' ');
      t = g_strconcat (s, r, NULL);

      g_free (s);
      g_free (r);

      return t;
    }

  if (is_newline_in_braces (&cur))
    {
      gchar *r = NULL;
      gchar *t = NULL;

      r = g_strnfill (rindenter->indent_width, rindenter->use_tabs ? '\t' : ' ');
      t = g_strconcat (s, r, "\n", s, NULL);

      *cursor_offset = -(strlen (s) + 1);

      g_free (s);
      g_free (r);

      return t;
    }

  return s;
}

static gboolean
ide_ruby_indenter_is_trigger (IdeIndenter *indenter,
                              GdkEventKey *event)
{
  switch (event->keyval)
    {
    case GDK_KEY_KP_Enter:
    case GDK_KEY_Return:
    case GDK_KEY_d:
    case GDK_KEY_e:
    case GDK_KEY_f:
    case GDK_KEY_n:
      return TRUE;

    default:
      return FALSE;
    }
}

static gchar *
ide_ruby_indenter_format (IdeIndenter *indenter,
                          GtkTextView *view,
                          GtkTextIter *begin,
                          GtkTextIter *end,
                          gint        *cursor_offset,
                          GdkEventKey *event)
{
  IdeRubyIndenter *rindenter = (IdeRubyIndenter *) indenter;
  gint tab_width;
  gint indent_width;
  gchar *ret = NULL;

  g_return_val_if_fail (IDE_IS_RUBY_INDENTER (rindenter), NULL);
  g_return_val_if_fail (IDE_IS_SOURCE_VIEW (view), NULL);

  if (GTK_SOURCE_IS_VIEW (view))
    {
      tab_width = gtk_source_view_get_tab_width (GTK_SOURCE_VIEW (view));
      indent_width = gtk_source_view_get_indent_width (GTK_SOURCE_VIEW (view));

      if (indent_width != -1)
        tab_width = indent_width;
    }

  rindenter->tab_width = tab_width;

  if (indent_width <= 0)
    rindenter->indent_width = tab_width;
  else
    rindenter->indent_width = indent_width;

  rindenter->use_tabs = !gtk_source_view_get_insert_spaces_instead_of_tabs (GTK_SOURCE_VIEW (view));

  switch (event->keyval)
    {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
      ret = ide_ruby_indenter_indent (rindenter, view,
                                      gtk_text_view_get_buffer (view),
                                      begin, cursor_offset);
      return ret;
    case GDK_KEY_d:
      return adjust_keyword_add (rindenter, view, begin, end);
    case GDK_KEY_e:
    case GDK_KEY_f:
    case GDK_KEY_n:
      return adjust_statement_keywords (rindenter, view, begin, end);
    default:
      break;
    }
  return NULL;
}


static void
indenter_iface_init (IdeIndenterInterface *iface)
{
  iface->is_trigger = ide_ruby_indenter_is_trigger;
  iface->format     = ide_ruby_indenter_format;
}

static void
ide_ruby_indenter_class_init (IdeRubyIndenterClass *klass)
{
}

static void
ide_ruby_indenter_class_finalize (IdeRubyIndenterClass *klass)
{
}

static void
ide_ruby_indenter_init (IdeRubyIndenter *self)
{
}

void
_ide_ruby_indenter_register_type (GTypeModule *module)
{
  ide_ruby_indenter_register_type (module);
}
