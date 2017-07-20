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

enum
{
  NORMAL,
  SPECIAL
};

struct keywords
{
  gchar *keyword;
  guint type;
} keywords_opening_scope[] = {
    {"begin", NORMAL},
    {"class", NORMAL},
    {"def", NORMAL},
    {"else", SPECIAL},
    {"elsif", SPECIAL},
    {"ensure", SPECIAL},
    {"for", NORMAL},
    {"if", NORMAL},
    {"module", NORMAL},
    {"rescue", SPECIAL},
    {"unless", NORMAL},
    {"untill", NORMAL},
    {"when", NORMAL},
    {"while", NORMAL}
};

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

  for (gint i = 0; i < G_N_ELEMENTS (keywords_opening_scope); i++)
    {
      if (g_strcmp0 (key[0], keywords_opening_scope[i].keyword) == 0)
        return i;
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
          if (((index = lookup_keyword_opening_scope (&copy)) != -1) && (keywords_opening_scope[index].type == NORMAL))
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
count_current_indent(const GtkTextIter *iter)
{
  GtkTextIter cur = *iter;
  gunichar chr = 0;
  guint indent = 0;

  chr = gtk_text_iter_get_char (&cur);

  while (chr == ' ')
    {
      indent++;
      gtk_text_iter_forward_char (&cur);
      chr = gtk_text_iter_get_char (&cur);
    }

  return indent;
}

static gchar *
ide_ruby_indenter_indent (IdeRubyIndenter *rindenter,
                          GtkTextView     *view,
                          GtkTextBuffer   *buffer,
                          GtkTextIter     *iter)
{
  GtkTextIter cur = *iter;
  gint cur_indent = -1;

  gtk_text_iter_backward_visible_line (&cur);

  cur_indent = count_current_indent (&cur);

  if (lookup_keyword_opening_scope (&cur) != -1)
    {
      cur_indent += rindenter->indent_width;
    }

  if (cur_indent > 0)
    return g_strnfill (cur_indent, ' ');
  return NULL;
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
                                      begin);
      return ret;
    case GDK_KEY_d:
      return adjust_keyword_add (rindenter, view, begin, end);
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
