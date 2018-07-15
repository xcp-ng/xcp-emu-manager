(** Get the path to the xenguest control socket for the specified domid. *)
val control_path : int -> string

(** The type of messages which can be sent to xenguest. *)
type message =
  | Set_args of (string * string) list
  | Migrate_init
  | Migrate_pause
  | Migrate_paused
  | Migrate_nonlive
  | Restore
  | Quit

(** Send a message to xenguest via the supplied socket. *)
val send : Unix.file_descr -> message -> unit

(** exec xenguest with the specified parameters. *)
val exec : Params.params -> unit
