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

#ifndef _CONTROL_H_
#define _CONTROL_H_

// =============================================================================
// Xenopsd client.
// See: https://wiki.xenproject.org/wiki/Xenopsd
// See: https://github.com/xapi-project/xenopsd
// =============================================================================

int control_init (int fdIn, int fdOut);

int control_get_fd_in ();

// Receive and process messages. Waiting ACK if needed.
int control_receive_and_process_messages (int timeout);

int control_send_prepare (const char *emuName);
int control_send_suspend ();
int control_send_progress (int progress);
int control_send_result (const char *emuName, const char *result);
int control_send_final_result ();

int control_report_error (int emuErrorCode);

#endif // ifndef _CONTROL_H_
