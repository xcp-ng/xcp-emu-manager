let rec really_read fd string off n =
  if n=0 then () else
    let m = Unix.read fd string off n in
    if m = 0 then raise End_of_file;
    really_read fd string (off+m) (n-m)

let really_read_string fd length =
  let buf = Bytes.make length '\000' in
  really_read fd buf 0 length;
  Bytes.unsafe_to_string buf

let rec restart_on_EINTR f x =
  try f x with Unix.Unix_error (Unix.EINTR, _, _) -> restart_on_EINTR f x
and really_write fd buffer offset len =
  let n = restart_on_EINTR (Unix.single_write_substring fd buffer offset) len in
  if n < len then really_write fd buffer (offset + n) (len - n);;

let really_write_string fd string =
	really_write fd string 0 (String.length string)
