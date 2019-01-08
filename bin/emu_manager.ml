module D = Debug.Make(struct let name = "xcp-emu-manager" end)
open D

let wait_for_ready xenguest_in_fd =
  let channel = Unix.in_channel_of_descr xenguest_in_fd in
  if input_line channel <> "Ready"
  then failwith "unexpected message from child"

let save sock control_in_chan control_out_chan hvm domid save_params =
  if not hvm
  then Xenguest.(send sock (Set_args ["pv", "true"]));

  if save_params.Params.live
  then begin
    if hvm
    then begin
      debug "Initiating QMP";
      let qmp_path = Printf.sprintf "/var/run/xen/qmp-libxl-%d" domid in
      let qmp_sock = Qmp_protocol.connect qmp_path in
      let (_ : Qmp.message) = Qmp_protocol.read qmp_sock in
      Qmp_protocol.write qmp_sock Qmp.(Command (None, Qmp_capabilities));
      let (_ : Qmp.message) = Qmp_protocol.read qmp_sock in

      Xenguest.(send sock Track_dirty);
      Xenguest.(send sock Migrate_progress);

      Qmp_protocol.write qmp_sock
        Qmp.(Command (None, Xen_set_global_dirty_log true));
      let (_ : Qmp.message) = Qmp_protocol.read qmp_sock in
      Qmp_protocol.close qmp_sock
    end else begin
      Xenguest.(send sock Track_dirty);
      Xenguest.(send sock Migrate_progress);
    end;

    Control.(send control_out_chan Prepare);
    Control.(expect_done control_in_chan);

    Xenguest.(send sock Migrate_live);

    (* read and relay progress events. *)
    let finished       = ref false in
    let progress       = ref 0 in
    let total          = ref 0 in
    let last_iteration = ref 0 in

    while not !finished do
      match Xenguest.receive sock with
      | Xenguest.(Progress {sent; remaining; iteration}) -> begin
        if remaining > 0
        then total := !total + remaining;

        progress := 100 * sent / !total;
        Control.(send control_out_chan (Progress !progress));

        if iteration > 0 && !last_iteration = 0
        then begin
          Control.(send control_out_chan Suspend);
          Control.(expect_done control_in_chan);

          Xenguest.(send sock Migrate_pause);
          Xenguest.(send sock Migrate_paused)
        end;

        last_iteration := iteration
      end
      | Xenguest.(Completed _) -> finished := true
      | _ -> ()
    done
  end else begin
    Control.(send control_out_chan Suspend);
    Control.(expect_done control_in_chan);

    Xenguest.(send sock Migrate_pause);
    Xenguest.(send sock Migrate_paused);

    Control.(send control_out_chan Prepare);
    Control.(expect_done control_in_chan);

    Xenguest.(send sock Migrate_nonlive);
    let (_ : Xenguest.event) = Xenguest.(receive sock) in ()
  end;

  if not save_params.Params.live
  then Control.(send control_out_chan (Progress 100));
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

  let rec wait_for_completion () =
    match Unix.select [sock] [] [] 30.0 with
    | sock' :: _, _, _ when sock = sock' -> begin
      match Xenguest.receive sock with
      | Xenguest.(Completed (Some {xenstore_mfn; console_mfn})) ->
        Control.(send control_out_chan (Result (xenstore_mfn, console_mfn)))
      | _ -> wait_for_completion ()
    end
    | _ -> wait_for_completion ()
  in

  begin
    try
      wait_for_completion ();
    with e ->
      debug "wait_for_completion failed: %s" (Printexc.to_string e)
  end;

  begin
    try
      Xenguest.(send sock Quit)
    with e ->
      debug "quit failed: %s" (Printexc.to_string e)
  end

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
  Xenguest.(send_init sock main_fd);

  match params.mode with
  | Save save_params ->
    save
      sock control_in_chan control_out_chan
      params.common.hvm params.common.domid save_params
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
  let live = ref false in

  (* Currently unused. *)
  let device_model = ref "" in

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
      "-live",
      String (function
        | "true" -> live := true
        | _      -> live := false),
      "Whether to save live, i.e. for a live migration";
      "-dm",
      Set_string device_model,
      "Device model";
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
    mode = Save {
      live = !live
    };
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
