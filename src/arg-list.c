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

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "arg-list.h"

// =============================================================================

static inline ArgNode *arg_list_create_node () {
  ArgNode *node = calloc(sizeof *node, 1);
  if (!node)
    syslog(LOG_ERR, "Unable to alloc list node.");
  return node;
}

static inline void arg_list_append (ArgNode **head, ArgNode *node) {
  if (*head) {
    ArgNode *it = *head;
    for (; it->next; it = it->next);
    it->next = node;
  } else
    *head = node;
}

// -----------------------------------------------------------------------------

void arg_list_free (ArgNode *head) {
  for (ArgNode *next; head; head = next) {
    next = head->next;
    free(head->key);
    free(head->value);
    free(head);
  }
}

int arg_list_append_str (ArgNode **head, const char *key, const char *value) {
  ArgNode *node = arg_list_create_node();
  if (!node) return -1;

  if (!(node->key = strdup(key)) || asprintf(&node->value, "\"%s\"", value) < 0) {
    free(node->key);
    free(node);
    return -1;
  }
  arg_list_append(head, node);

  return 0;
}

int arg_list_append_bool (ArgNode **head, const char *key, bool value) {
  ArgNode *node = arg_list_create_node();
  if (!node) return -1;

  if (!(node->key = strdup(key)) || !(node->value = strdup(value ? "true" : "false"))) {
    free(node->key);
    free(node);
    return -1;
  }
  arg_list_append(head, node);

  return 0;
}
