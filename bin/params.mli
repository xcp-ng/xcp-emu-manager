(** Parameters required for both saving and restoring. *)
type common_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
  hvm: bool;
}

(** Parameters required for saving only. *)
type save_params = {
  live: bool;
}

(** Parameters required for restoring only. *)
type restore_params = {
  store_port: int;
  console_port: int;
}

(** Mode-specific parameters. *)
type mode =
  | Save    of save_params
  | Restore of restore_params

(** A type for describing required xenguest behaviour. *)
type params = {
  common: common_params;
  mode:   mode;
}
