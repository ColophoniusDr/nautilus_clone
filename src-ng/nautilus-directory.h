/* Copyright (C) 2017 Ernestas Kulik <ernestask@gnome.org>
 *
 * This file is part of Nautilus.
 *
 * Nautilus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * Nautilus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Nautilus.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "nautilus-file.h"

#define NAUTILUS_TYPE_DIRECTORY (nautilus_directory_get_type ())

G_DECLARE_DERIVABLE_TYPE (NautilusDirectory, nautilus_directory,
                          NAUTILUS, DIRECTORY,
                          NautilusFile)

struct _NautilusDirectoryClass
{
    NautilusFileClass parent_class;
};

NautilusFile *nautilus_directory_new (GFile *location);
