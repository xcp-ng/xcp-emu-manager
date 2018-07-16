let wait_for_ready xenguest_in_fd =
  let channel = Unix.in_channel_of_descr xenguest_in_fd in
  if input_line channel <> "Ready"
  then failwith "unexpected message from child"

let save sock control_in_chan control_out_chan =
  Xenguest.(send sock (Set_args ["pv", "true"]));

  Control.(send control_out_chan Suspend);
  Control.(expect_done control_in_chan);

  Xenguest.(send sock Migrate_pause);
  Xenguest.(send sock Migrate_paused);

  Control.(send control_out_chan Prepare);
  Control.(expect_done control_in_chan);

  Xenguest.(send sock Migrate_nonlive);
  let (_ : string option) = Xenguest.(receive sock) in

  Control.(send control_out_chan (Result (0, 0)));

  Xenguest.(send sock Quit)

let restore sock control_in_chan control_out_chan restore_params =
  ()

let main_parent child_pid xenguest_in_fd params =
  wait_for_ready xenguest_in_fd;
  Unix.close xenguest_in_fd;

  let sock = Unix.socket Unix.PF_UNIX Unix.SOCK_STREAM 0 in
  let addr = Unix.ADDR_UNIX
    Params.(Xenguest.control_path params.common.domid)
  in
  Unix.connect sock addr;

  let open Params in
  let control_in_fd  = Fd_send_recv.fd_of_int params.common.control_in_fd in
  let control_out_fd = Fd_send_recv.fd_of_int params.common.control_out_fd in
  let main_fd        = Fd_send_recv.fd_of_int params.common.main_fd in

  let control_in_chan  = Unix.in_channel_of_descr control_in_fd in
  let control_out_chan = Unix.out_channel_of_descr control_out_fd in

  match params.mode with
  | Save ->
    save sock control_in_chan control_out_chan
  | Restore restore_params ->
    restore sock control_in_chan control_out_chan restore_params;

  Unix.close sock;
  Unix.close main_fd

let main fork params =
  if fork then begin
    let xenguest_in_fd, xenguest_out_fd = Unix.pipe () in
    match Unix.fork () with
    | 0 -> begin
      Unix.dup2 xenguest_out_fd Unix.stdout;
      Unix.close xenguest_out_fd;
      Unix.close xenguest_in_fd;
      Xenguest.exec params
    end
    | child_pid -> begin
      Unix.close xenguest_out_fd;
      main_parent child_pid xenguest_in_fd params
    end
  end else
    Xenguest.exec params

let () =
  let control_in_fd = ref (-1) in
  let control_out_fd = ref (-1) in
  let main_fd = ref (-1) in
  let domid = ref (-1) in

  let store_port = ref (-1) in
  let console_port = ref(-1) in

  let mode = ref "" in
  let fork = ref true in

  Arg.(parse
    [
      "-controlinfd",
      Set_int control_in_fd,
      "Control input file descriptor";
      "-controloutfd",
      Set_int control_out_fd,
      "Control output file descriptor";
      "-fd",
      Set_int main_fd,
      "Data file descriptor";
      "-domid",
      Set_int domid,
      "Domain ID";
      "-mode",
      Set_string mode,
      "Operation mode";
      "-store_port",
      Set_int store_port,
      "Store port";
      "-console_port",
      Set_int console_port,
      "Console port";
      "-fork",
      String (function
        | "true" -> fork := true
        | _      -> fork := false),
      "Whether to fork";
    ]
    (fun _ -> ())
    "emu-manager");

  if !control_in_fd  < 0 then failwith "bad controlinfd";
  if !control_out_fd < 0 then failwith "bad controloutfd";
  if !main_fd        < 0 then failwith "bad fd";
  if !domid          < 0 then failwith "bad domid";

  let open Params in
  let params = match !mode with
  | "save" -> {
    common = {
      control_in_fd  = !control_in_fd;
      control_out_fd = !control_out_fd;
      main_fd        = !main_fd;
      domid          = !domid;
    };
    mode = Save;
  }
  | "restore" -> begin
    if !store_port   < 0 then failwith "bad store_port";
    if !console_port < 0 then failwith "bad console_port";
    {
      common = {
        control_in_fd  = !control_in_fd;
        control_out_fd = !control_out_fd;
        main_fd        = !main_fd;
        domid          = !domid;
      };
      mode = Restore {
        store_port     = !store_port;
        console_port   = !console_port;
      };
    }
  end
  | _ -> failwith "unknown mode" in
  main !fork params
