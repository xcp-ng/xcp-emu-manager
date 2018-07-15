(** Get the path to the xenguest control socket for the specified domid. *)
val control_path : int -> string

(** exec xenguest with the specified parameters. *)
val exec : Params.params -> unit
