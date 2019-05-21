/*
 * xcp-emu-manager
 * Copyright (C) 2019  Vates SAS - ronan.abhamon@vates.fr
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _ARG_LIST_H_
#define _ARG_LIST_H_

#include <stdbool.h>

// =============================================================================

typedef struct ArgNode {
  struct ArgNode *next;
  char *key;
  char *value;
} ArgNode;

void arg_list_free (ArgNode *head);
int arg_list_append_str (ArgNode **head, const char *key, const char *value);
int arg_list_append_bool (ArgNode **head, const char *key, bool value);

#endif // ifndef _ARG_LIST_H_
