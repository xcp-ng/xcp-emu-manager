let xenguest_path = "/usr/libexec/xen/bin/xenguest"

let control_path domid = Printf.sprintf "/var/xen/xenguest/%d/control" domid

type message =
  | Set_args of (string * string) list
  | Migrate_init
  | Migrate_pause
  | Migrate_paused
  | Migrate_nonlive
  | Restore
  | Quit

let send fd message =
  (* Write the command. *)
  let out_json = match message with
  | Set_args args -> begin
    let args_json = `O (List.map (fun (k, v) -> (k, `String v)) args) in
    `O ["execute", `String "set_args"; "arguments", args_json]
  end
  | Migrate_init    -> `O ["execute", `String "migrate_init"]
  | Migrate_pause   -> `O ["execute", `String "migrate_pause"]
  | Migrate_paused  -> `O ["execute", `String "migrate_paused"]
  | Migrate_nonlive -> `O ["execute", `String "migrate_nonlive"]
  | Restore         -> `O ["execute", `String "restore"]
  | Quit            -> `O ["execute", `String "quit"]
  in
  IO.really_write_string fd (Ezjsonm.to_string out_json);

  (* Read the response. *)
  let in_json = `O ["return", `O []] in
  let in_string = Ezjsonm.to_string in_json in
  let (_:string) = IO.really_read_string fd ((String.length in_string) + 1) in
  ()

let receive fd =
  let chan = Unix.in_channel_of_descr fd in
  let data = match input_line chan |> Ezjsonm.from_string with
  | `O [
    "event", `String "MIGRATION";
    "data", `O data
  ] -> data
  | _ -> []
  in
  if List.mem_assoc "result" data
  then Some (Ezjsonm.get_string (List.assoc "result" data))
  else None

type args = {
  path: string;
  args: string array;
}

let args_of_params {Params.common = common} = {
  path = xenguest_path;
  args = [|
    xenguest_path;
    "-debug";
    "-domid"; string_of_int (common.Params.domid);
    "-controloutfd"; "2";
    "-controlinfd";  "0";
    "-mode"; "listen";
  |];
}

let exec params =
  let args = args_of_params params in
  Unix.execv args.path args.args
