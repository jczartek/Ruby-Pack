/* ide-ruby-indenter.h
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

#ifndef IDE_RUBY_INDENTER_H
#define IDE_RUBY_INDENTER_H

#include <ide.h>

G_BEGIN_DECLS

#define IDE_TYPE_RUBY_INDENTER (ide_ruby_indenter_get_type())

G_DECLARE_FINAL_TYPE (IdeRubyIndenter, ide_ruby_indenter, IDE, RUBY_INDENTER, IdeObject)

G_END_DECLS

#endif /* IDE_RUBY_INDENTER_H */

