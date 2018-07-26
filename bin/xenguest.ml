let xenguest_path = "/usr/libexec/xen/bin/xenguest"

let control_path domid = Printf.sprintf "/var/xen/xenguest/%d/control" domid

let expect_response fd =
  let in_chan = Unix.in_channel_of_descr fd in
  match input_line in_chan |> Ezjsonm.from_string with
  | `O ["return", `O []] -> ()
  | _                    -> failwith "bad response from xenguest"

let send_init fd fd_to_send =
  let out_json = `O ["execute", `String "migrate_init"] in
  let out_string = Ezjsonm.to_string out_json in
  let out_length = String.length out_string in
  if Fd_send_recv.send_fd fd out_string 0 out_length [] fd_to_send <> out_length
  then failwith "Failed to initialise xenguest";
  expect_response fd

type message =
  | Set_args of (string * string) list
  | Migrate_pause
  | Migrate_paused
  | Migrate_live
  | Migrate_nonlive
  | Migrate_progress
  | Restore
  | Quit
  | Track_dirty

let send fd message =
  (* Write the command. *)
  let out_json = match message with
  | Set_args args -> begin
    let args_json = `O (List.map (fun (k, v) -> (k, `String v)) args) in
    `O ["execute", `String "set_args"; "arguments", args_json]
  end
  | Migrate_pause    -> `O ["execute", `String "migrate_pause"]
  | Migrate_paused   -> `O ["execute", `String "migrate_paused"]
  | Migrate_live     -> `O ["execute", `String "migrate_live"]
  | Migrate_nonlive  -> `O ["execute", `String "migrate_nonlive"]
  | Migrate_progress -> `O ["execute", `String "migrate_progress"]
  | Restore          -> `O ["execute", `String "restore"]
  | Quit             -> `O ["execute", `String "quit"]
  | Track_dirty      -> `O ["execute", `String "track_dirty"]
  in
  IO.really_write_string fd (Ezjsonm.to_string out_json);
  expect_response fd

type completion = {
  xenstore_mfn: int;
  console_mfn: int;
}

type progress = {
  sent: int;
  remaining: int;
  iteration: int;
}

type event =
  | Completed of completion option
  | Progress of progress
  | Unknown

let receive fd =
  let chan = Unix.in_channel_of_descr fd in
  match input_line chan |> Ezjsonm.from_string with
  | `O [
    "event", `String "MIGRATION";
    "data", `O (("status", `String "completed") :: rest)
  ] -> Completed
    (try
      let result = Ezjsonm.get_string (List.assoc "result" rest) in
      Some (Scanf.sscanf
        result "%d %d"
        (fun xenstore_mfn console_mfn -> {xenstore_mfn; console_mfn}))
    with _ ->
      None)
  | `O [
    "event", `String "MIGRATION";
    "data", `O [
      "sent", `Float sent;
      "remaining", `Float remaining;
      "iteration", `Float iteration;
    ]
  ] -> Progress {
    sent = int_of_float sent;
    remaining = int_of_float remaining;
    iteration = int_of_float iteration;
  }
  | _ -> Unknown

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
  let env = [|
    "LD_PRELOAD=/usr/libexec/coreutils/libstdbuf.so";
    "_STDBUF_O=0";
  |] in
  Unix.execve args.path args.args env
