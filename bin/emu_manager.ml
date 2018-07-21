let wait_for_ready xenguest_in_fd =
  let channel = Unix.in_channel_of_descr xenguest_in_fd in
  if input_line channel <> "Ready"
  then failwith "unexpected message from child"

let save sock control_in_chan control_out_chan hvm =
  if not hvm
  then Xenguest.(send sock (Set_args ["pv", "true"]));

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

let restore sock control_in_chan control_out_chan hvm restore_params =
  let args =
    let hvm_args = if hvm then [] else ["pv", "true"] in
    let open Params in
    [
      "store_port", string_of_int restore_params.store_port;
      "console_port", string_of_int restore_params.console_port;
    ] @ hvm_args
  in

  Xenguest.(send sock (Set_args args));

  let (_ : Control.in_message) =  Control.receive control_in_chan in
  Xenguest.(send sock Restore);

  let xenstore_mfn, console_mfn = match Xenguest.(receive sock) with
  | Some result -> Scanf.sscanf result "%d %d" (fun a b -> a, b)
  | None        -> failwith "no result received"
  in

  Control.(send control_out_chan (Result (xenstore_mfn, console_mfn)));

  Xenguest.(send sock Quit)

let main_parent child_pid xenguest_in_fd params =
  wait_for_ready xenguest_in_fd;
  Unix.close xenguest_in_fd;

  let open Params in
  let control_in_fd  = Fd_send_recv.fd_of_int params.common.control_in_fd in
  let control_out_fd = Fd_send_recv.fd_of_int params.common.control_out_fd in
  let main_fd        = Fd_send_recv.fd_of_int params.common.main_fd in

  let control_in_chan  = Unix.in_channel_of_descr control_in_fd in
  let control_out_chan = Unix.out_channel_of_descr control_out_fd in

  let sock = Unix.socket Unix.PF_UNIX Unix.SOCK_STREAM 0 in
  let addr = Unix.ADDR_UNIX
    Params.(Xenguest.control_path params.common.domid)
  in
  Unix.connect sock addr;
  Xenguest.(send sock Migrate_init);

  match params.mode with
  | Save ->
    save
      sock control_in_chan control_out_chan params.common.hvm
  | Restore restore_params ->
    restore
      sock control_in_chan control_out_chan params.common.hvm restore_params;

  Unix.close sock;
  Unix.close main_fd;

  let should_stop = ref false in

  while not !should_stop do
    let pid, _ = Unix.wait () in
    if pid = child_pid
    then should_stop := true
  done

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
  | "save" | "hvm_save" -> {
    common = {
      control_in_fd  = !control_in_fd;
      control_out_fd = !control_out_fd;
      main_fd        = !main_fd;
      domid          = !domid;
      hvm            = !mode = "hvm_save";
    };
    mode = Save;
  }
  | "restore" | "hvm_restore" -> begin
    if !store_port   < 0 then failwith "bad store_port";
    if !console_port < 0 then failwith "bad console_port";
    {
      common = {
        control_in_fd  = !control_in_fd;
        control_out_fd = !control_out_fd;
        main_fd        = !main_fd;
        domid          = !domid;
        hvm            = !mode = "hvm_restore";
      };
      mode = Restore {
        store_port     = !store_port;
        console_port   = !console_port;
      };
    }
  end
  | _ -> failwith "unknown mode" in
  main !fork params
