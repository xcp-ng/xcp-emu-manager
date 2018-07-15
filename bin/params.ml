type save_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
}

type restore_params = {
  control_in_fd: int;
  control_out_fd: int;
  main_fd: int;
  domid: int;
  store_port: int;
  console_port: int;
}

type params =
  | Save of save_params
  | Restore of restore_params

