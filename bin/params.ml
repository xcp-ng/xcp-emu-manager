type common_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
}

type mode =
  | Save
  | Restore of {
    store_port: int;
    console_port: int;
  }

type params = {
  common: common_params;
  mode:   mode;
}
