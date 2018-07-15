(** Parameters required for both saving and restoring. *)
type common_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
}

(** Mode-specific parameters. *)
type mode =
  | Save
  | Restore of {
    store_port: int;
    console_port: int;
  }

(** A type for describing required xenguest behaviour. *)
type params = {
  common: common_params;
  mode:   mode;
}
