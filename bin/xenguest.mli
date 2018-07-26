(** Get the path to the xenguest control socket for the specified domid. *)
val control_path : int -> string

(** Send the migrate_init message along with a file descriptor. *)
val send_init : Unix.file_descr -> Unix.file_descr -> unit

(** The type of messages which can be sent to xenguest. *)
type message =
  | Set_args of (string * string) list
  | Migrate_pause
  | Migrate_paused
  | Migrate_live
  | Migrate_nonlive
  | Migrate_progress
  | Restore
  | Quit
  | Track_dirty

(** Send a message to xenguest via the supplied socket. *)
val send : Unix.file_descr -> message -> unit

(** Event which may be sent from xenguest on completion. *)
type completion = {
  xenstore_mfn: int;
  console_mfn: int;
}

(** The types of events which can be sent from xenguest. *)
type event =
  | Completed of completion option
  | Unknown

(** Receive an event from xenguest. *)
val receive : Unix.file_descr -> event

(** exec xenguest with the specified parameters. *)
val exec : Params.params -> unit
