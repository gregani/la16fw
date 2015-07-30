/*
 * This file is part of the la16fw project.
 *
 * Copyright (C) 2014-2015 Gregor Anich
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef GPIF_STUFF_H
#define GPIF_STUFF_H

#include <fx2types.h>

void gpif_stuff_init();
void gpif_stuff_start();
void gpif_stuff_abort();
void gpif_stuff_loop();

#endif /* GPIF_STUFF_H */
