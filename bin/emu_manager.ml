let main_parent child_pid in_fd params = ()

let main fork params =
  if fork then begin
    let in_fd, out_fd = Unix.pipe () in
    match Unix.fork () with
    | 0 -> begin
      Unix.dup2 out_fd Unix.stdout;
      Unix.close out_fd;
      Unix.close in_fd;
      Xenguest.exec params
    end
    | child_pid -> begin
      Unix.close out_fd;
      main_parent child_pid in_fd params
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
  | "save"    ->
    Save {
      control_in_fd  = !control_in_fd;
      control_out_fd = !control_out_fd;
      main_fd        = !main_fd;
      domid          = !domid;
    }
  | "restore" -> begin
    if !store_port   < 0 then failwith "bad store_port";
    if !console_port < 0 then failwith "bad console_port";
    Restore {
      control_in_fd  = !control_in_fd;
      control_out_fd = !control_out_fd;
      main_fd        = !main_fd;
      domid          = !domid;
      store_port     = !store_port;
      console_port   = !console_port;
    }
  end
  | _ -> failwith "unknown mode" in
  main !fork params
