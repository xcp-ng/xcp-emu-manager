type common_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
  hvm: bool;
}

type save_params = {
  live: bool;
}

type restore_params = {
  store_port: int;
  console_port: int;
}

type mode =
  | Save    of save_params
  | Restore of restore_params

type params = {
  common: common_params;
  mode:   mode;
}
