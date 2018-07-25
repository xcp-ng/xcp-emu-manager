type out_message =
  | Suspend
  | Prepare
  | Progress of int
  | Result of int * int

let output_line out_chan data =
  output_string out_chan (Printf.sprintf "%s\n" data);
  flush out_chan

let send out_chan = function
  | Suspend -> output_line out_chan "suspend:"
  | Prepare -> output_line out_chan "prepare:xenguest"
  | Progress progress ->
    output_line
      out_chan
      (Printf.sprintf "info:\\b\\b\\b\\b%d" progress)
  | Result (xenstore_mfn, console_mfn) ->
    output_line
      out_chan
      (Printf.sprintf "result:xenguest %d %d" xenstore_mfn console_mfn)

let expect_done in_chan =
  match input_line in_chan with
  | "done" -> ()
  | _      -> failwith "bad control message"

type in_message =
  | Restore

let receive in_chan =
  match input_line in_chan with
  | "restore:xenguest" -> Restore
  | _                  -> failwith "bad control message"
